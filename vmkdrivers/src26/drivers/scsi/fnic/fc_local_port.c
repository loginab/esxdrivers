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
 * $Id: fc_local_port.c 23901 2009-02-13 19:57:11Z jre $
 */

/*
 * Logical interface support.
 */

#include "sa_kernel.h"
#undef LIST_HEAD
#include "net_types.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_timer.h"
#include "sa_event.h"
#include "sa_hash.h"

#include "fc_fs.h"
#include "fc_gs.h"
#include "fc_ns.h"
#include "fc_els.h"
#include "fc_ils.h"
#include "fc_fc2.h"
#include "fc_scsi.h"

#include "fc_print.h"
#include "fc_types.h"
#include "fc_event.h"
#include "fc_virt_fab.h"
#include "fc_local_port.h"
#include "fc_remote_port.h"
#include "fc_disc_targ.h"
#include "fc_sess.h"
#include "fc_port.h"
#include "fc_frame.h"
#include "fc_exch.h"

#include "fc_sess_impl.h"
#include "fc_local_port_impl.h"
#include "fc_virt_fab_impl.h"
#include "fc_exch_impl.h"

/*
 * To do list:
 *
 * Setup dNS session.
 * Perform dNS registration after FLOGI.
 */

/*
 * Debugging tunables which are only set by debugger or at compile time.
 */
static int fc_local_port_debug;

/*
 * Fabric IDs to use for point-to-point mode.  Chosen on whims.
 */
#define FC_LOCAL_PTP_FID_LO     0x010101
#define FC_LOCAL_PTP_FID_HI     0x010102

#define	FC_FLOGI_FAST_RETRIES	10	/* FLOGI retries using E_D_TOV */
#define FC_FLOGI_SLOW_TOV	15000	/* slow retry timeout in milliseconds */

/*
 * static functions.
 */
static void fc_local_port_enter_init(struct fc_local_port *);
static void fc_local_port_enter_flogi(struct fc_local_port *);
static void fc_local_port_enter_dns(struct fc_local_port *);
static void fc_local_port_enter_dns_stop(struct fc_local_port *);
static void fc_local_port_enter_reg_pn(struct fc_local_port *);
static void fc_local_port_enter_reg_ft(struct fc_local_port *);
static void fc_local_port_enter_scr(struct fc_local_port *);
static void fc_local_port_enter_ready(struct fc_local_port *);
static void fc_local_port_enter_logo(struct fc_local_port *);

static void fc_local_port_ns_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_local_port_error(enum fc_event, void *);
static void fc_local_port_scr_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_local_port_port_event(int, void *);
static void fc_local_port_set_fid_int(struct fc_local_port *, fc_fid_t);

static inline void fc_local_port_enter_state(struct fc_local_port *lp,
				      enum fc_local_port_state state)
{
	ASSERT(fc_local_port_locked(lp));
	sa_timer_cancel(&lp->fl_timer);
	if (state != lp->fl_state)
		lp->fl_retry_count = 0;
	lp->fl_state = state;
}

static void fc_local_port_unlock_send(struct fc_local_port *lp)
{
	if (lp->fl_state == LOCAL_PORT_ST_DNS_STOP && lp->fl_dns_sess) {
		fc_local_port_unlock(lp);
		fc_sess_stop(lp->fl_dns_sess);
	} else {
		fc_local_port_unlock(lp);
	}
	ASSERT(atomic_read(&lp->fl_refcnt) != 0);
	sa_event_send_deferred(lp->fl_events);
}

/*
 * re-enter state for retrying a request after a timeout or alloc failure.
 */
static void fc_local_port_enter_retry(struct fc_local_port *lp)
{
	switch (lp->fl_state) {
	case LOCAL_PORT_ST_NONE:
	case LOCAL_PORT_ST_INIT:
	case LOCAL_PORT_ST_READY:
	case LOCAL_PORT_ST_RESET:
		ASSERT_NOTREACHED;
		break;
	case LOCAL_PORT_ST_FLOGI:
		fc_local_port_enter_flogi(lp);
		break;
	case LOCAL_PORT_ST_DNS:
		fc_local_port_enter_dns(lp);
		break;
	case LOCAL_PORT_ST_DNS_STOP:
		fc_local_port_enter_dns_stop(lp);
		break;
	case LOCAL_PORT_ST_REG_PN:
		fc_local_port_enter_reg_pn(lp);
		break;
	case LOCAL_PORT_ST_REG_FT:
		fc_local_port_enter_reg_ft(lp);
		break;
	case LOCAL_PORT_ST_SCR:
		fc_local_port_enter_scr(lp);
		break;
	case LOCAL_PORT_ST_LOGO:
		fc_local_port_enter_logo(lp);
		break;
	}
}

/*
 * enter next state for handling an exchange reject or retry exhaustion
 * in the current state.
 */
static void fc_local_port_enter_reject(struct fc_local_port *lp)
{
	switch (lp->fl_state) {
	case LOCAL_PORT_ST_NONE:
	case LOCAL_PORT_ST_INIT:
	case LOCAL_PORT_ST_READY:
	case LOCAL_PORT_ST_RESET:
		ASSERT_NOTREACHED;
		break;
	case LOCAL_PORT_ST_FLOGI:
		fc_local_port_enter_flogi(lp);
		break;
	case LOCAL_PORT_ST_REG_PN:
		fc_local_port_enter_reg_ft(lp);
		break;
	case LOCAL_PORT_ST_REG_FT:
		fc_local_port_enter_scr(lp);
		break;
	case LOCAL_PORT_ST_SCR:
	case LOCAL_PORT_ST_DNS_STOP:
		fc_local_port_enter_dns_stop(lp);
		break;
	case LOCAL_PORT_ST_DNS:
	case LOCAL_PORT_ST_LOGO:
		fc_local_port_enter_init(lp);
		break;
	}
}

static void fc_local_port_timeout(void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	fc_local_port_lock(lp);
	fc_local_port_enter_retry(lp);
	fc_local_port_unlock_send(lp);
}

static const char *fc_local_port_state_names[] = {
	[LOCAL_PORT_ST_NONE] =	"none",
	[LOCAL_PORT_ST_INIT] =	"idle",
	[LOCAL_PORT_ST_FLOGI] =	"FLOGI",
	[LOCAL_PORT_ST_DNS] =	"dNS",
	[LOCAL_PORT_ST_REG_PN] = "REG_PN",
	[LOCAL_PORT_ST_REG_FT] = "REG_FT",
	[LOCAL_PORT_ST_SCR] =	"SCR",
	[LOCAL_PORT_ST_READY] =	"ready",
	[LOCAL_PORT_ST_DNS_STOP] = "stop",
	[LOCAL_PORT_ST_LOGO] =	"LOGO",
	[LOCAL_PORT_ST_RESET] =	"reset",
};

const char *fc_local_port_state(struct fc_local_port *lp)
{
	const char *cp;

	cp = fc_local_port_state_names[lp->fl_state];
	if (!cp)
		cp = "unknown";
	return cp;
}

/*
 * Handle resource allocation problem by retrying in a bit.
 */
static void fc_local_port_retry(struct fc_local_port *lp)
{
	ASSERT(fc_local_port_locked(lp));
	if (lp->fl_retry_count == 0)
		SA_LOG("local port %6x alloc failure in state %s "
			"- will retry", lp->fl_fid, fc_local_port_state(lp));

	/*
	 * For FLOGI state, can't call fc_local_port_enter_reject() since
	 * it would redo FLOGI right away.  Retry continuously.
	 */
	if (lp->fl_state == LOCAL_PORT_ST_FLOGI)
		lp->fl_retry_count = 0;

	if (lp->fl_retry_count < lp->fl_retry_limit) {
		lp->fl_retry_count++;
		sa_timer_set(&lp->fl_timer, lp->fl_e_d_tov * 1000);
	} else {
		SA_LOG("local port %6x alloc failure in state %s "
			"- retries exhausted", lp->fl_fid,
			fc_local_port_state(lp));
		fc_local_port_enter_reject(lp);
	}
}

