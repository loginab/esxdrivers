/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006-2008 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fc_exch.c 18557 2008-09-14 22:36:38Z jre $
 */

/*
 * Fibre Channel exchange and sequence handling.
 */

#include "sa_kernel.h"
#include "queue.h"
#include "sa_assert.h"
#include "net_types.h"
#include "sa_log.h"
#include "sa_hash.h"
#include "sa_timer.h"

#include <linux/gfp.h>

#include "fc_encaps.h"
#include "fc_fc2.h"
#include "fc_fs.h"
#include "fc_ils.h"
#include "fc_els.h"

#include "fc_types.h"
#include "fc_event.h"
#include "fc_frame.h"
#include "fc_exch.h"
#include "fc_port.h"
#include "fc_print.h"

#include "fc_exch_impl.h"
#include "fcdev.h"

u16	openfc_cpu_mask =	FC_EXCH_POOLS - 1;
EXPORT_SYMBOL(openfc_cpu_mask);

/*
 * fc_exch_debug can be set in debugger or at compile time to get more logs.
 */
static int fc_exch_debug = 0;

static void fc_seq_fill_hdr(struct fc_seq *, struct fc_frame *);

static void fc_exch_hold(struct fc_exch *);
static void fc_exch_release(struct fc_exch *);
static void fc_exch_complete_locked(struct fc_exch *);
static void fc_exch_timeout(void *);
static void fc_exch_rrq(struct fc_exch *);
static void fc_exch_rrq_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_exch_recv_abts(struct fc_exch *, struct fc_frame *);
static void fc_exch_send_ba_rjt(struct fc_frame *, enum fc_ba_rjt_reason,
				enum fc_ba_rjt_explan);

/*
 * Internal implementation notes.
 *
 * See libfc/fc_exch.h for an overview on usage of this interface.
 * See exch_impl.h for notes on the data structures.
 *
 * The exchange manager is now per-session.
 * The sequence manager is one per exchange manager
 * and currently never separated.
 *
 * Section 9.8 in FC-FS-2 specifies:  "The SEQ_ID is a one-byte field
 * assigned by the Sequence Initiator that shall be unique for a specific
 * D_ID and S_ID pair while the Sequence is open."   Note that it isn't
 * qualified by exchange ID, which one might think it would be.
 * In practice this limits the number of open sequences and exchanges to 256
 * per session.  For most targets we could treat this limit as per exchange.
 *
 * Exchanges aren't currently timed out.  The exchange is freed when the last
 * sequence is received and all sequences are freed.  It's possible for the
 * remote port to leave an exchange open without sending any sequences.
 *
 * Notes on reference counts:
 *
 * Sequences and exchanges are reference counted and they get freed when
 * the reference count becomes zero.  Sequences hold reference counts on
 * the associated exchange.  Sequences can be freed only if their
 * seq_active flag is not set and their reference count has gone to zero.
 *
 * When calling a receive routine on a sequence for an incoming request or
 * response, the sequence is held by the caller and shouldn't be released
 * in the receive handler.
 *
 * Timeouts:
 * Sequences are timed out for E_D_TOV and R_A_TOV.
 *
 * Sequence event handling:
 *
 * The following events may occur on initiator sequences:
 *
 *      Send.
 *          For now, the whole thing is sent.
 *      Receive ACK
 *          This applies only to class F.
 *          The sequence is marked complete.
 *      ULP completion.
 *          The upper layer calls fc_seq_complete() or fc_seq_exch_complete().
 *          If anything's been sent, we still need to wait for the ACK (class F)
 *          before retiring the sequence ID, and the last sequence on the
 *          exchange.
 *      RX-inferred completion.
 *          When we receive the next sequence on the same exchange, we can
 *          retire the previous sequence ID.  (XXX not implemented).
 *      Timeout.
 *          R_A_TOV frees the sequence ID.  If we're waiting for ACK,
 *          E_D_TOV causes abort and retransmission?  XXX defer.
 *      Receive RJT
 *          XXX defer.
 *      Send ABTS
 *          On timeout.  XXX defer.
 *
 * The following events may occur on recipient sequences:
 *
 *      Receive
 *          Allocate sequence for first frame received.
 *          Hold during receive handler.
 *          Release when final frame received.
 *          Keep status of last N of these for the ELS RES command.  XXX TBD.
 *      Receive ABTS
 *          Deallocate sequence
 *      Send RJT
 *          Deallocate
 *
 * For now, we neglect conditions where only part of a sequence was
 * received or transmitted, or where out-of-order receipt is detected.
 */

/*
 * Locking notes:
 *
 * We run in thread context or a soft interrupt.
 *
 * To protect against concurrency between a worker thread code and timers,
 * sequence allocation and deallocation must be locked.
 *  - sequence refcnt can be done atomicly without locks.
 *  - allocation / deallocation can be in an per-exchange lock.
 */

/*
 * Setup memory allocation pools shared by all exchange managers.
 */
void fc_exch_module_init(void)
{
}

/*
 * Free memory allocation pools shared by all exchange managers.
 */
void fc_exch_module_exit(void)
{
}

/*
 * opcode names for debugging.
 */
static char *fc_exch_rctl_names[] = FC_RCTL_NAMES_INIT;

#define FC_TABLE_SIZE(x)   (sizeof (x) / sizeof (x[0]))

static inline const char *fc_exch_name_lookup(u_int op, char **table,
					      u_int max_index)
{
	const char *name = NULL;

	if (op < max_index)
		name = table[op];
	if (!name)
		name = "unknown";
	return name;
}

static const char *fc_exch_rctl_name(u_int op)
{
	return fc_exch_name_lookup(op, fc_exch_rctl_names,
					FC_TABLE_SIZE(fc_exch_rctl_names));
}

/*
 * Initialize an exchange manager.
 * Returns non-zero on allocation errors.
 */
static int fc_exch_mgr_init(struct fc_exch_mgr *mp, enum fc_class class,
		 fc_xid_t min_xid, fc_xid_t max_xid)
{
	fc_xid_t xid;
	struct fc_exch *ep;
	struct fc_exch_pool *pp;
	u_int pool;
	u_int pool_count;

	/*
	 * Check to make sure the declaration of ESB and SSB structures came out
	 * with the right size and no unexpected padding.
	 */
	ASSERT_NOTIMPL(sizeof(struct fc_esb) == FC_ESB_SIZE);
	ASSERT_NOTIMPL(sizeof(struct fc_ssb) == FC_SSB_SIZE);
	ASSERT_NOTIMPL(sizeof(struct fc_frame_header) == FC_FRAME_HEADER_LEN);

	mp->em_class = class;


	/*
	 * Initialize per-CPU free lists.
	 */
	pool_count = 1;
	{
		u_int cpu;

		for_each_present_cpu(cpu) {
			while (cpu >= pool_count) {
				pool_count <<= 1;
			}
		}
		openfc_cpu_mask &= pool_count - 1;
		pool_count = openfc_cpu_mask + 1;
	}

	/*
	 * Make min_xid hash to the first pool, and max_xid to the last, by
	 * increasing min_xid, and decreasing max_xid, respectively.
	 * Otherwise, the hash will be non-optimal and there may be some
	 * unused and uninitialized exchanges that fc_exch_lookup() would find.
	 */
	min_xid = (min_xid + (pool_count - 1)) & ~(pool_count - 1);
	max_xid = (max_xid - (pool_count - 1)) | (pool_count - 1);
	ASSERT(min_xid < max_xid);
	mp->em_min_xid = min_xid;
	mp->em_max_xid = max_xid;

	for (pool = 0; pool < pool_count; pool++) {
		pp = &mp->em_pool[pool];
		pp->emp_mgr = mp;
		pp->emp_exch_in_use = 0;
		spin_lock_init(&pp->emp_lock);
		TAILQ_INIT(&pp->emp_exch_busy);
		TAILQ_INIT(&pp->emp_exch_free);

		/*
		 * Initialize exchanges for the pool.
		 */
		for (xid = min_xid + pool; xid <= max_xid; 
				xid += (fc_xid_t)pool_count) {

			ASSERT((xid % pool_count) == pool);
			ep = &mp->em_exch[xid - min_xid];
			ep->ex_pool = pp;
			ep->ex_xid = xid;
		        ep->ex_e_stat = ESB_ST_COMPLETE;
			spin_lock_init(&ep->ex_lock);
 			sa_timer_init(&ep->ex_timer, fc_exch_timeout, ep);
			TAILQ_INSERT_TAIL(&pp->emp_exch_free, ep, ex_list);
			pp->emp_exch_total++;
		}
	}
	return (0);
}

/*
 * Allocate an exchange manager.
 */
struct fc_exch_mgr *fc_exch_mgr_alloc(enum fc_class class,
				      fc_xid_t min_xid, fc_xid_t max_xid)
{
	struct fc_exch_mgr *mp;
	size_t len;
	u_int order = 0;

	if (!min_xid)
		min_xid++;
	ASSERT(min_xid < max_xid);
	len = (max_xid + 1 - min_xid) * sizeof(struct fc_exch) + sizeof(*mp);
	while (len > (PAGE_SIZE << order))
		order++;
	mp = (struct fc_exch_mgr *) __get_free_pages(GFP_ATOMIC, order);
	if (mp) {
		memset(mp, 0, len);
		mp->em_order = (uint8_t) order;
		if (fc_exch_mgr_init(mp, class, min_xid, max_xid) != 0) {
			fc_exch_mgr_free(mp);
			mp = NULL;
		}
	}
	return mp;
}

/*
 * Free an exchange manager.
 * This is also used to recover from unsuccessful allocations.
 */
