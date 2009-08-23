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
 * $Id: fc_sess_impl.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_FC_SESS_IMPL_H_
#define _LIBFC_FC_SESS_IMPL_H_

#include "queue.h"
#include "fc_exch.h"
#include "sa_hash.h"
#include "sa_timer.h"
#include <linux/rcupdate.h>

enum fc_sess_state {
	SESS_ST_NONE = 0,
	SESS_ST_INIT,		/* initialized */
	SESS_ST_STARTED,	/* started */
	SESS_ST_PLOGI,		/* waiting for PLOGI completion */
	SESS_ST_PLOGI_RECV,	/* received PLOGI (as target) */
	SESS_ST_PRLI,		/* waiting for PRLI completion */
	SESS_ST_RTV,		/* waiting for RTV completion */
	SESS_ST_ERROR,		/* error */
	SESS_ST_READY,		/* ready for use */
	SESS_ST_LOGO,		/* port logout sent */
};

/*
 * Fibre Channel Session with remote port.
 */
struct fc_sess {
	TAILQ_ENTRY(fc_sess) fs_list;	/* list of session in fc_virt_fab */
	struct fc_local_port *fs_local_port;	/* local port (aka LIF) */
	struct fc_remote_port *fs_remote_port;	/* remote port */
	enum fc_sess_state fs_state;	/* session state */
	fc_fid_t	fs_local_fid;	/* local port fabric ID at create */
	fc_fid_t	fs_remote_fid;	/* remote port fabric ID */
	uint		fs_sess_id;	/* session ID under the LIF */
	atomic_t	fs_refcnt;	/* reference count */
	uint		fs_retries;	/* retry count in current state */
	struct sa_timer fs_timer;	/* retry timer */
	uint16_t	fs_max_payload;	/* max payload size in bytes */
	uint16_t	fs_max_seq;	/* max concurrent sequences */
	uint		fs_e_d_tov;	/* negotiated e_d_tov (msec) */
	uint		fs_r_a_tov;	/* received r_a_tov (msec) */
	uint8_t		fs_started : 1;	/* locally started */
	uint8_t		fs_plogi_held : 1; /* sess held by remote login */
	struct sa_event_list *fs_events; /* event list */
	enum fc_event	fs_last_event;	/* last reported event to clients */
	struct sa_hash_link fs_hash_link; /* link in hash by port pair */
	struct fc_lun_disc *fs_lun_disc; /* active LUN discovery if any */
	spinlock_t	fs_lock;	/* lock on state changes */
	struct rcu_head fs_rcu;		/* element for call_rcu() */
};

/*
 * Declaration of struct fc_sess_list.
 */
TAILQ_HEAD(fc_sess_list, fc_sess);

/*
 * Private interfaces for libfc use.
 */
void fc_sess_recv_req(struct fc_seq *, struct fc_frame *, void *sess_arg);

#endif /* _LIBFC_FC_SESS_IMPL_H_ */
