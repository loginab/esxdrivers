/* ****************************************************************
 * Portions Copyright 2005, 2009 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * VMKLinux supports a subset of the Linux signal-related operations and
 * state.  We only support signals for kernel threads (no user-level
 * signal delivery).  We only support pre-RT signals (signals 1 through
 * 31).  None of the "realtime" (i.e., POSIX signal queuing) signals are
 * supported.
 *
 * Signals may not be ignored.  Signals do not have handlers.  (Kernel
 * threads must explicitly check for pending signals, and flush all
 * signals after handling any they are interested in.)
 *
 * While Linux's existing task_struct is used to store the signal state,
 * we don't use much of it.  Just the signal masks for pending and blocked
 * low-numbered signals (task->pending.signal.sig[0] and
 * task->blocked.sig[0]) are used.
 */

#include <linux/list.h>
#include <linux/version.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/fs.h>

#include "linux_task.h"
#include "linux_stubs.h"
#include "vmklinux26_log.h"

#include "vmkapi.h"

/*
 * Only support the first 32 signals (none of the queuing or real-time
 * signals)
 */
#define LINUX_SIG_MAX 32


/*
 *----------------------------------------------------------------------
 *
 * siginitsetinv --
 *
 * 	Initialize all entries in the signal set (we only have 1) to the
 * 	*inverse* of the given mask.
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
siginitsetinv(sigset_t *set,
              unsigned long mask)
{
   struct task_struct *task;

   task = get_current();
   if (task) {
      unsigned long prevIRQL;
      LinuxTaskExt *te;
      
      te = container_of(task, LinuxTaskExt, task);
      prevIRQL = LinuxTask_Lock(te);
      /* Make sure the blocked signals array has only one element */
      VMK_ASSERT_ON_COMPILE((sizeof(task->blocked.sig) /
                             sizeof(task->blocked.sig[0])) == 1);
      task->blocked.sig[0] = ~mask;
      LinuxTask_Unlock(te, prevIRQL);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxSignalPendingInt --
 *
 *	 Same as signal_pending, but signal lock already held
 *
 * Results:
 *      0 if no signals pending on given task, non-zero if any are.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
LinuxSignalPendingInt(const struct task_struct* task)
{
   VMK_ASSERT(LinuxTask_IsLocked(container_of(task, LinuxTaskExt, task)) ==
              VMK_TRUE);

   /* Make sure there's only one element in the array of signals */
   VMK_ASSERT_ON_COMPILE((sizeof(task->blocked.sig)/
                          sizeof(task->blocked.sig[0])) == 1);
   VMK_ASSERT_ON_COMPILE((sizeof(task->pending.signal.sig)/
                          sizeof(task->pending.signal.sig)) == 1);

   VMKLNX_DEBUG(3, "task=%p pending=%#lx blocked=%#lx",
                 task, task->pending.signal.sig[0], task->blocked.sig[0]);
   return task->pending.signal.sig[0] & ~(task->blocked.sig[0]);
}

/**                                          
 *  signal_pending - Test if any non-blocked signals are pending
 *  @task: pointer to struct task_struct
 *                                           
 *  Tests if any non-blocked signals are pending
 *                                           
 *  RETURN VALUE:
 *       Return 0 if no signals are pending on given task, 
 *       non-zero if any.                                           
 */                                          
/* _VMKLNX_CODECHECK_: signal_pending */
int
signal_pending(struct task_struct* task)
{
   unsigned long prevIRQL;
   LinuxTaskExt *te;
   int rval;

   VMK_ASSERT(task);
   VMKLNX_DEBUG(4, "task=%p", task);

   te = container_of(task, LinuxTaskExt, task);
   VMK_ASSERT(LinuxTask_ValidTask(te) == VMK_TRUE);

   prevIRQL = LinuxTask_Lock(te);
   rval = LinuxSignalPendingInt(task);
   LinuxTask_Unlock(te, prevIRQL);
   return rval;
}


/*
 *----------------------------------------------------------------------
 *
 * recalc_sigpending --
 *
 *	Refresh signal pending bit in core vmkernel for the current task.
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
recalc_sigpending(void)
{
   struct task_struct* task = get_current();
   unsigned long prevIRQL;
   int pending;
   LinuxTaskExt *te = container_of(task, LinuxTaskExt, task);
   
   /*
    * Since drivers may twiddle .pending or .blocked directly, we'll
    * refresh the world-based signal pending bit.
    */

   prevIRQL = LinuxTask_Lock(te);
   pending = LinuxSignalPendingInt(task);
   LinuxTask_Unlock(te, prevIRQL);

   VMKLNX_DEBUG(4, "pending=%#x", pending);
}


