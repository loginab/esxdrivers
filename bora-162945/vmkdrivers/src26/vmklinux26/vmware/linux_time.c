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
 *  linux_time.c
 *
 * From linux-2.6.24.7/kernel/timer.c:
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * 1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 *
 * 1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-12-24  Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *             serialize accesses to xtime/lost_ticks).
 *                             Copyright (C) 1998  Andrea Arcangeli
 * 1999-03-10  Improved NTP compatibility by Ulrich Windl
 * 2002-05-31  Move sys_sysinfo here and make its locking sane, Robert Love
 * 2000-10-05  Implemented scalable SMP per-CPU timer handling.
 *                             Copyright (C) 2000, 2001, 2002  Ingo Molnar
 *             Designed by David S. Miller, Alexey Kuznetsov and Ingo Molnar
 *
 ******************************************************************/

#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/time.h>

#include "vmkapi.h"
#include "vmklinux26_dist.h"
#include "linux_stubs.h"

/**                                          
 *  init_timer - initialize a timer descriptor       
 *  @timer: timer to initialize     
 *                                           
 *  Initialize the private parts of a timer descriptor so it is valid
 *  for use.
 *  
 *  RETURN VALUE:
 *   None.                               
 */                                          
/* _VMKLNX_CODECHECK_: init_timer */
void fastcall 
init_timer(struct timer_list *timer)
{
   timer->vmk_handle = VMK_INVALID_TIMER;
}

/*
 *  __add_timer --
 *      Helper routine that translates jiffies into micro seconds
 *      and invokes vmk_TimerAdd to create the timer.
 *
 * Results:
 *      None.
 */
void
__add_timer(struct timer_list *timer)
{
   VMK_ReturnStatus status;
   vmk_uint64 timeout, current_jiffies;

   /*
    * Make sure the timeout value is at least 1 jiffy or (10000 usec).
    */
   current_jiffies = jiffies;
   if (timer->expires <= current_jiffies) {
      timeout = JIFFIES_TO_USEC(1);
   } else {
      timeout = JIFFIES_TO_USEC(timer->expires - current_jiffies);
   }

   /* vmkapi uses a 32-bit signed timeout value */
   if (timeout > INT_MAX) {
       timeout = INT_MAX;
   }

   /*
    * On Linux, there is no failure condition for addding a timer, so
    * panic on errors to ensure they don't go undetected.
    */
   status = vmk_TimerAdd(timer->function, timer->data, timeout, VMK_FALSE,
                         VMK_SP_RANK_UNRANKED, &timer->vmk_handle);
   if ((status != VMK_OK)) {
      vmk_Panic("vmklinux26: __add_timer failed. %s",
                vmk_StatusToString(status));
   }
}

/*
 *  __timer_pending --
 *      Check if a timer is pending.
 *
 * Results:
 *      0 means the timer was not pending when checked;
 *      1 means the timer was pending.
 */
int 
__timer_pending(const struct timer_list *timer)
{
   vmk_Timer h;
   vmk_Bool ret;

   VMK_ASSERT(timer != NULL);
   // Loop in case vmk_ModifyOrAddTimer concurrently changes the handle
   do {
      h = timer->vmk_handle;
      ret = vmk_TimerIsPending(h);
   } while (unlikely(h != timer->vmk_handle));
   return ret;
}

/**                                          
 *  del_timer - disarms and removes a timer.
 *  @timer: pointer to the descriptor of the intended timer
 *                                           
 *  Function to disarm and remove a timer. 
 *
 *  RETURN VALUE:
 *  1 - if it has disarmed a pending timer; 
 *  0 - otherwise.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: del_timer */
int 
del_timer(struct timer_list *timer)
{
   vmk_Bool ret;
   vmk_Timer h;

   VMK_ASSERT(timer != NULL);
   // Loop in case vmk_ModifyOrAddTimer concurrently changes the handle
   do {
      h = timer->vmk_handle;
      ret = (vmk_TimerRemove(h) == VMK_OK);
   } while (unlikely(!ret && h != timer->vmk_handle));
   return ret;
}

/**                                          
 *  del_timer_sync - disarms and removes a timer by waiting for timer handler to 
 *  complete.
 *  @timer: pointer to the timer struct to be deleted.    
 *                                           
 *  This function disarms and removes a timer. If the timer has gone off already,
 *  the function waits for it (the handler) to run to completion.
 *                                           
 *  RETURN VALUE:                     
 *  1 - if it has disarmed a timer 
 *  0 - otherwise
 */                                          
/* _VMKLNX_CODECHECK_: del_timer_sync */
int
del_timer_sync(struct timer_list *timer)
{
   vmk_Bool ret;
   vmk_Timer h;

   VMK_ASSERT(timer != NULL);
   // Loop in case vmk_ModifyOrAddTimer concurrently changes the handle
   do {
      h = timer->vmk_handle;
      ret = (vmk_TimerRemoveSync(h) == VMK_OK);
   } while (unlikely(!ret && h != timer->vmk_handle));
   return ret;
}

/**                                          
 *  __mod_timer - Setup (or modify) a timer 
 *  @timer: pointer to the timer to be added/modified.    
 *  @expires: expiration value.    
 *
 *  __mod_timer --
 *      Helper routine that modifies the expiration time of a timer if
 *      pending, or adds it as a new timer if not already pending.
 *
 * RETURN VALUE:
 *      0 means the timer was not pending when being modified;
 *      1 means the timer was pending.
 */
