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
 * $Id: fc_sess.c 22571 2009-01-13 21:02:55Z jre $
 */

/*
 * Session support.
 *
 * A session is a PLOGI/PRLI session, the state of the conversation between
 * a local port and a remote port.
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
#include "fc_els.h"
#include "fc_ils.h"
#include "fc_fc2.h"
#include "fc_fcp.h"

#include "fc_print.h"
#include "fc_types.h"
#include "fc_event.h"
#include "fc_sess.h"
#include "fc_port.h"
#include "fc_frame.h"
#include "fc_local_port.h"
#include "fc_remote_port.h"
#include "fc_exch.h"
#include "fc_event.h"

#include "fc_exch_impl.h"
#include "fc_virt_fab_impl.h"
#include "fc_local_port_impl.h"
#include "fc_sess_impl.h"

#include <linux/rcupdate.h>

/*
 * Debugging tunables which are only set by debugger or at compile time.
 */
static int fc_sess_debug;

/*
 * Declare hash type for lookup of session by local and remote FCID.
 */
#define	FC_SESS_HASH_SIZE       32	/* XXX increase later */

static int fc_sess_match(const sa_hash_key_t, void *);
static u_int32_t fc_sess_hash(const sa_hash_key_t);

static struct sa_hash_type fc_sess_hash_type = {
	.st_link_offset = offsetof(struct fc_sess, fs_hash_link),
	.st_match = fc_sess_match,
	.st_hash = fc_sess_hash,
};

/*
 * static functions.
 */
static void fc_sess_enter_init(struct fc_sess *);
static void fc_sess_enter_started(struct fc_sess *);
static void fc_sess_enter_plogi(struct fc_sess *);
static void fc_sess_enter_prli(struct fc_sess *);
static void fc_sess_enter_rtv(struct fc_sess *);
static void fc_sess_enter_ready(struct fc_sess *);
static void fc_sess_enter_logo(struct fc_sess *);
static void fc_sess_enter_error(struct fc_sess *);
static void fc_sess_local_port_event(int, void *);
static void fc_sess_recv_plogi_req(struct fc_sess *,
					struct fc_seq *, struct fc_frame *);
static void fc_sess_recv_prli_req(struct fc_sess *,
					struct fc_seq *, struct fc_frame *);
static void fc_sess_recv_prlo_req(struct fc_sess *,
					struct fc_seq *, struct fc_frame *);
static void fc_sess_recv_logo_req(struct fc_sess *,
					struct fc_seq *, struct fc_frame *);
static void fc_sess_delete(struct fc_sess *, void *);
static void fc_sess_timeout(void *);

/*
 * Lock session.
 */
static inline void fc_sess_lock(struct fc_sess *sess)
{
	spin_lock_bh(&sess->fs_lock);
}

/*
 * Unlock session without invoking pending events.
 */
static inline void fc_sess_unlock(struct fc_sess *sess)
{
	spin_unlock_bh(&sess->fs_lock);
}

#ifdef DEBUG_ASSERTS
/*
 * Check whether session is locked.
 */
static inline int fc_sess_locked(const struct fc_sess *sess)
{
#if !defined(__KERNEL__) || \
		defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return spin_is_locked((spinlock_t *) &sess->fs_lock);
#else
	return 1;
#endif /* not __KERNEL__ or CONFIG_SMP */
}
#endif /* DEBUG_ASSERTS */

/*
 * Unlock session.
 * This must handle operations that defer because they can't be done
 * with the session lock held.
 */
static inline void fc_sess_unlock_send(struct fc_sess *sess)
{
	ASSERT(atomic_read(&sess->fs_refcnt) != 0);
	fc_sess_hold(sess);
	fc_sess_unlock(sess);
	sa_event_send_deferred(sess->fs_events);
	fc_sess_release(sess);
}

static void fc_sess_state_enter(struct fc_sess *sess, enum fc_sess_state new)
{
	ASSERT(fc_sess_locked(sess));
	if (sess->fs_state != new)
		sess->fs_retries = 0;
	sess->fs_state = new;
}

/*
 * Create hash lookup table for sessions.
 */
int fc_sess_table_create(struct fc_virt_fab *vf)
{
	struct sa_hash *tp;

	tp = sa_hash_create(&fc_sess_hash_type, FC_SESS_HASH_SIZE);

	if (!tp)
		return -1;
	vf->vf_sess_by_fids = tp;
	return 0;
}

/*
 * Call a function for all sessions on the fabric.
 * The vf_lock must not be held during the callback.
 *
 * Note that the local port lock isn't needed to traverse the list of
 * local ports or the list of sessions on each local port.
 * fc_local_port_release(), used either here or in the callback,
 * requires the vf_lock, however.
 * 
 * Both the outer and inner loop work the same way.  They hold the 
 * current and the next element (local port or session) to keep them
 * from being deleted while the lock is given up.  They are guaranteed to
 * remain on the list while held.
 */
static void fc_sess_iterate(struct fc_virt_fab *vf,
		void (*func) (struct fc_sess *, void *), void *arg)
{
	struct fc_sess *sess;
	struct fc_sess *next;
	struct fc_local_port *lp;
	struct fc_local_port *next_lp;

	fc_virt_fab_lock(vf);
	lp = TAILQ_FIRST(&vf->vf_local_ports);
	if (lp) {
		fc_local_port_hold(lp);
		do {
			next_lp = TAILQ_NEXT(lp, fl_list);
			if (next_lp)
				fc_local_port_hold(next_lp);
			sess = TAILQ_FIRST(&lp->fl_sess_list);
			if (sess) {
				fc_sess_hold(sess);
				do {
					next = TAILQ_NEXT(sess, fs_list);
					if (next)
						fc_sess_hold(next);
					fc_virt_fab_unlock(vf);
					(*func) (sess, arg);
					fc_sess_release(sess);
					fc_virt_fab_lock(vf);
				} while ((sess = next) != NULL);
			}
			fc_virt_fab_unlock(vf);
			fc_local_port_release(lp);
			fc_virt_fab_lock(vf);
		} while ((lp = next_lp) != NULL);
	}
	fc_virt_fab_unlock(vf);
}

/*
 * Remove all sessions in a virtual fabric.
 * This takes care of freeing memory for incoming sessions.
 */
