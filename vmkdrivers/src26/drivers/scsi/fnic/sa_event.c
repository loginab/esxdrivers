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
 * $Id: sa_event.c 21807 2008-12-11 22:00:31Z cnelluri $
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_event.h"
#include "queue.h"

/*
 * Semi-opaque event structure.
 */
struct sa_event {
	TAILQ_ENTRY(sa_event) se_list;
	sa_event_handler_t *se_handler;
	void		*se_arg;
	u_char		se_flags;
};

#define SEV_MARK    0x01	/* special call marker event */

/*
 * Event list head.
 */
struct sa_event_list {
	TAILQ_HEAD(sa_event_head, sa_event) se_head;
	atomic_t	se_refcnt;	/* references to list header */
	atomic_t	se_pending_events; /* flags for pending events */
	spinlock_t	se_lock;	/* lock protecting list changes */
};

static void sa_event_list_hold(struct sa_event_list *);
static void sa_event_list_release(struct sa_event_list *);

/*
 * Common check for use by assertions.
 */
static inline int sa_event_list_initialized(struct sa_event_list *lp)
{
	return lp && lp->se_head.tqh_last && atomic_read(&lp->se_refcnt);
}

struct sa_event_list *sa_event_list_alloc(void)
{
	struct sa_event_list *lp;

	lp = sa_malloc(sizeof(*lp));
	if (lp) {
		memset(lp, 0, sizeof(*lp));
		TAILQ_INIT(&lp->se_head);
		atomic_set(&lp->se_refcnt, 1);
		spin_lock_init(&lp->se_lock);
		ASSERT(sa_event_list_initialized(lp));
	}
	return lp;
}

void sa_event_list_free(struct sa_event_list *lp)
{
	struct sa_event *ev;
	struct sa_event *next;
	unsigned long flags;

	ASSERT(sa_event_list_initialized(lp));
	spin_lock_irqsave(&lp->se_lock, flags);
	TAILQ_FOREACH_SAFE(ev, &lp->se_head, se_list, next) {
		if ((ev->se_flags & SEV_MARK) == 0) {
			TAILQ_REMOVE(&lp->se_head, ev, se_list);
			sa_free(ev);
		}
	}
	spin_unlock_irqrestore(&lp->se_lock, flags);
	sa_event_list_release(lp);
}

static void sa_event_list_free_int(struct sa_event_list *lp)
{
	ASSERT(!atomic_read(&lp->se_refcnt));
	sa_free(lp);
}

static void sa_event_list_hold(struct sa_event_list *lp)
{
	atomic_inc(&lp->se_refcnt);
	ASSERT(atomic_read(&lp->se_refcnt));
}

static void sa_event_list_release(struct sa_event_list *lp)
{
	ASSERT(atomic_read(&lp->se_refcnt));
	if (atomic_dec_and_test(&lp->se_refcnt))
		sa_event_list_free_int(lp);
}

/*
 * Queue handler for event.
 * The handler pointer is returned.  If the allocation fails, NULL is returned.
 * If the handler is already queued, just return the old handler pointer.
 */
struct sa_event *sa_event_enq(struct sa_event_list *lp,
			      void (*handler) (int, void *), void *arg)
{
	struct sa_event *ev;
	struct sa_event_head *hp;
	unsigned long flags;

	ASSERT(sa_event_list_initialized(lp));
	ASSERT(handler != NULL);
	spin_lock_irqsave(&lp->se_lock, flags);
	hp = &lp->se_head;
	TAILQ_FOREACH(ev, &lp->se_head, se_list) {
		if (ev->se_handler == handler && ev->se_arg == arg)
			break;
	}
	if (ev == NULL) {
		ev = sa_malloc(sizeof(*ev));
		if (ev != NULL) {
			memset(ev, 0, sizeof(*ev));
			ev->se_handler = handler;
			ev->se_arg = arg;
			TAILQ_INSERT_TAIL(hp, ev, se_list);
		}
	}
	spin_unlock_irqrestore(&lp->se_lock, flags);

	return ev;
}

