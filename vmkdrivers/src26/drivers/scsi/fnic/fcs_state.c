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
 * $Id: fcs_state.c 24206 2009-02-20 22:08:44Z jre $
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "fc_types.h"
#include "fc_frame.h"
#include "fc_port.h"
#include "fc_virt_fab.h"
#include "fc_local_port.h"
#include "fc_remote_port.h"
#include "fc_exch.h"
#include "fc_print.h"
#include "fc_sess.h"
#include "fc_disc_targ.h"
#include "fc_event.h"
#include "fc_ils.h"
#include "fc_fcp.h"
#include "fcs_state.h"
#include "fcs_state_impl.h"
#if defined(__KERNEL__) && !defined(__WINDOWS__) && !defined(FCOE_TARGET)
#include "openfc_ioctl.h"
#else
#define fcs_ev_add(sp, type, buf, len)
#define fcs_ev_destroy()
#define	fcs_ev_els NULL
#endif /* defined(__KERNEL__) && !defined(__WINDOWS__) */


static int fcs_debug;		/* set non-zero to get debug messages */

static void fcs_recv_req(void *, struct fc_frame *);
static void fcs_local_port_event(int, void *);
static int fcs_local_port_prli_accept(struct fc_local_port *,
				      struct fc_remote_port *, void *);
static void fcs_add_remote(void *, struct fc_remote_port *, enum fc_event);
static void fcs_sess_event(int, void *);
static void fcs_port_event(int, void *);

void fcs_module_init(void)
{
	fc_exch_module_init();
}

void fcs_module_exit(void)
{
	fc_exch_module_exit();
	fcs_ev_destroy();
}

static void fcs_disc_nop(void *arg)
{
}

static void fcs_rp_nop(void *arg, struct fc_remote_port *rp)
{
}

static int fcs_set_mfs_int(struct fcs_state *sp, u_int mfs)
{
	if (mfs >= FC_MIN_MAX_FRAME) {
		fc_port_set_max_frame_size(sp->fs_inner_port, mfs);
		return 0;
	}
	return -1;
}

int fcs_set_mfs(struct fcs_state *sp, u_int mfs)
{
	u_int old_mfs;
	int error;

	old_mfs = fc_port_get_max_frame_size(sp->fs_inner_port);
	error = fcs_set_mfs_int(sp, mfs);
	if (error == 0 && mfs < old_mfs)
		fcs_reset(sp);
	return error;
}

/*
 * Allocate the FCS state.
 * Called once per instance of the OpenFC driver.
 */
struct fcs_state *fcs_create(struct fcs_create_args *ap)
{
	struct fcs_state *sp;
	struct fc_port *inner_port;
	struct fc_port *outer_port;

	ASSERT(ap->fca_disc_done);
	ASSERT(ap->fca_port);

	sp = sa_malloc(sizeof(*sp));
	if (!sp)
		return NULL;
	memset(sp, 0, sizeof(*sp));

	sp->fs_args = *ap;		/* struct copy of args */

	sp->fs_vf = fc_virt_fab_alloc(0, FC_CLASS_3,
					ap->fca_min_xid, ap->fca_max_xid);

	if (!sp->fs_vf)
		goto error;

	if (!sp->fs_args.fca_remote_port_state_change)
		sp->fs_args.fca_remote_port_state_change = fcs_rp_nop;
	if (!sp->fs_args.fca_disc_done)
		sp->fs_args.fca_disc_done = fcs_disc_nop;

	inner_port = fc_port_alloc();

	if (!inner_port)
		goto error;
	sp->fs_inner_port = inner_port;
	outer_port = ap->fca_port;
	fcs_set_mfs_int(sp, fc_port_get_max_frame_size(outer_port));
	fc_port_set_ingress(inner_port, fcs_recv_req, sp);
	fc_port_set_egress(inner_port, (int (*)(void *, struct fc_frame *))
			   fc_port_egress, outer_port);
	fc_port_set_frame_alloc(inner_port, outer_port->np_frame_alloc);
	fc_port_set_ingress(outer_port,
			    (void (*)(void *, struct fc_frame *)) fcs_recv, sp);
	if (!fc_port_enq_handler(outer_port, fcs_port_event, sp))
		goto error;
	return sp;

error:
	fcs_destroy(sp);
	return NULL;
}

