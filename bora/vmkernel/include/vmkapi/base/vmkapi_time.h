/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Time                                                           */ /**
 *
 * \defgroup Time Time and Timers
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_TIME_H_
#define _VMKAPI_TIME_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"

/** \brief Known invalid value for a timer */
#define VMK_INVALID_TIMER 0

typedef vmk_int64  vmk_TimerRelCycles;
typedef vmk_uint64 vmk_TimerCycles;

typedef vmk_AddrCookie vmk_TimerCookie;
typedef void (*vmk_TimerCallback)(vmk_TimerCookie data);
typedef vmk_uint64 vmk_Timer;

/**
 * \brief Representation for Time 
 */
typedef struct {
   vmk_int64 sec;                /* seconds */
   vmk_int64 usec;               /* microseconds */
} vmk_TimeVal;


/* Convenient time constants */
#define VMK_USEC_PER_SEC         1000000
#define VMK_MSEC_PER_SEC         1000
#define VMK_USEC_PER_MSEC        1000
#define VMK_USECS_PER_JIFFY      10000
#define VMK_JIFFIES_PER_SECOND   (VMK_USEC_PER_SEC/VMK_USECS_PER_JIFFY)

/*
 ***********************************************************************
 * vmk_jiffies --                                                */ /**
 *
 * \ingroup Time
 * \brief A global that increments every VMK_USECS_PER_JIFFY
 *        microsceonds.
 *
 ***********************************************************************
 */
extern volatile unsigned long vmk_jiffies;

/*
 ***********************************************************************
 * vmk_GetTimerCycles --                                          */ /**
 *
 * \ingroup Time
 * \brief Return the time elapsed since the VMKernel was loaded.
 *
 ***********************************************************************
 */
vmk_TimerCycles vmk_GetTimerCycles(void);

/*
 ***********************************************************************
 * vmk_TimerCyclesPerSecond --                                    */ /**
 *
 * \ingroup Time
 * \brief Return the frequency in Hz of the vmk_GetTimerCycles() clock.
 *
 ***********************************************************************
 */
vmk_uint64 vmk_TimerCyclesPerSecond(void);

/*
 ***********************************************************************
 * vmk_TimerUSToTC --                                             */ /**
 *
 * \ingroup Time
 * \brief Convert microseconds into timer cycles.
 *
 ***********************************************************************
 */
vmk_TimerRelCycles vmk_TimerUSToTC(
   vmk_int64 us);

/*
 ***********************************************************************
 * vmk_TimerTCToUS --                                             */ /**
 *
 * \ingroup Time
 * \brief Convert timer cycles into microseconds.
 *
 ***********************************************************************
 */
vmk_int64 vmk_TimerTCToUS(
   vmk_TimerRelCycles cycles);

/*
 ***********************************************************************
 * vmk_TimerTCToMS --                                             */ /**
 *
 * \ingroup Time
 * \brief Convert timer cycles into milliseconds.
 *
 ***********************************************************************
 */
vmk_int64 vmk_TimerTCToMS(
   vmk_TimerRelCycles cycles);
   
/*
 ***********************************************************************
 * VMK_ABS_TIMEOUT_MS --                                          */ /**
 *
 * \ingroup Time
 * \brief Convert a delay in milliseocnds into an absolute time in
 *        milliseconds.
 *
 * \param[in] to_ms  Millisecond delay to convert to absolute time.
 *
 * \return Absolute time in milliseconds from the time of the call until
 *         the given delay. Returns zero if zero is passed in.
 *
 ***********************************************************************
 */
#define VMK_ABS_TIMEOUT_MS(to_ms) \
   ((to_ms) ?  vmk_TimerTCToMS(vmk_GetTimerCycles()) + (to_ms) : 0)

/*
 ***********************************************************************
 * vmk_GetTimeOfDay --                                            */ /**
 *
 * \ingroup Time
 * \brief Get the time in vmk_TimeVal representation.
 *
 ***********************************************************************
 */
void vmk_GetTimeOfDay(
   vmk_TimeVal *tv);

/*
 ***********************************************************************
 * vmk_GetUptime --                                               */ /**
 *
 * \ingroup Time
 * \brief Get the uptime in vmk_TimeVal representation.
 *
 ***********************************************************************
 */
void vmk_GetUptime(
   vmk_TimeVal *tv);

/*
 ***********************************************************************
 * vmk_DelayUsecs --                                            */ /**
 *
 * \ingroup Time
 * \brief Spin-wait for a specified number of microseconds.
 *
 ***********************************************************************
 */
void vmk_DelayUsecs(
   vmk_uint32 uSecs);