void fc_sess_table_destroy(struct fc_virt_fab *vf)
{
	fc_sess_iterate(vf, fc_sess_delete, NULL);
	fc_virt_fab_lock(vf);
	sa_hash_destroy(vf->vf_sess_by_fids);
	vf->vf_sess_by_fids = NULL;
	fc_virt_fab_unlock(vf);
	synchronize_rcu();
}

/*
 * Create session.
 * If the session already exists, find and hold it.
 */
struct fc_sess *fc_sess_create(struct fc_local_port *lp,
			       struct fc_remote_port *rp)
{
	struct fc_sess *sess;
	struct fc_sess *found;
	struct sa_event_list *events;
	struct fc_virt_fab *vp;
	u_int64_t key;
	

	ASSERT(lp->fl_vf == rp->rp_vf);


	sess = sa_malloc(sizeof(*sess));
	if (sess) {

		events = sa_event_list_alloc();

		if (!events) {
			sa_free(sess);
			sess = NULL;
		} else {
			vp = lp->fl_vf;

			/*
			 * Initialize session even though we might end up
			 * freeing it after getting the lock.
			 * This minimizes lock hold time.
			 */
			memset(sess, 0, sizeof(*sess));
			spin_lock_init(&sess->fs_lock);
			sa_spin_lock_debug_set_hier(&sess->fs_lock, 1);
			sess->fs_state = SESS_ST_INIT;
			atomic_set(&sess->fs_refcnt, 1);
			sess->fs_sess_id = lp->fl_next_sess_id++;
			sess->fs_events = events;
			sess->fs_remote_port = rp;
			sess->fs_local_port = lp;
			sess->fs_remote_fid = rp->rp_fid;
			sess->fs_local_fid = lp->fl_fid;
			sess->fs_max_payload = lp->fl_max_payload;
			sess->fs_e_d_tov = lp->fl_e_d_tov;
			sess->fs_r_a_tov = lp->fl_r_a_tov;
			sa_timer_init(&sess->fs_timer, fc_sess_timeout, sess);

			/*
			 * Since we didn't have the lock while possibly
			 * waiting for memory, check for a simultaneous
			 * creation of the same session.
			 */
			key = fc_sess_key(lp->fl_fid, rp->rp_fid);
			fc_virt_fab_lock(vp);
			found = sa_hash_lookup(vp->vf_sess_by_fids, &key);
			if (found) {
				fc_sess_hold(found);
				fc_virt_fab_unlock(vp);
				sa_free(sess);
				sa_event_list_free(events);
				sess = found;
			} else {
				fc_remote_port_hold(rp);
				fc_local_port_hold(lp);
				sa_hash_insert(vp->vf_sess_by_fids, &key, sess);
				TAILQ_INSERT_TAIL(&lp->fl_sess_list,
						sess, fs_list);
				rp->rp_sess = sess;
				rp->rp_sess_ready = 0;
				fc_virt_fab_unlock(vp);
			}
		}
	}
	return sess;
}

#if defined(__KERNEL__) && !defined(__WINDOWS__) && !defined(__VMKLNX__)
static void fc_sess_rcu_free(struct rcu_head *rcu)
{
	struct fc_sess *sess;

	sess = container_of(rcu, struct fc_sess, fs_rcu);
	sa_event_list_free(sess->fs_events);
	sa_free(sess);
}
#endif	/* __KERNEL__ and not __WINDOWS__ */

/*
 * Delete the session.
 * Called with the local port lock held, but the virtual fabric lock not held.
 */
static void fc_sess_delete(struct fc_sess *sess, void *arg)
{
	struct fc_local_port *lp;
	struct fc_remote_port *rp;
	struct fc_virt_fab *vp;
	struct fc_sess *found;
	u_int64_t key;

	ASSERT(sess);
	ASSERT(sess->fs_local_port);
	ASSERT(sess->fs_remote_port);
	lp = sess->fs_local_port;
	rp = sess->fs_remote_port;
	vp = lp->fl_vf;
	fc_local_port_event_deq(lp, fc_sess_local_port_event, sess);
	key = fc_sess_key(sess->fs_local_fid, sess->fs_remote_fid);

	fc_virt_fab_lock(vp);
	found = sa_hash_lookup_delete(vp->vf_sess_by_fids, &key);
	ASSERT(found);
	ASSERT(found == sess);
	TAILQ_REMOVE(&lp->fl_sess_list, sess, fs_list);	/* under vf_lock */
	fc_virt_fab_unlock(vp);

	sa_timer_cancel(&sess->fs_timer);
#if defined(__KERNEL__) && !defined(__WINDOWS__) && !defined(__VMKLNX__)
	call_rcu(&sess->fs_rcu, fc_sess_rcu_free);
#else /* __KERNEL__ and not __WINDOWS__ */
	sa_event_list_free(sess->fs_events);
	sa_free(sess);
#endif /* __KERNEL__ */
	rp->rp_sess_ready = 0;
	rp->rp_sess = NULL;
	fc_remote_port_release(rp);
	fc_local_port_release(lp);
}

void fc_sess_hold(struct fc_sess *sess)
{
	atomic_inc(&sess->fs_refcnt);
	ASSERT(atomic_read(&sess->fs_refcnt) != 0);
}

void fc_sess_release(struct fc_sess *sess)
{
	ASSERT(atomic_read(&sess->fs_refcnt) > 0);
	if (atomic_dec_and_test(&sess->fs_refcnt))
		fc_sess_delete(sess, NULL);
}

/*
 * Start the session login state machine.
 * Set it to wait for the local_port to be ready if it isn't.
 */
void fc_sess_start(struct fc_sess *sess)
{
	fc_sess_lock(sess);
	if (sess->fs_started == 0) {
		sess->fs_started = 1;
		fc_sess_hold(sess);		/* internal hold while active */
	}
	if (sess->fs_state == SESS_ST_INIT)
		fc_sess_enter_started(sess);
	else if (sess->fs_state == SESS_ST_ERROR) {
		fc_sess_enter_init(sess);
		fc_sess_enter_started(sess);
	}
	fc_sess_unlock_send(sess);
}

/*
 * Stop the session - log it off.
 */
void fc_sess_stop(struct fc_sess *sess)
{
	fc_sess_lock(sess);
	switch (sess->fs_state) {
	case SESS_ST_PRLI:
	case SESS_ST_RTV:
	case SESS_ST_READY:
		fc_sess_enter_logo(sess);
		break;
	default:
		fc_sess_enter_init(sess);
		break;
	}
	fc_sess_unlock_send(sess);
}