static int fcs_drop(void *arg, struct fc_frame *fp)
{
	if (fp) fc_frame_free(fp);
	return 0;
}

/*
 * Destroy and free the FCS state.
 */
void fcs_destroy(struct fcs_state *sp)
{
	struct fc_port *port;

	ASSERT(sp->fs_args.fca_port);
	flush_scheduled_work();

	sp->fs_args.fca_disc_done = fcs_disc_nop;
	sp->fs_args.fca_remote_port_state_change = fcs_rp_nop;
	fcs_ev_add(sp, OFC_EV_HBA_DEL, NULL, 0);

	fc_port_set_egress(sp->fs_args.fca_port, fcs_drop, NULL);

	fc_port_deq_handler(sp->fs_args.fca_port, fcs_port_event, sp);
	port = sp->fs_inner_port;
	if (port) {
		sp->fs_inner_port = NULL;
		fc_port_close_ingress(port);
		fc_port_close_egress(port);
	}
	fc_port_close_ingress(sp->fs_args.fca_port);
	if (sp->fs_local_port) {
		fc_local_port_destroy(sp->fs_local_port);
		fc_local_port_release(sp->fs_local_port);
	}
	if (sp->fs_vf)
		fc_virt_fab_free(sp->fs_vf);
	fc_port_close_egress(sp->fs_args.fca_port);
	sa_free(sp);
}

/*
 * XXX could be merely the ingress handler for the port?
 */
void fcs_recv(struct fcs_state *sp, struct fc_frame *fp)
{

	if (sp->fs_local_port) {
		fp->fr_in_port = sp->fs_inner_port;
		fc_local_port_recv(sp->fs_local_port, fp);
	} else {
		SA_LOG("fcs_local_port_set needed before receiving");
		fc_frame_free(fp);
	}
}

/*
 * Handler for new requests arriving.
 */
static void fcs_recv_req(void *sp_arg, struct fc_frame *fp)
{
	struct fcs_state *sp = sp_arg;
	struct fc_frame_header *fh;

	fh = fc_frame_header_get(fp);
	ASSERT(fh);

	if (fh->fh_type == FC_TYPE_FCP && sp->fs_args.fca_fcp_recv) {
		(*sp->fs_args.fca_fcp_recv)(fp->fr_seq,
						fp, sp->fs_args.fca_cb_arg);
	} else {
		fc_seq_hold(fp->fr_seq);
		fc_seq_exch_complete(fp->fr_seq);
		fc_frame_free(fp);
	}
}

/*
 * Set local port parameters.
 */
int fcs_local_port_set(struct fcs_state *sp, fc_wwn_t wwnn, fc_wwn_t wwpn)
{
	struct fc_local_port *lp;

	ASSERT(sp->fs_inner_port);
	ASSERT(!sp->fs_local_port);
	lp = fc_local_port_create(sp->fs_vf, sp->fs_inner_port, wwpn, wwnn,
			sp->fs_args.fca_e_d_tov,
			sp->fs_args.fca_plogi_retries);
	if (!lp)
		return -1;
	fc_local_port_set_prli_cb(lp, fcs_local_port_prli_accept, sp);
	fc_local_port_add_fc4_type(lp, FC_TYPE_FCP);
	fc_local_port_add_fc4_type(lp, FC_TYPE_CT);
	sp->fs_local_port = lp;
	fc_local_port_set_els_cb(lp, fcs_ev_els, sp);
	fcs_ev_add(sp, OFC_EV_HBA_ADD, NULL, 0);
	return 0;
}

/*
 * Start logins and discoveries.
 */
void fcs_start(struct fcs_state *sp)
{
	ASSERT(sp->fs_local_port);
	fc_local_port_logon(sp->fs_local_port, fcs_local_port_event, sp);
}

/*
 * Shutdown FCS, prepare for restart or fcs_destroy().
 */
void fcs_stop(struct fcs_state *sp)
{
	ASSERT(sp->fs_local_port);
	fc_local_port_logoff(sp->fs_local_port);
}