void fc_exch_mgr_free(struct fc_exch_mgr *mp)
{

	free_pages((unsigned long) mp, mp->em_order);
}

/*
 * Find an exchange, even if it is no longer in use.
 */
static inline struct fc_exch *fc_exch_lookup_raw(struct fc_exch_mgr *mp,
					     fc_xid_t xid)
{
	struct fc_exch *ep = NULL;

	if (xid >= mp->em_min_xid && xid <= mp->em_max_xid) {
		ep = &mp->em_exch[xid - mp->em_min_xid];
		ASSERT(ep->ex_xid == xid);
	}
	return ep;
}

/*
 * Find an exchange.
 */
static inline struct fc_exch *fc_exch_lookup(struct fc_exch_mgr *mp,
					     fc_xid_t xid)
{
	struct fc_exch *ep;

	ep = fc_exch_lookup_raw(mp, xid);
	if (ep && atomic_read(&ep->ex_refcnt) == 0 &&
	    (ep->ex_e_stat & ESB_ST_COMPLETE))
		ep = NULL;		/* exchange is free */
	return ep;
}

/*
 * Hold an exchange - keep it from being freed.
 */
static void fc_exch_hold(struct fc_exch *ep)
{
	atomic_inc(&ep->ex_refcnt);
	ASSERT(atomic_read(&ep->ex_refcnt) != 0);	/* detect overflow */
}

/*
 * Release a reference to an exchange.
 * If the refcnt goes to zero and the exchange is complete, it is freed.
 */
static void fc_exch_release(struct fc_exch *ep)
{
	struct fc_exch_pool *pp;

	ASSERT(atomic_read(&ep->ex_refcnt) != 0);
	if (atomic_dec_and_test(&ep->ex_refcnt) &&
	    (ep->ex_e_stat & ESB_ST_COMPLETE)) {
		pp = ep->ex_pool;
		spin_lock_bh(&pp->emp_lock);
		ASSERT(pp->emp_exch_in_use > 0);
		pp->emp_exch_in_use--;
		TAILQ_REMOVE(&pp->emp_exch_busy, ep, ex_list);
		TAILQ_INSERT_TAIL(&pp->emp_exch_free, ep, ex_list);
		spin_unlock_bh(&pp->emp_lock);
	}
}

/*
 * Release a reference to an exchange.
 * This version is called when we're locked and we're certain the
 * reference count will still be greater than 0 after the decrement.
 */
static void fc_exch_release_locked(struct fc_exch *ep)
{
	ASSERT(atomic_read(&ep->ex_refcnt) > 1);
	atomic_dec(&ep->ex_refcnt);
}

/*
 * Get the exchange for a sequence.
 * This would use container_of() but it isn't defined outside of the kernel.
 */
inline struct fc_exch *fc_seq_exch(const struct fc_seq *sp)
{
	ASSERT(sp);
	return (struct fc_exch *)
		((char *) sp - offsetof(struct fc_exch, ex_seq));
}

/*
 * Hold a sequence - keep it from being freed.
 */
inline void fc_seq_hold(struct fc_seq *sp)
{
	atomic_inc(&sp->seq_refcnt);
	ASSERT(atomic_read(&sp->seq_refcnt) != 0);
}

/*
 * Allocate a sequence.
 *
 * We don't support multiple originated sequences on the same exchange.
 * By implication, any previously originated sequence on this exchange
 * is complete, and we reallocate the same sequence.
 */
static struct fc_seq *fc_seq_alloc(struct fc_exch *ep, u_int8_t seq_id)
{
	struct fc_seq *sp;

	ASSERT(ep);
	sp = &ep->ex_seq;
	if (atomic_read(&sp->seq_refcnt) == 0 && sp->seq_active == 0)
		fc_exch_hold(ep);	/* hold exchange for the sequence */
	sp->seq_active = 1;
	sp->seq_s_stat = 0;
	sp->seq_f_ctl = 0;
	sp->seq_cnt = 0;
	sp->seq_id = seq_id;
	fc_seq_hold(sp);
	return (sp);
}

/*
 * Release a sequence.
 */
void fc_seq_release(struct fc_seq *sp)
{
	ASSERT(atomic_read(&sp->seq_refcnt) != 0);
	if (atomic_dec_and_test(&sp->seq_refcnt) && !sp->seq_active)
		fc_exch_release(fc_seq_exch(sp));
}

/*
 * Mark sequence complete.
 * The sequence may or may not have been active prior to this call.
 * The caller must hold the sequence and that hold is not released.
 */
inline void fc_seq_complete(struct fc_seq *sp)
{
	ASSERT(atomic_read(&sp->seq_refcnt) != 0);
	sp->seq_active = 0;
}

/*
 * Exchange timeout - handle exchange timer expiration.
 * The timer will have been canceled before this is called.
 * The exchange is held whenever the timer is scheduled.
 */
static void fc_exch_timeout(void *ep_arg)
{
	struct fc_exch *ep = ep_arg;
	struct fc_seq *sp = &ep->ex_seq;
	void	(*errh)(enum fc_event, void *);
	void	*arg;
	uint	e_stat;

	spin_lock_bh(&ep->ex_lock);
	e_stat = ep->ex_e_stat;
	if (e_stat & ESB_ST_COMPLETE) {
		ep->ex_e_stat = e_stat & ~ESB_ST_REC_QUAL;
		spin_unlock_bh(&ep->ex_lock);
		if (e_stat & ESB_ST_REC_QUAL)
			fc_exch_rrq(ep);
	} else if (e_stat & ESB_ST_ABNORMAL) {
		fc_seq_hold(sp);
		ep->ex_e_stat |= ESB_ST_COMPLETE;
		fc_seq_complete(sp);
		spin_unlock_bh(&ep->ex_lock);
		fc_seq_release(sp);
	} else {
		fc_seq_hold(sp);
		errh = ep->ex_errh;
		ep->ex_errh = NULL;
		arg = ep->ex_recv_arg;
		spin_unlock_bh(&ep->ex_lock);
		fc_seq_abort_exch(sp);		/* abort exchange */
		fc_seq_release(sp);
		if (errh)
			(*errh)(FC_EV_TIMEOUT, arg);
	}

	/*
	 * This release matches the hold taken when the timer was set.
	 */
	fc_exch_release(ep);
}

/*
 * Internal version of fc_exch_timer_set - used with lock held.
 */
static void fc_exch_timer_set_locked(struct fc_exch *ep, u_int timer_msec)
{
	if (!sa_timer_active(&ep->ex_timer))
		fc_exch_hold(ep);		/* hold for timer */
	sa_timer_set(&ep->ex_timer, timer_msec * 1000);
}

/*
 * Set timer for an exchange.
 * The time is a minimum delay in milliseconds until the timer fires.
 * Used by upper level protocols to time out the exchange.
 * The timer is canceled when it fires or when the exchange completes.
 */
void fc_exch_timer_set(struct fc_exch *ep, u_int timer_msec)
{
	spin_lock_bh(&ep->ex_lock);
	fc_exch_timer_set_locked(ep, timer_msec);
	spin_unlock_bh(&ep->ex_lock);
}

/*
 * Set timer for an exchange with sequence recovery.
 */
void fc_exch_timer_set_recover(struct fc_exch *ep, u_int timer_msec)
{
	spin_lock_bh(&ep->ex_lock);
	ep->ex_e_stat = (ep->ex_e_stat & ~ESB_ST_ERRP_MASK) | ESB_ST_ERRP_INF;
	fc_exch_timer_set_locked(ep, timer_msec);
	spin_unlock_bh(&ep->ex_lock);
}

/*
 * Abort the exchange for a sequence due to timeout or an upper-level abort.
 * Called without the exchange manager em_lock held.
 * Returns non-zero if a sequence could not be allocated.
 */
int fc_seq_abort_exch(struct fc_seq *req_sp)
{
	struct fc_seq *sp;
	struct fc_exch *ep;
	struct fc_frame *fp;

	ep = fc_seq_exch(req_sp);
	ASSERT(ep);

	/*
	 * If we originated the last sequence, send ABTS as a continuation
	 * of that sequence.  Otherwise, send the abort on a new sequence.
	 */
	spin_lock_bh(&ep->ex_lock);
	ep->ex_recv = NULL;
	ep->ex_errh = NULL;
	sp = &ep->ex_seq;
	if (sp->seq_s_stat & SSB_ST_RESP) {
		sp = fc_seq_alloc(ep, ++ep->ex_seq_id);
	} else {
		if (sp->seq_active == 0) {
			if (atomic_read(&sp->seq_refcnt) == 0)
				fc_exch_hold(ep); /* hold exch for the seq */
			sp->seq_active = 1;
		}
		sp->seq_s_stat = 0;
		sp->seq_f_ctl = 0;
		sp->seq_cnt++;
		fc_seq_hold(sp);
	}
	ep->ex_e_stat |= ESB_ST_SEQ_INIT | ESB_ST_ABNORMAL;
	fc_exch_timer_set_locked(ep, ep->ex_r_a_tov);
	atomic_inc(&ep->ex_pool->emp_mgr->em_stats.ems_ex_aborts);
	spin_unlock_bh(&ep->ex_lock);

	/*
	 * If not logged into the fabric, don't send ABTS but leave
	 * sequence active until next timeout.
	 */
	if (!ep->ex_s_id) {
		fc_seq_release(sp);
		return 0;
	}

	fp = fc_frame_alloc(ep->ex_port, 0);
	if (!fp) {
		fc_seq_release(sp);
		return ENOBUFS;
	}
	
	fc_frame_setup(fp, FC_RCTL_BA_ABTS, FC_TYPE_BLS);
	fc_frame_set_offset(fp, 0);
	return fc_seq_send(sp, fp);
}