/*
 * Reset the session - assume it is logged off.  Used after fabric logoff.
 * The local port code takes care of resetting the exchange manager.
 */
void fc_sess_reset(struct fc_sess *sess)
{
	struct fc_local_port *lp;
	struct fc_virt_fab *vp;
	struct fc_sess *found;
	struct fc_exch_mgr *mp;
	u_int64_t key;
	u_int started;
	u_int held;

	fc_sess_lock(sess);
	lp = sess->fs_local_port;
	if (sess->fs_state == SESS_ST_READY) {
		fc_sess_enter_init(sess);
		fc_sess_unlock(sess);
		mp = lp->fl_vf->vf_exch_mgr;
		fc_exch_mgr_reset(mp, sess->fs_remote_fid, 0);
		fc_exch_mgr_reset(mp, 0, sess->fs_remote_fid);
		fc_sess_lock(sess);
	}

	started = sess->fs_started;
	held = sess->fs_plogi_held;
	sess->fs_started = 0;
	sess->fs_plogi_held = 0;
	sess->fs_remote_port->rp_sess_ready = 0;

	if (lp->fl_fid != sess->fs_local_fid) {
		key = fc_sess_key(sess->fs_local_fid, sess->fs_remote_fid);
		vp = lp->fl_vf;
		fc_virt_fab_lock(vp);
		found = sa_hash_lookup_delete(vp->vf_sess_by_fids, &key);
		ASSERT(found);
		ASSERT(found == sess);
		sess->fs_local_fid = lp->fl_fid;
		key = fc_sess_key(sess->fs_local_fid, sess->fs_remote_fid);
		sa_hash_insert(vp->vf_sess_by_fids, &key, sess);
		fc_virt_fab_unlock(vp);
	}
	fc_sess_enter_init(sess);
	fc_sess_unlock_send(sess);
	if (started)
		fc_sess_release(sess);
	if (held)
		fc_sess_release(sess);
}

/*
 * Reset all sessions for a local port session list.
 * The vf_lock protects the list.
 * Don't hold the lock over the reset call, instead hold the session
 * as well as the next session on the list.
 * Holding the session must guarantee it'll stay on the same list.
 */
void
fc_sess_reset_list(struct fc_virt_fab *vp, struct fc_sess_list *sess_head)
{
	struct fc_sess *sess;
	struct fc_sess *next;

	fc_virt_fab_lock(vp);
	sess = TAILQ_FIRST(sess_head);
	if (sess) {
		fc_sess_hold(sess);
		for (; sess; sess = next) {
			next = TAILQ_NEXT(sess, fs_list);
			if (next)
				fc_sess_hold(next);	/* hold next session */
			fc_virt_fab_unlock(vp);
			fc_sess_reset(sess);
			fc_sess_release(sess);
			fc_virt_fab_lock(vp);
		}
	}
	fc_virt_fab_unlock(vp);
}

/*
 * Get a sequence to use the session.
 * External users shouldn't do this until notified that the session is ready.
 * Internally, it can be done anytime after the local_port is ready.
 */
struct fc_seq *fc_sess_seq_alloc(struct fc_sess *sess,
				 void (*recv)(struct fc_seq *,
					       struct fc_frame *, void *),
				 void (*errh)(enum fc_event, void *),
				 void *arg)
{
	struct fc_seq *sp;
	struct fc_exch *ep;
	struct fc_local_port *lp;

	if (!sess)
		return NULL;
	lp = sess->fs_local_port;
	sp = fc_seq_start_exch(lp->fl_vf->vf_exch_mgr,
			       recv, errh, arg, sess->fs_local_fid,
			       sess->fs_remote_fid);
	if (sp) {
		ep = fc_seq_exch(sp);
		ep->ex_port = lp->fl_port;
		ep->ex_max_payload = sess->fs_max_payload;
		ep->ex_r_a_tov = sess->fs_r_a_tov;
	}
	return sp;
}

/*
 * Send a frame on a session using a new exchange.
 * External users shouldn't do this until notified that the session is ready.
 * Internally, it can be done anytime after the local_port is ready.
 */
int fc_sess_send_req(struct fc_sess *sess, struct fc_frame *fp,
		void (*recv)(struct fc_seq *, struct fc_frame *, void *),
		void (*errh)(enum fc_event, void *),
		void *arg)
{
	struct fc_frame_header *fh;
	struct fc_seq *sp;
	struct fc_local_port *lp;
	int rc;

	fh = fc_frame_header_get(fp);
	ASSERT(fh);
	ASSERT(fh->fh_r_ctl != 0);	/* caller must use fc_frame_setup() */

	sp = fc_sess_seq_alloc(sess, recv, errh, arg);
	if (sp) {
		sp->seq_f_ctl |= FC_FC_SEQ_INIT;
		lp = sess->fs_local_port;
		if (lp->fl_e_d_tov)
			fc_exch_timer_set(fc_seq_exch(sp), lp->fl_e_d_tov);
		rc = fc_seq_send(sp, fp);
	} else {
		fc_frame_free(fp);
		rc = ENOMEM;
	}
	return rc;
}

/*
 * Handle events from the local port.  These can be READY or CLOSED.
 */
static void fc_sess_local_port_event(int event, void *sess_arg)
{
	struct fc_sess *sess = sess_arg;

	fc_sess_lock(sess);
	if (event == FC_EV_READY && sess->fs_state == SESS_ST_STARTED)
		fc_sess_enter_plogi(sess);
	else if (event == FC_EV_CLOSED)
		fc_sess_enter_init(sess);
	fc_sess_unlock_send(sess);
}

static void fc_sess_enter_started(struct fc_sess *sess)
{
	struct fc_local_port *lp;

	/*
	 * If the local port is already logged on, advance to next state.
	 * Otherwise the local port will be logged on by fc_sess_unlock().
	 */
	fc_sess_state_enter(sess, SESS_ST_STARTED);
	lp = sess->fs_local_port;
	if (sess == lp->fl_dns_sess || fc_local_port_test_ready(lp))
		fc_sess_enter_plogi(sess);
}

/*
 * Handle exchange reject or retry exhaustion in various states.
 */
