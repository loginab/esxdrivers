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
 * $Id: fc_exch_impl.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_FC_EXCH_IMPL_H_
#define _LIBFC_FC_EXCH_IMPL_H_

#include "sa_kernel.h"
#include "sa_timer.h"
#include "fc_ils.h"
#include "queue.h"

/*
 * Fibre Channel Exchanges and Sequences - software state.
 */
#define FC_SEQ_RETRY_LIMIT  2	/* number of retries per ABTS */

/*
 * Structure and function definitions for managing Fibre Channel Exchanges
 * and Sequences.
 *
 * The three primary structures used here are fc_exch_mgr, fc_exch, and fc_seq.
 *
 * fc_exch_mgr is holds the exchange state for an N port or set of N ports.
 *
 * fc_exch holds state for one exchange and links to its active sequences.
 *
 * fc_seq holds the state for an individual sequence.
 */

/*
 * Sequence.
 */
struct fc_seq {
	uint8_t 	seq_id;		/* seq ID */
	uint8_t 	seq_active;	/* active flag */
	uint16_t 	seq_s_stat;	/* flags for sequence status block */
	uint16_t	seq_cnt;	/* frames sent so far on sequence */
	atomic_t 	seq_refcnt;	/* reference counter */
	uint32_t 	seq_f_ctl;	/* F_CTL flags for frames */
};

/*
 * Exchange.
 *
 * Locking notes: The ex_lock protects changes to the following fields:
 *      ex_e_stat, ex_f_ctl, ex_seq.seq_s_stat, ex_seq.seq_f_ctl.
 *      ex_seq_id
 *      sequence allocation
 */
struct fc_exch {
	/*
	 * The initial fields are initialized and
	 * don't get cleared for the life of the exchange manager.
	 */
	struct fc_exch_pool *ex_pool;	/* exchange pool */
	fc_xid_t	ex_xid;		/* our exchange ID */
	TAILQ_ENTRY(fc_exch) ex_list;	/* free or busy list linkage */
	spinlock_t 	ex_lock;	/* lock covering exchange state */
	atomic_t 	ex_refcnt;	/* reference counter */
	struct sa_timer ex_timer;	/* timer for upper level protocols */

	/*
	 * Fields after ex_refcnt are cleared when an exchange is reallocated.
	 */
#define fc_exch_clear_start ex_port
	struct fc_port 	*ex_port;	/* port to peer (s/b in remote port) */
	fc_xid_t	ex_ox_id;	/* originator's exchange ID */
	fc_xid_t	ex_rx_id;	/* responder's exchange ID */
	fc_fid_t	ex_orig_fid;	/* originator's FCID */
	fc_fid_t	ex_s_id;	/* source ID */
	fc_fid_t	ex_d_id;	/* destination ID */
	uint32_t	ex_e_stat;	/* exchange status for ESB */
	uint32_t	ex_rec_data;	/* FC-4 value for REC */
	uint32_t	ex_r_a_tov;	/* r_a_tov from session (msec) */
	uint8_t		ex_seq_id;	/* last sequence ID used */
	uint16_t	ex_max_payload;	/* maximum payload size in bytes */
	uint32_t	ex_f_ctl;	/* F_CTL flags for sequences */
	enum fc_class	ex_class;	/* class of service */
	struct fc_seq	ex_seq;		/* single sequence */

	/*
	 * Handler for responses to this current exchange.
	 */
	void		(*ex_recv)(struct fc_seq *, struct fc_frame *, void *);
	void		(*ex_errh)(enum fc_event, void *);
	void		*ex_recv_arg;	/* 3rd arg for recv or error handler */
};

/*
 * Exchange pool.
 * This is a per-CPU free / used list of exchanges managed by the same
 * exchange manager.
 */
struct fc_exch_pool {
	struct fc_exch_mgr *emp_mgr;		/* exchange manager */
	u_int 		emp_exch_in_use; 	/* exchanges in use */
	u_int 		emp_exch_total; 	/* exchanges in pool */
	TAILQ_HEAD(, fc_exch) emp_exch_free;	/* list of free exchanges */
	TAILQ_HEAD(, fc_exch) emp_exch_busy;	/* list of busy exchanges */
	spinlock_t 	emp_lock;		/* lock covering this pool */
};

/*
 * Exchange manager.
 *
 * This structure is the center for creating exchanges and sequences.
 * It can be assigned to each session or to a local port or fabric.
 * It manages the allocation of exchange IDs.
 */
struct fc_exch_mgr {
	enum fc_class	em_class;	/* default class for sequences */
	uint8_t		em_order;	/* log2 of page count in kernel */
	fc_xid_t	em_min_xid;	/* lowest numbered exchange ID */
	fc_xid_t	em_max_xid;	/* highest numbered exchange ID */
	struct fc_exch_pool em_pool[FC_EXCH_POOLS];	/* pools */
	struct {
		atomic_t ems_error_no_free_exch;
		atomic_t ems_error_xid_not_found;
		atomic_t ems_error_xid_busy;
		atomic_t ems_error_seq_not_found;
		atomic_t ems_error_non_bls_resp;
		atomic_t ems_ex_aborts;
		atomic_t ems_seq_aborts;
		atomic_t ems_error_abort_in_prog;
		atomic_t ems_error_in_rec_qual;
	} em_stats;
	struct fc_exch	em_exch[0];	/* direct table of exchanges */
					/* the exchanges must be last */
};

#endif /* _LIBFC_FC_EXCH_IMPL_H_ */