/*
 * Declare hash table type for lookup by FCID.
 */
#define FC_LOCAL_PORT_HASH_SIZE         8	/* smallish for now */

static int fc_local_port_fid_match(sa_hash_key_t, void *);
static u_int32_t fc_local_port_fid_hash(sa_hash_key_t);

static struct sa_hash_type fc_local_port_hash_by_fid = {
	.st_link_offset = offsetof(struct fc_local_port, fl_hash_link),
	.st_match = fc_local_port_fid_match,
	.st_hash = fc_local_port_fid_hash,
};

/*
 * Return max segment size for local port.
 */
static u_int fc_local_port_mfs(struct fc_local_port *lp)
{
	return lp->fl_max_payload;
}

static void fc_local_port_ptp_setup(struct fc_local_port *lp,
	fc_fid_t remote_fid, fc_wwn_t remote_wwpn, fc_wwn_t remote_wwnn)
{
	struct fc_remote_port *rp;

	rp = fc_remote_port_lookup_create(lp->fl_vf, remote_fid, remote_wwpn,
					  remote_wwnn);
	if (rp) {
		if (lp->fl_ptp_rp)
			fc_remote_port_release(lp->fl_ptp_rp);
		lp->fl_ptp_rp = rp;
		fc_local_port_enter_ready(lp);
	}
}

static void fc_local_port_ptp_clear(struct fc_local_port *lp)
{
	struct fc_remote_port *rp;

	ASSERT(fc_local_port_locked(lp));
	rp = lp->fl_ptp_rp;
	if (rp) {
		lp->fl_ptp_rp = NULL;
		fc_remote_port_release(rp);
	}
}

/*
 * Fill in FLOGI or PLOGI command for request.
 */
void
fc_local_port_flogi_fill(struct fc_local_port *lp,
			 struct fc_els_flogi *flogi, u_int op)
{
	struct fc_els_csp *sp;
	struct fc_els_cssp *cp;
	u_int mfs;

	mfs = fc_local_port_mfs(lp);

	memset(flogi, 0, sizeof(*flogi));
	flogi->fl_cmd = (net8_t)op;
	net64_put(&flogi->fl_wwpn, lp->fl_port_wwn);
	net64_put(&flogi->fl_wwnn, lp->fl_node_wwn);

	sp = &flogi->fl_csp;
	sp->sp_hi_ver = 0x20;
	sp->sp_lo_ver = 0x20;
	net16_put(&sp->sp_bb_cred, 10);		/* this gets set by gateway */
	net16_put(&sp->sp_bb_data, (u_int16_t)mfs);
	cp = &flogi->fl_cssp[3 - 1];		/* class 3 parameters */
	net16_put(&cp->cp_class, FC_CPC_VALID | FC_CPC_SEQ);
	if (op != ELS_FLOGI) {
		net16_put(&sp->sp_features, FC_SP_FT_CIRO);
		net16_put(&sp->sp_tot_seq, 255);	 /* seq. we accept */
		net16_put(&sp->sp_rel_off, 0x1f);
		net32_put(&sp->sp_e_d_tov, lp->fl_e_d_tov);

		net16_put(&cp->cp_rdfs, (u_int16_t)mfs);
		net16_put(&cp->cp_con_seq, 255);
		cp->cp_open_seq = 1;
	}
}

/*
 * Get max payload size from PLOGI response.
 */
u_int
fc_local_port_get_payload_size(struct fc_els_flogi *flp, u_int maxval)
{
	u_int mfs;

	/*
	 * Get max payload from the common service parameters and the
	 * class 3 receive data field size.
	 */
	mfs = net16_get(&flp->fl_csp.sp_bb_data) & FC_SP_BB_DATA_MASK;
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	mfs = net16_get(&flp->fl_cssp[3 - 1].cp_rdfs);
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	return maxval;
}

/*
 * Handle incoming ELS FLOGI response.
 * Save parameters of remote switch.  Finish exchange.
 */
static void
fc_local_port_flogi_resp(struct fc_seq *sp, struct fc_frame *fp, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame_header *fh;
	struct fc_els_flogi *flp;
	struct fc_sess *sess = NULL;
	fc_fid_t did;
	u_int16_t csp_flags;
	u_int	r_a_tov;
	u_int	e_d_tov;
	uint16_t mfs;

	fh = fc_frame_header_get(fp);
	did = net24_get(&fh->fh_d_id);
	if (fc_frame_payload_op(fp) == ELS_LS_ACC && did != 0) {
		if (fc_local_port_debug)
			SA_LOG("assigned fid %x", did);
		fc_local_port_lock(lp);
		fc_local_port_set_fid_int(lp, did);
		flp = fc_frame_payload_get(fp, sizeof(*flp));
		if (flp) {
			mfs = net16_get(&flp->fl_csp.sp_bb_data) &
			    FC_SP_BB_DATA_MASK;
			if (mfs >= FC_SP_MIN_MAX_PAYLOAD &&
			    mfs < lp->fl_max_payload)
				lp->fl_max_payload = mfs;
			csp_flags = net16_get(&flp->fl_csp.sp_features);
			r_a_tov = net32_get(&flp->fl_csp.sp_r_a_tov);
			e_d_tov = net32_get(&flp->fl_csp.sp_e_d_tov);
			if (csp_flags & FC_SP_FT_EDTR)
				e_d_tov /= 1000000;
			if ((csp_flags & FC_SP_FT_FPORT) == 0) {
				if (e_d_tov > lp->fl_e_d_tov)
					lp->fl_e_d_tov = e_d_tov;
				lp->fl_r_a_tov = 2 * e_d_tov;
				SA_LOG("point-to-point mode");
				fc_local_port_ptp_setup(lp,
						net24_get(&fh->fh_s_id),
						net64_get(&flp->fl_wwpn),
						net64_get(&flp->fl_wwnn));
			} else {
				lp->fl_e_d_tov = e_d_tov;
				lp->fl_r_a_tov = r_a_tov;
				fc_local_port_enter_dns(lp);
				sess = lp->fl_dns_sess;
			}
		}
		fc_local_port_unlock_send(lp);

		/*
		 * If dNS session isn't ready, start its logon.
		 */
		if (sess != NULL && sess->fs_state != SESS_ST_READY)
			fc_sess_start(sess);	/* start the PLOGI ASAP */
	} else {
		SA_LOG("bad FLOGI response\n");
		fc_print_frame_hdr((char *) __FUNCTION__, fp);	/* XXX */
	}
	fc_frame_free(fp);
}

/*
 * Send ELS request to peer.
 * Handles retry if the sequence of frame wasn't allocated.
 */
static void fc_local_port_els_send(struct fc_local_port *lp,
					struct fc_seq *sp, struct fc_frame *fp)
{
	if (sp && fp) {
		fc_seq_exch(sp)->ex_port = lp->fl_port;
		fc_exch_timer_set(fc_seq_exch(sp), lp->fl_e_d_tov);
		if (lp->fl_state == LOCAL_PORT_ST_FLOGI &&
		    lp->fl_retry_count > FC_FLOGI_FAST_RETRIES)
			fc_exch_timer_set(fc_seq_exch(sp), FC_FLOGI_SLOW_TOV);
		else
			fc_exch_timer_set(fc_seq_exch(sp), lp->fl_e_d_tov);
		if (fc_seq_send_req(sp, fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS, 0))
			fc_local_port_retry(lp);
	} else {
		if (sp)
			fc_seq_exch_complete(sp);
		fc_local_port_retry(lp);
	}
}

/*
 * Send ELS (extended link service) FLOGI request to peer.
 */
