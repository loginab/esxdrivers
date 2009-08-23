/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fcs_cmd.c 18557 2008-09-14 22:36:38Z jre $
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "fc_types.h"
#include "fc_event.h"
#include "fc_frame.h"
#include "fc_port.h"
#include "fc_virt_fab.h"
#include "fc_virt_fab_impl.h"
#include "fc_local_port.h"
#include "fc_remote_port.h"
#include "fc_exch.h"
#include "fc_print.h"
#include "fc_sess.h"
#include "fc_event.h"
#include "fc_fs.h"
#include "fc_ils.h"
#include "fc_fcp.h"
#include "fcs_state.h"
#include "fcs_state_impl.h"

/*
 * Context for fcs_cmd_send.
 */
struct fcs_cmd {
	struct fc_frame		*fcc_resp_frame; /* frame for response */
	struct fc_seq		*fcc_seq;
	struct fc_frame_header	fcc_req_hdr;	/* header from request */
	uint16_t		fcc_seq_cnt;	/* next fh_seq_cnt expected */
	uint16_t		fcc_resp_len;	/* bytes of response recvd. */
	enum fc_event		fcc_event;	/* event received on error */
	struct completion	fcc_complete;
	struct kref		fcc_kref;
	spinlock_t		fcc_lock;
};

static void fcs_cmd_recv(struct fc_seq *, struct fc_frame *, void *);
static void fcs_cmd_error(enum fc_event, void *);
static void fcs_cmd_sess_event(int, void *);

static void fcs_cmd_free(struct kref *krp)
{
	sa_free(container_of(krp, struct fcs_cmd, fcc_kref));
}

/*
 * Send command for OpenFC ioctl and wait for response.
 * The frame fp passed in will be sent and freed.  
 * The response frame rfp will be filled in with the response and not freed.
 * Note that the response frame may be filled in by several frames in the
 * response sequence, so may be larger than the max payload size of 2112.
 * The response frame as filled in here won't have a header.
 * The context is allocated here and freed by the receive or error handler,
 * even if we're interrupted.
 *
 * Returns non-zero (negative errno) on error.
 *
 * Using netlink sockets as an asynchronous interface might be neater.
 */
int fcs_cmd_send(struct fcs_state *sp, struct fc_frame *fp,
		struct fc_frame *rfp, u_int time_ms, u_int do_plogi)
{
	struct fc_seq *seq;
	struct fc_frame_header *fh;
	struct fc_local_port *lp;
	struct fc_sess *sess = NULL;
	struct fcs_cmd	*cp;
	fc_fid_t did;
	u_int	op;
	u_int	mfs;
	int	rc;

	might_sleep();
	ASSERT(!rfp->fr_sg_len);
	lp = sp->fs_local_port;
	fh = fc_frame_header_get(fp);

	cp = sa_malloc(sizeof (*cp));
	memset(cp, 0, sizeof (*cp));
	init_completion(&cp->fcc_complete);
	kref_init(&cp->fcc_kref);
	spin_lock_init(&cp->fcc_lock);
	cp->fcc_resp_frame = rfp;

        /* 
	 * Get a session.
	 */
	did = net24_get(&fh->fh_d_id);
	rc = -EINVAL;
	if (did == 0 || did == FC_FID_BCAST)
		goto out;
	op = fc_frame_payload_op(fp);
	sess = fc_sess_lookup_create(lp, did, 0);
	rc = -ENOMEM;
	if (!sess)
		goto out;
	if (do_plogi && !fc_sess_is_ready(sess)) {
		if (!fc_sess_event_enq(sess, fcs_cmd_sess_event, cp))
			goto out;
		fc_sess_start(sess);
		while (!fc_sess_is_ready(sess)) {
			rc = wait_for_completion_interruptible(
						&cp->fcc_complete);
			if (rc)
				goto out;
		}
	}
	seq = fc_sess_seq_alloc(sess, fcs_cmd_recv, fcs_cmd_error, cp);
	if (!seq) {
		rc = -ENOMEM;
		goto out;
	}
	mfs = fc_seq_mfs(seq);

	/*
	 * Check the length against session limits and interface MTU.
	 */
	rc = -EMSGSIZE;
	if (fp->fr_len - sizeof (*fh) > mfs) {
		fc_seq_exch_complete(seq);
		goto out;
	}