static void fc_sess_reject(struct fc_sess *sess)
{
	switch (sess->fs_state) {
	case SESS_ST_PLOGI:
	case SESS_ST_PRLI:
		fc_sess_enter_error(sess);
		break;
	case SESS_ST_RTV:
		fc_sess_enter_ready(sess);
		break;
	case SESS_ST_LOGO:
		fc_sess_enter_init(sess);
		break;
	case SESS_ST_NONE:
	case SESS_ST_PLOGI_RECV:
	case SESS_ST_STARTED:
		SA_LOG("sess to %x state %d",
			sess->fs_remote_fid, sess->fs_state);
		break;
	case SESS_ST_READY:
	case SESS_ST_INIT:
	case SESS_ST_ERROR:
		break;
	}
}

/*
 * Timeout handler for retrying after allocation failures or exchange timeout.
 */
static void fc_sess_timeout_locked(struct fc_sess *sess)
{
	switch (sess->fs_state) {
	case SESS_ST_PLOGI:
		fc_sess_enter_plogi(sess);
		break;
	case SESS_ST_PRLI:
		fc_sess_enter_prli(sess);
		break;
	case SESS_ST_RTV:
		fc_sess_enter_rtv(sess);
		break;
	case SESS_ST_LOGO:
		fc_sess_enter_logo(sess);
		break;
	case SESS_ST_NONE:
	case SESS_ST_PLOGI_RECV:
	case SESS_ST_STARTED:
		SA_LOG("sess to %x state %d",
			sess->fs_remote_fid, sess->fs_state);
		ASSERT_NOTREACHED;
		break;
	case SESS_ST_READY:
	case SESS_ST_INIT:
	case SESS_ST_ERROR:
		break;
	}
}

/*
 * Timeout handler for retrying after allocation failures or exchange timeout.
 */
static void fc_sess_timeout(void *sess_arg)
{
	struct fc_sess *sess = sess_arg;

	fc_sess_lock(sess);
	fc_sess_timeout_locked(sess);
	fc_sess_unlock(sess);
}

/*
 * Handle retry via timeout.
 */
static void fc_sess_retry(struct fc_sess *sess)
{
	struct fc_local_port *lp;

	ASSERT(fc_sess_locked(sess));

	lp = sess->fs_local_port;
	if (sess->fs_retries < lp->fl_retry_limit) {
		sess->fs_retries++;
		sa_timer_set(&sess->fs_timer, sess->fs_e_d_tov * 1000);
	} else {
		SA_LOG("sess %6x error in state %d - retries exhausted",
				sess->fs_remote_fid, sess->fs_state);
		fc_sess_reject(sess);
	}
}

/*
 * Handle retry for allocation failure via timeout.
 */
static void fc_sess_retry_alloc(struct fc_sess *sess)
{
	if (sess->fs_retries == 0)
		SA_LOG("sess %6x alloc failure in state %d - will retry",
				sess->fs_remote_fid, sess->fs_state);
	fc_sess_retry(sess);
}

/*
 * Handle error event from a sequence issued by the state machine.
 */
static void fc_sess_els_error(enum fc_event event, void *sess_arg,
				enum fc_sess_state exp_state)
{
	struct fc_sess *sess = sess_arg;

	fc_sess_lock(sess);
	if (fc_sess_debug)
		SA_LOG("state %d event %d retries %d",
		       sess->fs_state, event, sess->fs_retries);
	if (event == FC_EV_CLOSED) {
		fc_sess_enter_init(sess);
	} else if (sess->fs_state == exp_state) {
		if (event == FC_EV_TIMEOUT &&
		    sess->fs_retries++ < sess->fs_local_port->fl_retry_limit)
			fc_sess_timeout_locked(sess);
		else
			fc_sess_reject(sess);
	}
	fc_sess_unlock_send(sess);
	fc_sess_release(sess);
}

/*
 * Handle incoming ELS PLOGI response.
 * Save parameters of target.  Finish exchange.
 */
static void fc_sess_plogi_recv_resp(struct fc_seq *sp, struct fc_frame *fp,
					void *sess_arg)
{
	struct fc_sess *sess = sess_arg;
	struct fc_els_ls_rjt *rjp;
	struct fc_els_flogi *plp;
	u_int tov;
	uint16_t csp_seq;
	uint16_t cssp_seq;
	u_int op;

	op = fc_frame_payload_op(fp);
	fc_sess_lock(sess);
	if (sess->fs_state != SESS_ST_PLOGI)
		goto out;
	if (op == ELS_LS_ACC &&
	    (plp = fc_frame_payload_get(fp, sizeof(*plp))) != NULL) {
		fc_remote_port_set_name(sess->fs_remote_port,
					net64_get(&plp->fl_wwpn),
					net64_get(&plp->fl_wwnn));
		tov = net32_get(&plp->fl_csp.sp_e_d_tov);
		if (net16_get(&plp->fl_csp.sp_features) & FC_SP_FT_EDTR)
			tov /= 1000;
		if (tov > sess->fs_e_d_tov)
			sess->fs_e_d_tov = tov;
		csp_seq = net16_get(&plp->fl_csp.sp_tot_seq);
		cssp_seq = net16_get(&plp->fl_cssp[3 - 1].cp_con_seq);
		if (cssp_seq < csp_seq)
			csp_seq = cssp_seq;
		sess->fs_max_seq = csp_seq;
		sess->fs_max_payload = 
                  (uint16_t)fc_local_port_get_payload_size(plp,
					sess->fs_local_port->fl_max_payload);
		if (sess->fs_state == SESS_ST_PLOGI)
			fc_sess_enter_prli(sess);
	} else {
		rjp = fc_frame_payload_get(fp, sizeof(*rjp));
		if (op == ELS_LS_RJT && rjp != NULL &&
		    (rjp->er_reason == ELS_RJT_INPROG ||
		     rjp->er_reason == ELS_RJT_BUSY ||
		     (rjp->er_reason == ELS_RJT_UNAB &&
		      rjp->er_explan == ELS_EXPL_INPROG)))
			fc_sess_retry(sess);    /* try again in a while */
		else
			fc_sess_reject(sess);   /* error */
	}
out:
	fc_sess_unlock_send(sess);
	fc_sess_release(sess);			/* release hold for exchange */
	fc_frame_free(fp);
}

/*
 * Handle an error event from a PLOGI sequence.
 */
static void fc_sess_els_plogi_error(enum fc_event event, void *sess_arg)
{
	fc_sess_els_error(event, sess_arg, SESS_ST_PLOGI);
}

/*
 * Send ELS (extended link service) PLOGI request to peer.
 */