static void fc_local_port_flogi_send(struct fc_local_port *lp)
{
	struct fc_frame *fp;
	struct fc_els_flogi *flp;
	struct fc_seq *sp;

	sp = fc_seq_start_exch(lp->fl_vf->vf_exch_mgr,
			       fc_local_port_flogi_resp,
			       fc_local_port_error, lp, 0, FC_FID_FLOGI);
	if (!sp)
		goto retry;
	fp = fc_frame_alloc(lp->fl_port, sizeof(*flp));
	if (!fp) {
		fc_seq_exch_complete(sp);
		goto retry;
	}
	flp = fc_frame_payload_get(fp, sizeof(*flp));
	ASSERT(flp);
	fc_local_port_flogi_fill(lp, flp, ELS_FLOGI);
	fc_local_port_els_send(lp, sp, fp);
	return;
retry:
	fc_local_port_retry(lp);
}

/*
 * A received FLOGI request indicates a point-to-point connection.
 * Accept it with the common service parameters indicating our N port.
 * Set up to do a PLOGI if we have the higher-number WWPN.
 */
static void fc_local_port_recv_flogi_req(struct fc_seq *sp_in,
					struct fc_frame *rx_fp, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct fc_seq *sp;
	struct fc_els_flogi *flp;
	struct fc_els_flogi *new_flp;
	fc_wwn_t remote_wwpn;
	fc_fid_t remote_fid;
	fc_fid_t local_fid;

	fh = fc_frame_header_get(rx_fp);
	ASSERT(fh);
	remote_fid = net24_get(&fh->fh_s_id);
	flp = fc_frame_payload_get(rx_fp, sizeof(*flp));
	if (!flp)
		goto out;
	remote_wwpn = net64_get(&flp->fl_wwpn);
	if (remote_wwpn == lp->fl_port_wwn) {
		SA_LOG("FLOGI from port with same WWPN %llx "
		       "possible configuration error.", remote_wwpn);
		goto out;
	}
	SA_LOG("FLOGI from port WWPN %llx ", remote_wwpn);
	fc_local_port_lock(lp);

	/*
	 * Reset any existing sessions and exchanges.
	 */
#if 0	/* XXX - can't do this yet without clearing the FLOGI exchange */
	fc_local_port_enter_init(lp);
	ASSERT(lp->fl_fid == 0);
#endif	/* XXX */

	/*
	 * XXX what is the right thing to do for FIDs?
	 * The originator might expect our S_ID to be 0xfffffe.
	 * But if so, both of us could end up with the same FID.
	 */
	local_fid = FC_LOCAL_PTP_FID_LO;
	if (remote_wwpn < lp->fl_port_wwn) {
		local_fid = FC_LOCAL_PTP_FID_HI;
		if (!remote_fid || remote_fid == local_fid) {
			remote_fid = FC_LOCAL_PTP_FID_LO;
		}
	} else if (!remote_fid) {
		remote_fid = FC_LOCAL_PTP_FID_HI;
	}
	fc_local_port_set_fid_int(lp, local_fid);

	fc_local_port_enter_ready(lp);

	fp = fc_frame_alloc(lp->fl_port, sizeof(*flp));
	if (fp) {
		sp = fc_seq_start_next(rx_fp->fr_seq);
		ASSERT(sp);		
		fc_seq_set_addr(sp, remote_fid, local_fid);
		new_flp = fc_frame_payload_get(fp, sizeof (*flp));
		ASSERT(new_flp);
		fc_local_port_flogi_fill(lp, new_flp, ELS_FLOGI);
		new_flp->fl_cmd = (net8_t) ELS_LS_ACC;

		/*
		 * Send the response.  If this fails, the originator should
		 * repeat the sequence.
		 */
		(void) fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
	} else {
		fc_local_port_retry(lp);
	}
	fc_local_port_ptp_setup(lp, remote_fid, remote_wwpn,
				net64_get(&flp->fl_wwnn));
	fc_local_port_unlock_send(lp);
out:
	sp = rx_fp->fr_seq;
	fc_seq_complete(sp);
	fc_frame_free(rx_fp);
}

static void fc_local_port_enter_flogi(struct fc_local_port *lp)
{
	fc_local_port_enter_state(lp, LOCAL_PORT_ST_FLOGI);
	fc_local_port_ptp_clear(lp);
	fc_local_port_flogi_send(lp);
}

/*
 * Handle events from DNS session.
 */
static void fc_local_port_sess_event(int event, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_sess *sess = NULL;

	if (lp->fl_state == LOCAL_PORT_ST_RESET) {
		return;
	}
	fc_local_port_hold(lp);
	fc_local_port_lock(lp);
	sa_timer_cancel(&lp->fl_timer);

	if (fc_local_port_debug) {
		SA_LOG("local fid %6x dNS session event %d\n", lp->fl_fid,
		       event);
	}
	switch (event) {
	case FC_EV_READY:
		if (lp->fl_state == LOCAL_PORT_ST_DNS)
			fc_local_port_enter_reg_pn(lp);
		break;
	case FC_EV_RJT:
	case FC_EV_CLOSED:
		sess = lp->fl_dns_sess;
		lp->fl_dns_sess = NULL;
		if (lp->fl_state == LOCAL_PORT_ST_DNS_STOP)
			fc_local_port_enter_logo(lp);
		else
			fc_local_port_enter_flogi(lp);
/* XXX - don't want to re-login again always -
 * stay in error state - per Kanna */
		break;
	default:
		SA_LOG("unexpected event %d from dNS session", event);
		break;
	}
	fc_local_port_unlock_send(lp);
	if (sess) {
		fc_sess_event_deq(sess, fc_local_port_sess_event, lp);
		fc_sess_release(sess);
	}
	fc_local_port_release(lp);
}

/*
 * Setup session to dNS if not already set up.
 */
static void fc_local_port_enter_dns(struct fc_local_port *lp)
{
	struct fc_sess *sess;
	struct fc_remote_port *rp;

	fc_local_port_enter_state(lp, LOCAL_PORT_ST_DNS);
	sess = lp->fl_dns_sess;
	if (!sess) {
		/*
		 * Set up remote port to directory server.
		 */
		rp = fc_remote_port_lookup_create(lp->fl_vf, FC_FID_DIR_SERV,
							0, 0);
		if (!rp)
			goto err;
		sess = fc_sess_create(lp, rp);	/* will hold the remote port */
		fc_remote_port_release(rp);
		rp = NULL;
		if (!sess)
			goto err;
		if (!fc_sess_event_enq(sess, fc_local_port_sess_event, lp)) {
			fc_sess_release(sess);
			goto err;
		}
		lp->fl_dns_sess = sess;
	}
	if (sess->fs_state == SESS_ST_READY)
		fc_local_port_enter_reg_pn(lp);
	return;

	/*
	 * Resource allocation problem (malloc).  Try again in 500 mS.
	 */
err:
	fc_local_port_retry(lp);
}

/*
 * Fill in dNS request header.
 */
static void
fc_local_port_fill_dns_hdr(struct fc_local_port *lp, struct fc_ct_hdr *ct,
			   u_int op, u_int req_size)
{
	memset(ct, 0, sizeof(*ct) + req_size);
	ct->ct_rev = FC_CT_REV;
	ct->ct_fs_type = FC_FST_DIR;
	ct->ct_fs_subtype = FC_NS_SUBTYPE;
	net16_put(&ct->ct_cmd, op);
}

/*
 * Test for dNS accept in response payload.
 */
static int fc_local_port_dns_acc(struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_ct_hdr *ct;
	int rc = 0;

	fh = fc_frame_header_get(fp);
	ct = fc_frame_payload_get(fp, sizeof(*ct));
	if (fh && ct && fh->fh_type == FC_TYPE_CT &&
	    ct->ct_fs_type == FC_FST_DIR &&
	    ct->ct_fs_subtype == FC_NS_SUBTYPE &&
	    net16_get(&ct->ct_cmd) == FC_FS_ACC) {
		rc = 1;
	}
	return rc;
}