	cp->fcc_seq = seq;
	fc_exch_set_port(fc_seq_exch(seq), sp->fs_args.fca_port);
	cp->fcc_req_hdr = *fh;
	kref_get(&cp->fcc_kref);		/* take ref for response */

	rc = -fc_seq_send_req(seq, fp, fh->fh_r_ctl, fh->fh_type,
				net32_get(&fh->fh_parm_offset));
	if (rc) {
		fc_seq_exch_complete(seq);
		kref_put(&cp->fcc_kref, fcs_cmd_free);
		goto out;
	}
	fc_exch_timer_set(fc_seq_exch(seq), time_ms);

	/*
	 * Wait for reply or timeout.
	 * Send succeeded, so the exchange is expected to complete either
 	 * through a response frame or a timeout.  The reference on the
	 * command will be dropped one way or another.
	 */
	rc = wait_for_completion_interruptible(&cp->fcc_complete);
	rfp->fr_len = cp->fcc_resp_len;
	if (rc) {
		fc_seq_abort_exch(seq);		/* abort if possible */
		goto out;
	}
	switch (cp->fcc_event) {
	case FC_EV_NONE:
	case FC_EV_ACC:
		break;
	case FC_EV_RJT:
		rc = -EIO;
		break;
	case FC_EV_TIMEOUT:
		rc = -EINTR;
		break;
	default:
		SA_LOG("unexpected event %d", cp->fcc_event);
		rc = -EINVAL;
		break;
	}
out:
	if (sess) {
		fc_sess_event_deq(sess, fcs_cmd_sess_event, cp);
		fc_sess_release(sess);
	}
	kref_put(&cp->fcc_kref, fcs_cmd_free);
	return rc;
}

static void fcs_cmd_recv(struct fc_seq *seq, struct fc_frame *fp, void *arg)
{
	struct fcs_cmd *cp = arg;
	struct fc_frame_header *fh;
	uint32_t offset;
	uint32_t len;
	u_int	error = 0;		/* XXX for debugging */

	ASSERT(!fp->fr_sg_len);
	ASSERT(!(fp->fr_flags & FCPHF_CRC_UNCHECKED));

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != cp->fcc_req_hdr.fh_type) 
		error |= 1;
	if (fh->fh_type == FC_TYPE_CT && fh->fh_r_ctl != FC_RCTL_DD_SOL_CTL)
		error |= 2;
	if (fh->fh_type == FC_TYPE_ELS && fh->fh_r_ctl != FC_RCTL_ELS_REP) 
		error |= 4;
	if (net32_get(&fh->fh_parm_offset) != cp->fcc_resp_len)
		error |= 8;
	if (net16_get(&fh->fh_seq_cnt) != cp->fcc_seq_cnt)
		error |= 0x10;
	spin_lock_bh(&cp->fcc_lock);
	offset = cp->fcc_resp_len;
	len = fp->fr_len - sizeof (*fh);
	if (offset + len > cp->fcc_resp_frame->fr_len)
		error |= 0x20;
	if (error) {
		SA_LOG("rejecting - unexpected header fields %x", error);
		fc_print_frame_hdr(__FUNCTION__, fp); 	/* XXX */
		cp->fcc_event = FC_EV_RJT;		/* cause error return */
	} else {
		memcpy((char *) cp->fcc_resp_frame->fr_hdr + offset,
			(char *) (fp->fr_hdr + 1) + offset, len);
		cp->fcc_resp_len += len;
		cp->fcc_seq_cnt++;
	}
	spin_unlock_bh(&cp->fcc_lock);
	if (fp->fr_eof == FC_EOF_T) {
		complete(&cp->fcc_complete);
		kref_put(&cp->fcc_kref, fcs_cmd_free);
	}
	fc_frame_free(fp);
}

static void fcs_cmd_error(enum fc_event event, void *arg)
{
	struct fcs_cmd *cp = arg;

	spin_lock_bh(&cp->fcc_lock);
	cp->fcc_event = event;
	cp->fcc_seq = NULL;
	spin_unlock_bh(&cp->fcc_lock);
	complete(&cp->fcc_complete);
	kref_put(&cp->fcc_kref, fcs_cmd_free);
}

static void fcs_cmd_sess_event(int event, void *arg)
{
	struct fcs_cmd *cp = arg;

	if (event == FC_EV_READY) {
		complete(&cp->fcc_complete);
	}
}
