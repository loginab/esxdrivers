/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: sa_timer.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_TIMER_H_
#define _LIBSA_TIMER_H_

#include <linux/timer.h>

/*
 * Timer facility.
 */

struct sa_timer {
	struct timer_list tm_list;
};


#define SA_TIMER_UNITS  (1000 * 1000UL)	/* number of timer ticks per second */


/*
 * Initialize a timer structure.  Set handler.
 */
static inline void sa_timer_init(struct sa_timer *tm,
					void (*handler)(void *), void *arg)
{
	setup_timer(&tm->tm_list, (void (*)(unsigned long)) handler,
			    (unsigned long) arg);
}

/*
 * Test whether the timer is active.
 */
static inline int sa_timer_active(struct sa_timer *tm)
{
	return timer_pending(&tm->tm_list);
}

/*
 * Allocate a timer structure.  Set handler.
 */
struct sa_timer *sa_timer_alloc(void (*)(void *arg), void *arg);

/*
 * Set timer to fire.   Delta is in microseconds from now.
 */
void sa_timer_set(struct sa_timer *, u_long delta);

/*
 * Cancel timer.
 */
void sa_timer_cancel(struct sa_timer *);

/*
 * Free (and cancel) timer.
 */
void sa_timer_free(struct sa_timer *);


/*
 * Handle timer checks.  Called from select loop or other periodic function.
 *
 * The struct timeval passed in indicates how much time has passed since
 * the last call, and is set before returning to the maximum amount of time
 * that should elapse before the next call.
 *
 * Returns 1 if any timer handlers were invoked, 0 otherwise.
 */
int sa_timer_check(struct timeval *);

/*
 * Get time in nanoseconds since some arbitrary time.
 */
u_int64_t sa_timer_get(void);

/*
 * Get time in seconds since some arbitrary time.
 */
u_int sa_timer_get_secs(void);

#endif /* _LIBSA_TIMER_H_ */