static void fc_sess_enter_plogi(struct fc_sess *sess)
{
	struct fc_frame *fp;
	struct fc_els_flogi *rp;

	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_PLOGI);
	sess->fs_max_payload = sess->fs_local_port->fl_max_payload;
	fp = fc_frame_alloc(sess->fs_local_port->fl_port, sizeof(*rp));
	if (!fp) {
		fc_sess_retry_alloc(sess);
		return;
	}
	rp = fc_frame_payload_get(fp, sizeof(*rp));
	ASSERT(rp);
	fc_local_port_flogi_fill(sess->fs_local_port, rp, ELS_PLOGI);
	sess->fs_e_d_tov = sess->fs_local_port->fl_e_d_tov;
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (fc_sess_send_req(sess, fp, fc_sess_plogi_recv_resp,
					fc_sess_els_plogi_error, sess)) {
		fc_sess_retry_alloc(sess);
	} else {
		fc_sess_hold(sess);
	}
}

/*
 * Handle PRLI response.
 */
static void fc_sess_prli_resp(struct fc_seq *sp, struct fc_frame *fp,
					void *sess_arg)
{
	struct fc_sess *sess = sess_arg;
	u_int op;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;

	fc_sess_lock(sess);
	op = fc_frame_payload_op(fp);

	/*
	 * For PRLI, get the remote port's service parameter flags.
	 */
	switch (sess->fs_state) {
	case SESS_ST_PRLI:
		if (op != ELS_LS_ACC) {
			if (fc_sess_debug) {
				SA_LOG("bad ELS response.  state %d\n",
				       sess->fs_state);
				fc_print_frame_hdr((char *)__FUNCTION__, fp);
			}
			fc_sess_enter_error(sess);
			break;
		}
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		if (pp && pp->prli.prli_spp_len >= sizeof(pp->spp)) {
			sess->fs_remote_port->rp_fcp_parm =
				net32_get(&pp->spp.spp_params);
		}
		fc_sess_enter_rtv(sess);
		break;

	default:
		if (fc_sess_debug)
			SA_LOG("response in state %d ignored\n",
				sess->fs_state);
		break;
	}
	fc_sess_unlock_send(sess);
	fc_sess_release(sess);			/* release hold for exchange */
	fc_frame_free(fp);
}

/*
 * Handle an error event from a PRLI sequence.
 */
static void fc_sess_els_prli_error(enum fc_event event, void *sess_arg)
{
	fc_sess_els_error(event, sess_arg, SESS_ST_PRLI);
}

/*
 * Send ELS PRLI request to target.
 */
static void fc_sess_enter_prli(struct fc_sess *sess)
{
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_frame *fp;

	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_PRLI);

	/*
	 * Special case if session is for name server or any other
	 * well-known address:  Skip the PRLI step.
	 * This should be made more general, possibly moved to the FCP layer.
	 */
	if (sess->fs_remote_fid >= FC_FID_DOM_MGR) {
		fc_sess_enter_ready(sess);
		return;
	}
	fp = fc_frame_alloc(sess->fs_local_port->fl_port, sizeof(*pp));
	if (!fp) {
		fc_sess_retry_alloc(sess);
		return;
	}
	pp = fc_frame_payload_get(fp, sizeof(*pp));
	ASSERT(pp);
	memset(pp, 0, sizeof(*pp));
	pp->prli.prli_cmd = ELS_PRLI;
	pp->prli.prli_spp_len = sizeof(struct fc_els_spp);
	net16_put(&pp->prli.prli_len, sizeof(*pp));
	pp->spp.spp_type = FC_TYPE_FCP;
	pp->spp.spp_flags = FC_SPP_EST_IMG_PAIR;
	net32_put(&pp->spp.spp_params, sess->fs_remote_port->rp_local_fcp_parm);
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (fc_sess_send_req(sess, fp, fc_sess_prli_resp,
					fc_sess_els_prli_error, sess)) {
		fc_sess_retry_alloc(sess);
	} else {
		fc_sess_hold(sess);
	}
}

/*
 * Handle incoming ELS response.
 * Many targets don't seem to support this.
 */
static void fc_sess_els_rtv_resp(struct fc_seq *sp, struct fc_frame *fp,
					void *sess_arg)
{
	struct fc_sess *sess = sess_arg;
	u_int op;

	fc_sess_lock(sess);
	if (sess->fs_state != SESS_ST_RTV)
		goto out;
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		struct fc_els_rtv_acc *rtv;
		uint32_t	toq;
		uint32_t	tov;

		rtv = fc_frame_payload_get(fp, sizeof(*rtv));
		if (rtv) {
			toq = net32_get(&rtv->rtv_toq);
			tov = net32_get(&rtv->rtv_r_a_tov);
			if (tov == 0)
				tov = 1;
			sess->fs_r_a_tov = tov;
			tov = net32_get(&rtv->rtv_e_d_tov);
			if (toq & FC_ELS_RTV_EDRES)
				tov /= 1000000;
			if (tov == 0)
				tov = 1;
			sess->fs_e_d_tov = tov;
		}
	}
	fc_sess_enter_ready(sess);
out:
	fc_sess_unlock_send(sess);
	fc_sess_release(sess);
	fc_frame_free(fp);
}

/*
 * Handle an error event from a RTV sequence.
 */
static void fc_sess_els_rtv_error(enum fc_event event, void *sess_arg)
{
	fc_sess_els_error(event, sess_arg, SESS_ST_RTV);
}

/*
 * Send ELS RTV (Request Timeout Value) request to remote port.
 */
static void fc_sess_enter_rtv(struct fc_sess *sess)
{
	struct fc_els_rtv *rtv;
	struct fc_frame *fp;

	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_RTV);

	fp = fc_frame_alloc(sess->fs_local_port->fl_port, sizeof(*rtv));
	if (!fp) {
		fc_sess_retry_alloc(sess);
		return;
	}
	rtv = fc_frame_payload_get(fp, sizeof(*rtv));
	ASSERT(rtv);
	memset(rtv, 0, sizeof(*rtv));
	rtv->rtv_cmd = ELS_RTV;
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (fc_sess_send_req(sess, fp, fc_sess_els_rtv_resp,
					fc_sess_els_rtv_error, sess)) {
		fc_sess_retry_alloc(sess);
	} else {
		fc_sess_hold(sess);
	}
}

/*
 * Register event handler.
 * Session locks are not needed, the sa_event mechanism has its own locks.
 */
