/* ****************************************************************
 * Portions Copyright 1998 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

#include <asm/semaphore.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "vmkapi.h"
#include "linux_stubs.h"
#include "linux_task.h"

#define VMKLNX_LOG_HANDLE LinTask
#include "vmklinux26_log.h"

static vmk_SpinlockIRQ semaLock;
vmk_SpinlockIRQ taskLock;
vmk_WorldPrivateInfoKey vmklnx_taskKey;
static struct list_head taskFreeChain;
static vmk_MemPool tasksMemPool;
static vmk_MachPage tasksStartPage;
static LinuxTaskExt *tasks;
static vmk_SpinlockIRQ taskFreeLock;
static void LinuxTaskDestruct(vmk_AddrCookie teCookie,
                              vmk_WorldPrivateInfoKey key,
                              vmk_WorldID id);



/*
 *----------------------------------------------------------------------
 *
 * LinuxTaskGetCurrentDescriptor --
 *
 *      Get the linux extended task_struct pointer associated with the 
 *      current running world.
 *
 * Results:
 *      The pointer to the task_struct of the current world.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static inline LinuxTaskExt *
LinuxTaskGetCurrentDescriptor(void)
{
   LinuxTaskExt *te;
   VMK_ReturnStatus status;

   status = vmk_WorldGetPrivateInfo(vmklnx_taskKey, (vmk_AddrCookie *)&te);
   if (status != VMK_OK) {
      return NULL;
   }

   return te;
}



/*
 *----------------------------------------------------------------------
 *
 * LinuxTaskSetCurrentDescriptor --
 *
 *      Bind the extended task info into the current world's private data area.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
LinuxTaskSetCurrentDescriptor(LinuxTaskExt *te)
{
   if (te != NULL) {
      vmk_WorldSetPrivateInfo(vmklnx_taskKey, te);
   } else {
      vmk_WorldUnsetPrivateInfo(vmklnx_taskKey);
   }
}

/*
 *----------------------------------------------------------------------
 *
 * LinuxTaskConstruct --
 *      Allocate an extended task descriptor
 *
 * Results:
 *      Pointer of the newly allocated extended task descriptor upon
 *      success; otherwise NULL.
 *
 * Side effects:
 *	The task structure is zero filled.
 *
 *----------------------------------------------------------------------
 */

static LinuxTaskExt *
LinuxTaskConstruct(vmk_Bool useModID, 
                   u32 flags, 
                   pid_t pid, 
                   long state,
                   int (*func)(void *),
                   void *arg)
{
   LinuxTaskExt *te;
   struct task_struct *task;
   vmk_ModuleID modID;
   vmk_HeapID heapID;
   unsigned long prevIRQL;
   struct list_head *lte;

   if (useModID == VMK_TRUE) {
      modID  = vmk_ModuleStackTop();
      VMK_ASSERT(modID != VMK_INVALID_MODULE_ID);
      VMK_ASSERT(modID != VMK_VMKERNEL_MODULE_ID);

      heapID = vmk_ModuleGetHeapID(modID);
   } else {
      modID  = vmklinuxModID;
      heapID = vmklinux_HeapID;
   }

   prevIRQL = vmk_SPLockIRQ(&taskFreeLock);
   if (unlikely(list_empty(&taskFreeChain))) {
      te = NULL;
   } else {
      lte = taskFreeChain.next;
      list_del_init(lte);
      te = container_of(lte, LinuxTaskExt, freeChain);
   }
   vmk_SPUnlockIRQ(&taskFreeLock, prevIRQL);

   if (unlikely(te == NULL)) {
      VMKLNX_WARN("Could not allocate memory for LinuxTaskExt");
      return NULL;
   }

   memset(te, 0, sizeof(LinuxTaskExt));

   te->flags   = flags;
   te->modID   = modID;
   te->heapID  = heapID;
   te->func    = func;
   te->arg     = arg;

   task        = &te->task;
   task->pid   = pid;
   task->state = state;

   VMKLNX_DEBUG(1, "te=%p task=%p", te, &te->task);

   return te;
}