/*
 *----------------------------------------------------------------------
 *
 * flush_signals --
 *
 *	Mark all signals as handled on given task (must map to current
 *	world).
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
flush_signals(struct task_struct* task)
{
   LinuxTaskExt *te;
   unsigned long prevIRQL;

   VMK_ASSERT(task != NULL);
   VMKLNX_DEBUG(1, "task=%p", task);

   // Only on ourselves
   VMK_ASSERT(get_current() == task);

   te = container_of(task, LinuxTaskExt, task);
   
   /* Ensure there is only 1 entry in the pending signal sig array */
   VMK_ASSERT_ON_COMPILE((sizeof(task->pending.signal.sig)/
                          sizeof(task->pending.signal.sig[0])) == 1);

   prevIRQL = LinuxTask_Lock(te);
   task->pending.signal.sig[0] = 0;
   LinuxTask_Unlock(te, prevIRQL);
}


/*
 *----------------------------------------------------------------------
 *
 * block_all_signals --
 *
 *      Unsupported
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
block_all_signals(int (*notifier)(void *priv),
                  void *priv,
                  sigset_t *mask)
{
   VMKLNX_DEBUG(0, "not implemented. notifier=%p priv=%p mask=...", 
                 notifier, priv);
}


/*
 *----------------------------------------------------------------------
 *
 * unblock_all_signals --
 *
 *      Unsupported.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
unblock_all_signals(void)
{
   // Not used by ISCSI, so not implemented.
   VMKLNX_DEBUG(0, "not implemented");   
}


/*
 *----------------------------------------------------------------------
 *
 * LinuxSignalSendByTask --
 *
 *	Send the given signal to the given task.
 *
 * Results:
 *      0 if signalled, -ESRCH otherwise.
 *
 * Side effects:
 *	Signal added to pending mask on target
 *
 *----------------------------------------------------------------------
 */

