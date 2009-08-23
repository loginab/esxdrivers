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
 * $Id: fc_event.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_EVENT_H_
#define _LIBFC_EVENT_H_

/*
 * Definitions for events that can occur on local ports, remote ports, and
 * sessions, and may be handled by state machines for these objects.
 * The order and number of these may effect state machine table sizes.
 */
enum fc_event {
	FC_EV_NONE = 0,		/* non-event */
	FC_EV_ACC,		/* request accepted */
	FC_EV_RJT,		/* request rejected */
	FC_EV_TIMEOUT,		/* timer expired */
	FC_EV_START,		/* upper layer requests startup / login */
	FC_EV_STOP,		/* upper layer requests shutdown / logout */
	FC_EV_READY,		/* lower level is ready */
	FC_EV_DOWN,	        /* lower level has no link or connection */
	FC_EV_CLOSED,		/* lower level shut down or disabled */
	FC_EV_LIMIT		/* basis for private events */
};

#endif /* _LIBFC_EVENT_H_ */