/*
 * Handle the response to an ABTS for exchange or sequence.
 * This can be BA_ACC or BA_RJT.
 */
static void fc_exch_abts_resp(struct fc_exch *ep, struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_ba_acc *ap;
	uint16_t low;
	uint16_t high;

	fh = fc_frame_header_get(fp);
	if (fc_exch_debug)
		SA_LOG("exch: BLS rctl %x - %s\n",
		    fh->fh_r_ctl, fc_exch_rctl_name(fh->fh_r_ctl));
	fc_exch_hold(ep);
	spin_lock_bh(&ep->ex_lock);
	switch (fh->fh_r_ctl) {
	case FC_RCTL_BA_ACC:
		ap = fc_frame_payload_get(fp, sizeof(*ap));
		if (!ap)
			break;

		/*
		 * Decide whether to establish a Recovery Qualifier.
		 * We do this if there is a non-empty SEQ_CNT range and
		 * SEQ_ID is the same as the one we aborted.
		 */
		low = net16_get(&ap->ba_low_seq_cnt);
		high = net16_get(&ap->ba_high_seq_cnt);
		if ((ep->ex_e_stat & ESB_ST_REC_QUAL) == 0 &&
		    (ap->ba_seq_id_val != FC_BA_SEQ_ID_VAL ||
		     ap->ba_seq_id == ep->ex_seq_id) && low != high) {
			ep->ex_e_stat |= ESB_ST_REC_QUAL;
			fc_exch_hold(ep);  /* hold for recovery qualifier */
			fc_exch_timer_set_locked(ep, 2 * ep->ex_r_a_tov);
		}
		break;
	case FC_RCTL_BA_RJT:
		break;
	default:
		break;
	}
	if (net24_get(&fh->fh_f_ctl) & FC_FC_LAST_SEQ)
		fc_exch_complete_locked(ep);
	spin_unlock_bh(&ep->ex_lock);
	fc_exch_release(ep);
	fc_frame_free(fp);
}

/*
 * Receive BLS sequence.
 * This is always a sequence initiated by the remote side.
 * We may be either the originator or recipient of the exchange.
 */
void fc_exch_recv_bls(struct fc_exch_mgr *mp, struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_seq *sp;
	struct fc_exch *ep;
	uint32_t f_ctl;

	fh = fc_frame_header_get(fp);
	f_ctl = net24_get(&fh->fh_f_ctl);
	fp->fr_seq = NULL;

	ep = fc_exch_lookup(mp, net16_get((f_ctl & FC_FC_EX_CTX) ?
					  &fh->fh_ox_id : &fh->fh_rx_id));
	if (ep && (f_ctl & FC_FC_SEQ_INIT)) {
		spin_lock_bh(&ep->ex_lock);
		ep->ex_e_stat |= ESB_ST_SEQ_INIT;
		spin_unlock_bh(&ep->ex_lock);
	}
	if (f_ctl & FC_FC_SEQ_CTX) {
		/*
		 * A response to a sequence we initiated.
		 * This should only be ACKs for class 2 or F.
		 */
		switch (fh->fh_r_ctl) {
		case FC_RCTL_ACK_1:
		case FC_RCTL_ACK_0:
			if (ep) {
				spin_lock_bh(&ep->ex_lock);
				sp = &ep->ex_seq;	/* XXX same sequence? */
				fc_seq_complete(sp);
				spin_unlock_bh(&ep->ex_lock);
			}
			break;
		default:
			if (fc_exch_debug)
				SA_LOG("BLS rctl %x - %s received",
				       fh->fh_r_ctl,
				       fc_exch_rctl_name(fh->fh_r_ctl));
			break;
		}
		fc_frame_free(fp);
	} else {
		switch (fh->fh_r_ctl) {
		case FC_RCTL_BA_ABTS:
			fc_exch_recv_abts(ep, fp);
			break;
		case FC_RCTL_BA_RJT:
		case FC_RCTL_BA_ACC:
			if (ep)
				fc_exch_abts_resp(ep, fp);
			else
				fc_frame_free(fp);
			break;
		default:			/* ignore junk */
			fc_frame_free(fp);
			break;
		}
	}
}

/*
 * Mark a sequence and its exchange both complete.
 * Caller holds the sequence but not the exchange.
 * This call releases the sequence for the caller.
 * This is usually used when a sequence has been allocated but couldn't be
 * sent for some reason, e.g., when a fc_frame_alloc() fails.
 */
void fc_seq_exch_complete(struct fc_seq *sp)
{
	fc_exch_complete(fc_seq_exch(sp));
	fc_seq_complete(sp);
	fc_seq_release(sp);
}

/*
 * Mark exchange complete - internal version called with ex_lock held.
 */
static void fc_exch_complete_locked(struct fc_exch *ep)
{
	fc_seq_hold(&ep->ex_seq);
	fc_seq_complete(&ep->ex_seq);
	fc_seq_release(&ep->ex_seq);
	ep->ex_e_stat |= ESB_ST_COMPLETE;
	ep->ex_recv = NULL;
	ep->ex_errh = NULL;

	/*
	 * Assuming in-order delivery, the timeout for RRQ is 0, not R_A_TOV.
	 * Here, we allow a short time for frames which may have been
	 * re-ordered in various kernel queues or due to interrupt balancing.
	 * Also, using a timer here allows us to issue the RRQ after the
	 * exchange lock is dropped.
	 */
	if (unlikely(ep->ex_e_stat & ESB_ST_REC_QUAL)) {
		if (!sa_timer_active(&ep->ex_timer))
			fc_exch_hold(ep);	/* hold for timer */
		sa_timer_set(&ep->ex_timer, 10);
	} else if (sa_timer_active(&ep->ex_timer)) {
		sa_timer_cancel(&ep->ex_timer);
		fc_exch_release_locked(ep);	/* drop hold for timer */
	}
}

/*
 * Mark exchange complete.
 * The state may be available for ILS Read Exchange Status (RES) for a time.
 * The caller doesn't necessarily hold the exchange.
 */
void fc_exch_complete(struct fc_exch *ep)
{
	spin_lock_bh(&ep->ex_lock);
	fc_exch_complete_locked(ep);
	spin_unlock_bh(&ep->ex_lock);
}

/*
 * Allocate a new exchange.
 */
static struct fc_exch *fc_exch_alloc(struct fc_exch_mgr *mp)
{
	struct fc_exch_pool *pp;
	struct fc_exch *ep = NULL;

#if defined(CONFIG_SMP) && defined(__KERNEL__) && !defined(__WINDOWS__)
	pp = &mp->em_pool[smp_processor_id() & openfc_cpu_mask];
#else /* __KERNEL__ */
	pp = mp->em_pool;
#endif /* __KERNEL__ */

	spin_lock_bh(&pp->emp_lock);
	ep = TAILQ_FIRST(&pp->emp_exch_free);
	if (!ep) {
		atomic_inc(&mp->em_stats.ems_error_no_free_exch);
		spin_unlock_bh(&pp->emp_lock);
	} else {
		TAILQ_REMOVE(&pp->emp_exch_free, ep, ex_list);
		TAILQ_INSERT_TAIL(&pp->emp_exch_busy, ep, ex_list);
		pp->emp_exch_in_use++;
		spin_unlock_bh(&pp->emp_lock);

		ASSERT(ep->ex_pool == pp);
		ASSERT(atomic_read(&ep->ex_refcnt) == 0);
		ASSERT(ep->ex_xid != 0);
		ASSERT(spin_can_lock(&ep->ex_lock));

		/*
		 * Clear the portion of the exchange not maintained
		 * for the duration of the exchange manager.
		 */
		memset((char *) ep +
			offsetof(struct fc_exch, fc_exch_clear_start), 0,
			sizeof(*ep) - offsetof(struct fc_exch,
						fc_exch_clear_start));
		ASSERT(ep->ex_pool == pp);
		ASSERT(atomic_read(&ep->ex_refcnt) == 0);
		ASSERT(ep->ex_xid != 0);
		ASSERT(spin_can_lock(&ep->ex_lock));

		ep->ex_f_ctl = FC_FC_FIRST_SEQ;	/* next seq is first seq */
		ep->ex_rx_id = FC_XID_UNKNOWN;
		ep->ex_class = mp->em_class;
		ep->ex_seq_id = (uint8_t) -1;	/* gets pre-incremented later */
		ep->ex_r_a_tov = FC_DEF_R_A_TOV;

		/*
		 * Set up as if originator.  Caller may change this.
		 */
		ep->ex_ox_id = ep->ex_xid;
		fc_exch_hold(ep);	/* hold for caller */
	}
	return ep;
}

/*
 * Allocate a new exchange as originator.
 */
static struct fc_exch *fc_exch_orig(struct fc_exch_mgr *mp)
{
	struct fc_exch *ep;

	ep = fc_exch_alloc(mp);
	if (ep)
		ep->ex_e_stat |= ESB_ST_SEQ_INIT;
	return ep;
}

/*
 * Allocate a new exchange as responder.
 * Sets the responder ID in the frame header.
 */