void sa_event_deq_ev(struct sa_event_list *lp, struct sa_event *ev)
{
	unsigned long flags;

	spin_lock_irqsave(&lp->se_lock, flags);
	ASSERT(sa_event_list_initialized(lp));
	ASSERT((ev->se_flags & SEV_MARK) == 0);
	TAILQ_REMOVE(&lp->se_head, ev, se_list);
	spin_unlock_irqrestore(&lp->se_lock, flags);
	sa_free(ev);
}

void
sa_event_deq(struct sa_event_list *lp, void (*handler)(int, void *), void *arg)
{
	struct sa_event *ev;
	struct sa_event *next;
	unsigned long flags;

	spin_lock_irqsave(&lp->se_lock, flags);
	ASSERT(sa_event_list_initialized(lp));

	TAILQ_FOREACH_SAFE(ev, &lp->se_head, se_list, next) {
		if (ev->se_handler == handler && ev->se_arg == arg) {
			TAILQ_REMOVE(&lp->se_head, ev, se_list);
			sa_free(ev);
			break;
		}
	}
	spin_unlock_irqrestore(&lp->se_lock, flags);
}

/*
 * Call event on the list.
 * A temporary entry is inserted into the list to track our progress, and
 * we hold a reference on the list to make sure the whole thing isn't
 * removed.
 */
void sa_event_call(struct sa_event_list *lp, int rc)
{
	struct sa_event *ev;
	struct sa_event mark;		/* special list element on stack */
	unsigned long flags;

	spin_lock_irqsave(&lp->se_lock, flags);
	ASSERT(sa_event_list_initialized(lp));
	memset(&mark, 0, sizeof(mark));
	mark.se_flags = SEV_MARK;

	sa_event_list_hold(lp);
	ev = TAILQ_FIRST(&lp->se_head);
	while (ev != NULL) {
		if (ev->se_flags & SEV_MARK) {
			ev = TAILQ_NEXT(ev, se_list);
			continue;
		}
		TAILQ_INSERT_AFTER(&lp->se_head, ev, &mark, se_list);
		spin_unlock_irqrestore(&lp->se_lock, flags);

		ASSERT(ev->se_handler != NULL);
		(*ev->se_handler)(rc, ev->se_arg);

		spin_lock_irqsave(&lp->se_lock, flags);
		ev = TAILQ_NEXT(&mark, se_list);
		TAILQ_REMOVE(&lp->se_head, &mark, se_list);
	}
	spin_unlock_irqrestore(&lp->se_lock, flags);
	sa_event_list_release(lp);
}

/*
 * Set an event to be delivered later.
 */
void sa_event_call_defer(struct sa_event_list *lp, int event)
{
	ASSERT(sa_event_list_initialized(lp));
	ASSERT(event >= 0);
	ASSERT(event < (int)sizeof(lp->se_pending_events) * 8);
	atomic_set_mask(1 << event, &lp->se_pending_events);
}

/*
 * Cancel an event that might've been deferred.
 */
void sa_event_call_cancel(struct sa_event_list *lp, int event)
{
	ASSERT(sa_event_list_initialized(lp));
	ASSERT(event >= 0);
	ASSERT(event < (int)sizeof(lp->se_pending_events) * 8);
	atomic_clear_mask(1 << event, &lp->se_pending_events);
}

/*
 * Deliver deferred events.
 */
void sa_event_send_deferred(struct sa_event_list *lp)
{
	u_int32_t mask;
	u_int event;

	ASSERT(sa_event_list_initialized(lp));
	ASSERT(sizeof(mask) == sizeof(lp->se_pending_events));

	while ((mask = atomic_read(&lp->se_pending_events)) != 0) {
		event = ffs(mask) - 1;
		ASSERT(event < sizeof(lp->se_pending_events) * 8);
		atomic_clear_mask(1 << event, &lp->se_pending_events);
		sa_event_call(lp, event);
	}
}