/*
 ***********************************************************************
 * vmk_TimerAdd --                                                */ /**
 *
 * \ingroup Time
 * \brief Schedule a timer.
 *
 * The VMKernel can schedule simultaneously a limited number of timers
 * for each CPU.
 *
 * \warning Timers are a limited resource.  The VMKernel does not
 *          guarantee to provide more than 100 concurrent timers per CPU
 *          system-wide, and exceeding the limit is a fatal error.
 *
 * \param[in]  callback     Timer callback.
 * \param[in]  data         Argument passed to the timer callback on
 *                          timeout.
 * \param[in]  timeoutUs    Timeout in microseconds.
 * \param[in]  periodic     Whether the timer should automatically
 *                          reschedule itself.
 * \param[in]  rank         Major rank of the timer; see explanation of
 *                          lock and timer ranks in vmkapi_lock.h.
 * \param[out] timer        Timer reference.
 * 
 * \retval VMK_NO_RESOURCES Couldn't schedule the timer.
 * \retval VMK_OK           The timer was successfully scheduled.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_TimerAdd(
   vmk_TimerCallback callback,
   vmk_TimerCookie data,
   vmk_int32 timeoutUs,
   vmk_Bool periodic,
   vmk_SpinlockRank rank,
   vmk_Timer *timer);

/*
 ***********************************************************************
 * vmk_TimerModifyOrAdd --                                        */ /**
 *
 * \ingroup Time
 * \brief Schedule or reschedule a timer.
 *
 * Atomically remove the timer referenced by *timer (if pending) and
 * reschedule it with the given new parameters, possibly replacing
 * *timer with a new timer reference.  It is permissible for *timer to
 * be VMK_INVALID_TIMER on input; in this case a new timer reference is
 * always returned.  This function is slower than vmk_TimerAdd and
 * should be used only if the atomic replacement semantics are needed.
 *
 * \param[in]     callback   Timer callback
 * \param[in]     data       Argument passed to the timer callback on
 *                           timeout.
 * \param[in]     timeoutUs  Timeout in microseconds
 * \param[in]     periodic   Whether the timer should automatically
 *                           reschedule itself
 * \param[in]     rank       Major rank of the timer; see explanation of
 *                           lock and timer ranks in vmkapi_lock.h.
 * \param[in,out] timer      Timer reference
 * \param[out]    pending    Whether the timer was still pending when
 *                           modified.
 * 
 * \retval VMK_NO_RESOURCES  Couldn't schedule the timer.
 * \retval VMK_OK            The timer was successfully scheduled.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_TimerModifyOrAdd(
   vmk_TimerCallback callback,
   vmk_TimerCookie data,
   vmk_int32 timeoutUs,
   vmk_Bool periodic,
   vmk_SpinlockRank rank,
   vmk_Timer *timer,
   vmk_Bool *pending);

/*
 ***********************************************************************
 * vmk_TimerRemove --                                             */ /**
 *
 * \ingroup Time
 * \brief Cancel a scheduled timer.
 *
 * \param[in]  timer     A timer reference.
 *
 * \retval VMK_OK        The timer was successfully cancelled. If the
 *                       timer was one-shot, it did not fire and never
 *                       will.
 * \retval VMK_NOT_FOUND The timer had previously been removed, was a
 *                       one-shot that already fired, or is
 *                       VMK_INVALID_TIMER.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_TimerRemove(
   vmk_Timer timer);

/*
 ***********************************************************************
 * vmk_TimerRemoveSync --                                         */ /**
 *
 * \ingroup Time
 * \brief Cancel a scheduled timer
 *
 * If the timer fired before it could be cancelled, spin until the timer
 * callback completes.
 *
 * \warning This function must not be called from the timer callback
 *          itself.  It must be called with current lock rank less than
 *          the timer's rank; see an explanation of lock and timer ranks
 *          in vmkapi_lock.h.
 *
 * \param[in]  timer    A timer reference.
 *
 * \retval VMK_OK          The timer was successfully cancelled. If the
 *                         timer was one-shot, it did not fire and
 *                         never will.
 * \retval VMK_NOT_FOUND   The timer had previously been removed, was a
 *                         one-shot that already fired, or is
 *                         VMK_INVALID_TIMER.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_TimerRemoveSync(
  vmk_Timer timer);

/*
 ***********************************************************************
 * vmk_TimerIsPending --                                            */ /**
 *
 * \ingroup Time
 * \brief Query a timer to see if it is pending.
 *
 * \param[in]  timer  A timer reference.
 *
 * \retval VMK_TRUE   The timer is pending.  For one-shot timers, this
 *                    means the timer has neither fired nor been removed.
 *                    For periodic timers, it means the timer has not
 *                    been removed.
 * \retval VMK_FALSE  The timer is not pending.  For one-shot timers,
 *                    this means the timer has already fired, is in
 *                    the process of firing, or has been removed.  For
 *                    periodic timers, it means the timer has been
 *                    removed.  VMK_FALSE is also returned for
 *                    VMK_INVALID_TIMER.
 *
 ***********************************************************************
 */
vmk_Bool vmk_TimerIsPending(
   vmk_Timer timer);

#endif /* _VMKAPI_TIME_H_ */
/** @} */