static struct fc_exch *fc_exch_resp(struct fc_exch_mgr *mp,
				    const struct fc_frame *fp)
{
	struct fc_exch *ep;
	struct fc_frame_header *fh;
	u_int16_t rx_id;

	ep = fc_exch_alloc(mp);
	if (ep) {
		ep->ex_port = fp->fr_in_port;
		ep->ex_class = fc_frame_class(fp);

		/*
		 * Set EX_CTX indicating we're responding on this exchange.
		 */
		ep->ex_f_ctl |= FC_FC_EX_CTX;	/* we're responding */
		ep->ex_f_ctl &= ~FC_FC_FIRST_SEQ;	/* not new */
		fh = fc_frame_header_get(fp);
		ep->ex_s_id = net24_get(&fh->fh_d_id);
		ep->ex_d_id = net24_get(&fh->fh_s_id);
		ep->ex_orig_fid = ep->ex_d_id;

		/*
		 * fc_exch_alloc() has placed the XID in the originator field.
		 * Move it to the responder field, and set the originator
		 * XID from the frame.
		 */
		ep->ex_rx_id = ep->ex_xid;
		ep->ex_ox_id = net16_get(&fh->fh_ox_id);
		ep->ex_e_stat |= ESB_ST_RESP | ESB_ST_SEQ_INIT;
		if ((net24_get(&fh->fh_f_ctl) & FC_FC_SEQ_INIT) == 0)
			ep->ex_e_stat &= ~ESB_ST_SEQ_INIT;

		/*
		 * Set the responder ID in the frame header.
		 * The old one should've been 0xffff.
		 * If it isn't, don't assign one.
		 * Incoming basic link service frames may specify
		 * a referenced RX_ID.
		 */
		if (fh->fh_type != FC_TYPE_BLS) {
			rx_id = net16_get(&fh->fh_rx_id);
			ASSERT(rx_id == FC_XID_UNKNOWN);
			net16_put(&fh->fh_rx_id, ep->ex_rx_id);
		}
	}
	return ep;
}

/*
 * Find a sequence for receive where the other end is originating the sequence.
 */
static enum fc_pf_rjt_reason fc_seq_lookup_recip(struct fc_exch_mgr *mp,
						 struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_exch *ep = NULL;
	struct fc_seq *sp = NULL;
	enum fc_pf_rjt_reason reject = FC_RJT_NONE;
	u_int32_t f_ctl;
	fc_xid_t xid;

	f_ctl = net24_get(&fh->fh_f_ctl);
	ASSERT((f_ctl & FC_FC_SEQ_CTX) == 0);

	/*
	 * Lookup or create the exchange if we will be creating the sequence.
	 */
	if (f_ctl & FC_FC_EX_CTX) {
		xid = net16_get(&fh->fh_ox_id);	/* we originated exch */
		ep = fc_exch_lookup(mp, xid);
		if (!ep) {
			atomic_inc(&mp->em_stats.ems_error_xid_not_found);
			reject = FC_RJT_OX_ID;
			goto out;
		}
		fc_exch_hold(ep);
		if (ep->ex_rx_id == FC_XID_UNKNOWN) {
			ep->ex_rx_id = net16_get(&fh->fh_rx_id);
		} else if (ep->ex_rx_id != net16_get(&fh->fh_rx_id)) {
			reject = FC_RJT_OX_ID;
			goto out;
		}
	} else {
		xid = net16_get(&fh->fh_rx_id);	/* we are the responder */

		/*
		 * Special case for MDS issuing an ELS TEST with a
		 * bad rx_id of 0.
		 * XXX take this out once we do the proper reject.
		 */
		if (xid == 0 && fh->fh_r_ctl == FC_RCTL_ELS_REQ &&
		    fc_frame_payload_op(fp) == ELS_TEST) {
			net16_put(&fh->fh_rx_id, FC_XID_UNKNOWN);
			xid = FC_XID_UNKNOWN;
		}

		/*
		 * new sequence - find the exchange
		 */
		ep = fc_exch_lookup(mp, xid);
		if ((f_ctl & FC_FC_FIRST_SEQ) && fc_sof_is_init(fp->fr_sof)) {
			if (ep) {
				atomic_inc(&mp->em_stats.
						ems_error_xid_busy);
				reject = FC_RJT_RX_ID;
				goto out;
			}
			ep = fc_exch_resp(mp, fp);
			if (!ep) {
				reject = FC_RJT_EXCH_EST; /* XXX */
				goto out;
			}
			xid = ep->ex_xid;	/* get our XID */
		} else if (ep) {
			fc_exch_hold(ep);	/* hold matches alloc */
		} else {
			atomic_inc(&mp->em_stats.
					ems_error_xid_not_found);
			reject = FC_RJT_RX_ID;	/* XID not found */
			goto out;
		}
	}

	/*
	 * At this point, we should have the exchange.
	 * Find or create the sequence.
	 */
	ASSERT(ep);
	if (fc_sof_is_init(fp->fr_sof)) {
		sp = fc_seq_alloc(ep, fh->fh_seq_id);
		fc_exch_release(ep);	/* sequence now holds exch */
		if (!sp) {
			reject = FC_RJT_SEQ_XS;	/* exchange shortage */
			goto out;
		}
		sp->seq_s_stat |= SSB_ST_RESP;
	} else {
		sp = &ep->ex_seq;
		sp->seq_active = 1;
		if (sp->seq_id == fh->fh_seq_id) {
			fc_seq_hold(sp);	/* hold to match alloc */
			fc_exch_release(ep);	/* sequence now holds exch */
		} else {
			atomic_inc(&mp->em_stats.ems_error_seq_not_found);
			reject = FC_RJT_SEQ_ID;	/* sequence should exist */
			fc_exch_release(ep);
			goto out;
		}
	}
	ASSERT(sp);
	ASSERT(ep);
	ASSERT(ep == fc_seq_exch(sp));
	ASSERT(atomic_read(&sp->seq_refcnt));

	if (f_ctl & FC_FC_SEQ_INIT)
		ep->ex_e_stat |= ESB_ST_SEQ_INIT;
	fp->fr_seq = sp;
out:
	return reject;
}

/*
 * Find the sequence for a frame being received.
 * We originated the sequence, so it should be found.
 * We may or may not have originated the exchange.
 * Does not hold the sequence for the caller.
 */
static struct fc_seq *fc_seq_lookup_orig(struct fc_exch_mgr *mp,
					 struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_exch *ep;
	struct fc_seq *sp = NULL;
	u_int32_t f_ctl;
	fc_xid_t xid;

	f_ctl = net24_get(&fh->fh_f_ctl);
	ASSERT(f_ctl & FC_FC_SEQ_CTX);
	xid = net16_get((f_ctl & FC_FC_EX_CTX) ?
			 &fh->fh_ox_id : &fh->fh_rx_id);
	ep = fc_exch_lookup(mp, xid);
	if (ep && ep->ex_seq.seq_id == fh->fh_seq_id) {

		/*
		 * Save the RX_ID if we didn't previously know it.
		 */
		sp = &ep->ex_seq;
		if ((f_ctl & FC_FC_EX_CTX) != 0 &&
				ep->ex_rx_id == FC_XID_UNKNOWN) {
			ep->ex_rx_id = net16_get(&fh->fh_rx_id);
		}
	}
	return (sp);
}

/*
 * Set the output port to be used for an exchange.
 */
void fc_exch_set_port(struct fc_exch *ep, struct fc_port *port)
{
	ep->ex_port = port;
}

/*
 * Set addresses for an exchange.
 */
void fc_exch_set_addr(struct fc_exch *ep, fc_fid_t orig_id, fc_fid_t resp_id)
{
	ep->ex_orig_fid = orig_id;
	if (ep->ex_e_stat & ESB_ST_RESP) {
		ep->ex_s_id = resp_id;
		ep->ex_d_id = orig_id;
	} else {
		ep->ex_s_id = orig_id;
		ep->ex_d_id = resp_id;
	}
}

/*
 * Set addresses for the exchange of a sequence.
 */
void fc_seq_set_addr(struct fc_seq *sp, fc_fid_t orig_fid, fc_fid_t resp_fid)
{
	fc_exch_set_addr(fc_seq_exch(sp), orig_fid, resp_fid);
}


/*
 * Start a new sequence as originator on an existing exchange.
 * This will never return NULL.
 */
struct fc_seq *fc_seq_start(struct fc_exch *ep)
{
	struct fc_seq *sp = NULL;

	spin_lock_bh(&ep->ex_lock);
	ASSERT((ep->ex_e_stat & ESB_ST_COMPLETE) == 0);

	ep->ex_seq_id++;
	sp = fc_seq_alloc(ep, ep->ex_seq_id);
	ASSERT(sp);
	if (fc_exch_debug)
		SA_LOG("exch %4x f_ctl %6x seq %2x f_ctl %6x\n",
		       ep->ex_xid, ep->ex_f_ctl, sp->seq_id, sp->seq_f_ctl);
	spin_unlock_bh(&ep->ex_lock);
	return sp;
}

/*
 * Allocate a new sequence on the same exchange as the supplied sequence.
 * This will never return NULL.
 */
struct fc_seq *fc_seq_start_next(struct fc_seq *sp)
{
	return fc_seq_start(fc_seq_exch(sp));
}
/*
 * Allocate a new sequence on the same exchange as the supplied sequence.
 * also set the f_ctl of the new sequence 
 * This will never return NULL.
 */
struct fc_seq *fc_seq_start_next_fctl(struct fc_seq *sp, u_int32_t f_ctl)
{
	struct fc_seq * new_sp;

	new_sp =  fc_seq_start(fc_seq_exch(sp));
	if (new_sp) {
		new_sp->seq_f_ctl = f_ctl;
	}
	return new_sp;
}

/*
 * Set the receive handler and arg for the exchange of an existing sequence.
 */
void fc_seq_set_recv(struct fc_seq *sp,
			 void (*recv)(struct fc_seq *,
					struct fc_frame *, void *),
			 void *arg)
{
	struct fc_exch *ep = fc_seq_exch(sp);

	ep->ex_recv = recv;
	ep->ex_recv_arg = arg;
}