static int
LinuxSignalSendByTask(struct task_struct *task,
                      int sig)
{
   LinuxTaskExt *te;
   unsigned long prevIRQL;
   int p;

   VMK_ASSERT(task != NULL);

   te = container_of(task, LinuxTaskExt, task);
   VMK_ASSERT(LinuxTask_ValidTask(te) == VMK_TRUE);

   prevIRQL = LinuxTask_Lock(te);

   // Subtract 1 because signal 0 isn't valid, but bit 0 is
   task->pending.signal.sig[0] |= (1UL << (sig - 1));

   p = LinuxSignalPendingInt(task);

   LinuxTask_Unlock(te, prevIRQL);

   if (p != 0
       && (task->state == TASK_INTERRUPTIBLE) 
       && (te->flags & LT_TASK_SUSPENDED)) {
      /*
       * Wake up task since the signal is not blocked and the task is
       * suspended.
       */
      LinuxTask_Resume(te);
   }

   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * kill_proc --
 *
 *	Send given signal to given pid.  fromKernel must be 1.
 *
 * Results:
 *      0 on OK, -<errno> on error
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
kill_proc(pid_t pid,
          int sig,
          int fromKernel) // sent by kernel or kernel-thread.  Ignored.
{
   LinuxTaskExt *te;
   vmk_WorldPrivateDataHandle handle;
   int status;

   VMK_ASSERT(pid);
   VMK_ASSERT(fromKernel == 1);
   VMK_ASSERT(sig > 0);
   VMK_ASSERT(sig < LINUX_SIG_MAX);
   
   VMKLNX_DEBUG(0, "pid=%d, sig=%d", pid, sig);

   if (unlikely(pid < 0)) {
      VMKLNX_WARN("World %d tried to signal invalid pid (%d)...",
                  vmk_WorldGetID(), pid);
         return -ESRCH;
   }

   te = LinuxTask_FindByPid(pid, &handle);
   if (unlikely(te == NULL)) {
      VMKLNX_WARN("World %d tried to signal non-existing pid (%d)...",
                  vmk_WorldGetID(), pid);
      return -ESRCH;
   }

   status = LinuxSignalSendByTask(&te->task, sig);

   vmk_ReleasePrivateInfo(handle);

   return status;
}


/*
 *----------------------------------------------------------------------
 *
 * kill_proc_info_as_uid --
 *
 *	Send given signal to given pid.
 *
 * Results:
 *      0 on OK, -<errno> on error
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int 
kill_proc_info_as_uid(int signum,
                      struct siginfo *sinfo,
                      pid_t pid,
                      uid_t uid,
                      uid_t euid,
                      u32   secid)
{
   VMK_ASSERT(signum > 0);
   VMK_ASSERT(signum < LINUX_SIG_MAX);
   VMK_ASSERT(!uid);
   VMK_ASSERT(!euid);

   VMKLNX_DEBUG(0, "kill_proc_info_as_uid with signum=%d, "
                    "1st 8 bytes of siginfo=%llx, pid=%d, uid=%d, "
                    "euid=%d, secid=%d)",
                    signum, 
                    *(unsigned long long *)sinfo, pid, uid, 
                    euid, secid);

   return kill_proc(pid, signum, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * send_sig --
 *
 * Results:
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int 
send_sig(int signum,
         struct task_struct *task, 
         int fromKernel)
{
   VMK_ASSERT(signum > 0);
   VMK_ASSERT(signum < LINUX_SIG_MAX);
   VMK_ASSERT(fromKernel == 1);

   return LinuxSignalSendByTask(task, signum);
}


/*
 *----------------------------------------------------------------------
 *
 * send_sig_info --
 *
 * Results:
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int 
send_sig_info(int signum,
              struct siginfo *sinfo,
              struct task_struct *task)
{
   VMK_ASSERT(signum > 0);
   VMK_ASSERT(signum < LINUX_SIG_MAX);

   return LinuxSignalSendByTask(task, signum);
}


/**                                          
 *  fasync_helper - initialize an fasync_struct
 *  @fd: ignored
 *  @filep: used to initialize *fapp if *fapp is NULL
 *  @on: ignored
 *  @fapp: *fapp is initialized if it is NULL
 *
 *  Initializes the given fasync_struct. 
 *                                           
 *  NOTE:                   
 *  This function is currently unsupported on ESXi.
 *
 *  ESX Deviation Notes:
 *  On Linux, *fapp points to a valid fasync_struct but on vmklinux,
 *  it points at opaque internal data. Do not attempt to
 *  reference fasync struct members from the value returned in
 *  *fapp.
 *
 *  RETURN VALUES:
 *  0 for success,
 *  -EFAULT if the given fapp is NULL
 */                                          
/* _VMKLNX_CODECHECK_: fasync_helper */
int
fasync_helper(int fd, struct file *filep, int on, struct fasync_struct **fapp)
{
   /*
    * On Linux, this function saves the fd and filep in an fsync_struct
    * and enqueues the struct in a linked list.  For vmklinux, the
    * fasync_struct list is maintained on the COS side.
    *
    * Initializes *fapp if it is NULL, using the given filep.
    */
   if (fapp == NULL)
      return -EFAULT;

   if (*fapp == NULL)
      *fapp = (struct fasync_struct *)(vmk_VirtAddr)filep->f_owner.pid;

   return 0;
}


/**                                          
 *  kill_fasync - Send an fasync signal on behalf of a device       
 *  @fapp: Target for fasync    
 *  @sig: Signal to send
 *  @band: Signal band info 
 *                                           
 *  Deliver a SIGIO signal to a user program running in the COS that is
 *  attached to a vmkernel chardev and has requested fasync notification.                       
 *                                           
 *  ESX Deviation Notes:
 *               
 *  The sig and band arguments are ignored. 
 *
 *  Do not attempt to dereference the fapp argument since it contains
 *  vmklnx private data and may not be a valid pointer.
 *
 *  This function is unsupported for Visor.
 *                                           
 *  RETURN VALUE:
 *  This function does not return a value.
 */                                          
/* _VMKLNX_CODECHECK_: kill_fasync */
void
kill_fasync(struct fasync_struct **fapp, int sig, int band)
{
   vmk_uint16  major;
   vmk_uint16  minor;
   int         pid;

    /*
     * Use the major and minor device numbers stored in 
     * fapp to tell COS which device driver fasync_struct
     * queue to use when calling kill_fasync.
     */
   if ((fapp == NULL) || (*fapp == NULL))
      return;

   pid = (int)(vmk_VirtAddr) *fapp;
   LinuxChar_PIDToMajorMinor(pid, &major, &minor);

   vmk_CharDevNotifyFasyncCompleteWithMinor(major, minor);
}