/* _VMKLNX_CODECHECK_: __mod_timer */
int
__mod_timer(struct timer_list *timer, unsigned long expires)
{
   vmk_Bool pending;
   vmk_uint64 timeout, current_jiffies;

   BUG_ON(!timer->function);
   timer->expires = expires;

   /*
    * Make sure the timeout value is at least 1 jiffy or (10000 usec).
    */
   current_jiffies = jiffies;
   if (timer->expires <= current_jiffies) {
      timeout = JIFFIES_TO_USEC(1);
   } else {
      timeout = JIFFIES_TO_USEC(timer->expires - current_jiffies);
   }

   /* vmkapi uses a 32-bit signed timeout value */
   if (timeout > INT_MAX) {
       timeout = INT_MAX;
   }

   /*
    * On Linux, there is no failure condition for addding a timer, so
    * panic on errors to ensure they don't go undetected.
    */
   if (vmk_TimerModifyOrAdd(timer->function, timer->data, timeout, VMK_FALSE,
                            VMK_SP_RANK_UNRANKED,
                            &timer->vmk_handle, &pending) != VMK_OK) {
      vmk_Panic("vmklinux26: __mod_timer failed.");
   }
   return pending;
}

/**
 *  get_seconds - Return time in seconds since 1970.
 *
 *	Return time in seconds since 1970.                                           
 *
 *  RETURN VALUE:
 *  The time in seconds since 1970
 */                                          
/* _VMKLNX_CODECHECK_: get_seconds */
unsigned long get_seconds(void)
{
     vmk_TimeVal tv;
     vmk_GetTimeOfDay(&tv);
     return tv.sec;
}

/**
 *  current_kernel_time - get current kernel time
 *
 *  The return value is the time passed  since 1970
 *
 *  RETURN VALUE:
 *  current kernel time
 */
/* _VMKLNX_CODECHECK_: current_kernel_time */
struct timespec current_kernel_time(void)
{
     vmk_TimeVal tv;
     struct timespec ts;

     vmk_GetTimeOfDay(&tv);
     ts.tv_sec = tv.sec;
     ts.tv_nsec = tv.usec * 1000;
     return ts;
}

/**
 * The round_jiffies() and family were taken from the
 * 2.6.24-stable kernel git tree.
 **/
/**
 * __round_jiffies - function to round jiffies to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * __round_jiffies() rounds an absolute time in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The exact rounding is skewed for each processor to avoid all
 * processors firing at the exact same time, which could lead
 * to lock contention or spurious cache line bouncing.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long __round_jiffies(unsigned long j, int cpu)
{
	int rem;
	unsigned long original = j;

	/*
	 * We don't want all cpus firing their timers at once hitting the
	 * same lock or cachelines, so we skew each extra cpu with an extra
	 * 3 jiffies. This 3 jiffies came originally from the mm/ code which
	 * already did this.
	 * The skew is done by adding 3*cpunr, then round, then subtract this
	 * extra offset again.
	 */
	j += cpu * 3;

	rem = j % HZ;

	/*
	 * If the target jiffie is just after a whole second (which can happen
	 * due to delays of the timer irq, long irq off times etc etc) then
	 * we should round down to the whole second, not up. Use 1/4th second
	 * as cutoff for this rounding as an extreme upper bound for this.
	 */
	if (rem < HZ/4) /* round down */
		j = j - rem;
	else /* round up */
		j = j - rem + HZ;

	/* now that we have rounded, subtract the extra skew again */
	j -= cpu * 3;

	if (j <= jiffies) /* rounding ate our timeout entirely; */
		return original;
	return j;
}

/**
 * __round_jiffies_relative - function to round jiffies to a full second
 * @j: the time in (relative) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * __round_jiffies_relative() rounds a time delta  in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The exact rounding is skewed for each processor to avoid all
 * processors firing at the exact same time, which could lead
 * to lock contention or spurious cache line bouncing.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long __round_jiffies_relative(unsigned long j, int cpu)
{
	/*
	 * In theory the following code can skip a jiffy in case jiffies
	 * increments right between the addition and the later subtraction.
	 * However since the entire point of this function is to use approximate
	 * timeouts, it's entirely ok to not handle that.
	 */
	return  __round_jiffies(j + jiffies, cpu) - jiffies;
}

/**
 * round_jiffies - function to round jiffies to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 *
 * round_jiffies() rounds an absolute time in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * RETURN VALUE:
 * The return value is the rounded version of the @j parameter.
 */
/* _VMKLNX_CODECHECK_: round_jiffies */
unsigned long round_jiffies(unsigned long j)
{
	return __round_jiffies(j, raw_smp_processor_id());
}

/**
 * round_jiffies_relative - function to round jiffies to a full second
 * @j: the time in (relative) jiffies that should be rounded
 *
 * round_jiffies_relative() rounds a time delta  in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * RETURN VALUE:
 * The return value is the rounded version of the @j parameter.
 */
/* _VMKLNX_CODECHECK_: round_jiffies_relative */
unsigned long round_jiffies_relative(unsigned long j)
{
	return __round_jiffies_relative(j, raw_smp_processor_id());
}


