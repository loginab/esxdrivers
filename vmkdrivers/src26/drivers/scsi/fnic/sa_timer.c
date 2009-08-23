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
 * $Id: sa_timer.c 18557 2008-09-14 22:36:38Z jre $
 */

#include "sa_kernel.h"
#include <linux/timer.h>
#include <linux/jiffies.h>
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_timer.h"


/*
 * Allocate a timer structure.  Set handler.
 */
struct sa_timer *sa_timer_alloc(void (*handler)(void *), void *arg)
{
	struct sa_timer *tm;

	tm = sa_malloc(sizeof(*tm));
	if (tm)
		sa_timer_init(tm, handler, arg);
	return tm;
}

u_int64_t sa_timer_get(void)
{
	return (u_int64_t) get_jiffies_64() * TICK_NSEC;
}

/*
 * Get monotonic time since some arbitrary time in the past.
 * If _POSIX_MONOTONIC_CLOCK isn't available, we'll use time of day.
 */
u_int sa_timer_get_secs(void)
{
	return jiffies_to_msecs(get_jiffies_64()) / 1000;
}

/*
 * Set timer to fire.   Delta is in microseconds from now.
 */
void sa_timer_set(struct sa_timer *tm, u_long delta_usec)
{
	mod_timer((struct timer_list *) tm,
			jiffies + usecs_to_jiffies(delta_usec));
}

/*
 * Cancel timer if it is active.
 */
void sa_timer_cancel(struct sa_timer *tm)
{
	del_timer((struct timer_list *) tm);
}

/*
 * Free (and cancel) timer.
 */
void sa_timer_free(struct sa_timer *tm)
{
	del_timer_sync((struct timer_list *) tm);
	sa_free(tm);
}

