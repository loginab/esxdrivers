/* ****************************************************************
 * Portions Copyright 2007 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/******************************************************************
 *
 *  linux_kthread.c
 *
 *      Emulation for Linux kthreads.
 *
 * From linux-2.6.18-8/kernel/kthread.c:
 *
 * Copyright (C) 2004 IBM Corporation, Rusty Russell.
 *
 ******************************************************************/

#include <asm/semaphore.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include "linux_stubs.h"
#include "linux_task.h"

struct kthread_create_info
{
   int (*threadfn)(void *data);
   void *data;
   struct completion done;
};

struct kthread_stop_info
{
   pid_t k;
   int err;
   struct completion done;
};

static struct kthread_stop_info kthread_stop_info;
static struct mutex kthread_stop_lock;

/**                                          
 *  kthread_should_stop - check if the current kernel thread needs to exit
 *                                           
 *  Check if the calling kthread has been notified to stop.
 *  The notification can only be delivered via an invocation of
 *  kthread_stop() by a different thread. 
 *  
 *                                           
 *  RETURN VALUE:
 *  Non-zero if a stop notification has been delivered; otherwise 0.
 *
 *  SEE ALSO:
 *  kthread_create() and kthread_stop()
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: kthread_should_stop */
int 
kthread_should_stop(void) 
{
   return (kthread_stop_info.k == get_current()->pid);
}

static int 
kthread(void *_create) 
{
   struct kthread_create_info *create = _create;
   int (*threadfn)(void *data);
   void *data;
   int ret = -EINTR;

   VMK_ASSERT_CPU_HAS_INTS_ENABLED();

   threadfn = create->threadfn;
   data = create->data;

   complete(&create->done);

    __set_current_state(TASK_INTERRUPTIBLE);
   schedule();

   if (!kthread_should_stop()) {
      ret = threadfn(data);
   }

   if (kthread_should_stop()) {
      kthread_stop_info.err = ret;
      complete(&kthread_stop_info.done);
   }

   return ret;
}

/*
 * ----------------------------------------------------------------------------- 
 *
 * LinuxKthread_Cleanup --
 *
 *    Called when vmklinux is cleaning up.
 *
 *
 * Results:
 *   void
 *
 * Side effects:
 *   kthread locking primitives are destroyed
 *
 * -----------------------------------------------------------------------------
 */
void 
LinuxKthread_Cleanup(void)
{
   mutex_destroy(&kthread_stop_lock);
}


/*
 * ----------------------------------------------------------------------------- 
 *
 * LinuxKthread_Init --
 *
 *    Initializes kthread data structures
 *
 *
 * Results:
 *    void
 *
 * Side effects:
 *    kthread locking primitives are initialized
 *
 * -----------------------------------------------------------------------------
 */
void LinuxKthread_Init(void) 
{
   mutex_init(&kthread_stop_lock);
}

/**                                          
 *  kthread_create - create a kernel thread
 *  @threadfn: pointer to the thread entry function
 *  @data: argument to be passed to @threadfn
 *  @namefmt: printf style format specification string
 *                                           
 *  Create a new kernel thread using @threadfn as the thread starting entry
 *  point. @data is passed as argument to @threadfn when it is invoked.
 *
 *  The thread is named according the format specification provided by 
 *  @namefmt.  Additional input required by @namefmt should be provided 
 *  in the list of arguments that go after @namefmt.
 *                                           
 *  ESX Deviation Notes:                     
 *  On esx, there are no threads, or light weight processes. Scheduleable
 *  execution units are implemented as Worlds. The resulting world created
 *  by the function is named after the module that invokes it, not by the 
 *  name specified in the argument list. However, the name specified in the 
 *  call is kept in the "comm" field in task_struct. 
 *                                           
 *  RETURN VALUE:
 *  On success, return a pointer to the task_struct of the new thread;
 *  on failure, return -errno (negative errno) casted as a pointer.
 *
 *  SEE ALSO:
 *  kthread_stop() and kthread_should_stop()
 */                                          
/* _VMKLNX_CODECHECK_: kthread_create */
struct task_struct *
kthread_create(int (*threadfn)(void *data),
				   void *data,
				   const char namefmt[],
				   ...)
{
   struct kthread_create_info create;
   LinuxTaskExt *te;
   struct task_struct *task;
   va_list args;

   create.threadfn = threadfn;
   create.data = data;

   init_completion(&create.done);

   te = LinuxTask_Create(kthread, &create, USE_MOD_ID, LT_KTHREAD);

   if (te == NULL) {
      return ERR_PTR(-ECHILD);
   }

   task = &te->task;
   va_start(args, namefmt);
   vmk_Vsnprintf(task->comm, sizeof(task->comm), namefmt, args);
   va_end(args);

   wait_for_completion(&create.done); 

   return task;
}

/**                                          
 *  kthread_stop - deliver a stop notification to a kernel thread 
 *  @k: a pointer to a task_struct 
 *                                           
 *  Deliver a stop notification to the kthread identified by the arugment 
 *  @k. This function will block until the targeted kthread has exited.
 *                                           
 *  RETURN VALUE:
 *  The exit value returned by @k.
 *                                           
 *  SEE ALSO:
 *  kthread_create() and kthread_should_stop()
 */                                          
/* _VMKLNX_CODECHECK_: kthread_stop */
int 
kthread_stop(struct task_struct *k) 
{
   int ret;

   if (!k)
      return -EINVAL;
   
   mutex_lock(&kthread_stop_lock);

   init_completion(&kthread_stop_info.done);
   smp_wmb();

   kthread_stop_info.k = k->pid;

   wake_up_process(k);
   
   wait_for_completion(&kthread_stop_info.done);

   kthread_stop_info.k = 0;
   ret = kthread_stop_info.err;

   mutex_unlock(&kthread_stop_lock);

   return ret;
}

/*
 * -----------------------------------------------------------------------------
 *
 * kthread_bind --
 *
 *   Mark a kthread as only being able to run on the given physical CPU
 * 
 * Results:
 *   None.
 *
 * -----------------------------------------------------------------------------
 */
/**                                          
 *  kthread_bind - Bind a kthread's execution to a specific physical CPU       
 *  @task: The task_struct object for the thread being modified
 *  @cpu: The physical cpu, 0-indexed, to which the thread will be bound   
 *                                           
 *  kthread_bind takes a Linux task that has not yet begun execution and marks
 *  its scheduling parameters such that the task, once started, will only ever
 *  execute on the given physical CPU.  kthread_bind() must be called prior to 
 *  calling wake_up_process().
 *                                                             
 *  Users of kthread_bind() should keep in mind that unlike Linux, ESX has many
 *  more schedulable entities within the VMM and among other virtual machines.
 *  Given that ESX tasks and threads are non-preemptable, one must exercise
 *  extreme caution when using kthread_bind so as to not waste processing resources
 *  that could be used for scheduling VMs or other schedulable entities that
 *  aid in I/O and resource management.   Because ESX has far more things to
 *  schedule than Linux, using kthread_bind may have unintended consequences
 *  in ESX, perhaps reducing performance of the overall system.  Avoid using
 *  this function if at all possible. 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: kthread_bind */
void 
kthread_bind(struct task_struct *task, unsigned int cpu)
{
   vmk_AffinityMask affinity;

   VMK_ASSERT(task != NULL);

   if ((affinity = vmk_AffinityMaskCreate(VMK_VMKERNEL_MODULE_ID)) != NULL) {
      vmk_AffinityMaskAdd(cpu, affinity);
      vmk_WorldSetAffinity(task->pid, affinity);
      vmk_AffinityMaskDestroy(affinity);
   }
}
