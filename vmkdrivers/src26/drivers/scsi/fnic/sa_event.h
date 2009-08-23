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
 * $Id: sa_event.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_SA_EVENT_H_
#define _LIBSA_SA_EVENT_H_

/*
 * General event mechanism.
 *
 * Events are separately locked and MP-safe in the kernel.
 *
 * Events are scheduled on a list headed by struct sa_event_head.
 *
 * sa_event_list_init(&head) initializes the list.
 * sa_event_list_destroy(&head) frees any resources associated with the list.
 *
 * sa_event_enq(&head, cb, arg) allocates and enqueues an event handler.
 * The arguments specify the callback and argument for the callback.
 *
 * sa_event_deq(&head, cb, arg) dequeues a previously scheduled event specified
 * by handler and argument, which must be unique.
 *
 * sa_event_deq_ev(&head, ev) dequeues a previously scheduled event by pointer.
 *
 * sa_event_call(&head, rc), calls all of the event handlers on the list.
 *
 * sa_event_call does not dequeue the event.  The list may change during the
 * callback.
 */

/*
 * Semi-opaque event list element.
 */
struct sa_event;		/* event list element */
struct sa_event_list;		/* list header */

/*
 * Callback type for event handlers.
 */
typedef void (sa_event_handler_t) (int rc, void *arg);

/*
 * Functions.
 */
struct sa_event_list *sa_event_list_alloc(void);
void sa_event_list_free(struct sa_event_list *);
struct sa_event *sa_event_enq(struct sa_event_list *, sa_event_handler_t *,
			      void *arg);
void sa_event_deq(struct sa_event_list *, sa_event_handler_t *, void *);
void sa_event_deq_ev(struct sa_event_list *, struct sa_event *);

void sa_event_call(struct sa_event_list *, int rc);

/*
 * Functions managing deferred events.
 */
void sa_event_call_defer(struct sa_event_list *, int event);
void sa_event_call_cancel(struct sa_event_list *, int event);
void sa_event_send_deferred(struct sa_event_list *);

#endif /* _LIBSA_SA_EVENT_H_ */