/*
 * Reset FCS.  Redo discovery.  Relogon to all sessions.
 * The caller may not have dropped its references to remote ports.
 * We logoff the local port and log back on when that's done,
 * which restarts discovery.
 */
void fcs_reset(struct fcs_state *sp)
{
	struct fc_local_port *lp;

	ASSERT(sp->fs_local_port);
	lp = sp->fs_local_port;
	sp->fs_disc_done = 0;
	fc_local_port_reset(lp);
}

/*
 * Like fcs_reset, but does not try to re-login.
 */
void fcs_quiesce(struct fcs_state *sp)
{
	struct fc_local_port *lp;

	ASSERT(sp->fs_local_port);
	lp = sp->fs_local_port;
	sp->fs_disc_done = 0;
	fc_local_port_quiesce(lp);
}

static void fcs_local_port_event(int event, void *fcs_arg)
{
	struct fcs_state *sp = fcs_arg;
	struct fc_local_port *lp;
	int rc;

	lp = sp->fs_local_port;
	switch (event) {
	case FC_EV_READY:
		if (sp->fs_args.fca_service_params & FCP_SPPF_INIT_FCN) {
			rc = fc_disc_targ_start(lp, FC_TYPE_FCP,
						fcs_add_remote, sp);
			if (rc != 0)
				SA_LOG("target discovery start error %d", rc);
		} else {
			(*sp->fs_args.fca_disc_done)(sp->fs_args.fca_cb_arg);
		}
		fcs_ev_add(sp, OFC_EV_PT_ONLINE, NULL, 0);
		break;
	case FC_EV_DOWN:	/* local port will re-logon when it can */
		break;
	case FC_EV_CLOSED:	/* local port closed by driver */
		fcs_ev_add(sp, OFC_EV_PT_OFFLINE, NULL, 0);
		break;
	default:
		SA_LOG("unexpected event %d\n", event);
		break;
	}
}

/*
 * callback from local port when a PLOGI request is received
 */
static int fcs_local_port_prli_accept(struct fc_local_port *lp,
				struct fc_remote_port *rp, void *fcs_arg)
{
	struct fcs_state *sp = fcs_arg;
	int reject = 0;

	rp->rp_local_fcp_parm = sp->fs_args.fca_service_params;
	if (sp->fs_args.fca_prli_accept)
		reject = (*sp->fs_args.fca_prli_accept)(sp->fs_args.fca_cb_arg,
							rp);
	if (fcs_debug)
		SA_LOG("%s remote fid %6x",
		       reject ? "reject" : "accept", rp->rp_fid);
	return reject;
}

fc_fid_t fcs_get_fid(const struct fcs_state * sp)
{
	return fc_local_port_get_fid(sp->fs_local_port);
}

static void fcs_remote_work(struct work_struct *work)
{
	struct fc_remote_port *rp;
	struct fcs_state *sp;
	void *arg;

	rp = container_of(work, struct fc_remote_port, rp_work);
	sp = rp->rp_fcs_priv;
	arg = sp->fs_args.fca_cb_arg;

	(*sp->fs_args.fca_remote_port_state_change)(arg, rp);

	fc_remote_port_release(rp);	/* may delete remote port */
}

static void fcs_remote_change(struct fcs_state *sp, struct fc_remote_port *rp)
{
	if (schedule_work(&rp->rp_work))
		fc_remote_port_hold(rp);
}

/*
 * Notification from discovery of a new remote port.
 * Create a session and wait for notification on the session state before
 * reporting the remote port as usable/found.
 * rp is NULL if discovery is complete.
 */
static void fcs_add_remote(void *fcs_arg, struct fc_remote_port *rp,
				enum fc_event event)
{
	struct fcs_state *sp = fcs_arg;
	struct fc_local_port *lp;
	struct fc_sess *sess;

	lp = sp->fs_local_port;
	ASSERT(lp);

	if (rp && rp->rp_fcs_priv == NULL) {
		rp->rp_fcs_priv = sp;
#if defined(__VMKLNX__)
		INIT_WORK(&rp->rp_work, fcs_remote_work);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
		INIT_WORK(&rp->rp_work,
			  (void (*)(void *))fcs_remote_work, &rp->rp_work);
#else
		INIT_WORK(&rp->rp_work, fcs_remote_work);
#endif /* __VMKLNX__ */
	}