/*
 * Start a new sequence as originator on a new exchange.
 * Returns with a reference count held on the sequence but not on the exchange
 * for the caller.  The exchange will be held for the sequence.
 */
struct fc_seq *fc_seq_start_exch(struct fc_exch_mgr *mp,
				 void (*recv)(struct fc_seq *,
						struct fc_frame *, void *),
				 void (*errh)(enum fc_event, void *),
				 void *arg, fc_fid_t sid, fc_fid_t did)
{
	struct fc_exch *ep;
	struct fc_seq *sp;

	ep = fc_exch_orig(mp);
	sp = NULL;
	if (ep) {
		fc_exch_set_addr(ep, sid, did);
		ep->ex_recv = recv;
		ep->ex_errh = errh;
		ep->ex_recv_arg = arg;
		sp = fc_seq_start(ep);
		fc_exch_release(ep);
	}
	return sp;
}

size_t fc_seq_mfs(struct fc_seq * sp)
{
	size_t mfs;

	mfs = fc_seq_exch(sp)->ex_max_payload;
	ASSERT(mfs >= FC_SP_MIN_MAX_PAYLOAD);
	ASSERT(mfs <= FC_SP_MAX_MAX_PAYLOAD);
	return mfs;
}

/*
 * Send a frame in a sequence where more will follow.
 *
 * This sets some of the F_CTL flags in the packet, depending on the
 * state of the sequence and exchange.
 *
 * FC_FC_EX_CTX is set if we're responding to the exchange.
 * FC_FC_SEQ_CTX is set if we're responding to the sequence.
 * FC_FC_FIRST_SEQ is set on every frame in the first sequence of the exchange.
 * FC_FC_LAST_SEQ is set on every frame in the last sequence of the exchange.
 * FC_FC_END_SEQ is set on the last frame of a sequence (not here).
 *
 * Some f_ctl bits must be specified in the fc_seq by the caller:
 * FC_FC_SEQ_INIT is set by the caller if sequence initiative should
 * be transferred.  FC_FC_LAST_SEQ is set on the last sequence of the exchange.
 *
 * This will update the following flags for the sequence and exchange:
 * In the exchange, FC_FC_FIRST_SEQ is set on creation of originating
 * exchanges, it is used to initialize the flags in the first sequence
 * and then cleared in the exchange.
 */
int fc_seq_send_frag(struct fc_seq *sp, struct fc_frame *fp)
{
	struct fc_exch *ep;
	struct fc_port *port;
	struct fc_frame_header *fh;
	enum fc_class class;
	u_int32_t f_ctl;

	ep = fc_seq_exch(sp);
	ASSERT(ep);
	ASSERT(ep->ex_e_stat & ESB_ST_SEQ_INIT);
	port = ep->ex_port;
	ASSERT(port);
	ASSERT((sp->seq_f_ctl & FC_FC_END_SEQ) == 0);
	ASSERT(fp->fr_len % 4 == 0);	/* can't pad except on last frame */

	class = ep->ex_class;
	fp->fr_sof = class;
	if (sp->seq_cnt)
		fp->fr_sof = fc_sof_normal(class);

	/*
	 * Save sequence initiative flag for the final frame, and take it out
	 * of the flags until then.
	 */
	f_ctl = sp->seq_f_ctl | ep->ex_f_ctl;
	f_ctl &= ~FC_FC_SEQ_INIT;
	fp->fr_eof = FC_EOF_N;

	fc_seq_fill_hdr(sp, fp);

	fh = fc_frame_header_get(fp);
	net24_put(&fh->fh_f_ctl, f_ctl);

	/*
	 * Update the exchange and sequence flags.
	 */
	spin_lock_bh(&ep->ex_lock);
	net16_put(&fh->fh_seq_cnt, sp->seq_cnt);
	sp->seq_cnt++;
	sp->seq_f_ctl = f_ctl;			/* save for possible abort */
	ep->ex_f_ctl &= ~FC_FC_FIRST_SEQ;	/* not first seq */
	sp->seq_active = 1;
	spin_unlock_bh(&ep->ex_lock);

	/*
	 * Send the frame.
	 */
	return fc_port_egress(port, fp);
}

/*
 * Send the last frame of a sequence.
 * See notes on fc_seq_send_frag(), above.
 */
int fc_seq_send(struct fc_seq *sp, struct fc_frame *fp)
{
	struct fc_exch *ep;
	struct fc_port *port;
	struct fc_frame_header *fh;
	enum fc_class class;
	u_int32_t f_ctl;
	u_int16_t fill;

	ep = fc_seq_exch(sp);
	ASSERT(ep);
	ASSERT(ep->ex_e_stat & ESB_ST_SEQ_INIT);
	port = ep->ex_port;
	ASSERT(port);
	ASSERT((sp->seq_f_ctl & FC_FC_END_SEQ) == 0);


	class = ep->ex_class;
	fp->fr_sof = class;
	if (sp->seq_cnt)
		fp->fr_sof = fc_sof_normal(class);
	fp->fr_eof = FC_EOF_T;
	if (fc_sof_needs_ack(class))
		fp->fr_eof = FC_EOF_N;

	fc_seq_fill_hdr(sp, fp);
	fh = fc_frame_header_get(fp);

	/*
	 * Form f_ctl.
	 * The number of fill bytes to make the length a 4-byte multiple is 
	 * the low order 2-bits of the f_ctl.  The fill itself will have been
	 * cleared by the frame allocation.
	 * After this, the length will be even, as expected by the transport.
	 * Don't include the fill in the f_ctl saved in the sequence.
	 */
	fill = fp->fr_len & 3;
	if (fill) {
		fill = 4 - fill;
		fp->fr_len += fill;
	}
	f_ctl = sp->seq_f_ctl | ep->ex_f_ctl | FC_FC_END_SEQ;
	net24_put(&fh->fh_f_ctl, f_ctl | fill);
	net16_put(&fh->fh_seq_cnt, sp->seq_cnt++);

	/*
	 * Update the exchange and sequence flags,
	 * assuming all frames for the sequence have been sent.
	 * We can only be called to send once for each sequence.
	 */
	spin_lock_bh(&ep->ex_lock);
	sp->seq_f_ctl = f_ctl;			/* save for possible abort */
	ep->ex_f_ctl &= ~FC_FC_FIRST_SEQ;	/* not first seq */
	sp->seq_active = 1;
	if (f_ctl & FC_FC_LAST_SEQ)
		fc_exch_complete_locked(ep);
	if (f_ctl & FC_FC_SEQ_INIT)
		ep->ex_e_stat &= ~ESB_ST_SEQ_INIT;
	spin_unlock_bh(&ep->ex_lock);
	fc_seq_release(sp);

	/*
	 * Send the frame.
	 */
	return fc_port_egress(port, fp);
}

/*
 * Send a sequence, which is also the last sequence in the exchange.
 * See notes on fc_seq_send();
 */
int fc_seq_send_last(struct fc_seq *sp, struct fc_frame *fp,
		 enum fc_rctl rctl, enum fc_fh_type fh_type)
{
	sp->seq_f_ctl |= FC_FC_LAST_SEQ;
	fc_frame_setup(fp, rctl, fh_type);
	return fc_seq_send(sp, fp);
}

/*
 * Send a request sequence, and transfer sequence initiative.
 * See notes on fc_seq_send();
 */
int fc_seq_send_tsi(struct fc_seq *sp, struct fc_frame *fp)
{
	sp->seq_f_ctl |= FC_FC_SEQ_INIT;
	return fc_seq_send(sp, fp);
}

/*
 * Send a request sequence, and transfer sequence initiative.
 * See notes on fc_seq_send();
 */
int fc_seq_send_req(struct fc_seq *sp, struct fc_frame *fp,
		enum fc_rctl rctl, enum fc_fh_type fh_type,
		u_int32_t parm_offset)
{
	sp->seq_f_ctl |= FC_FC_SEQ_INIT;
	fc_frame_setup(fp, rctl, fh_type);
	fc_frame_set_offset(fp, parm_offset);
	return fc_seq_send(sp, fp);
}

/*
 * Send a request sequence, using a new sequence on the same exchange
 * as the supplied one, and transfer sequence initiative.
 * See notes on fc_seq_send();
 */
int fc_seq_send_next_req(struct fc_seq *sp, struct fc_frame *fp)
{
	sp = fc_seq_start_next(sp);
	ASSERT(sp);
	sp->seq_f_ctl |= FC_FC_SEQ_INIT;
	return fc_seq_send(sp, fp);
}

/*
 * Send ACK_1 (or equiv.) indicating we received something.
 * The frame we're acking is supplied.
 */
