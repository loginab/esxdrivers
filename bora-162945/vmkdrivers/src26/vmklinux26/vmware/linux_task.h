/* ****************************************************************
 * Portions Copyright 2005 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_task.h --
 *
 *      Linux kernel task compatibility.
 */

#ifndef _LINUX_TASK_H_
#define _LINUX_TASK_H_

#include <sched.h>
#include "vmkapi.h"

typedef struct LinuxTaskExt {
   struct list_head   node;
   struct task_struct task;
   struct task_struct *parent;
   int                (*func)(void *);
   void               *arg;
   int                (*kthreadFunc)(void *);
   void               *kthreadArg;
   int                retval;
   vmk_ModuleID       modID;
   vmk_HeapID         heapID;
   u32                flags;
   u32                events;
   void               *private;
   int                private_usage;
   struct list_head   freeChain;
} LinuxTaskExt;

/*
 * Bit definitions for the event field in LinuxTaskExt.
 */
#define LTE_RESUME		0x0001       /* Tell task to resume execution */

/* 
 * Bit definitions for the flags field in LinuxTaskExt.
 */
#define LT_VMKLINUX_OWNER	0x01     /* Task created by vmklinux          */
#define LT_KTHREAD              0x02     /* Synchronous kthread creation      */
#define LT_TASK_SUSPENDED       0x04     /* Task is suspended                 */

#define INVALID_PID VMK_INVALID_WORLD_ID

extern vmk_SpinlockIRQ taskLock;

extern void LinuxTask_Init(void);
extern void LinuxTask_Cleanup(void);
extern void LinuxTask_WaitEvent(u32 eventID);
extern void LinuxTask_SendEvent(LinuxTaskExt *te, u32 eventID);
extern LinuxTaskExt *LinuxTask_FindByPid(pid_t pid, vmk_WorldPrivateDataHandle *handle);

#define USE_MOD_ID	VMK_TRUE /* Use with the useModID parameter below */
extern LinuxTaskExt *LinuxTask_Create(int (*fn)(void *),
		                      void *arg,
		                      vmk_Bool useModID,
                                      u32 flags);


extern void LinuxTask_Exit(LinuxTaskExt *te);
#if defined(VMX86_DEBUG)
extern vmk_Bool LinuxTask_ValidTask(LinuxTaskExt *te);
extern vmk_Bool LinuxTask_IsLocked(LinuxTaskExt *te);
#else
#define LinuxTask_ValidTask(te)
#define LinuxTask_IsLocked(te)
#endif /* defined(VMX86_DEBUG) */


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_IsVmklinuxTask --
 *      Check to see if the task identified by te is a
 *      task created by vmklinux.
 *
 * Results:
 *      1 if current task is created by vmklinux; 0 if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static inline int
LinuxTask_IsVmklinuxTask(LinuxTaskExt *te)
{
   return te->flags & LT_VMKLINUX_OWNER;
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_Suspend --
 *      Suspend the execution of the current running task.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static inline void
LinuxTask_Suspend(void)
{
   LinuxTask_WaitEvent(LTE_RESUME);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTaskResume --
 *      Resume the execution of the task identified by te.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static inline void
LinuxTask_Resume(LinuxTaskExt *te)
{
   LinuxTask_SendEvent(te, LTE_RESUME);
}

/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_Lock --
 *      Acquire exclusive access to the extended task descriptor
 *      identified by te.
 *
 * Results:
 *      Return the interrupt level
 *
 * Side effects:
 *	Disable interrupt.
 *
 *----------------------------------------------------------------------
 */

static inline unsigned long 
LinuxTask_Lock(LinuxTaskExt *te)
{
   /*
    * We may need per LinuxTaskExt lock. But for now, we
    * go with a global lock.
    */
   return vmk_SPLockIRQ(&taskLock);
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxTask_Unlock --
 *      Open up access to the extended task descriptor identified by
 *      te to other process, and restore the interrupt level to prevIRQL.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static inline void
LinuxTask_Unlock(LinuxTaskExt *te, unsigned long prevIRQL)
{
   vmk_SPUnlockIRQ(&taskLock, prevIRQL);
}

static inline void
LinuxTask_SetPrivateData(void *data, int usageFlag)
{
   LinuxTaskExt   *te;
   unsigned long prevIRQL;

   te = container_of(get_current(), LinuxTaskExt, task);
   prevIRQL = LinuxTask_Lock(te);

   VMK_ASSERT(te->private == NULL);
   VMK_ASSERT(te->private_usage == 0);

   te->private       = data;
   te->private_usage = usageFlag;

   LinuxTask_Unlock(te, prevIRQL);
}

static inline void
LinuxTask_ClearPrivateData(void *data, int usageFlag)
{
   LinuxTaskExt   *te;
   unsigned long prevIRQL;

   te = container_of(get_current(), LinuxTaskExt, task);
   prevIRQL = LinuxTask_Lock(te);

   VMK_ASSERT(te->private == data);
   VMK_ASSERT(te->private_usage == usageFlag);

   te->private       = NULL;
   te->private_usage = 0;

   LinuxTask_Unlock(te, prevIRQL);
}

static inline void *
LinuxTask_GetPrivateData(LinuxTaskExt *te)
{
   VMK_ASSERT(te->private != NULL);
   return te->private;
}
#endif /* _LINUX_TASK_H_ */