/*
 * Register port name with name server.
 */
static void fc_local_port_enter_reg_pn(struct fc_local_port *lp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_rn_id rn;
	} *rp;

	fc_local_port_enter_state(lp, LOCAL_PORT_ST_REG_PN);
	fp = fc_frame_alloc(lp->fl_port, sizeof(*rp));
	if (!fp) {
		fc_local_port_retry(lp);
		return;
	}
	rp = fc_frame_payload_get(fp, sizeof(*rp));
	ASSERT(rp);
	memset(rp, 0, sizeof(*rp));
	fc_local_port_fill_dns_hdr(lp, &rp->ct, FC_NS_RPN_ID, sizeof(rp->rn));
	net24_put(&rp->rn.fr_fid.fp_fid, lp->fl_fid);
	net64_put(&rp->rn.fr_wwn, lp->fl_port_wwn);
	fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
	if (fc_sess_send_req(lp->fl_dns_sess, fp, fc_local_port_ns_resp,
				 fc_local_port_error, lp))
		fc_local_port_retry(lp);
}

/*
 * Handle response from name server.
 */
static void
fc_local_port_ns_resp(struct fc_seq *sp, struct fc_frame *fp, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	fc_local_port_lock(lp);
	sa_timer_cancel(&lp->fl_timer);
	if (fc_local_port_dns_acc(fp)) {
		if (lp->fl_state == LOCAL_PORT_ST_REG_PN)
			fc_local_port_enter_reg_ft(lp);
		else
			fc_local_port_enter_scr(lp);

	} else {
		fc_local_port_retry(lp);
	}
	fc_local_port_unlock_send(lp);
	fc_frame_free(fp);
}

/*
 * Register FC4-types with name server.
 */
static void fc_local_port_enter_reg_ft(struct fc_local_port *lp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_fid fid;	/* port ID object */
		struct fc_ns_fts fts;	/* FC4-types object */
	} *rp;
	struct fc_ns_fts *lps;
	int i;

	fc_local_port_enter_state(lp, LOCAL_PORT_ST_REG_FT);
	lps = &lp->fl_ns_fts;
	i = sizeof(lps->ff_type_map) / sizeof(lps->ff_type_map[0]);
	while (--i >= 0)
		if (net32_get(&lps->ff_type_map[i]) != 0)
			break;
	if (i >= 0) {
		fp = fc_frame_alloc(lp->fl_port, sizeof(*rp));
		if (fp) {
			rp = fc_frame_payload_get(fp, sizeof(*rp));
			ASSERT(rp);
			fc_local_port_fill_dns_hdr(lp, &rp->ct,
						   FC_NS_RFT_ID,
						   sizeof(*rp) -
						   sizeof(struct fc_ct_hdr));
			net24_put(&rp->fid.fp_fid, lp->fl_fid);
			rp->fts = *lps;
			fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
			if (fc_sess_send_req(lp->fl_dns_sess, fp,
					 fc_local_port_ns_resp,
					 fc_local_port_error, lp))
				fc_local_port_retry(lp);
		} else {
			fc_local_port_retry(lp);
		}
	} else {
		fc_local_port_enter_scr(lp);
	}
}

static void fc_local_port_enter_scr(struct fc_local_port *lp)
{
	struct fc_frame *fp;
	struct fc_els_scr *scr;
	struct fc_seq *sp;

	fc_local_port_enter_state(lp, LOCAL_PORT_ST_SCR);
	sp = fc_seq_start_exch(lp->fl_vf->vf_exch_mgr,
			       fc_local_port_scr_resp, fc_local_port_error,
			       lp, lp->fl_fid, FC_FID_FCTRL);
	if (!sp)
		goto retry;
	fp = fc_frame_alloc(lp->fl_port, sizeof(*scr));
	if (!fp) {
		fc_seq_exch_complete(sp);
		goto retry;
	}
	scr = fc_frame_payload_get(fp, sizeof(*scr));
	ASSERT(scr);
	memset(scr, 0, sizeof(*scr));
	scr->scr_cmd = ELS_SCR;
	scr->scr_reg_func = ELS_SCRF_FULL;
	fc_local_port_els_send(lp, sp, fp);
	return;
retry:
	fc_local_port_retry(lp);
}

static void
fc_local_port_scr_resp(struct fc_seq *sp, struct fc_frame *fp,
		       void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	fc_local_port_lock(lp);
	fc_local_port_enter_ready(lp);
	fc_local_port_unlock_send(lp);
	fc_frame_free(fp);
}

static void fc_local_port_enter_ready(struct fc_local_port *lp)
{
	fc_local_port_enter_state(lp, LOCAL_PORT_ST_READY);
	sa_event_call_cancel(lp->fl_events, FC_EV_CLOSED);
	sa_event_call_defer(lp->fl_events, FC_EV_READY);
}

/*
 * Logoff DNS session.
 * fc_local_port_unlock_send will stop the session after the lock is free.
 * We should get an event call when the session has been logged out.
 */
static void fc_local_port_enter_dns_stop(struct fc_local_port *lp)
{
	fc_local_port_enter_state(lp, LOCAL_PORT_ST_DNS_STOP);
	if (!lp->fl_dns_sess)
		fc_local_port_enter_logo(lp);
}


static void
fc_local_port_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	fc_frame_free(fp);
	fc_local_port_lock(lp);
	fc_local_port_enter_init(lp);
	fc_local_port_unlock_send(lp);
}

/*
 * Logout from fabric.
 */
static void fc_local_port_enter_logo(struct fc_local_port *lp)
{
	struct fc_frame *fp;
	struct fc_els_logo *logo;
	struct fc_seq *sp;
	struct fc_sess *sess;

	/*
	 * DNS session should be closed so we can release it here.
	 */
	fc_local_port_enter_state(lp, LOCAL_PORT_ST_LOGO);
	sess = lp->fl_dns_sess;
	if (sess) {
		fc_sess_release(sess);
		lp->fl_dns_sess = NULL;
	}

	sp = fc_seq_start_exch(lp->fl_vf->vf_exch_mgr,
			       fc_local_port_logo_resp,
			       fc_local_port_error, lp, lp->fl_fid,
			       FC_FID_FLOGI);
	if (!sp)
		goto retry;
	fp = fc_frame_alloc(lp->fl_port, sizeof(*logo));
	if (!fp) {
		fc_seq_exch_complete(sp);
		goto retry;
	}
	logo = fc_frame_payload_get(fp, sizeof(*logo));
	ASSERT(logo);
	memset(logo, 0, sizeof(*logo));
	logo->fl_cmd = ELS_LOGO;
	net24_put(&logo->fl_n_port_id, lp->fl_fid);
	net64_put(&logo->fl_n_port_wwn, lp->fl_port_wwn);
	fc_local_port_els_send(lp, sp, fp);
	return;
retry:
	fc_local_port_retry(lp);
}

int fc_local_port_table_create(struct fc_virt_fab *vp)
{
	struct sa_hash *hp;

	ASSERT(vp);
	ASSERT(!vp->vf_lport_by_fid);

	hp = sa_hash_create(&fc_local_port_hash_by_fid,
					     FC_LOCAL_PORT_HASH_SIZE);

	if (!hp)
		return -1;
	vp->vf_lport_by_fid = hp;
	TAILQ_INIT(&vp->vf_local_ports);

	return 0;
}

void fc_local_port_table_destroy(struct fc_virt_fab *vp)
{
	ASSERT(TAILQ_EMPTY(&vp->vf_local_ports));
	sa_hash_destroy(vp->vf_lport_by_fid);
}

/*
 * Create Local Port.
 */