static void
fc_seq_send_ack(struct fc_seq *sp, const struct fc_frame *rx_fp)
{
	struct fc_frame *fp;
	struct fc_frame_header *rx_fh;
	struct fc_frame_header *fh;
	u_int f_ctl;

	/*
	 * Don't send ACKs for class 3.
	 */
	if (fc_sof_needs_ack(rx_fp->fr_sof)) {
		fp = fc_frame_alloc(fc_seq_exch(sp)->ex_port, 0);
		ASSERT_NOTIMPL(fp);
		if (!fp)
			return;
		fc_seq_fill_hdr(sp, fp);
		fh = fc_frame_header_get(fp);
		fh->fh_r_ctl = FC_RCTL_ACK_1;
		fh->fh_type = FC_TYPE_BLS;

		/*
		 * Form f_ctl by inverting EX_CTX and SEQ_CTX (bits 23, 22).
		 * Echo FIRST_SEQ, LAST_SEQ, END_SEQ, END_CONN, SEQ_INIT.
		 * Bits 9-8 are meaningful (retransmitted or unidirectional).
		 * Last ACK uses bits 7-6 (continue sequence),
		 * bits 5-4 are meaningful (what kind of ACK to use).
		 */
		rx_fh = fc_frame_header_get(rx_fp);
		f_ctl = net24_get(&rx_fh->fh_f_ctl);
		f_ctl &= FC_FC_EX_CTX | FC_FC_SEQ_CTX |
		    FC_FC_FIRST_SEQ | FC_FC_LAST_SEQ |
		    FC_FC_END_SEQ | FC_FC_END_CONN | FC_FC_SEQ_INIT |
		    FC_FC_RETX_SEQ | FC_FC_UNI_TX;
		f_ctl ^= FC_FC_EX_CTX | FC_FC_SEQ_CTX;
		net24_put(&fh->fh_f_ctl, f_ctl);

		fh->fh_seq_id = rx_fh->fh_seq_id;
		fh->fh_seq_cnt = rx_fh->fh_seq_cnt;
		net32_put(&fh->fh_parm_offset, 1);	/* ack single frame */

		fp->fr_sof = rx_fp->fr_sof;
		if (f_ctl & FC_FC_END_SEQ) {
			fp->fr_eof = FC_EOF_T;
		} else {
			fp->fr_eof = FC_EOF_N;
		}
		(void) fc_port_egress(rx_fp->fr_in_port, fp);
	}
}

/*
 * Handle an incoming ABTS.  This would be for target mode usually,
 * but could be due to lost FCP transfer ready, confirm or RRQ.
 * We always handle this as an exchange abort, ignoring the parameter.
 */
static void fc_exch_recv_abts(struct fc_exch *ep, struct fc_frame *rx_fp)
{
	struct fc_frame *fp;
	struct fc_ba_acc *ap;
	struct fc_frame_header *fh;
	struct fc_seq *sp;

	if (!ep)
		goto reject;
	spin_lock_bh(&ep->ex_lock);
	if (ep->ex_e_stat & ESB_ST_COMPLETE) {
		spin_unlock_bh(&ep->ex_lock);
		goto reject;
	}
	if (!(ep->ex_e_stat & ESB_ST_REC_QUAL))
		fc_exch_hold(ep);		/* hold for REC_QUAL */
	ep->ex_e_stat |= ESB_ST_ABNORMAL | ESB_ST_REC_QUAL;
	fc_exch_timer_set_locked(ep, ep->ex_r_a_tov);

	fp = fc_frame_alloc(rx_fp->fr_in_port, sizeof(*ap));
	if (!fp) {
		spin_unlock_bh(&ep->ex_lock);
		goto free;
	}
	fh = fc_frame_header_get(fp);
	ap = fc_frame_payload_get(fp, sizeof(*ap));
	memset(ap, 0, sizeof(*ap));
	sp = &ep->ex_seq;
	net16_put(&ap->ba_high_seq_cnt, 0xffff);
	if (sp->seq_s_stat & SSB_ST_RESP) {
		ap->ba_seq_id = sp->seq_id;
		ap->ba_seq_id_val = FC_BA_SEQ_ID_VAL;
		ap->ba_low_seq_cnt = fh->fh_seq_cnt;
		ap->ba_high_seq_cnt = fh->fh_seq_cnt;
		if (sp->seq_active)
			net16_put(&ap->ba_low_seq_cnt, sp->seq_cnt);
	}
	sp = fc_seq_alloc(ep, ++ep->ex_seq_id);
	spin_unlock_bh(&ep->ex_lock);
	fc_seq_send_last(sp, fp, FC_RCTL_BA_ACC, FC_TYPE_BLS);
	fc_frame_free(rx_fp);
	return;
	
reject:
	fc_exch_send_ba_rjt(rx_fp, FC_BA_RJT_UNABLE, FC_BA_RJT_INV_XID);
free:
	fc_frame_free(rx_fp);
}

/*
 * Send BLS Reject.
 * The frame we're rejecting is supplied.
 */
static void
fc_exch_send_ba_rjt(struct fc_frame *rx_fp, enum fc_ba_rjt_reason reason,
		   enum fc_ba_rjt_explan explan)
{
	struct fc_frame *fp;
	struct fc_frame_header *rx_fh;
	struct fc_frame_header *fh;
	struct fc_ba_rjt *rp;
	u_int f_ctl;

	fp = fc_frame_alloc(rx_fp->fr_in_port, sizeof(*rp));
	if (!fp)
		return;
	fh = fc_frame_header_get(fp);
	rx_fh = fc_frame_header_get(rx_fp);

	memset(fh, 0, sizeof(*fh) + sizeof(*rp));

	rp = fc_frame_payload_get(fp, sizeof(*rp));
	rp->br_reason = reason;
	rp->br_explan = explan;

	/*
	 * seq_id, cs_ctl, df_ctl and param/offset are zero.
	 */
	fh->fh_s_id = rx_fh->fh_d_id;
	fh->fh_d_id = rx_fh->fh_s_id;
	fh->fh_ox_id = rx_fh->fh_rx_id;
	fh->fh_rx_id = rx_fh->fh_ox_id;
	fh->fh_seq_cnt = rx_fh->fh_seq_cnt;
	fh->fh_r_ctl = FC_RCTL_BA_RJT;
	fh->fh_type = FC_TYPE_BLS;

	/*
	 * Form f_ctl by inverting EX_CTX and SEQ_CTX (bits 23, 22).
	 * Echo FIRST_SEQ, LAST_SEQ, END_SEQ, END_CONN, SEQ_INIT.
	 * Bits 9-8 are meaningful (retransmitted or unidirectional).
	 * Last ACK uses bits 7-6 (continue sequence),
	 * bits 5-4 are meaningful (what kind of ACK to use).
	 * Always set LAST_SEQ, END_SEQ.
	 */
	f_ctl = net24_get(&rx_fh->fh_f_ctl);
	f_ctl &= FC_FC_EX_CTX | FC_FC_SEQ_CTX |
	    FC_FC_END_CONN | FC_FC_SEQ_INIT |
	    FC_FC_RETX_SEQ | FC_FC_UNI_TX;
	f_ctl ^= FC_FC_EX_CTX | FC_FC_SEQ_CTX;
	f_ctl |= FC_FC_LAST_SEQ | FC_FC_END_SEQ;
	f_ctl &= ~FC_FC_FIRST_SEQ;
	net24_put(&fh->fh_f_ctl, f_ctl);

	fp->fr_sof = fc_sof_class(rx_fp->fr_sof);
	fp->fr_eof = FC_EOF_T;
	if (fc_sof_needs_ack(fp->fr_sof))
		fp->fr_sof = FC_EOF_N;
	(void) fc_port_egress(rx_fp->fr_in_port, fp);
}

/*
 * Handle receive where the other end is originating the sequence and exchange.
 */
void fc_exch_recv_req(struct fc_exch_mgr *mp, struct fc_frame *fp,
		size_t max_payload,
		void (*dflt_recv)(struct fc_seq *, struct fc_frame *,
				void *), void *arg)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_seq *sp = NULL;
	struct fc_exch *ep = NULL;
	enum fc_sof sof;
	enum fc_eof eof;
	u_int32_t f_ctl;
	enum fc_pf_rjt_reason reject;
	void (*recv)(struct fc_seq *, struct fc_frame *, void *);
	void *ex_arg;

	fp->fr_seq = NULL;
	reject = fc_seq_lookup_recip(mp, fp);
	if (reject == FC_BA_RJT_NONE) {
		sp = fp->fr_seq;	/* sequence will be held */
		ASSERT(sp);
		ASSERT(atomic_read(&sp->seq_refcnt) != 0);
		ep = fc_seq_exch(sp);
		ASSERT(ep);
		ASSERT(atomic_read(&ep->ex_refcnt) != 0);
		ep->ex_max_payload = (u_int16_t)max_payload;
		sof = fp->fr_sof;
		eof = fp->fr_eof;
		f_ctl = net24_get(&fh->fh_f_ctl);
		fc_seq_send_ack(sp, fp);

		spin_lock_bh(&ep->ex_lock);
		recv = ep->ex_recv;
		ex_arg = ep->ex_recv_arg;
		if (eof == FC_EOF_T &&
		    (f_ctl & (FC_FC_LAST_SEQ | FC_FC_END_SEQ)) ==
		    (FC_FC_LAST_SEQ | FC_FC_END_SEQ)) {
			fc_exch_complete_locked(ep);
			ASSERT(atomic_read(&sp->seq_refcnt) != 0);
			ASSERT(fc_seq_exch(sp) == ep);
			fc_seq_complete(sp);
		}
		spin_unlock_bh(&ep->ex_lock);

		/*
		 * Call the receive function.
		 * The sequence is held (has a refcnt) for us,
		 * but not for the receive function.  The receive
		 * function is not expected to do a fc_seq_release()
		 * or fc_seq_complete().
		 *
		 * The receive function may allocate a new sequence
		 * over the old one, so we shouldn't change the
		 * sequence after this.
		 *
		 * The frame will be freed by the receive function.
		 */
		if (recv)
			(recv)(sp, fp, ex_arg);
		else
			(*dflt_recv)(sp, fp, arg);
		fp = NULL;	/* frame has been freed */
		fc_seq_release(sp);
	} else {
		if (fc_exch_debug)
			SA_LOG("exch/seq lookup failed: reject %x\n", reject);
		fc_frame_free(fp);
	}
}