struct sa_event *fc_sess_event_enq(struct fc_sess *sess,
					sa_event_handler_t handler, void *arg)
{
	return sa_event_enq(sess->fs_events, handler, arg);
}

/*
 * Unregister event handler.
 * Session locks are not needed, the sa_event mechanism has its own locks.
 */
void fc_sess_event_deq(struct fc_sess *sess, sa_event_handler_t handler,
		  void *arg)
{
	sa_event_deq(sess->fs_events, handler, arg);
}

static void fc_sess_enter_ready(struct fc_sess *sess)
{
	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_READY);
	sa_event_call_cancel(sess->fs_events, FC_EV_CLOSED);
	sa_event_call_cancel(sess->fs_events, FC_EV_RJT);
	sa_event_call_defer(sess->fs_events, FC_EV_READY);
}

static void fc_sess_enter_init(struct fc_sess *sess)
{
	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_INIT);
	sa_event_call_cancel(sess->fs_events, FC_EV_READY);
	sa_event_call_cancel(sess->fs_events, FC_EV_RJT);
	sa_event_call_defer(sess->fs_events, FC_EV_CLOSED);
}

static void fc_sess_enter_error(struct fc_sess *sess)
{
	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_ERROR);
	sa_event_call_cancel(sess->fs_events, FC_EV_READY);
	sa_event_call_cancel(sess->fs_events, FC_EV_CLOSED);
	sa_event_call_defer(sess->fs_events, FC_EV_RJT);
}

/*
 * Handle LOGO response.
 */
static void fc_sess_els_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
					void *sess_arg)
{
	struct fc_sess *sess = sess_arg;
	u_int op;

	fc_sess_lock(sess);
	if (sess->fs_state == SESS_ST_LOGO) {
		op = fc_frame_payload_op(fp);
		if (op == ELS_LS_ACC) {
			fc_sess_enter_init(sess);
		} else {
			if (fc_sess_debug) {
				SA_LOG("bad ELS response.  "
				       "state %d\n", sess->fs_state);
				fc_print_frame_hdr((char *)__FUNCTION__, fp);
			}
			fc_sess_enter_error(sess);
		}
	}
	fc_sess_unlock_send(sess);
	fc_sess_release(sess);			/* release hold for exchange */
	fc_frame_free(fp);
}

/*
 * Handle an error event from a LOGO sequence.
 */
static void fc_sess_els_logo_error(enum fc_event event, void *sess_arg)
{
	fc_sess_els_error(event, sess_arg, SESS_ST_LOGO);
}

static void fc_sess_enter_logo(struct fc_sess *sess)
{
	struct fc_frame *fp;
	struct fc_els_logo *logo;
	struct fc_local_port *lp;

	ASSERT(fc_sess_locked(sess));
	fc_sess_state_enter(sess, SESS_ST_LOGO);
	lp = sess->fs_local_port;
	fp = fc_frame_alloc(lp->fl_port, sizeof(*logo));
	if (!fp) {
		fc_sess_retry_alloc(sess);
		return;
	}
	logo = fc_frame_payload_get(fp, sizeof(*logo));
	ASSERT(logo);
	memset(logo, 0, sizeof(*logo));
	logo->fl_cmd = ELS_LOGO;
	net24_put(&logo->fl_n_port_id, lp->fl_fid);
	net64_put(&logo->fl_n_port_wwn, lp->fl_port_wwn);

	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (fc_sess_send_req(sess, fp, fc_sess_els_logo_resp,
					fc_sess_els_logo_error, sess)) {
		fc_sess_retry_alloc(sess);
	} else {
		fc_sess_hold(sess);
	}
}

/*
 * Get local port.
 */
struct fc_local_port *fc_sess_get_local_port(struct fc_sess *sess)
{
	return sess->fs_local_port;
}

/*
 * Get remote port.
 */
struct fc_remote_port *fc_sess_get_remote_port(struct fc_sess *sess)
{
	return sess->fs_remote_port;
}

/*
 * Get local FC_ID.
 */
fc_fid_t fc_sess_get_sid(struct fc_sess * sess)
{
	return sess->fs_local_fid;
}

/*
 * Get remote FC_ID.
 */
fc_fid_t fc_sess_get_did(struct fc_sess * sess)
{
	return sess->fs_remote_fid;
}

/*
 * Get max payload size.
 */
u_int fc_sess_get_max_payload(struct fc_sess * sess)
{
	return sess->fs_max_payload;
}

/*
 * Get virtual fabric pointer.
 */
struct fc_virt_fab *fc_sess_get_virt_fab(struct fc_sess *sess)
{
	return sess->fs_local_port->fl_vf;
}

/*
 * Get E_D_TOV.
 */
u_int fc_sess_get_e_d_tov(struct fc_sess *sess)
{
	return sess->fs_e_d_tov;
}

/*
 * Get R_A_TOV.
 */
u_int fc_sess_get_r_a_tov(struct fc_sess *sess)
{
	return sess->fs_r_a_tov;
}

int fc_sess_is_ready(struct fc_sess *sess)
{
	return sess->fs_state == SESS_ST_READY;
}

/*
 * Handle a request received by the exchange manager for the session.
 * This may be an entirely new session, or a PLOGI or LOGO for an existing one.
 * This will free the frame.
 */
void fc_sess_recv_req(struct fc_seq *sp, struct fc_frame *fp, void *sess_arg)
{
	struct fc_sess *sess = sess_arg;
	struct fc_frame_header *fh;
	u_int op;

	fh = fc_frame_header_get(fp);
	if (fh->fh_r_ctl == FC_RCTL_ELS_REQ && fh->fh_type == FC_TYPE_ELS) {
		op = fc_frame_payload_op(fp);
		switch (op) {
		case ELS_PLOGI:
			fc_sess_recv_plogi_req(sess, sp, fp);
			break;
		case ELS_PRLI:
			fc_sess_recv_prli_req(sess, sp, fp);
			break;
		case ELS_PRLO:
			fc_sess_recv_prlo_req(sess, sp, fp);
			break;
		case ELS_LOGO:
			fc_sess_recv_logo_req(sess, sp, fp);
			break;
		case ELS_REC:
			fc_exch_mgr_els_rec(sp, fp);
			break;
		case ELS_RRQ:
			fc_exch_mgr_els_rrq(sp, fp);
			break;
		default:
			SA_LOG("unhandled ELS request %x", op);
			/* fall-through */
		case ELS_RTV:
			fc_seq_ls_rjt(sp, ELS_RJT_UNSUP, ELS_EXPL_NONE);
			fc_frame_free(fp);
			break;
		}
	} else {
		fc_port_ingress(sess->fs_local_port->fl_port, fp);
	}
}