struct fc_local_port *fc_local_port_create(struct fc_virt_fab *vf,
					   struct fc_port *port,
					   fc_wwn_t wwpn, fc_wwn_t wwnn,
					   u_int timeout_msec,
					   u_int retry_limit)
{
	struct fc_local_port *lp;

	/*
	 * Static checks for packet structure sizes.
	 * These catch some obvious errors in structure definitions.
	 * These should generate no code since they can be tested
	 * at compile time.
	 */
#ifdef DEBUG_ASSERTS
	fc_fs_size_checks();
	fc_gs_size_checks();
	fc_ils_size_checks();
	fc_els_size_checks();
#endif /* DEBUG_ASSERTS */

	lp = sa_malloc(sizeof(*lp));
	if (!lp)
		return NULL;
	memset(lp, 0, sizeof(*lp));
	lp->fl_vf = vf;
	atomic_set(&lp->fl_refcnt, 1);
	lp->fl_port = port;
	lp->fl_port_wwn = wwpn;
	lp->fl_node_wwn = wwnn;
	lp->fl_state = LOCAL_PORT_ST_INIT;
	lp->fl_e_d_tov = timeout_msec;
	lp->fl_r_a_tov = 2 * timeout_msec;
	lp->fl_retry_limit = (uint8_t)retry_limit;
	lp->fl_disc_holdoff = FCDT_HOLDOFF;

	lp->fl_events = sa_event_list_alloc();

	if (!lp->fl_events)
		goto err;
	lp->fl_next_sess_id = 1;

	sa_timer_init(&lp->fl_timer, fc_local_port_timeout, lp);

	TAILQ_INIT(&lp->fl_sess_list);
	spin_lock_init(&lp->fl_lock);
	sa_spin_lock_debug_set_hier(&lp->fl_lock, 0);

	if (!fc_port_enq_handler(port, fc_local_port_port_event, lp)) {
		sa_event_list_free(lp->fl_events);
		goto err;
	}
	fc_virt_fab_lock(vf);
	TAILQ_INSERT_TAIL(&vf->vf_local_ports, lp, fl_list);
	fc_virt_fab_unlock(vf);
	fc_local_port_lock(lp);
	fc_local_port_enter_init(lp);
	fc_local_port_unlock(lp);
	return lp;
err:
	sa_free(lp);
	return NULL;
}

/*
 * Set Local Port to a well known address.
 * Does not change the state.
 * This is used for point-to-point mode and for special fabric ports in fab.c.
 * This skips the fabric login and DNS steps.
 */
static void
fc_local_port_set_fid_int(struct fc_local_port *lp, fc_fid_t fid)
{
	struct fc_local_port *found;
	struct fc_virt_fab *vf;

	ASSERT(fc_local_port_locked(lp));
	vf = lp->fl_vf;
	if (lp->fl_fid != fid) {
		if (fc_local_port_debug) {
			SA_LOG("changing local port fid from %x to %x",
			       lp->fl_fid, fid);
		}
		fc_virt_fab_lock(vf);
		if (lp->fl_fid) {
			found = sa_hash_lookup_delete(vf->vf_lport_by_fid,
						&lp->fl_fid);
			ASSERT(found);
			ASSERT(found == lp);
		}
		ASSERT(!sa_hash_lookup(vf->vf_lport_by_fid, &fid));
		lp->fl_fid = fid;
		if (fid != 0)
			sa_hash_insert(vf->vf_lport_by_fid, &fid, lp);
		fc_virt_fab_unlock(vf);
		fc_sess_reset_list(vf, &lp->fl_sess_list); /* gets sess lock */
	}
}

/*
 * Set Local Port to a well known address.
 */
void fc_local_port_set_fid(struct fc_local_port *lp, fc_fid_t fid)
{
	fc_local_port_lock(lp);
	if (lp->fl_fid != fid) {
		fc_local_port_set_fid_int(lp, fid);

		if (fid != 0 && lp->fl_state == LOCAL_PORT_ST_INIT)
			fc_local_port_enter_ready(lp);
		else
			fc_local_port_enter_init(lp);
	}
	fc_local_port_unlock_send(lp);
}

/*
 * Get Local Port FC_ID.
 */
fc_fid_t fc_local_port_get_fid(const struct fc_local_port *lp)
{
	return lp->fl_fid;
}

/*
 * Add a supported FC-4 type.
 */
void fc_local_port_add_fc4_type(struct fc_local_port *lp, enum fc_fh_type type)
{
	net32_t *mp;

	mp = &lp->fl_ns_fts.ff_type_map[type / FC_NS_BPW];
	net32_put(mp, net32_get(mp) | 1UL << (type % FC_NS_BPW));
}

/*
 * Set FC-4 map.
 */
void fc_local_port_set_fc4_map(struct fc_local_port *lp, u_int32_t * map)
{
	net32_t *mp;
	u_int i;

	mp = lp->fl_ns_fts.ff_type_map;
	for (i = 0; i < FC_NS_TYPES / FC_NS_BPW; i++)
		net32_put(mp++, *map++);
}

/*
 * Get the pointer to the FC-4 map.
 */
const struct fc_ns_fts *fc_local_port_get_fc4_map(struct fc_local_port *lp)
{
	return &lp->fl_ns_fts;
}

struct fc_els_rnid_gen * fc_local_port_get_rnidp(struct fc_local_port *lp)
{
	return &lp->fl_rnid_gen;
}

static void fc_local_port_delete(struct fc_local_port *lp)
{
	struct fc_local_port *found;
	struct fc_virt_fab *vf;

	ASSERT(!atomic_read(&lp->fl_refcnt));

	if (fc_local_port_debug)
		SA_LOG("local port %6x delete", lp->fl_fid);
	vf = lp->fl_vf;


	fc_virt_fab_lock(vf);
	if (lp->fl_fid) {
		found = sa_hash_lookup_delete(vf->vf_lport_by_fid, &lp->fl_fid);
		ASSERT(found = lp);
		lp->fl_fid = 0;
	}
	TAILQ_REMOVE(&vf->vf_local_ports, lp, fl_list);
	fc_virt_fab_unlock(vf);
	ASSERT(!lp->fl_dns_sess);	/* otherwise it holds the local port */
	sa_event_list_free(lp->fl_events);
	sa_free(lp);
}

void fc_local_port_hold(struct fc_local_port *lp)
{
	atomic_inc(&lp->fl_refcnt);
	ASSERT(atomic_read(&lp->fl_refcnt) > 0);
}

void fc_local_port_release(struct fc_local_port *lp)
{
	ASSERT(atomic_read(&lp->fl_refcnt) > 0);
	if (atomic_dec_and_test(&lp->fl_refcnt))
		fc_local_port_delete(lp);
}

/*
 * Add an event handler for the local port.
 */
struct sa_event *fc_local_port_event_enq(struct fc_local_port *lp,
			sa_event_handler_t * handler, void *arg)
{
	return sa_event_enq(lp->fl_events, handler, arg);
}

/*
 * Remove an event handler for the local port.
 */
void fc_local_port_event_deq(struct fc_local_port *lp,
			sa_event_handler_t * handler, void *arg)
{
	sa_event_deq(lp->fl_events, handler, arg);
}

/*
 * Start Local Port state machine for FLOGI, etc.
 */
void fc_local_port_logon(struct fc_local_port *lp, sa_event_handler_t * cb,
			void *arg)
{
	enum fc_event event = FC_EV_NONE;

	if (cb && !fc_local_port_event_enq(lp, cb, arg))
		event = FC_EV_RJT;
	fc_local_port_lock(lp);
	lp->fl_logon_req = 1;
	if (lp->fl_state == LOCAL_PORT_ST_INIT) {
		if (fc_port_ready(lp->fl_port))
			fc_local_port_enter_flogi(lp);
	} else if (lp->fl_state == LOCAL_PORT_ST_READY) {
		event = FC_EV_READY;
	}
	fc_local_port_unlock_send(lp);
	if (event != FC_EV_NONE)
		(*cb)(event, arg);
}