	if (event == FC_EV_CLOSED) {
		ASSERT(rp->rp_sess_ready == 0);
		if (fcs_debug)
			SA_LOG("removing remote fid %x wwpn %llx ref %d",
			       rp->rp_fid, rp->rp_port_wwn,
			       atomic_read(&rp->rp_refcnt));
		fcs_ev_add(sp, OFC_EV_TARG_REMOVED,
				&rp->rp_port_wwn, sizeof (rp->rp_port_wwn));
		fcs_remote_change(sp, rp);
	} else if (rp) {
		fcs_ev_add(sp, OFC_EV_PT_NEW_TARG, NULL, 0);
		rp->rp_local_fcp_parm = sp->fs_args.fca_service_params;
		rp->rp_fcs_priv = sp;
		if (event == FC_EV_START) {
			if (fcs_debug)
				SA_LOG("new remote fid %x wwpn %llx",
				       rp->rp_fid, rp->rp_port_wwn);
			sess = rp->rp_sess;
			if (sess) {
				sp->fs_disc_done = 0;
				fc_sess_event_enq(sess, fcs_sess_event, rp);
				fc_sess_start(sess);
			}
		} else if (fcs_debug) {
			SA_LOG("old remote fid %x wwpn %llx", rp->rp_fid,
			       rp->rp_port_wwn);
		}
	} else {
		if (fcs_debug)
			SA_LOG("discovery complete");
		if (!sp->fs_disc_done)
			(*sp->fs_args.fca_disc_done)(sp->fs_args.fca_cb_arg);
		sp->fs_disc_done = 1;
	}
}

/*
 * Session event handler.
 * Note that the argument is the associated remote port for now.
 */
static void fcs_sess_event(int event, void *rp_arg)
{
	struct fc_remote_port *rp = rp_arg;
	struct fcs_state *sp;
	void	*arg;

	sp = rp->rp_fcs_priv;
	ASSERT(sp);
	arg = sp->fs_args.fca_cb_arg;

	switch (event) {
	case FC_EV_READY:
		rp->rp_sess_ready = 1;
		if (fcs_debug)
			SA_LOG("remote %6x ready", rp->rp_fid);
		fcs_remote_change(sp, rp);
		fcs_ev_add(sp, OFC_EV_TARG_ONLINE,
				&rp->rp_port_wwn, sizeof (rp->rp_port_wwn));
		break;
	case FC_EV_RJT:				/* retries exhausted */
		if (fcs_debug)
			SA_LOG("remote %6x error", rp->rp_fid);
		break;
	case FC_EV_CLOSED:
		rp->rp_sess_ready = 0;
		if (fcs_debug)
			SA_LOG("remote %6x closed", rp->rp_fid);
		fcs_remote_change(sp, rp);
		fcs_ev_add(sp, OFC_EV_TARG_OFFLINE,
				&rp->rp_port_wwn, sizeof (rp->rp_port_wwn));
		break;
	default:
		break;
	}
}

/*
 * Return a session that can be used for access to a remote port.
 * If there is no session, or it is not ready (PRLI is not complete),
 * NULL is returned.
 */
struct fc_sess *fcs_sess_get(struct fcs_state *sp, struct fc_remote_port *rp)
{
	struct fc_sess *sess = NULL;

	if (rp->rp_sess_ready)
		sess = rp->rp_sess;
	return sess;
}

static void fcs_port_event(int event, void *sp_arg)
{
	struct fcs_state *sp = sp_arg;

	switch (event) {
	case FC_EV_DOWN:
		fcs_ev_add(sp, OFC_EV_LINK_DOWN, NULL, 0);
		break;
	case FC_EV_READY:
		fcs_ev_add(sp, OFC_EV_LINK_UP, NULL, 0);
		break;
	}
	ASSERT(sp->fs_inner_port);
	fc_port_send_event(sp->fs_inner_port, event);
}

struct fc_local_port *fcs_get_local_port(struct fcs_state *sp)
{
	return sp->fs_local_port;
}