/*
 * Handle incoming PLOGI request.
 */
static void fc_sess_recv_plogi_req(struct fc_sess *sess, 
	struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_frame *fp = rx_fp;
	struct fc_frame_header *fh;
	struct fc_remote_port *rp;
	struct fc_local_port *lp;
	struct fc_els_flogi *pl;
	struct fc_exch_mgr *em;
	fc_fid_t sid;
	fc_wwn_t wwpn;
	fc_wwn_t wwnn;
	enum fc_els_rjt_reason reject = 0;

	fh = fc_frame_header_get(fp);
	sid = net24_get(&fh->fh_s_id);
	pl = fc_frame_payload_get(fp, sizeof(*pl));
	if (!pl) {
		SA_LOG("incoming PLOGI from %x too short", sid);
		/* XXX TBD: send reject? */
		fc_frame_free(fp);
		return;
	}
	ASSERT(pl);	/* checked above */
	wwpn = net64_get(&pl->fl_wwpn);
	wwnn = net64_get(&pl->fl_wwnn);
	fc_sess_lock(sess);
	rp = sess->fs_remote_port;
	lp = sess->fs_local_port;

	/*
	 * If the session was just created, possibly due to the incoming PLOGI,
	 * set the state appropriately and accept the PLOGI.
	 *
	 * If we had also sent a PLOGI, and if the received PLOGI is from a
	 * higher WWPN, we accept it, otherwise an LS_RJT is sent with reason
	 * "command already in progress".
	 *
	 * XXX TBD: If the session was ready before, the PLOGI should result in
	 * all outstanding exchanges being reset.
	 */
	switch (sess->fs_state) {
	case SESS_ST_INIT:
		if (!lp->fl_prli_accept) {
			/*
			 * The upper level protocol isn't expecting logins.
			 */
			SA_LOG("incoming PLOGI from %6x wwpn %llx state INIT "
			    		"- reject\n", sid, wwpn);
			reject = ELS_RJT_UNSUP;
		} else {
			if (fc_sess_debug)
				SA_LOG("incoming PLOGI from %6x "
					"wwpn %llx state INIT "
					"- accept\n", sid, wwpn);
		}
		break;

	case SESS_ST_STARTED:
	case SESS_ST_PLOGI_RECV:
		break;
	case SESS_ST_PLOGI:
		if (fc_sess_debug) 
			SA_LOG("incoming PLOGI from %x in PLOGI state %d",
			       sid, sess->fs_state);
		if (wwpn < lp->fl_port_wwn)
			reject = ELS_RJT_INPROG;
		break;
	case SESS_ST_PRLI:
	case SESS_ST_RTV:
	case SESS_ST_ERROR:
	case SESS_ST_READY:
		if (fc_sess_debug)
			SA_LOG("incoming PLOGI from %x in logged-in state %d "
				"- accept", sid, sess->fs_state);
		/* XXX TBD - should reset */
		break;
	case SESS_ST_NONE:
	default:
		if (fc_sess_debug)
			SA_LOG("incoming PLOGI from %x in unexpected state %d",
			       sid, sess->fs_state);
		break;
	}

	/*
	 * we'll only accept a login if the port name
	 * matches or was unknown.
	 */
	if (!reject && rp->rp_port_wwn != 0 && rp->rp_port_wwn != wwpn) {
		SA_LOG("incoming PLOGI from name %llx expected %llx\n",
				wwpn, rp->rp_port_wwn);
		reject = ELS_RJT_UNAB;
	}

	if (reject) {
		fc_seq_ls_rjt(sp, reject, ELS_EXPL_NONE);
		fc_frame_free(fp);
	} else if ((fp = fc_frame_alloc(lp->fl_port, sizeof(*pl))) == NULL) {
		fp = rx_fp;
		fc_seq_ls_rjt(sp, ELS_RJT_UNAB, ELS_EXPL_NONE);
		fc_frame_free(fp);
	} else {
		sp = fc_seq_start_next(sp);
		ASSERT(sp);
		fc_remote_port_set_name(rp, wwpn, wwnn);

		/* 
		 * Get session payload size from incoming PLOGI.
		 */
		sess->fs_max_payload = 
                  (uint16_t)fc_local_port_get_payload_size(pl,
					lp->fl_max_payload);
		fc_frame_free(rx_fp);
		pl = fc_frame_payload_get(fp, sizeof(*pl));
		ASSERT(pl);
		fc_local_port_flogi_fill(lp, pl, ELS_LS_ACC);

		/*
		 * Send LS_ACC.  If this fails, the originator should retry.
		 */
		fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);

		if (!sess->fs_plogi_held) {
			sess->fs_plogi_held = 1;
			fc_sess_hold(sess);	/* represents login */
		}

		switch (sess->fs_state) {
		case SESS_ST_STARTED:
		case SESS_ST_PLOGI:
			fc_sess_enter_prli(sess);
			break;
		case SESS_ST_PLOGI_RECV:
		case SESS_ST_LOGO:
		case SESS_ST_PRLI:
			break;
		case SESS_ST_RTV:
		case SESS_ST_ERROR:
		case SESS_ST_READY:
			rp->rp_sess_ready = 0;
			fc_sess_enter_init(sess);
			fc_sess_unlock_send(sess);	/* sends closed event */

			em = lp->fl_vf->vf_exch_mgr;
			fc_exch_mgr_reset(em, 0, sid);
			fc_exch_mgr_reset(em, sid, 0);
			fc_sess_start(sess);
			return;
		case SESS_ST_INIT:
		default:
			fc_sess_state_enter(sess, SESS_ST_PLOGI_RECV);
			break;
		}
	}
	fc_sess_unlock_send(sess);
}

/*
 * Handle incoming PRLI request.
 */