/*
 * Send LOGO and Disable Local Port state machine.
 */
void fc_local_port_logoff(struct fc_local_port *lp)
{
	fc_local_port_lock(lp);
	lp->fl_logon_req = 0;
	switch (lp->fl_state) {
	case LOCAL_PORT_ST_NONE:
	case LOCAL_PORT_ST_INIT:
		break;
	case LOCAL_PORT_ST_FLOGI:
	case LOCAL_PORT_ST_LOGO:
	case LOCAL_PORT_ST_RESET:
		fc_local_port_enter_init(lp);
		break;
	case LOCAL_PORT_ST_DNS:
	case LOCAL_PORT_ST_DNS_STOP:
		fc_local_port_enter_logo(lp);
		break;
	case LOCAL_PORT_ST_REG_PN:
	case LOCAL_PORT_ST_REG_FT:
	case LOCAL_PORT_ST_SCR:
	case LOCAL_PORT_ST_READY:
		fc_local_port_enter_dns_stop(lp);
		break;
	}
	fc_local_port_unlock_send(lp);
}

/*
 * Put the local port back into the initial state.  Reset all sessions.
 * This is called after a SCSI reset or the driver is unloading
 * or the program is exiting.
 */
static void fc_local_port_enter_init(struct fc_local_port *lp)
{
	struct fc_sess *sess;
	u_int mfs;

	ASSERT(atomic_read(&lp->fl_refcnt) > 0);
	ASSERT(fc_local_port_locked(lp));

	if (fc_local_port_debug)
		SA_LOG("new state init");
	sess = lp->fl_dns_sess;
	if (sess) {
		fc_sess_event_deq(sess, fc_local_port_sess_event, lp);
		fc_sess_release(sess);
		lp->fl_dns_sess = NULL;
	}
	fc_local_port_ptp_clear(lp);

	mfs = fc_port_get_max_frame_size(lp->fl_port);
	if (mfs < FC_MIN_MAX_FRAME) {
		SA_LOG("warning: port max frame size %d too small", mfs);
		mfs = FC_MIN_MAX_FRAME;
	}
	mfs -= sizeof(struct fc_frame_header);
	if (mfs > FC_MAX_PAYLOAD)
		mfs = FC_MAX_PAYLOAD;
	mfs &= ~3;
	lp->fl_max_payload = (uint16_t) mfs;

	/*
	 * Setting state RESET keeps fc_local_port_error() callbacks
	 * by fc_exch_mgr_reset() from recursing on the lock.
	 * It also causes fc_local_port_sess_event() to ignore events.
	 * The lock is held for the duration of the time in RESET state.
	 */
	lp->fl_state = LOCAL_PORT_ST_RESET;
	lp->fl_disc_req = 0;        /* clear any pending redisc req */
	fc_exch_mgr_reset(lp->fl_vf->vf_exch_mgr, 0, 0);
	fc_local_port_set_fid_int(lp, 0); /* gets sess lock */
	fc_local_port_enter_state(lp, LOCAL_PORT_ST_INIT);
	sa_event_call_cancel(lp->fl_events, FC_EV_READY);
	sa_event_call_defer(lp->fl_events, FC_EV_CLOSED);
	if (lp->fl_logon_req && fc_port_ready(lp->fl_port))
		fc_local_port_enter_flogi(lp);
}

/*
 * Put the local port back into the initial state.  Reset all sessions.
 * This is called after a SCSI reset or the driver is unloading
 * or the program is exiting.
 * Enters the INIT state which causes an FC_EV_CLOSED event.
 */
void fc_local_port_reset(struct fc_local_port *lp)
{
	fc_local_port_lock(lp);
	fc_local_port_enter_init(lp);
	fc_local_port_unlock_send(lp);
}

/*
 * Reset the local port and don't logon again.
 * This is called after a SCSI reset or the driver is unloading
 * or the program is exiting.
 * It isn't possible to send logoffs or other traffic.
 */
void fc_local_port_destroy(struct fc_local_port *lp)
{
	fc_local_port_quiesce(lp);
}

void fc_local_port_quiesce(struct fc_local_port *lp)
{
	fc_local_port_lock(lp);
	lp->fl_logon_req = 0;
	fc_local_port_enter_init(lp);
	fc_local_port_unlock_send(lp);
}

/*
 * Return non-zero if the local port is ready for use (logged in).
 */
int fc_local_port_test_ready(struct fc_local_port *lp)
{
	return lp->fl_state == LOCAL_PORT_ST_READY;
}

/*
 * Return non-zero if the local port is in the init state, where nothing
 * is logged in, no sessions have been created, etc.
 */
int fc_local_port_test_init(struct fc_local_port *lp)
{
	return lp->fl_state == LOCAL_PORT_ST_INIT;
}

/*
 * Handle an individual change event from a received RSCN.
 * Returns non-zero if target discovery should be repeated.
 */
static int fc_local_port_change(struct fc_local_port *lp,
			enum fc_els_rscn_ev_qual ev_qual,
			fc_fid_t fid, fc_fid_t mask)
{
	int redisc = 1;

	switch (ev_qual) {
	case ELS_EV_QUAL_NONE:
	case ELS_EV_QUAL_REM_OBJ:
	case ELS_EV_QUAL_NS_OBJ:
		if (mask == 0xffffff) {
			fc_disc_targ_single(lp, fid);
			redisc = 0;
		}
		break;
	case ELS_EV_QUAL_PORT_ATTR:
	case ELS_EV_QUAL_SERV_OBJ:
	case ELS_EV_QUAL_SW_CONFIG:
		break;
	default:
		if (fc_local_port_debug)
			SA_LOG("unexpected event qualifier %x", ev_qual);
		break;
	}
	if (fc_local_port_debug)
		SA_LOG("RSCN qual %x fid %x mask %x redisc %d",
			ev_qual, fid, mask, redisc, redisc);
	return redisc;
}

/*
 * Handle received RSCN - registered state change notification.
 */
static void fc_local_port_rscn_req(struct fc_seq *sp, struct fc_frame *fp,
					void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_els_rscn *rp;
	struct fc_els_rscn_page *pp;
	u_int len;
	int error;
	int redisc = 0;
	enum fc_els_rscn_ev_qual ev_qual;
	enum fc_els_rscn_addr_fmt fmt;
	fc_fid_t mask;

	rp = fc_frame_payload_get(fp, sizeof(*rp));
	if (rp && rp->rscn_page_len == sizeof(*pp) &&
	    (len = net16_get(&rp->rscn_plen)) >= sizeof(*rp)) {
		len -= sizeof(*rp);
		if (len == 0) {
			redisc = 1;
			if (lp->fl_els_cb) {
				(*lp->fl_els_cb)(lp->fl_els_cb_arg,
						ELS_RSCN, NULL, 0);
			}
		} else {
			for (pp = (struct fc_els_rscn_page *) (rp + 1);
			     len > 0; len -= sizeof(*pp), pp++) {
				ev_qual = pp->rscn_page_flags >>
				    ELS_RSCN_EV_QUAL_BIT;
				ev_qual &= ELS_RSCN_EV_QUAL_MASK;
				fmt = pp->rscn_page_flags >>
				    ELS_RSCN_ADDR_FMT_BIT;
				fmt &= ELS_RSCN_ADDR_FMT_MASK;
				switch (fmt) {
				case ELS_ADDR_FMT_PORT:
					mask = 0xffffff;
					break;
				case ELS_ADDR_FMT_AREA:
					mask = 0xffff00;
					break;
				case ELS_ADDR_FMT_DOM:
					mask = 0xff0000;
					break;
				case ELS_ADDR_FMT_FAB:
				default:
					mask = 0;
					break;
				}
				redisc |= fc_local_port_change(lp, ev_qual,
						net24_get(&pp->rscn_fid), mask);
				if (lp->fl_els_cb) {
					(*lp->fl_els_cb)(lp->fl_els_cb_arg,
							ELS_RSCN,
							pp, sizeof (*pp));
				}
			}
		}
		fc_seq_ls_acc(sp);
		if (redisc && lp->fl_disc_cb) {
			if (fc_local_port_debug)
				SA_LOG("RSCN received: rediscovering");
			error = fc_disc_targ_restart(lp);
			ASSERT_NOTIMPL(error == 0);
		} else if (fc_local_port_debug) {
			SA_LOG("RSCN received: not rediscovering. "
			       "redisc %d state %d disc_cb %p in_prog %d",
			       redisc, lp->fl_state, lp->fl_disc_cb,
			       lp->fl_disc_in_prog);
		}
	} else {
		fc_seq_ls_rjt(sp, ELS_RJT_LOGIC, ELS_EXPL_NONE);
	}
	fc_frame_free(fp);
}

