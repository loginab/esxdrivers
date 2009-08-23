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
 * $Id: fc_local_port_impl.h 23901 2009-02-13 19:57:11Z jre $
 */

#ifndef _LIBFC_FC_LOCAL_PORT_IMPL_H_
#define _LIBFC_FC_LOCAL_PORT_IMPL_H_

#include "fc_sess.h"
#include "fc_sess_impl.h"
#include "sa_hash.h"
#include "fc_ns.h"

struct fc_els_rscn_page;

enum fc_local_port_state {
	LOCAL_PORT_ST_NONE = 0,
	LOCAL_PORT_ST_INIT,
	LOCAL_PORT_ST_FLOGI,
	LOCAL_PORT_ST_DNS,
	LOCAL_PORT_ST_REG_PN,
	LOCAL_PORT_ST_REG_FT,
	LOCAL_PORT_ST_SCR,
	LOCAL_PORT_ST_READY,
	LOCAL_PORT_ST_DNS_STOP,
	LOCAL_PORT_ST_LOGO,
	LOCAL_PORT_ST_RESET,
};

/*
 * Local Port / Logical interface (aka LIF).
 *
 * Locking notes:
 * Most of these fields are protected by fl_lock, with the exception that;
 * fl_list, fl_hash_link, fl_sess_list, are protected by the virtual fabric
 * lock.  The event list is protected by a lock in the sa_event facility.
 * The refcnt is atomic, and release doesn't acquire fl_lock, just vf_lock.
 */
struct fc_local_port {
	struct fc_virt_fab *fl_vf;		/* virtual fabric */
	TAILQ_ENTRY(fc_local_port) fl_list;	/* list headed in virt_fab */
	struct fc_port	*fl_port;		/* port to use when sending */
	struct fc_sess	*fl_dns_sess;		/* session for dNS queries */
	struct fc_remote_port *fl_ptp_rp;	/* point-to-point remote port */
	struct fc_sess_list fl_sess_list;	/* list of sessions */
	u_int		fl_hba_lp_index; 	/* HBA LOCAL_PORT index */
	u_int		fl_lp_id;		/* global LOCAL_PORT ID */
	atomic_t	fl_refcnt;		/* reference count */
	u_int		fl_flags;		/* flags and options */
	fc_wwn_t	fl_port_wwn;		/* world-wide port name */
	fc_wwn_t	fl_node_wwn;		/* world-wide node name */
	fc_fid_t	fl_fid;			/* fabric ID (after FLOGI) */
	enum fc_local_port_state fl_state;	/* state of FLOGI and dNS */
	void		*fl_client_priv; 	/* client private state */
	void		*fl_impl_priv;		/* impl-private state */
	u_int		fl_e_d_tov;		/* E_D_TOV in milliseconds */
	u_int		fl_r_a_tov;		/* R_A_TOV in milliseconds */
	uint16_t	fl_max_payload;		/* max payload size in bytes */
	uint8_t		fl_retry_limit;		/* retry limit */
	uint8_t		fl_retry_count;		/* retries attempted */
	uint8_t		fl_logon_req;		/* logon req. by upper layer */
	u_int		fl_next_sess_id;	/* ID for next session */

	struct fc_ns_fts fl_ns_fts;		/* FC-4 type masks */
	int 		(*fl_prli_accept)(struct fc_local_port *,
		 		struct fc_remote_port *,
				void *);	/* callback for incoming PRLI */
	void		*fl_prli_cb_arg;	/* arg for PRLI callback */
	struct sa_event_list *fl_events;	/* event list head */
	struct sa_hash_link fl_hash_link;	/* hash list linkage */
	struct sa_timer	fl_timer;		/* timer for state events */

	/*
	 * Callback and state for target discoveries.
	 * Discoveries can take place without the local port changing states.
	 */
	enum fc_fh_type	fl_disc_type;		/* FC4 types to discover */
	void		(*fl_disc_cb)(void *,
				struct fc_remote_port *, enum fc_event);
	void		*fl_disc_cb_arg;	/* argument for callback */
	u_int		fl_disc_ver;		/* discovery repeat count */
	u_char		fl_disc_retries;	/* retries done so far */
	u_char		fl_disc_holdoff;	/* (secs) delay after RSCN */
	u_char		fl_disc_in_prog;	/* discovery in progress */
	u_char		fl_disc_req;		/* discovery requested */
	struct sa_timer	fl_disc_timer;		/* timer for continuing */
	u_short		fl_disc_seq_cnt;	/* sequence count expected */
	u_char		fl_disc_buf_len;	/* valid bytes in buffer */
	struct fc_gpn_ft_resp fl_disc_buf;	/* partial name buffer */

	spinlock_t	fl_lock;		/* serializes state changes */
	struct fc_els_rnid_gen fl_rnid_gen;	/* RNID information */

	/*
	 * Callbacks for ELS RSCN/RLIR events.  Used by FCS for HBA-API events.
	 */
	void		(*fl_els_cb)(void *, u_int, void *, size_t);
	void		*fl_els_cb_arg;

	atomic_t	fl_link_fail;		/* link failures */
};

/*
 * fl_flags.
 */
#define FCLF_EN_DNS_REG		1	/* register with DNS after FLOGI */

/*
 * Default holdoff time.
 */
#define	FCDT_HOLDOFF		3	/* (secs) delay discovery after RSCN */

/*
 * Locking code.
 */
static inline void fc_local_port_lock(struct fc_local_port *lp)
{
	spin_lock_bh(&lp->fl_lock);
}

static inline void fc_local_port_unlock(struct fc_local_port *lp)
{
	spin_unlock_bh(&lp->fl_lock);
}

/*
 * Test for locking asserts.
 */
static inline int fc_local_port_locked(struct fc_local_port *lp)
{
#if !defined(__KERNEL__) || \
		defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return spin_is_locked(&lp->fl_lock);
#else
	return 1;
#endif /* not __KERNEL__ or CONFIG_SMP */
}

#endif /* _LIBFC_FC_LOCAL_PORT_IMPL_H_ */