static void fc_sess_recv_prli_req(struct fc_sess *sess,
	struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct fc_local_port *lp;
	struct fc_remote_port *rp;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp *rspp;	/* request service param page */
	struct fc_els_spp *spp;		/* response spp */
	u_int	len;
	u_int	plen;
	enum fc_els_rjt_reason reason = ELS_RJT_UNAB;
	enum fc_els_rjt_explan explan = ELS_EXPL_NONE;
	enum fc_els_spp_resp resp;

	fh = fc_frame_header_get(rx_fp);
	lp = sess->fs_local_port;
	switch (sess->fs_state) {
	case SESS_ST_PLOGI_RECV:
	case SESS_ST_PRLI:
	case SESS_ST_READY:
		if (lp->fl_prli_accept &&
		    (*lp->fl_prli_accept)(lp, sess->fs_remote_port,
					   lp->fl_prli_cb_arg) == 0) {
			reason = ELS_RJT_NONE;
		}
		break;
	default:
		break;
	}
	len = rx_fp->fr_len - sizeof (*fh);
	pp = fc_frame_payload_get(rx_fp, sizeof(*pp));
	if (pp == NULL) {
		reason = ELS_RJT_PROT;
		explan = ELS_EXPL_INV_LEN;
	} else {
		plen = net16_get(&pp->prli.prli_len);
		if ((plen % 4) != 0 || plen > len) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		} else if (plen < len) {
			len = plen;
		}
		plen = pp->prli.prli_spp_len;
		if ((plen % 4) != 0 || plen < sizeof (*spp) ||
		    plen > len || len < sizeof (*pp)) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		}
		rspp = &pp->spp;
	}
	if (reason != ELS_RJT_NONE ||
	    (fp = fc_frame_alloc(lp->fl_port, len)) == NULL) {
		fc_seq_ls_rjt(sp, reason, explan);
	} else {
		sp = fc_seq_start_next(sp);
		ASSERT(sp);
		pp = fc_frame_payload_get(fp, len);
		ASSERT(pp);
		memset(pp, 0, len);
		pp->prli.prli_cmd = ELS_LS_ACC;
		pp->prli.prli_spp_len = (net8_t) plen;
		net16_put(&pp->prli.prli_len, (u_int16_t)len);
		len -= sizeof (struct fc_els_prli);

		/*
		 * Go through all the service parameter pages and build
		 * response.  If plen indicates longer SPP than standard,
		 * use that.  The entire response has been pre-cleared above.
		 */
		spp = &pp->spp; 
		while (len >= plen) {
			spp->spp_type = rspp->spp_type;
			spp->spp_type_ext = rspp->spp_type_ext;
			spp->spp_flags = rspp->spp_flags &
					FC_SPP_EST_IMG_PAIR;
			resp = FC_SPP_RESP_ACK;
			if (rspp->spp_flags & FC_SPP_RPA_VAL) {
				resp = FC_SPP_RESP_NO_PA;
			}
			switch (rspp->spp_type) {
			case 0:			/* common to all FC-4 types */
				break;
			case FC_TYPE_FCP:
				rp = sess->fs_remote_port;
				ASSERT(rp);
				rp->rp_fcp_parm = net32_get(&rspp->spp_params);
				net32_put(&spp->spp_params,
				    rp->rp_local_fcp_parm);
				break;
			default:
				resp = FC_SPP_RESP_INVL;
				break;
			}
			spp->spp_flags |= resp;
			len -= plen,
			rspp = (struct fc_els_spp *) ((char *) rspp + plen);
			spp = (struct fc_els_spp *) ((char *) spp + plen);
		}

		/*
		 * Send LS_ACC.  If this fails, the originator should retry.
		 */
		fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);

		/*
		 * Get lock and re-check state.
		 */
		fc_sess_lock(sess);
		switch (sess->fs_state) {
		case SESS_ST_PLOGI_RECV:
		case SESS_ST_PRLI:
			fc_sess_enter_rtv(sess);
			break;
		case SESS_ST_READY:
			break;
		default:
			break;
		}
		fc_sess_unlock_send(sess);
	}
	fc_frame_free(rx_fp);
}

/*
 * Handle incoming PRLO request.
 */
static void fc_sess_recv_prlo_req(struct fc_sess *sess, struct fc_seq *sp,
	struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	fh = fc_frame_header_get(fp);
	SA_LOG("incoming PRLO from %x state %d",
	       net24_get(&fh->fh_s_id), sess->fs_state);
	fc_seq_ls_rjt(sp, ELS_RJT_UNAB, ELS_EXPL_NONE);
	fc_frame_free(fp);
}

/*
 * Handle incoming LOGO request.
 */
static void fc_sess_recv_logo_req(struct fc_sess *sess, struct fc_seq *sp,
	struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	u_int	held;

	fh = fc_frame_header_get(fp);
	fc_sess_lock(sess);
	held = sess->fs_plogi_held;
	sess->fs_plogi_held = 0;
	fc_sess_enter_init(sess);
	fc_sess_unlock_send(sess);
	if (held)
		fc_sess_release(sess);
	fc_seq_ls_acc(sp);
	fc_frame_free(fp);
}

static int fc_sess_match(sa_hash_key_t key, void *sess_arg)
{
	struct fc_sess *sess = sess_arg;

	return *(u_int64_t *) key ==
			fc_sess_key(sess->fs_local_fid, sess->fs_remote_fid);
}

static u_int32_t fc_sess_hash(sa_hash_key_t keyp)
{
	u_int64_t key = *(u_int64_t *) keyp;

	return (u_int32_t)((key >> 20) ^ key);
}

/*
 * Lookup or create a new session.
 * Returns with the session held.
 */
struct fc_sess *fc_sess_lookup_create(struct fc_local_port *lp,
				      fc_fid_t fid, fc_wwn_t wwpn)
{
	struct fc_virt_fab *vp;
	struct fc_remote_port *rp;
	struct fc_sess *sess;
	u_int64_t key;

	ASSERT(fid != 0);
	ASSERT(lp);
	ASSERT(lp->fl_vf);

	vp = lp->fl_vf;

	/*
	 * Look for the source as a remote port in the existing session table.
	 */
	key = fc_sess_key(lp->fl_fid, fid);
	rcu_read_lock();
	sess = sa_hash_lookup(vp->vf_sess_by_fids, &key);

	/*
	 * Create new session if we didn't find one.
	 */
	if (!sess) {
		rcu_read_unlock();
		rp = fc_remote_port_lookup_create(vp, fid, wwpn, 0);
		if (rp) {
			sess = fc_sess_create(lp, rp);	/* holds remote port */
			fc_remote_port_release(rp);
		}
	} else {
		fc_sess_hold(sess);
		rcu_read_unlock();
	}
	return sess;
}