/*
 * Handle received RLIR - registered link incident report.
 */
static void fc_local_port_rlir_req(struct fc_seq *sp, struct fc_frame *fp,
					void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	u_int len;

	if (lp->fl_els_cb) {
		len = fp->fr_len - sizeof (struct fc_frame_header);
		(*lp->fl_els_cb)(lp->fl_els_cb_arg, ELS_RLIR, 
				fc_frame_payload_get(fp, len), len);
	}
	fc_seq_ls_acc(sp);
	fc_frame_free(fp);
}

/*
 * Handle received ECHO.
 */
static void fc_local_port_echo_req(struct fc_seq *sp, struct fc_frame *in_fp,
					void *lp_arg)
		   
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame *fp;
	u_int len;
	void *pp;
	void *dp;

	len = in_fp->fr_len - sizeof(struct fc_frame_header);
	pp = fc_frame_payload_get(in_fp, len);
	ASSERT(pp);
	if (len < sizeof(net32_t))
		len = sizeof(net32_t);
	fp = fc_frame_alloc(lp->fl_port, len);
	if (fp) {
		dp = fc_frame_payload_get(fp, len);
		ASSERT(dp);
		memcpy(dp, pp, len);
		net32_put(dp, ELS_LS_ACC << 24);
		fc_seq_send_last(fc_seq_start_next(sp), fp,
					FC_RCTL_ELS_REP, FC_TYPE_ELS);
	} else {
		fc_seq_exch_complete(sp);
	}
	fc_frame_free(in_fp);
}

/*
 * Handle received RLS request: Read Link Error Status Block.
 */
static void fc_local_port_rls_req(struct fc_seq *sp, struct fc_frame *in_fp,
				  void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame *fp;
	struct fc_els_rls *req;
	struct fc_els_rls_resp *rp;

	req = fc_frame_payload_get(in_fp, sizeof(*req));
	if (!req) {
		fc_seq_ls_rjt(sp, ELS_RJT_LOGIC, ELS_EXPL_NONE);
		goto drop;
	}
	fp = fc_frame_alloc(lp->fl_port, sizeof(*rp));
	if (!fp) {
		fc_seq_exch_complete(sp);
		goto drop;
	}
	rp = fc_frame_payload_get(fp, sizeof(*rp));
	ASSERT(rp);
	memset(rp, 0, sizeof(*rp));
	rp->rls_cmd = ELS_LS_ACC;
	net32_put(&rp->rls_lesb.lesb_link_fail, atomic_read(&lp->fl_link_fail));

	fc_seq_send_last(fc_seq_start_next(sp), fp,
			 FC_RCTL_ELS_REP, FC_TYPE_ELS);
drop:
	fc_frame_free(in_fp);
}

/*
 * Handle received RNID.
 */
static void fc_local_port_rnid_req(struct fc_seq *sp, struct fc_frame *in_fp,
				void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame *fp;
	struct fc_els_rnid *req;
	struct {
		struct fc_els_rnid_resp rnid;
		struct fc_els_rnid_cid	cid;
		struct fc_els_rnid_gen	gen;
	} *rp;
	u_int8_t fmt;
	size_t len;

	req = fc_frame_payload_get(in_fp, sizeof (*req));
	if (!req) {
		fc_seq_ls_rjt(sp, ELS_RJT_LOGIC, ELS_EXPL_NONE);
	} else {
		fmt = req->rnid_fmt;
		len = sizeof (*rp);
		if (fmt != ELS_RNIDF_GEN ||
		    net32_get(&lp->fl_rnid_gen.rnid_atype) == 0) {
			fmt = ELS_RNIDF_NONE;	/* nothing to provide */
			len -= sizeof (rp->gen);
		}
		fp = fc_frame_alloc(lp->fl_port, len);
		if (fp) {
			rp = fc_frame_payload_get(fp, len);
			ASSERT(rp);
			memset(rp, 0, len);
			rp->rnid.rnid_cmd = ELS_LS_ACC;
			rp->rnid.rnid_fmt = fmt;
			rp->rnid.rnid_cid_len = sizeof (rp->cid);
			net64_put(&rp->cid.rnid_wwpn, lp->fl_port_wwn);
			net64_put(&rp->cid.rnid_wwnn, lp->fl_node_wwn);
			if (fmt == ELS_RNIDF_GEN) {
				rp->rnid.rnid_sid_len = sizeof (rp->gen);
				memcpy(&rp->gen, &lp->fl_rnid_gen,
					sizeof(rp->gen));
			}
			fc_seq_send_last(fc_seq_start_next(sp), fp,
						FC_RCTL_ELS_REP, FC_TYPE_ELS);
		} else {
			fc_seq_exch_complete(sp);
		}
	}
	fc_frame_free(in_fp);
}

/*
 * Handle received ADISC.
 */
static void fc_local_port_adisc_req(struct fc_seq *sp, struct fc_frame *in_fp,
				void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_frame *fp;
	struct fc_els_adisc *req, *rp;
	size_t len;

	req = fc_frame_payload_get(in_fp, sizeof (*req));
	if (!req) {
		fc_seq_ls_rjt(sp, ELS_RJT_LOGIC, ELS_EXPL_NONE);
	} else {
		len = sizeof (*rp);
		fp = fc_frame_alloc(lp->fl_port, len);
		if (fp) {
			rp = fc_frame_payload_get(fp, len);
			ASSERT(rp);
			memset(rp, 0, len);
			rp->adisc_cmd = ELS_LS_ACC;
			net64_put(&rp->adisc_wwpn, lp->fl_port_wwn);
			net64_put(&rp->adisc_wwnn, lp->fl_node_wwn);
			net24_put(&rp->adisc_port_id, lp->fl_fid);
			fc_seq_send_last(fc_seq_start_next(sp), fp,
						FC_RCTL_ELS_REP, FC_TYPE_ELS);
		} else {
			fc_seq_exch_complete(sp);
		}
	}
	fc_frame_free(in_fp);
}

/*
 * Handle received fabric logout request.
 */
static void fc_local_port_recv_logo_req(struct fc_seq *sp, struct fc_frame *fp,
					void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	fc_seq_ls_acc(sp);
	fc_local_port_reset(lp);
	fc_frame_free(fp);
}

/*
 * Check whether this is an ELS request that doesn't require a session,
 * such as an RSCN, ECHO, RNID, FLOGI, or LOGO.
 * If so, handle it here.
 * Returns NULL if the request has been handled.
 */