/*
 * Handle receive where the other end is originating the sequence in
 * response to our exchange.
 */
void fc_exch_recv_seq_resp(struct fc_exch_mgr *mp, struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_seq *sp;
	struct fc_exch *ep;
	enum fc_sof sof;
	u_int32_t f_ctl;
	void (*recv)(struct fc_seq *, struct fc_frame *, void *);
	void *ex_arg;
	uint16_t seq_cnt;

	ep = fc_exch_lookup(mp, net16_get(&fh->fh_ox_id));
	if (!ep) {
		atomic_inc(&mp->em_stats.ems_error_xid_not_found);
		goto out;
	}
	if (ep->ex_rx_id == FC_XID_UNKNOWN) {
		ep->ex_rx_id = net16_get(&fh->fh_rx_id);
	}
	if (ep->ex_s_id != 0 && ep->ex_s_id != net24_get(&fh->fh_d_id)) {
		atomic_inc(&mp->em_stats.ems_error_xid_not_found);
		goto out;
	}
	if (ep->ex_d_id != net24_get(&fh->fh_s_id) &&
	    ep->ex_d_id != FC_FID_FLOGI) {
		atomic_inc(&mp->em_stats.ems_error_xid_not_found);
		goto out;
	}
	seq_cnt = net16_get(&fh->fh_seq_cnt);

	/*
	 * Drop frames while abort outstanding.
	 * Drop frames inside recovery qualifier.
	 */
	if (unlikely(ep->ex_e_stat & (ESB_ST_ABNORMAL | ESB_ST_REC_QUAL))) {
		if (ep->ex_e_stat & ESB_ST_ABNORMAL)
			atomic_inc(&mp->em_stats.ems_error_abort_in_prog);
		if (ep->ex_e_stat & ESB_ST_REC_QUAL)
			atomic_inc(&mp->em_stats.ems_error_in_rec_qual);
		goto out;
	}

	sof = fp->fr_sof;
	if (fc_sof_is_init(sof)) {
		sp = fc_seq_alloc(ep, fh->fh_seq_id);
		ASSERT(sp);
		sp->seq_s_stat |= SSB_ST_RESP;
	} else {
		sp = &ep->ex_seq;
		if (sp->seq_id != fh->fh_seq_id) {
			atomic_inc(&mp->em_stats.ems_error_seq_not_found);
			goto out;
		}
		sp->seq_active = 1;
		fc_seq_hold(sp);	/* hold to match alloc */
	}
	f_ctl = net24_get(&fh->fh_f_ctl);
	if (f_ctl & FC_FC_SEQ_INIT)
		ep->ex_e_stat |= ESB_ST_SEQ_INIT;
	fp->fr_seq = sp;
	if (fc_sof_needs_ack(sof)) {
		fc_seq_send_ack(sp, fp);
	}
	recv = ep->ex_recv;
	ex_arg = ep->ex_recv_arg;

	if (fh->fh_type != FC_TYPE_FCP && fp->fr_eof == FC_EOF_T &&
	    (f_ctl & (FC_FC_LAST_SEQ | FC_FC_END_SEQ)) ==
	    (FC_FC_LAST_SEQ | FC_FC_END_SEQ)) {
		spin_lock_bh(&ep->ex_lock);
		fc_exch_complete_locked(ep);
		ASSERT(atomic_read(&sp->seq_refcnt) != 0);
		ASSERT(fc_seq_exch(sp) == ep);
		fc_seq_complete(sp);
		spin_unlock_bh(&ep->ex_lock);
	}

	/*
	 * Call the receive function.
	 * The sequence is held (has a refcnt) for us,
	 * but not for the receive function.  The receive
	 * function is not expected to do a fc_seq_release()
	 * or fc_seq_complete().
	 *
	 * The receive function may allocate a new sequence
	 * over the old one, so we shouldn't change the
	 * sequence after this.
	 *
	 * The frame will be freed by the receive function.
	 */
	if (recv)
		(*recv)(sp, fp, ex_arg);
	else
		fc_frame_free(fp);
	fp = NULL;	/* frame has been freed */
	fc_seq_release(sp);
	return;
out:
	fc_frame_free(fp);
}

/*
 * Handle receive for a sequence where other end is responding to our sequence.
 */
void fc_exch_recv_resp(struct fc_exch_mgr *mp, struct fc_frame *fp)
{
	struct fc_seq *sp;

	sp = fc_seq_lookup_orig(mp, fp);	/* doesn't hold sequence */
	if (!sp) {
		atomic_inc(&mp->em_stats.ems_error_xid_not_found);
		if (fc_exch_debug)
			SA_LOG("seq lookup failed");
	} else {
		atomic_inc(&mp->em_stats.ems_error_non_bls_resp);
		if (fc_exch_debug) {
			SA_LOG("non-BLS response to sequence");
			fc_print_frame_hdr("fc_seq_recv_resp: "
					"non BLS response", fp);
		}
	}
	fc_frame_free(fp);
}

/*
 * Fill in frame header.
 *
 * The following fields are the responsibility of this routine:
 *      d_id, s_id, df_ctl, ox_id, rx_id, cs_ctl, seq_id
 *
 * The following fields are handled by the caller.
 *      r_ctl, type, f_ctl, seq_cnt, parm_offset
 *
 * That should be a complete list.
 *
 * We may be the originator or responder to the exchange.
 * We may be the originator or responder to the sequence.
 */
static void fc_seq_fill_hdr(struct fc_seq *sp, struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	struct fc_exch *ep;

	ep = fc_seq_exch(sp);
	ASSERT(ep);

	net24_put(&fh->fh_s_id, ep->ex_s_id);
	net24_put(&fh->fh_d_id, ep->ex_d_id);
	net16_put(&fh->fh_ox_id, ep->ex_ox_id);
	net16_put(&fh->fh_rx_id, ep->ex_rx_id);
	fh->fh_seq_id = sp->seq_id;
	fh->fh_cs_ctl = 0;
	fh->fh_df_ctl = 0;
}

/*
 * Accept sequence with LS_ACC.
 * If this fails due to allocation or transmit congestion, assume the
 * originator will repeat the sequence.
 */
void fc_seq_ls_acc(struct fc_seq *req_sp)
{
	struct fc_seq *sp;
	struct fc_els_ls_acc *acc;
	struct fc_frame *fp;

	sp = fc_seq_start_next(req_sp);
	ASSERT(sp);
	fp = fc_frame_alloc(fc_seq_exch(sp)->ex_port, sizeof(*acc));
	if (fp) {
		acc = fc_frame_payload_get(fp, sizeof(*acc));
		ASSERT(acc);
		memset(acc, 0, sizeof(*acc));
		acc->la_cmd = ELS_LS_ACC;
		fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
	} else {
		fc_seq_exch_complete(sp);
	}
}

/*
 * Reject sequence with ELS LS_RJT.
 * If this fails due to allocation or transmit congestion, assume the
 * originator will repeat the sequence.
 */
void fc_seq_ls_rjt(struct fc_seq *req_sp, enum fc_els_rjt_reason reason,
	      enum fc_els_rjt_explan explan)
{
	struct fc_seq *sp;
	struct fc_els_ls_rjt *rjt;
	struct fc_frame *fp;

	sp = fc_seq_start_next(req_sp);
	ASSERT(sp);
	fp = fc_frame_alloc(fc_seq_exch(sp)->ex_port, sizeof(*rjt));
	if (fp) {
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		ASSERT(rjt);
		memset(rjt, 0, sizeof(*rjt));
		rjt->er_cmd = ELS_LS_RJT;
		rjt->er_reason = reason;
		rjt->er_explan = explan;
		fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
	} else {
		fc_seq_exch_complete(sp);
	}
}

static void fc_exch_reset(struct fc_exch *ep)
{
	struct fc_seq *sp;
	void (*errh)(enum fc_event, void *);
	void *arg;

	spin_lock_bh(&ep->ex_lock);
	errh = ep->ex_errh;
	ep->ex_errh = NULL;
	if (ep->ex_e_stat & ESB_ST_REC_QUAL)
		fc_exch_release_locked(ep);	/* drop hold for rec_qual */
	ep->ex_e_stat &= ~ESB_ST_REC_QUAL;
	if (ep->ex_e_stat & ESB_ST_COMPLETE)
		errh = NULL;
	arg = ep->ex_recv_arg;
	if (sa_timer_active(&ep->ex_timer)) {
		sa_timer_cancel(&ep->ex_timer);
		fc_exch_release_locked(ep);	/* drop hold for timer */
	}
	sp = &ep->ex_seq;
	ep->ex_e_stat |= ESB_ST_COMPLETE;
	if (sp->seq_active) {
		fc_seq_hold(sp);
		spin_unlock_bh(&ep->ex_lock);
		fc_seq_complete(sp);
		fc_seq_release(sp);		/* may release exchange */
	} else {
		spin_unlock_bh(&ep->ex_lock);
	}
	if (errh)
		(*errh)(FC_EV_CLOSED, arg);
}

/*
 * Reset an exchange manager, releasing all sequences and exchanges.
 * If s_id is non-zero, reset only exchanges we source from that FID.
 * If d_id is non-zero, reset only exchanges destined to that FID.
 *
 * Currently, callers always use a d_id of 0, so this could be simplified later.
 */
void fc_exch_mgr_reset(struct fc_exch_mgr *mp, fc_fid_t s_id, fc_fid_t d_id)
{
	struct fc_exch *ep;
	struct fc_exch *next;
	struct fc_exch_pool *pp;

	for (pp = mp->em_pool; pp < &mp->em_pool[FC_EXCH_POOLS]; pp++) {
		TAILQ_FOREACH_SAFE(ep, &pp->emp_exch_busy, ex_list, next) {
			if ((s_id == 0 || s_id == ep->ex_s_id) &&
			    (d_id == 0 || d_id == ep->ex_d_id)) {
				fc_exch_reset(ep);
			}
		}
	}
}