#if defined(VMX86_DEBUG)
/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_ValidTask --
 *      Verify if the pointer te is indeed pointing to a valid
 *      extended task descriptor. 
 *
 * Results:
 *      TRUE if te is a valid pointer; otherwise FALSE.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

vmk_Bool
LinuxTask_ValidTask(LinuxTaskExt *te)
{
   vmk_WorldPrivateDataHandle handle;
   LinuxTaskExt *taskFound;
   vmk_Bool ret;

   taskFound = LinuxTask_FindByPid(te->task.pid, &handle);
   if (taskFound == NULL) {
      return VMK_FALSE;
   }

   ret = (taskFound == te) ? VMK_TRUE : VMK_FALSE;

   vmk_ReleasePrivateInfo(handle);

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_IsLocked --
 *      To verify that the current world has exclusive access to the
 *      extended task descriptor identied by te.
 *
 * Results:
 *      TRUE if the current world has exclusive access; otherwise
 *      FALSE;
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

vmk_Bool
LinuxTask_IsLocked(LinuxTaskExt *te)
{
   return vmk_SPIsLockedIRQ(&taskLock) ? VMK_TRUE : VMK_FALSE;
}
#endif /* defined(VMX86_DEBUG) */

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_GetCurrent --
 *
 *      Gets current task pointer for the current world. This function is
 *      ONLY used by the linux get_current function to return a pointer
 *      to the task struct of the running process.
 *      See current.h in include/asm-i386/current.h for more details.
 *      If you ever change this function you also need to change the
 *      prototype in current.h since that file cannot include
 *      linux_stubs.h because it often comes in very early...
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

struct task_struct *
vmklnx_GetCurrent(void)
{
   LinuxTaskExt *te;
   vmk_WorldID currWorldID;

   te = LinuxTaskGetCurrentDescriptor();
   if (te != NULL) {
      return &te->task;;
   }

   currWorldID = vmk_WorldGetID();
   VMK_ASSERT(currWorldID != VMK_INVALID_WORLD_ID);

   /*
    * No matching world found in the private info.
    * This means we come across this world for the 
    * first time, and this is definite not a world
    * created by vmklinux. 
    * Let's create a task struct for this world.
    */

   /*
    * Don't use Module ID here since this is a task
    * created outside of vmklinux, this can be a
    * helper world from vmkernel.
    */
   te = LinuxTaskConstruct(VMK_FALSE, 
                           0, 
                           currWorldID, 
                           TASK_RUNNING,
                           NULL,
                           NULL);

   if (te == NULL) {
      /*
       * vmklinux relies on the task_struct to suspend
       * and resume a task. Without the task_struct, 
       * we would lose synchronization support for this task.
       * This is serious enough to panic the system
       */
      VMKLNX_PANIC("Couldn't allocate task_struct for non-vmklinux task");
   }

   LinuxTaskSetCurrentDescriptor(te);

   return &te->task;
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_WaitEvent --
 *      Block the current world until the event identified by eventID
 *      is delivered to the world.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void 
LinuxTask_WaitEvent(u32 eventID)
{
   struct task_struct *task = vmklnx_GetCurrent();
   unsigned long prevIRQL;
   LinuxTaskExt *te = container_of(task, LinuxTaskExt, task);

   prevIRQL = vmk_SPLockIRQ(&taskLock);

   VMKLNX_DEBUG(1, "eventID=%d task=%p te=%p te->events=%d",
                 eventID, task, te, te->events);

   while ((te->events & eventID) == 0) {
      te->flags |= LT_TASK_SUSPENDED;
   
      VMKLNX_DEBUG(1, "task=%p (te=%p) suspending...", task, te);

      vmk_WorldWaitIRQ((vmk_WorldEventId)te, &taskLock, 0, prevIRQL);
   
      VMKLNX_DEBUG(1, "task=%p (te=%p) resumed.", task, te);

      prevIRQL = vmk_SPLockIRQ(&taskLock);
   
      te->flags &= ~LT_TASK_SUSPENDED;

      if (te->events & eventID) {
         break;
      }

      VMKLNX_DEBUG(1, "Spurious wakeup: "
                       "pid=%d te->events=0x%x eventID=0x%x .", 
                       task->pid, te->events, eventID);
   }

   te->events &= ~eventID;
   task->state = TASK_RUNNING;
   vmk_SPUnlockIRQ(&taskLock, prevIRQL);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_SendEvent --
 *      Delivered the event identified by eventID to the world identified
 *      the task descriptor te. If world te is suspended, it will be 
 *      resumed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
LinuxTask_SendEvent(LinuxTaskExt *te, u32 eventID)
{
   unsigned long prevIRQL;

   VMKLNX_DEBUG(1, "eventID=%d task=%p te=%p te->events=%d",
                 eventID, &te->task, te, te->events);

   prevIRQL = vmk_SPLockIRQ(&taskLock);

   te->events |= eventID;

   vmk_WorldWakeup((vmk_WorldEventId)te);

   vmk_SPUnlockIRQ(&taskLock, prevIRQL);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_Exit --
 *      Terminate the current world, removing the the descriptor from the
 *      world private data area (which will free it via the destructor).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
LinuxTask_Exit(LinuxTaskExt *te)
{
   VMK_ASSERT(LinuxTaskGetCurrentDescriptor() == te);

   vmk_WorldUnsetPrivateInfo(vmklnx_taskKey);

   vmk_WorldExit(VMK_OK);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_FindByPid --
 *      Locate the world whose process ID is pid.
 *
 * Results:
 *      Return the pointer of the extended task descriptor of the world.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

LinuxTaskExt *
LinuxTask_FindByPid(pid_t pid, vmk_WorldPrivateDataHandle *handle)
{
   LinuxTaskExt *taskFound = NULL;
   VMK_ReturnStatus status;

   status = vmk_WorldGetPrivateInfoWithHold(vmklnx_taskKey, pid,
                              (vmk_AddrCookie *)&taskFound, handle);
   if (status == VMK_OK) {
      return taskFound;
   }

   return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxStartFunc --
 *      This is start up function for a Linux task. It sets up the
 *      runtime for the task and release the task resources on exit.
 *      data is the argument passes to the task main function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
LinuxStartFunc(void *data)
{
   LinuxTaskExt *te = (LinuxTaskExt *) data;
   struct task_struct *task = &te->task;

   VMK_ASSERT_CPU_HAS_INTS_ENABLED();

   task->pid = vmk_WorldGetID();
   VMK_ASSERT(task->pid != VMK_INVALID_WORLD_ID);

   LinuxTaskSetCurrentDescriptor(te);

   VMKLNX_DEBUG(1, "task=%p te=%p flags=%d func=%p arg=%p", 
                 task, te, te->flags, te->func, te->arg);

   task->state = TASK_RUNNING;

   VMKAPI_MODULE_CALL(te->modID, te->retval, te->func, te->arg);

   VMKLNX_DEBUG(1, "Task func=%p exited.", te->func);
 
   LinuxTask_Exit(te);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_Create --
 *      Create a vmklinux world to execute function fn, passing arg as
 *      argument to fn. If useModID is true, fn is executed in the context 
 *      of the caller's module. If LinuxTask_Create is used to create
 *      linux kthread, LT_KTHREAD should be passed as flag argument.
 *
 * Results:
 *      The extended task descriptor of the new world.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * LinuxTask_Create() returns NULL on error and struct task_struct * on success.
 */
LinuxTaskExt *
LinuxTask_Create(int (*fn)(void *),
		  void * arg,
		  vmk_Bool useModID,
                  u32 flags)
{
   LinuxTaskExt *te;
   vmk_WorldID worldID;
#define TASK_NAME_LENGTH	64
   char name[TASK_NAME_LENGTH];

   /*
    * Allocate new task struct.
    */
   te = LinuxTaskConstruct(USE_MOD_ID, 
                           flags|LT_VMKLINUX_OWNER, 
                           0, 
                           TASK_STOPPED,
                           fn,
                           arg);
   if (te == NULL) {
      return NULL;
   }

   /*
    * Use the name of the module as the task name.
    */
   vmk_ModuleGetName(te->modID, name, TASK_NAME_LENGTH);
   
   VMKLNX_DEBUG(1, "%s: te=%p flags=%d fn=%p arg=%p", 
                 name, te, te->flags, fn, arg);

   if (vmk_WorldCreate(te->modID, name, (vmk_WorldStartFunc)LinuxStartFunc, te,
                       &worldID) != VMK_OK) {
      LinuxTaskDestruct(te, vmklnx_taskKey, te->task.pid);
      return NULL;
   }

   // XXX worldIDs are not valid in the same range as linux PIDs.
   // Probably okay in the kernel, though.
   return te;
}

/*
 * kernel_thread() is wrapper around LinuxTask_Create(). This function gets
 * called from the driver code. And we enforce that the module id is set.
 */
/**                                          
 *  kernel_thread - Spawn a new kernel thread       
 *  @fn: body function for the thread  
 *  @arg: argument to pass to the body function
 *  @flags: flags that control the thread creation
 *                                           
 *  Spawns a new kernel thread to run the given body function in.                     
 *                                           
 *  ESX Deviation Notes:                     
 *  The flags argument is ignored.
 *
 *  In ESX, vmklinux kernel threads are mapped onto System Worlds. System
 *  Worlds are currently non-preemptable. So they will only yield the CPU
 *  when blocking or yielding voluntarily and can only be interrupted by
 *  hardware interrupts and NMIs.
 *
 *  RETURN VALUE:
 *    Task PID on success.
 *    Negative errno on failure.                                     
 */                                          
/* _VMKLNX_CODECHECK_: kernel_thread */
int 
kernel_thread(int (*fn)(void *),
              void * arg,
              unsigned long flags)
{
   LinuxTaskExt *te;
   te = LinuxTask_Create(fn, arg, USE_MOD_ID, 0);
   if (te != NULL) {
      return te->task.pid;
   }

   return -ECHILD;
}

fastcall
/**                                          
 *  wake_up_process - wake up a given task 
 *  @task: a given task to be resumed 
 *                                           
 *  Wake up and resume a task.
 *                                          
 *  RETURN VALUE:
 *  Always return 1.
 */                                          
/* _VMKLNX_CODECHECK_: wake_up_process */
int wake_up_process(struct task_struct *task) 
{
   VMK_ASSERT(task && task->pid);

   LinuxTaskExt *te = container_of(task, LinuxTaskExt, task);

   LinuxTask_Resume(te);
   return 1;
}

/**                                          
 *  schedule - select a new process to be executed 
 *
 *  Selects a new process to be executed
 *
 *  RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: schedule */
void 
schedule(void)
{
   struct task_struct *task = get_current();

   if (unlikely((signal_pending(task) 
       && (task->state == TASK_INTERRUPTIBLE 
           || task->state == TASK_RUNNING)))) {
      return;
   }

   if (unlikely(task->state == TASK_RUNNING)) {
      yield();
      return;
   }

   vmk_WorldAssertIsSafeToBlock();
   LinuxTask_Suspend();
}

static void
__wait_timeout(unsigned long data)
{
   struct task_struct *task = (struct task_struct *) data;

   VMK_ASSERT(task && task->pid);

   LinuxTaskExt *te = container_of(task, LinuxTaskExt, task);

   LinuxTask_Resume(te);
}

static signed long
__schedule_timeout(struct task_struct *current_task, signed long wait_time)
{
   unsigned long timeout = wait_time;
   signed long diff;
   long state;
   struct timer_list wait_timer;
   int ret;

   if (unlikely(wait_time == MAX_SCHEDULE_TIMEOUT)) {
      schedule();
      return wait_time;
   }

   if (unlikely(wait_time < 0)) {
      VMKLNX_DEBUG(1, "negative timeout value %lx from %p",
                     wait_time, __builtin_return_address(0));
      return 0;
   }

   timeout = wait_time + jiffies;
   state = current_task->state;

   setup_timer(&wait_timer, __wait_timeout, (unsigned long) current_task);

   /*
    * We might need to set a timer ***twice*** because jiffies may not have
    * advanced fully for the first timer.  That's because jiffies is
    * maintained on CPU 0, whereas the local CPU clock may not be fully
    * synchronized to it.
    *
    * See PR 330905.
    */
   for (;;) {
      __mod_timer(&wait_timer, timeout);
      schedule();
      ret = del_timer_sync(&wait_timer);
      diff = timeout - jiffies;
      if (likely(diff <= 0 || ret)) {
         break;
      }
      // set the state back in preparation for blocking again
      current_task->state = state;
   }

   return diff < 0 ? 0 : diff;
}

/**
 *  schedule_timeout - sleep until timeout
 *  @wait_time: timeout value in jiffies
 *
 *  Make the current task sleep until specified number of jiffies have elapsed.
 *  The routine may return early if a signal is delivered to the current task.
 *  Specifying a timeout value of MAX_SCHEDULE_TIMEOUT will schedule
 *  the CPU away without a bound on the timeout.
 *
 *  RETURN VALUE:
 *  0 if the timer expired in time,
 *  remaining time in jiffies if function returned early, or
 *  MAX_SCHEDULE_TIMEOUT if timeout value of MAX_SCHEDULE_TIMEOUT was specified
 */
/* _VMKLNX_CODECHECK_: schedule_timeout */
fastcall signed long
schedule_timeout(signed long wait_time)
{
   return __schedule_timeout(get_current(), wait_time);
}

/**
 *  schedule_timeout_interruptible - sleep until timeout or event
 *  @wait_time: the timeout in jiffies
 *
 *  Make the current task sleep until @wait_time jiffies have elapsed or if a
 *  signal is delivered to the current task.
 *
 *  RETURN VALUE:
 *  Returns the remaining time in jiffies.
 *
 */
/* _VMKLNX_CODECHECK_: schedule_timeout_interruptible */
signed long 
schedule_timeout_interruptible(signed long wait_time)
{
   struct task_struct *current_task = get_current();

   current_task->state = TASK_INTERRUPTIBLE;
   return __schedule_timeout(current_task, wait_time);
}

/**                                          
 *  schedule_timeout_uninterruptible - sleep until at least specified timeout
 *                                     has elapsed
 *  @wait_time: timeout value in jiffies
 *                                           
 *  Make the current task sleep until at least specified number of jiffies 
 *  have elapsed. Specifying a timeout value of MAX_SCHEDULE_TIMEOUT will 
 *  schedule the CPU away without a bound on the timeout.
 *  
 *  RETURN VALUE:
 *  0 or MAX_SCHEDULE_TIMEOUT if timeout value of MAX_SCHEDULE_TIMEOUT 
 *  was specified
 */                                          
/* _VMKLNX_CODECHECK_: schedule_timeout_uninterruptible */
signed long 
schedule_timeout_uninterruptible(signed long wait_time)
{
   struct task_struct *current_task = get_current();

   current_task->state = TASK_UNINTERRUPTIBLE;
   return __schedule_timeout(current_task, wait_time);
}

/**                                          
 *  cond_resched - latency reduction via explicit rescheduling
 *                                           
 *  Performs 'possible' rescheduling of this task by invoking builtin 
 *  throttling yield mechanism. The task may or may not yield the CPU 
 *  depending on the decision. 
 *  
 *  RETURN VALUE:
 *  0
 *
 *  ESX DEVIATION NOTES: Always returns 0, unlike Linux where the
 *  return value indicates whether a reschedule was done in fact.
 */                                          
/* _VMKLNX_CODECHECK_: cond_resched */
int
cond_resched(void)
{
   /*
    * Let vmk_WorldYield builtin throttling yield mechanism to
    * decide whether this world should yield the CPU.
    */
   vmk_WorldYield();

   /*
    * Always assume we have not yielded.
    */
   return 0;
}

/**                                          
 *  yield - yield the CPU       
 *                                           
 *  Yield the CPU to other tasks.                       
 *                                           
 *  ESX Deviation Notes:                     
 *  The calling task will be descheduled for at least 1 millisecond. 
 *
 *  RETURN VALUE:
 *  This function does not return a value.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: yield */
void 
yield(void)
{
   //XXX this will probably crash network drivers.

   /*
    * PR240566:
    * Before we have a true yield. Suspend the current world
    * for 1 usec.
    */

   vmk_WorldSleep(1);
}

/**                                          
 *  msleep - Deschedules the current task for a duration
 *  @msecs: time, in milliseconds, that the current task needs to be descheduled
 *                                           
 *  Deschedules the current task for a duration
 *
 *  See Also:
 *  msleep_interruptible
 */                                          
/* _VMKLNX_CODECHECK_: msleep */
void
msleep(unsigned int msecs)
{
   signed long sleep_time = msecs_to_jiffies(msecs) + 1;

   while (sleep_time) {
      sleep_time = schedule_timeout_uninterruptible(sleep_time);
   }
}

/**                                          
 *  msleep_interruptible - deschedules the current task for a duration
 *  @msecs: time, in milliseconds, that the current task needs to be descheduled.
 *                                           
 *  Deschedules the current task for a duration. The function returns when
 *  the task has been descheduled for the whole intended duration,
 *  or returns when the task is interrupted by an external event
 *  while it is descheduled. In this case, the amount of time unslept is
 *  returned.
 *
 *  Return Value:
 *  Amount of time unslept
 *
 *  See Also:
 *  msleep
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: msleep_interruptible */
unsigned long
msleep_interruptible(unsigned int msecs)
{
   signed long sleep_time = msecs_to_jiffies(msecs) + 1;
   struct task_struct *current_task = get_current();

   while(sleep_time && !signal_pending(current_task)) {
      current_task->state = TASK_INTERRUPTIBLE;
      sleep_time = __schedule_timeout(current_task, sleep_time);
   }

   return jiffies_to_msecs(sleep_time);
}

/*
 *----------------------------------------------------------------------
 *
 * LinuxTaskDestruct --
 *      Free the memory associated with the extended task descriptor
 *      identified by the pointer te. te must be pointing to an
 *      extented task structure belonging to a world created by 
 *      vmklinux.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Extended task structures of worlds that are not created
 *      by vmklinux and that have exited already are also free.
 *
 *----------------------------------------------------------------------
 */

static void
LinuxTaskDestruct(vmk_AddrCookie teCookie, vmk_WorldPrivateInfoKey key,
                    vmk_WorldID id)
{
   LinuxTaskExt *te = teCookie.ptr;
   unsigned long prevIRQL;


   VMK_ASSERT(te->task.pid == id);
   VMK_ASSERT(key == vmklnx_taskKey);

   prevIRQL = vmk_SPLockIRQ(&taskFreeLock);
   list_add(&te->freeChain, &taskFreeChain);
   vmk_SPUnlockIRQ(&taskFreeLock, prevIRQL);
}


/*
 *----------------------------------------------------------------------------
 *
 * LinuxTask(Init|Cleanup)Tasks --
 *
 *      Initializes and cleans up the array of LinuxTaskExt structures.  Once
 *      initialized, tasks will point to an array of size vmk_WorldsMax()
 *      allocated from a MemPool on pages in high memory.
 *
 * Results:
 *      Init: VMK_OK on success.
 *      Cleanup: None.
 *
 * Side effects:
 *      Will vmk_Panic() on failure.
 *
 *----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxTaskInitTasks(void)
{
   VMK_ReturnStatus status;
   vmk_uint32 maxWorlds;
   vmk_MemPoolProps props;
   vmk_MachPageRange pageRange;
   vmk_VirtAddr va;
   int i;

   /*
    * The LinuxTaskExt structures are allocated from a MemPool to avoid using
    * low memory.  This also avoids an awkward resizing of the module heap.
    */
   maxWorlds = vmk_WorldsMax();
   props.reservation = ((maxWorlds * sizeof *tasks) + (PAGE_SIZE - 1)) / PAGE_SIZE;
   props.limit = props.reservation;

   status = vmk_MemPoolCreate("LinuxTaskMemPool", &props, &tasksMemPool);
   if (status != VMK_OK) {
      vmk_Panic("vmklinux: LinuxTask_Init: vmk_MemPoolCreate failed %s",
                vmk_StatusToString(status));
   }

   status = vmk_MemPoolAlloc(tasksMemPool, NULL, props.reservation, VMK_TRUE,
                             &tasksStartPage);
   if (status != VMK_OK) {
      vmk_Panic("vmklinux: LinuxTaskInitTasks: vmk_MemPoolAlloc failed %s",
                vmk_StatusToString(status));
   }

   pageRange.startPage = tasksStartPage;
   pageRange.numPages = props.reservation;
   status = vmk_MapVA(&pageRange, 1, &va);
   if (status != VMK_OK) {
      vmk_Panic("vmklinux: LinuxTasksInitTasks: vmk_MapVA failed %s",
                vmk_StatusToString(status));
   }

   tasks = (LinuxTaskExt *)va;
   VMK_ASSERT(tasks != NULL);

   INIT_LIST_HEAD(&taskFreeChain);
   for (i = 0; i < maxWorlds; ++i) {
      list_add(&tasks[i].freeChain, &taskFreeChain);
   }

   return VMK_OK;
}

void
LinuxTaskCleanupTasks(void)
{
   VMK_ReturnStatus status;

   status = vmk_UnmapVA((vmk_VirtAddr)tasks);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_MemPoolFree(&tasksStartPage);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_MemPoolDestroy(tasksMemPool);
   VMK_ASSERT(status == VMK_OK);
}


/*
 *----------------------------------------------------------------------------
 *
 * LinuxTask_(Init|Cleanup) --
 *
 *      Initialization and cleanup of LinuxTask.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

void
LinuxTask_Init(void)
{
   VMK_ReturnStatus status;

   VMKLNX_CREATE_LOG();

   status = vmk_SPCreateIRQ(&semaLock, vmklinuxModID, "semaLck", NULL,
                            VMK_SP_RANK_IRQ_BLOCK);
   VMK_ASSERT(status == VMK_OK);
  
   status = vmk_SPCreateIRQ(&taskLock, vmklinuxModID,
                            "taskLock", NULL, VMK_SP_RANK_IRQ_BLOCK);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_SPCreateIRQ(&taskFreeLock, vmklinuxModID,
                            "taskFreeLock", NULL, VMK_SP_RANK_IRQ_BLOCK+1);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_WorldPrivateKeyCreate(&vmklnx_taskKey, LinuxTaskDestruct);
   if (status != VMK_OK) {
      vmk_Panic("vmklinux: LinuxTask_Init: "
                "vmk_WorldPrivateKeyCreate for vmklnx_taskKey failed %s",
                vmk_StatusToString(status));
   }

   status = LinuxTaskInitTasks();
   VMK_ASSERT(status == VMK_OK);
}

void
LinuxTask_Cleanup(void)
{
   LinuxTaskCleanupTasks();
   vmk_SPDestroyIRQ(&semaLock);
   vmk_SPDestroyIRQ(&taskLock);
   vmk_SPDestroyIRQ(&taskLock);
   vmk_WorldPrivateKeyDestroy(vmklnx_taskKey);
   VMKLNX_DESTROY_LOG();
}