static struct fc_frame *fc_local_port_fab_req(struct fc_local_port *lp,
				   			struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	void (*recv)(struct fc_seq *, struct fc_frame *, void *);
	fc_fid_t sid;
	u_int	op;

	/*
	 * Check opcode.
	 * If we haven't finished FLOGI, don't take anything but another FLOGI.
	 */
	op = fc_frame_payload_op(fp);
	if (op == ELS_FLOGI) {
		recv = fc_local_port_recv_flogi_req;
		goto deliver;
	}
	if (lp->fl_state == LOCAL_PORT_ST_FLOGI) {
		fc_frame_free(fp);
		return NULL;
	}
	switch (op) {
	case ELS_LOGO:
		fh = fc_frame_header_get(fp);
		sid = net24_get(&fh->fh_s_id);
		if (sid != FC_FID_FLOGI)
			return fp;
		recv = fc_local_port_recv_logo_req;
		break;
	case ELS_RSCN:
		if (lp->fl_state != LOCAL_PORT_ST_READY) {
			fc_frame_free(fp);
			return NULL;
		}
		recv = fc_local_port_rscn_req;
		break;
	case ELS_RLS:
		recv = fc_local_port_rls_req;
		break;
	case ELS_ECHO:
		recv = fc_local_port_echo_req;
		break;
	case ELS_RLIR:
		recv = fc_local_port_rlir_req;
		break;
	case ELS_RNID:
		recv = fc_local_port_rnid_req;
		break;
	case ELS_ADISC:
		recv = fc_local_port_adisc_req;
		break;
	default:
		return fp;
	}
deliver:
	fc_exch_recv_req(lp->fl_vf->vf_exch_mgr, fp, lp->fl_max_payload,
	    recv, lp);
	return NULL;
}

/*
 * Handle a request received by the exchange manager for the local port.
 * This may need to be distributed to a session or may need to create
 * a new session.  This will free the frame.
 */
static void
fc_local_port_recv_req(struct fc_seq *sp, struct fc_frame *fp,
		       void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;
	struct fc_sess *sess;
	struct fc_frame_header *fh;
	fc_fid_t sid;

	fh = fc_frame_header_get(fp);
	sid = net24_get(&fh->fh_s_id);
	sess = fc_sess_lookup_create(lp, sid, 0);

	/*
	 * Send the packet to the session.
	 */
	if (sess) {
		fc_seq_exch(sp)->ex_max_payload = sess->fs_max_payload;
		fc_sess_recv_req(sp, fp, sess);
		fc_sess_release(sess);
	} else {
		fc_seq_ls_rjt(sp, ELS_RJT_UNAB, ELS_EXPL_NONE);
		fc_frame_free(fp);
	}
}

/*
 * Receive a frame for a local port.
 * The frame may be directed to any local port in the virtual fabric.
 */
void fc_local_port_recv(struct fc_local_port *lp, struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_exch_mgr *mp;
	struct fc_sess *sess;
	u_int64_t key;
	u_int32_t f_ctl;
	fc_fid_t s_id;
	fc_fid_t d_id;

	if (lp->fl_state == LOCAL_PORT_ST_INIT) {
		fc_frame_free(fp);
		return;
	}

	/*
	 * If frame is marked invalid, just drop it.
	 */
	f_ctl = net24_get(&fh->fh_f_ctl);
	switch (fp->fr_eof) {
	case FC_EOF_T:
		if (f_ctl & FC_FC_END_SEQ)
			fp->fr_len -= FC_FC_FILL(f_ctl);
		/* fall through */
	case FC_EOF_N:
		mp = lp->fl_vf->vf_exch_mgr;
		if (fh->fh_type == FC_TYPE_BLS)
			fc_exch_recv_bls(mp, fp);
		else if ((f_ctl & (FC_FC_EX_CTX | FC_FC_SEQ_CTX)) ==
			 FC_FC_EX_CTX)
			fc_exch_recv_seq_resp(mp, fp);
		else if (f_ctl & FC_FC_SEQ_CTX)
			fc_exch_recv_resp(mp, fp);
		else {

			/*
			 * Handle special ELS cases like FLOGI, LOGO, and
			 * RSCN here.  These don't require a session.
			 * Even if we had a session, it might not be ready.
			 */
			if (fh->fh_type == FC_TYPE_ELS &&
			    fh->fh_r_ctl == FC_RCTL_ELS_REQ) {
				fp = fc_local_port_fab_req(lp, fp);
				if (!fp)
					break;
			}

			/*
			 * Find session.
			 * If this is a new incoming PLOGI, we won't find it. 
			 */
			s_id = net24_get(&fh->fh_s_id);
			d_id = net24_get(&fh->fh_d_id);
			key = fc_sess_key(d_id, s_id);
			rcu_read_lock();
			sess = sa_hash_lookup(lp->fl_vf->vf_sess_by_fids, &key);
			if (!sess) {
				rcu_read_unlock();
				fc_exch_recv_req(mp, fp, lp->fl_max_payload,
						fc_local_port_recv_req, lp);
			} else {
				fc_sess_hold(sess);
				rcu_read_unlock();
				fc_exch_recv_req(mp, fp, sess->fs_max_payload,
						fc_sess_recv_req, sess);
				fc_sess_release(sess);
			}
		}
		break;

	default:
		SA_LOG("dropping invalid frame (eof %x)", fp->fr_eof);
		fc_print_frame_hdr("fc_local_port_recv - invalid frame", fp);
		fc_frame_free(fp);
		break;
	}
}

/*
 * Handle errors on local port requests.
 * Just put event into the state machine.
 * Don't get locks if in RESET state.
 * The only events possible here so far are exchange TIMEOUT and CLOSED (reset).
 */
static void
fc_local_port_error(enum fc_event event, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	if (lp->fl_state == LOCAL_PORT_ST_RESET)
		return;

	fc_local_port_lock(lp);
	if (event == FC_EV_TIMEOUT) {
		if (lp->fl_retry_count < lp->fl_retry_limit) {
			lp->fl_retry_count++;
			fc_local_port_enter_retry(lp);
		} else {
			fc_local_port_enter_reject(lp);

		}
	}
	if (fc_local_port_debug)
		SA_LOG("event %x retries %d limit %d",
		       event, lp->fl_retry_count, lp->fl_retry_limit);
	fc_local_port_unlock_send(lp);
}

static int fc_local_port_fid_match(const sa_hash_key_t key, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	return * (fc_fid_t *) key == lp->fl_fid;
}

static u_int32_t fc_local_port_fid_hash(const sa_hash_key_t key)
{
	fc_fid_t fid = * (fc_fid_t *) key;

	return (fid >> 8) ^ fid;
}

/*
 * Handle state change from the ingress/egress port.
 */
static void fc_local_port_port_event(int event, void *lp_arg)
{
	struct fc_local_port *lp = lp_arg;

	if (fc_local_port_debug)
		SA_LOG("local fid %6x port event %d", lp->fl_fid, event);
	switch ((enum fc_event)event) {
	case FC_EV_READY:
		fc_local_port_lock(lp);
		if (lp->fl_logon_req && lp->fl_state == LOCAL_PORT_ST_INIT)
			fc_local_port_enter_flogi(lp);
		fc_local_port_unlock_send(lp);
		break;

	case FC_EV_DOWN:
		atomic_inc(&lp->fl_link_fail);
		/* fall-through */
	case FC_EV_CLOSED:		/* FCS or upper layer should handle */
		fc_local_port_reset(lp);
		break;

	default:
		SA_LOG("unexpected event %d", event);
		break;
	}
}

void fc_local_port_set_prli_cb(struct fc_local_port *lp,
			int (*prli_accept_cb) (struct fc_local_port *,
			struct fc_remote_port *, void *), void *arg)
{
	lp->fl_prli_accept = prli_accept_cb;
	lp->fl_prli_cb_arg = arg;
}

void fc_local_port_set_els_cb(struct fc_local_port *lp,
			void (*func)(void *, u_int, void *, size_t),
			void *arg)
{
	lp->fl_els_cb = func;
	lp->fl_els_cb_arg = arg;
}