void fc_seq_get_xids(struct fc_seq *sp, fc_xid_t *ox_id, fc_xid_t *rx_id)
{
	struct fc_exch *ep;

	ep = fc_seq_exch(sp);
	*ox_id = ep->ex_ox_id;
	*rx_id = ep->ex_rx_id;
}

/*
 * Handle incoming ELS REC - Read Exchange Concise.
 * Note that the requesting port may be different than the S_ID in the request.
 */
void fc_exch_mgr_els_rec(struct fc_seq *sp, struct fc_frame *rfp)
{
	struct fc_frame *fp;
	struct fc_exch *ep;
	struct fc_exch_mgr *em;
	struct fc_els_rec *rp;
	struct fc_els_rec_acc *acc;
	fc_fid_t s_id;
	fc_xid_t rx_id;
	fc_xid_t ox_id;

	rp = fc_frame_payload_get(rfp, sizeof(*rp));
	if (!rp)
		goto reject;
	s_id = net24_get(&rp->rec_s_id);
	rx_id = net16_get(&rp->rec_rx_id);
	ox_id = net16_get(&rp->rec_ox_id);

	/*
	 * Currently it's hard to find the local S_ID from the exchange
	 * manager.  This will eventually be fixed, but for now it's easier
	 * to lookup the subject exchange twice, once as if we were
	 * the initiator, and then again if we weren't.
	 */
	em = fc_seq_exch(sp)->ex_pool->emp_mgr;
	ep = fc_exch_lookup_raw(em, ox_id);
	if (!ep || ep->ex_orig_fid != s_id) {
		ep = NULL;
		if (rx_id != FC_XID_UNKNOWN)
			ep = fc_exch_lookup_raw(em, rx_id);
		if (!ep) {
			struct fc_exch_pool *pp;

			for (pp = em->em_pool; pp < &em->em_pool[FC_EXCH_POOLS];
			     pp++) {
				spin_lock_bh(&pp->emp_lock);
				TAILQ_FOREACH(ep, &pp->emp_exch_busy, ex_list) {
					if (ep->ex_rx_id != FC_XID_UNKNOWN &&
					    rx_id != FC_XID_UNKNOWN &&
					    ep->ex_rx_id != rx_id)
						continue;
					if (ep->ex_ox_id == ox_id &&
					    ep->ex_orig_fid == s_id)
						break;
				}
				spin_unlock_bh(&pp->emp_lock);
				if (ep)
					break;
			}
			if (!ep)
				goto reject;
		}
	}
	ASSERT(ep);		/* we found it */

	fp = fc_frame_alloc(fc_seq_exch(sp)->ex_port, sizeof(*acc));
	if (!fp) {
		fc_seq_exch_complete(sp);
		goto out;
	}
	acc = fc_frame_payload_get(fp, sizeof(*acc));
	ASSERT(acc);
	memset(acc, 0, sizeof(*acc));
	acc->reca_cmd = ELS_LS_ACC;
	acc->reca_ox_id = rp->rec_ox_id;
	acc->reca_ofid = rp->rec_s_id;
	net16_put(&acc->reca_rx_id, ep->ex_rx_id);
	net24_put(&acc->reca_rfid,
		  (ep->ex_s_id == ep->ex_orig_fid) ? ep->ex_d_id : ep->ex_s_id);
	net32_put(&acc->reca_fc4value, ep->ex_rec_data);
	net32_put(&acc->reca_e_stat, ep->ex_e_stat &
		  (ESB_ST_RESP | ESB_ST_SEQ_INIT | ESB_ST_COMPLETE));

	/*
	 * We don't have the exchange locked or held.
	 * We can't hold the exchange if it has completed, because
	 * that'll mess up the mp->emp_exch_in_use counter on release.
	 * Revalidate the exchange and if it has changed, send a reject.
	 */
	if (ep->ex_ox_id != ox_id || ep->ex_orig_fid != s_id ||
	    (rx_id != ep->ex_rx_id && ep->ex_rx_id != FC_XID_UNKNOWN &&
	     rx_id != FC_XID_UNKNOWN)) {
		fc_frame_free(fp);
		goto reject;
	}
	sp = fc_seq_start_next(sp);
	ASSERT(sp);
	fc_seq_send_last(sp, fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
out:
	fc_frame_free(rfp);
	return;
reject:
	fc_seq_ls_rjt(sp, ELS_RJT_UNAB, ELS_EXPL_OXID_RXID);
	fc_frame_free(rfp);
}

/*
 * Set FC-4 recovery data for sequence exchange.
 */
void fc_seq_rec_data(struct fc_seq *sp, uint32_t data)
{
	fc_seq_exch(sp)->ex_rec_data = data;
}

/*
 * Send ELS RRQ - Reinstate Recovery Qualifier.
 * This tells the remote port to stop blocking the use of
 * the exchange and the seq_cnt range.
 */
static void fc_exch_rrq(struct fc_exch *ep)
{
	struct fc_seq *sp;
	struct fc_els_rrq *rrq;
	struct fc_frame *fp;

	if (ep->ex_e_stat & ESB_ST_RESP)
		sp = fc_seq_start_exch(ep->ex_pool->emp_mgr, fc_exch_rrq_resp,
			NULL, NULL, ep->ex_d_id, ep->ex_s_id);
	else
		sp = fc_seq_start_exch(ep->ex_pool->emp_mgr, fc_exch_rrq_resp,
			NULL, NULL, ep->ex_s_id, ep->ex_d_id);
	if (!sp)
		return;
	fc_seq_exch(sp)->ex_port = ep->ex_port;
	fp = fc_frame_alloc(ep->ex_port, sizeof(*rrq));
	if (!fp) {
		fc_seq_exch_complete(sp);
		return;
	}
	rrq = fc_frame_payload_get(fp, sizeof(*rrq));
	memset(rrq, 0, sizeof(*rrq));
	rrq->rrq_cmd = ELS_RRQ;
	net24_put(&rrq->rrq_s_id, ep->ex_s_id);
	net16_put(&rrq->rrq_ox_id, ep->ex_ox_id);
	net16_put(&rrq->rrq_rx_id, ep->ex_rx_id);
	if (!fc_seq_send_req(sp, fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS, 0))
		fc_exch_timer_set(fc_seq_exch(sp), ep->ex_r_a_tov);
}

/*
 * Handle response from RRQ.
 * Not much to do here, really.
 * Should report errors.
 */
static void fc_exch_rrq_resp(struct fc_seq *sp, struct fc_frame *fp, void *arg)
{
	u_int	op;

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_RJT)
		SA_LOG("LS_RJT for RRQ");
	else if (op != ELS_LS_ACC)
		SA_LOG("unexpected response op %x for RRQ", op);
	fc_frame_free(fp);
}

/*
 * Handle incoming ELS RRQ - Reset Recovery Qualifier.
 */
void fc_exch_mgr_els_rrq(struct fc_seq *sp, struct fc_frame *fp)
{
	struct fc_exch_mgr *mp;
	struct fc_exch *ep;		/* request or subject exchange */
	struct fc_els_rrq *rp;
	uint32_t sid;
	uint16_t xid;
	enum fc_els_rjt_explan explan;

	rp = fc_frame_payload_get(fp, sizeof(*rp));
	explan = ELS_EXPL_INV_LEN;
	if (!rp)
		goto reject;

	/*
	 * lookup subject exchange.
	 */
	ep = fc_seq_exch(sp);
	sid = net24_get(&rp->rrq_s_id);		/* subject source */
	xid = net16_get(ep->ex_d_id == sid ? &rp->rrq_ox_id : &rp->rrq_rx_id);
	mp = ep->ex_pool->emp_mgr;
	ep = fc_exch_lookup(mp, xid);

	spin_lock_bh(&ep->ex_lock);
	explan = ELS_EXPL_OXID_RXID;
	if (!ep)
		goto unlock_reject;
	if (ep->ex_ox_id != net16_get(&rp->rrq_ox_id))
		goto unlock_reject;
	if (ep->ex_rx_id != net16_get(&rp->rrq_rx_id) &&
	    ep->ex_rx_id != FC_XID_UNKNOWN)
		goto unlock_reject;
	explan = ELS_EXPL_SID;
	if (ep->ex_s_id != sid)
		goto unlock_reject;

	/*
	 * Clear Recovery Qualifier state, and cancel timer if complete.
	 */
	fc_exch_hold(ep);
	if (ep->ex_e_stat & ESB_ST_REC_QUAL)
		fc_exch_release_locked(ep);
	if ((ep->ex_e_stat & ESB_ST_COMPLETE) &&
	    sa_timer_active(&ep->ex_timer)) {	/* XXX what if not complete? */
		sa_timer_cancel(&ep->ex_timer);
		fc_exch_release_locked(ep);	/* drop hold for timer */
	}
	ep->ex_e_stat &= ~ESB_ST_REC_QUAL;
	spin_unlock_bh(&ep->ex_lock);
	fc_exch_release(ep);

	/*
	 * Send LS_ACC.
	 */
	fc_seq_ls_acc(sp);
	fc_frame_free(fp);
	return;

unlock_reject:
	spin_unlock_bh(&ep->ex_lock);
reject:
	fc_seq_ls_rjt(sp, ELS_RJT_LOGIC, explan);
	fc_frame_free(fp);
}
