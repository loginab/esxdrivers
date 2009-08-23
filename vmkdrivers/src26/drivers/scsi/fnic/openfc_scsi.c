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
 * $Id: openfc_scsi.c 18551 2008-09-14 20:37:13Z jre $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

/*
 * non linux dot h files
 */
#include "sa_kernel.h"
#include "fc_types.h"
#include "sa_assert.h"
#include "fc_event.h"
#include "fc_port.h"
#include "fc_remote_port.h"
#include "fcoeioctl.h"
#include "fcdev.h"
#include "fc_fc2.h"
#include "fc_frame.h"
#include "fc_exch.h"
#include "fc_sess.h"
#include "fc_event.h"
#include "sa_log.h"
#include "openfc.h"
#include "openfc_scsi_pkt.h"
#include "crc32_le.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)		/* XXX temp */
#define sg_page(sg)	((sg)->page)
#define sg_next(sg)	(sg + 1)
#define page_link	page
#if !defined(__VMKLNX__)
static inline void sg_set_page(struct scatterlist *sg, struct page *page,
			       unsigned int len, unsigned int offset)
{
	sg->page = page;
	sg->offset = offset;
	sg->length = len;
}
#endif
#endif

int		openfc_debug = 0;
/*
 * function prototypes
 * FC scsi I/O related functions
 */
static void openfc_scsi_recv_data(struct fc_scsi_pkt *, struct fc_frame *);
static void openfc_scsi_recv(struct fc_seq *, struct fc_frame *, void *);
static void openfc_scsi_fcp_resp(struct fc_scsi_pkt *, struct fc_frame *);
static void openfc_scsi_complete(struct fc_scsi_pkt *);
static void openfc_tm_done(struct fc_seq *, struct fc_frame *, void *);
static void openfc_scsi_error(enum fc_event, void *);
static void openfc_abort_internal(struct fc_scsi_pkt *);
void openfc_scsi_cleanup(struct fc_scsi_pkt *);
static void openfc_timeout_error(struct fc_scsi_pkt *);
static void openfc_scsi_retry_cmd(struct fc_scsi_pkt *);
static int openfc_scsi_send_cmd(struct fc_scsi_pkt *);
static void openfc_scsi_timeout(struct fc_scsi_pkt *);
static void openfc_scsi_rec(struct fc_scsi_pkt *);
static void openfc_scsi_rec_resp(struct fc_seq *, struct fc_frame *, void *);
static void openfc_scsi_rec_error(enum fc_event, void *);

static void openfc_scsi_srr(struct fc_scsi_pkt *, enum fc_rctl, uint32_t);
static void openfc_scsi_srr_resp(struct fc_seq *, struct fc_frame *, void *);
static void openfc_scsi_srr_error(enum fc_event, void *);

/*
 * task managment timeout value
 */
#define FCOE_TM_TIMEOUT	     10000	/* mSec */
#define OPENFC_MAX_ERROR_CNT  5
#define OPENFC_MAX_RECOV_RETRY 3

/*
 * Lock fc_scsi_pkt.
 * The outstandingcmd queue lock is used to serialize access to the fc_scsi_pkt.
 * Allows for the command to be deleted and the queue to change while spinning.
 * Returns NULL if the request has been deleted.
 */
static inline struct openfc_cmdq *openfc_scsi_lock(struct fc_scsi_pkt *fsp)
{
	struct openfc_cmdq *qp;

	qp = &fsp->openfcp->outstandingcmd[fsp->idx];
	spin_lock(&qp->scsi_pkt_lock);
	if (qp->ptr != fsp) {
		spin_unlock(&qp->scsi_pkt_lock);
		qp = NULL;
	}
	return qp;
}

static inline void openfc_scsi_unlock(struct openfc_cmdq *qp)
{
	spin_unlock(&qp->scsi_pkt_lock);
}

static void openfc_timer_set(void (*handler)(struct fc_scsi_pkt *),
			     struct fc_scsi_pkt *sp, uint32_t delay)
{
	if (timer_pending(&sp->timer))
		del_timer_sync(&sp->timer);
	setup_timer(&sp->timer, (void (*)(ulong))handler, (ulong)sp);
	sp->timer.expires = jiffies + delay;
	add_timer(&sp->timer);
}

/*
 * End a request with a retry suggestion.
 */
static void openfc_scsi_retry(struct fc_scsi_pkt *fsp)
{
	fsp->status_code = OPENFC_ERROR;
	fsp->io_status = SUGGEST_RETRY << 24;
	openfc_scsi_complete(fsp);
}

/*
 * Receive SCSI data from target.
 * Called after receiving solicited data.
 */
static void openfc_scsi_recv_data(struct fc_scsi_pkt *fsp, struct fc_frame *fp)
{
	struct scsi_cmnd *sc = fsp->cmd;
	struct openfc_softc *openfcp = fsp->openfcp;
	struct fcoe_dev_stats *sp;
	struct fc_frame_header *fh;
	uint	crc;
	uint 	copy_len = 0;
	size_t	start_offset;
	size_t	offset;
	size_t	len;
	void	*buf;

	if (!sc->request_buffer)
		return;
	fh = fc_frame_header_get(fp);
	offset = net32_get(&fh->fh_parm_offset);
	start_offset = offset;
	len = fp->fr_len - sizeof(*fh);
	buf = fc_frame_payload_get(fp, 0);

	if (offset + len > fsp->data_len) {
		/*
		 * this should never happen
		 */
		if ((fp->fr_flags & FCPHF_CRC_UNCHECKED) &&
		    fc_frame_crc_check(fp))
			goto crc_err;
		if (openfc_debug) {
			SA_LOG("data received past end.	 "
			       "len %zx offset %zx "
			       "data_len %x\n", len, offset, fsp->data_len);
		}
		openfc_scsi_retry(fsp);
		return;
	}
	if (offset != fsp->xfer_len)
		fsp->state |= OPENFC_SRB_DISCONTIG;

	crc = 0;
	if (sc->use_sg) {
		struct scatterlist *sg;
		int nsg;
		size_t remaining, sg_bytes;
		size_t off;
		void *page_addr;

		if (fp->fr_flags & FCPHF_CRC_UNCHECKED)
			crc = crc32_sb8_64_bit(~0, (u8 *) fh, sizeof(*fh));

		sg = (struct scatterlist *)sc->request_buffer;
		nsg = sc->use_sg;
		remaining = len;

		while (remaining > 0 && nsg > 0) {
			if (offset >= sg->length) {
				offset -= sg->length;
				sg = sg_next(sg);
				nsg--;
				continue;
			}
			sg_bytes = min(remaining, sg->length - offset);

			/*
			 * The scatterlist item may be bigger than PAGE_SIZE,
			 * but we are limited to mapping PAGE_SIZE at a time.
			 */
			off = offset + sg->offset;
			sg_bytes = min(sg_bytes, (size_t)
				(size_t)(PAGE_SIZE - (off & ~PAGE_MASK)));
			page_addr = kmap_atomic(sg_page(sg) +
						(off >> PAGE_SHIFT),
						KM_SOFTIRQ0);
			if (!page_addr)
				break;		/* XXX panic? */

			if (!(fsp->state & OPENFC_SRB_ABORT_PENDING)) {
				if (fp->fr_flags & FCPHF_CRC_UNCHECKED) {
					crc = crc32_copy(crc,
							 (char *)page_addr +
							 (off & ~PAGE_MASK),
							 buf, sg_bytes);
				} else {
					__memcpy((char *)page_addr +
						 (off & ~PAGE_MASK),
						 buf, sg_bytes);
				}
			}
			kunmap_atomic(page_addr, KM_SOFTIRQ0);
			buf += sg_bytes;
			offset += sg_bytes;
			remaining -= sg_bytes;
			copy_len += sg_bytes;
		}
		if (fp->fr_flags & FCPHF_CRC_UNCHECKED)
			goto crc_check;
	} else if (fp->fr_flags & FCPHF_CRC_UNCHECKED) {
		crc = crc32_sb8_64_bit(~0, (u8 *)fh, sizeof(*fh));
		crc = crc32_copy(crc, (void *)sc->request_buffer + offset,
				 buf, len);
		copy_len = len;
crc_check:
		buf = fc_frame_payload_get(fp, 0);
		if (len % 4) {
			crc = crc32_sb8_64_bit(crc, buf + len, 4 - (len % 4));
			len += 4 - (len % 4);
		}
		if (~crc != le32_to_cpu(*(__le32 *)(buf + len))) {
crc_err:
			sp = openfcp->fd.dev_stats[smp_processor_id()];
			sp->ErrorFrames++;
			if (sp->InvalidCRCCount++ < 5)
				SA_LOG("CRC error on data frame");
			/*
			 * Assume the frame is total garbage.
			 * We may have copied it over the good part
			 * of the buffer.
			 * If so, we need to retry the entire operation.
			 * Otherwise, ignore it.
			 */
			if (fsp->state & OPENFC_SRB_DISCONTIG)
				openfc_scsi_retry(fsp);
			return;
		}
	} else {
		__memcpy((void *)sc->request_buffer + offset, buf, len);
		copy_len = len;
	}
	if (fsp->xfer_contig_end == start_offset)
		fsp->xfer_contig_end += copy_len;
	fsp->xfer_len += copy_len;

	/*
	 * In the very rare event that this data arrived after the response
	 * and completes the transfer, call the completion handler.
	 */
	if (unlikely(fsp->state & OPENFC_SRB_RCV_STATUS) &&
	    fsp->xfer_len == fsp->data_len - fsp->scsi_resid)
		openfc_scsi_complete(fsp);
}

/*
 * Send SCSI data to target.
 * Called after receiving a Transfer Ready data descriptor.
 */
static void openfc_scsi_send_data(struct fc_scsi_pkt *fsp, struct fc_seq *sp,
				  size_t offset, size_t len,
				  struct fc_frame *oldfp, int sg_supp)
{
	struct scsi_cmnd *sc;
	struct scatterlist *fsg;
	struct scatterlist *sg;
	int		nsg;
	struct scatterlist local_sg;
	struct fc_frame *fp = NULL;
	struct openfc_softc *openfcp = fsp->openfcp;
	size_t		remaining;
	size_t		mfs;
	size_t		tlen;
	size_t		sg_bytes;
	size_t		frame_offset;
	int		error;
	void		*data = NULL;
	void		*page_addr;
	int		using_sg = sg_supp;

	if (unlikely(offset + len > fsp->data_len)) {
		/*
		 * this should never happen
		 */
		if (openfc_debug) {
			SA_LOG("xfer-ready past end. len %zx offset %zx\n",
			       len, offset);
		}
		openfcp->outstandingcmd[fsp->idx].ptr = NULL;
		openfc_abort_internal(fsp);
		return;
	} else if (offset != fsp->xfer_len) {
		/*
		 *  Out of order Data Request - no problem, but unexpected.
		 */
		if (openfc_debug) {
			SA_LOG("xfer-ready non-contiguous. "
			       "len %zx offset %zx\n", len, offset);
		}
	}
	mfs = fc_seq_mfs(sp);
	ASSERT(mfs >= FC_MIN_MAX_PAYLOAD);
	ASSERT(mfs <= FC_MAX_PAYLOAD);
	if (mfs > 512)
		mfs &= ~(512 - 1);		/* round down to block size */
	ASSERT(mfs >= FC_MIN_MAX_PAYLOAD);	/* won't go below 256 */
	ASSERT(len > 0);
	sc = fsp->cmd;
	nsg = sc->use_sg;
	if (nsg) {

		/*
		 * If a get_page()/put_page() will fail, don't use sg lists
		 * in the fc_frame structure.
		 *
		 * The put_page() may be long after the I/O has completed
		 * in the case of FCoE, since the network driver does it
		 * via free_skb().
		 *
		 * Test this case with 'dd </dev/zero >/dev/st0 bs=64k'.
		 */
		if (using_sg) {
			for (sg = sc->request_buffer; nsg--; sg = sg_next(sg)) {
				if (page_count(sg_page(sg)) == 0 ||
				    (sg_page(sg)->flags & (
						1 << PG_lru |
						1 << PG_private	|
						1 << PG_locked |
						1 << PG_active |
						1 << PG_dirty |
						1 << PG_slab |
						1 << PG_swapcache |
						1 << PG_writeback |
						1 << PG_reserved |
						1 << PG_buddy))) {
					using_sg = 0;
					break;
				}
			}
			nsg = sc->use_sg;
		}
		sg = (struct scatterlist *) sc->request_buffer;
	} else {
		/* this should not happen */
		sg_init_one(&local_sg, sc->request_buffer, sc->request_bufflen);
		sg = &local_sg;
		nsg = 1;
	}
	remaining = len;
	frame_offset = offset;
	tlen = 0;
	sp = fc_seq_start_next_fctl(sp, FC_FC_REL_OFF);
	ASSERT(sp);
	
	while (remaining > 0 && nsg > 0) {
		if (offset >= sg->length) {
			offset -= sg->length;
			sg = sg_next(sg);
			nsg--;
			continue;
		}
		if (!fp) {
			tlen = min(mfs, remaining);
			if (using_sg) {
				fp = fc_frame_alloc(openfcp->fcs_port, 0);
			} else {
				fp = fc_frame_alloc(openfcp->fcs_port, tlen);
				data = (void *)(fp->fr_hdr + 1);
			}
			ASSERT_NOTIMPL(fp != NULL);	/* XXX */
			fc_frame_setup(fp, FC_RCTL_DD_SOL_DATA, FC_TYPE_FCP);
			fc_frame_set_offset(fp, frame_offset);
		}
		sg_bytes = min(tlen, sg->length - offset);
		if (using_sg) {
			/*
			 * do not need to do get page
			 * because we can unpin the memory when the
			 * status of this I/O is received
			 */
			ASSERT(fp->fr_sg_len < FC_FRAME_SG_LEN);
			fsg = &fp->fr_sg[fp->fr_sg_len++];
			sg_set_page(fsg, sg_page(sg),
				    sg_bytes, sg->offset + offset);
		} else {
			u_int off = offset + sg->offset;

			/*
			 * The scatterlist item may be bigger than PAGE_SIZE,
			 * but we must not cross pages inside the kmap.
			 */
			sg_bytes = min(sg_bytes, (size_t)(PAGE_SIZE -
			  	(off & ~PAGE_MASK)));
			page_addr = kmap_atomic(sg_page(sg) +
				(off >> PAGE_SHIFT),
				KM_SOFTIRQ0);
			__memcpy(data, (char *) page_addr + (off & ~PAGE_MASK),
				 sg_bytes);
			kunmap_atomic(page_addr, KM_SOFTIRQ0);
			data += sg_bytes;
		}
		fp->fr_len += sg_bytes;
		offset += sg_bytes;
		frame_offset += sg_bytes;
		tlen -= sg_bytes;
		remaining -= sg_bytes;

		if (remaining == 0) {
			error = fc_seq_send_tsi(sp, fp);
		} else if (tlen == 0) {
			error = fc_seq_send_frag(sp, fp);
		} else {
			continue;
		}
		fp = NULL;

		ASSERT(!error);
		if (error) {
			/*
			 * we need to handle this case -XXX
			 */
			openfc_scsi_retry(fsp);
			return;
		}
	}
	fsp->xfer_len += len;	/* premature count? */
}

/*
 * fcs exch mgr calls this routine to process scsi
 * exchanges.
 *
 * Return   : None
 * Context  : called from Soft IRQ context
 *	      can not called holding list lock
 */

static void openfc_scsi_recv(struct fc_seq *sp, struct fc_frame *fp, void *arg)
{
	struct fc_scsi_pkt *fsp = (struct fc_scsi_pkt *) arg;
	struct openfc_cmdq *qp;
	struct openfc_softc *openfcp;
	struct fc_frame_header *fh;
	struct os_lun  *lun;
	struct fc_data_desc *dd;
	u_int		r_ctl;
	u_int8_t        sg_supp;

	fh = fc_frame_header_get(fp);
	r_ctl = fh->fh_r_ctl;
	openfcp = fsp->openfcp;

	if (!(openfcp->state & OPENFC_RUNNING)) {
		goto out;
	}
	qp = openfc_scsi_lock(fsp);
	if (!qp)
		goto out;
	fsp->last_pkt_time = jiffies;

	lun = fsp->disk;
	if (lun && lun->error_cnt) {
		lun->state = OPENFC_LUN_READY;
		lun->error_cnt = 0;
	}

	if (r_ctl == FC_RCTL_DD_DATA_DESC) {
		/*
		 * received XFER RDY from the target
		 * need to send data to the target
		 */
		ASSERT(!(fp->fr_flags & FCPHF_CRC_UNCHECKED));
		dd = fc_frame_payload_get(fp, sizeof(*dd));
		ASSERT_NOTIMPL(dd != NULL);

		/*
		 * if the I/O request does not have sg list then
		 * copy user data into packet
		 */	
		sg_supp = (openfcp->dev->capabilities & TRANS_C_SG);
		if (fsp->cmd->use_sg == 0) {
			sg_supp = 0;
		}		
		openfc_scsi_send_data(fsp, sp,
			      (size_t) net32_get(&dd->dd_offset),
			      (size_t) net32_get(&dd->dd_len), fp, sg_supp);
		fc_seq_rec_data(sp, fsp->xfer_len);
	} else if (r_ctl == FC_RCTL_DD_SOL_DATA) {
		/*
		 * received a DATA frame
		 * next we will copy the data to the system buffer
		 */
		ASSERT(fp->fr_len >= sizeof(*fh)); /* data may be zero len */
		openfc_scsi_recv_data(fsp, fp);
		fc_seq_rec_data(sp, fsp->xfer_contig_end);
	} else if (r_ctl == FC_RCTL_DD_CMD_STATUS) {
		ASSERT(!(fp->fr_flags & FCPHF_CRC_UNCHECKED));
		openfc_scsi_fcp_resp(fsp, fp);
	} else {
		SA_LOG("unexpected frame.  r_ctl %x", r_ctl);
	}
	openfc_scsi_unlock(qp);
out:
	fc_frame_free(fp);
}

static void openfc_scsi_fcp_resp(struct fc_scsi_pkt *fsp, struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fcp_resp *fc_rp;
	struct fcp_resp_ext *rp_ex;
	struct fcp_resp_rsp_info *fc_rp_info;
	u_int32_t expected_len;
	u_int32_t respl = 0;
	u_int32_t snsl = 0;
	u_int8_t flags = 0;

	if (unlikely(fp->fr_len < sizeof(*fh) + sizeof(*fc_rp)))
		goto len_err;
	fc_rp = (struct fcp_resp *)(fp->fr_hdr + 1);
	fsp->cdb_status = fc_rp->fr_status;
	flags = fc_rp->fr_flags;
	fsp->scsi_comp_flags = flags;
	expected_len = fsp->data_len;

	if (unlikely((flags & ~FCP_CONF_REQ) || fc_rp->fr_status)) {
		rp_ex = (void *)(fc_rp + 1);
		if (flags & (FCP_RSP_LEN_VAL | FCP_SNS_LEN_VAL)) {
			if (fp->fr_len < sizeof(*fh) + sizeof(*fc_rp) +
			    sizeof(*rp_ex))
				goto len_err;
			fc_rp_info = (struct fcp_resp_rsp_info *)(rp_ex + 1);
			if (flags & FCP_RSP_LEN_VAL) {
				respl = net32_get(&rp_ex->fr_rsp_len);
				if (fp->fr_len < sizeof(*fh) + sizeof(*fc_rp) +
				    sizeof(*rp_ex) + respl)
					goto len_err;
				if ((respl != 0 && respl != 4 && respl != 8) ||
				    (fc_rp_info->rsp_code != FCP_TMF_CMPL))
					goto err;
			}
			if (flags & FCP_SNS_LEN_VAL) {
				snsl = net32_get(&rp_ex->fr_sns_len);
				if (snsl > SCSI_SENSE_BUFFERSIZE)
					snsl = SCSI_SENSE_BUFFERSIZE;
				memcpy(fsp->cmd->sense_buffer,
				       (char *)fc_rp_info + respl, snsl);
			}
		}
		if (flags & (FCP_RESID_UNDER | FCP_RESID_OVER)) {
			if (fp->fr_len < sizeof(*fh) + sizeof(*fc_rp) +
			    sizeof(rp_ex->fr_resid))
				goto len_err;
			if (flags & FCP_RESID_UNDER) {
				fsp->scsi_resid = net32_get(&rp_ex->fr_resid);
				/*
				 * The cmnd->underflow is the minimum number of
				 * bytes that must be transfered for this
				 * command.  Provided a sense condition is not
				 * present, make sure the actual amount
				 * transferred is at least the underflow value
				 * or fail.
				 */
				if (!(flags & FCP_SNS_LEN_VAL) &&
				    (fc_rp->fr_status == 0) &&
				    (fsp->cmd->request_bufflen -
				     fsp->scsi_resid) < fsp->cmd->underflow)
					goto err;
				expected_len -= fsp->scsi_resid;
			} else {
				fsp->status_code = OPENFC_ERROR;
			}
		}
	}
	fsp->state |= OPENFC_SRB_RCV_STATUS;

	/*
	 * Check for missing or extra data frames.
	 */
	if (unlikely(fsp->xfer_len != expected_len)) {
		if (fsp->xfer_len < expected_len) {
			/*
			 * Some data may be queued locally,
			 * Wait a at least one jiffy to see if it is delivered.
			 * If this expires without data, we'll do SRR.
			 */
			openfc_timer_set(openfc_scsi_timeout, fsp, 2);
			return;
		}
		fsp->status_code = OPENFC_DATA_OVRRUN;
		SA_LOG("tgt %6x xfer len %zx greater than expected len %x. "
		       "data len %zx\n",
		       fsp->rp->rp_fid,
		       fsp->xfer_len, expected_len, fsp->data_len);
	}
	openfc_scsi_complete(fsp);
	return;

len_err:
	SA_LOG("short FCP response. flags 0x%x len %u respl %u snsl %u",
	       flags, fp->fr_len, respl, snsl);
err:
	fsp->status_code = OPENFC_ERROR;
	openfc_scsi_complete(fsp);
}

/*
 * Complete request.
 *
 * Called holding scsi_pkt_lock.
 * All completions, including errors come through here.
 * This may be long after the response was received if data was lost.
 */
static void openfc_scsi_complete(struct fc_scsi_pkt *fsp)
{
	struct fc_seq *sp;

	if (fsp->idx) {
		struct openfc_cmdq *qp;

		qp = &fsp->openfcp->outstandingcmd[fsp->idx];
		ASSERT(qp->ptr);
		ASSERT(qp->ptr == fsp);
		qp->ptr = NULL;
	}

	if (timer_pending(&fsp->timer))
		del_timer_sync(&fsp->timer);

	/*
	 * Test for transport underrun, independent of response underrun status.
	 */
	if (fsp->xfer_len < fsp->data_len && !fsp->io_status &&
	    (!(fsp->scsi_comp_flags & FCP_RESID_UNDER) ||
	       fsp->xfer_len < fsp->data_len - fsp->scsi_resid)) {
		fsp->status_code = OPENFC_DATA_UNDRUN;
		fsp->io_status = (SUGGEST_RETRY << 24);
	}

	sp = fsp->seq_ptr;
	fsp->seq_ptr = NULL;
	if (sp) {
		if (unlikely(fsp->scsi_comp_flags & FCP_CONF_REQ)) {
			struct fc_frame *conf_frame;
			struct fc_seq *csp;

			csp = fc_seq_start_next_fctl(sp, 0);
			conf_frame = fc_frame_alloc(fsp->openfcp->fcs_port, 0);
			if (conf_frame)
				fc_seq_send_last(csp, conf_frame,
						 FC_RCTL_DD_SOL_CTL,
						 FC_TYPE_FCP);
			else
				fc_seq_exch_complete(csp);
		}
		fc_seq_exch_complete(sp); /* drop hold by openfc_scsi_send() */
	}
	ASSERT(fsp->done);
	fsp->done(fsp);			/* calls upper level and free fsp */
}

/*
 * Cancel a request without sending abort or confirmation.
 * This is called with the packet lock held.
 */
void openfc_scsi_abort_iocontext(struct fc_scsi_pkt *fsp)
{
	struct fc_seq  *sp = fsp->seq_ptr;

	if (!(fsp->state & OPENFC_SRB_RCV_STATUS)) {
		fsp->seq_ptr = NULL;
		if (sp)
			fc_seq_exch_complete(sp);
		
		fsp->status_code = OPENFC_ERROR;
		fsp->io_status = (SUGGEST_RETRY << 24);
		(*fsp->done)(fsp);
	}
}

/*
 * called by upper layer protocol
 *
 * Return   : zero for success and -1 for failure
 * Context  : called from process context or timer context
 *	      must not be called holding list lock
 */
int openfc_scsi_send(struct fcdev *dev, struct fc_scsi_pkt *fsp)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	u_int32_t	idx, i;
	unsigned long	flags;
	void		*p;

	/*
	 * Acquire the list lock
	 */
	spin_lock_irqsave(&openfcp->outstandingcmd_lock, flags);
	i = openfcp->current_cmd_indx;
	/*
	 * looking for an empty slot in outstandingcmd array
	 */
	for (idx = 1; idx < OPENFC_MAX_OUTSTANDING_COMMANDS; idx++) {
		i++;
		if (i >= OPENFC_MAX_OUTSTANDING_COMMANDS) {
			i = 1;
		}
		/*
		 * looking for an empty slot
		 * first check if the pointer is zero
		 * if it is zero then try to get the lock 
		 * and if we get the lock then it is an emty slot
		 * otherwise it is in processo of getting empty
		 */
		if (openfcp->outstandingcmd[i].ptr == 0) {
			if (spin_trylock
			    (&openfcp->outstandingcmd[i].scsi_pkt_lock))
				break;
		}
	}
	if (idx == OPENFC_MAX_OUTSTANDING_COMMANDS) {
		SA_LOG("outstanding cmd array is full");
		spin_unlock_irqrestore(&openfcp->outstandingcmd_lock, flags);

		/*
		 * here we return to the upper layer with an error
		 * upper layer will retry the i/o later
		 */
		return -1;
	}

	/*
	 * at this time we have the slot index and the lock to 
	 * the slot.
	 */
	openfcp->current_cmd_indx = i;
	openfcp->outstandingcmd[i].ptr = (void *) fsp;
	fsp->idx = i;
	spin_unlock(&openfcp->outstandingcmd[i].scsi_pkt_lock);
	spin_unlock_irqrestore(&openfcp->outstandingcmd_lock, flags);
	p = xchg(&((CMD_SP(fsp->cmd))), &openfcp->outstandingcmd[i]);
	net32_put(&fsp->cdb_cmd.fc_dl, fsp->data_len);
	fsp->cdb_cmd.fc_flags = fsp->req_flags & ~FCP_CFL_LEN_MASK;

	net64_put((net64_t *) fsp->cdb_cmd.fc_lun, (fsp->lun << 48));
	memcpy(fsp->cdb_cmd.fc_cdb, fsp->cmd->cmnd, fsp->cmd->cmd_len);
	return openfc_scsi_send_cmd(fsp);
}

static int openfc_scsi_send_cmd(struct fc_scsi_pkt *fsp)
{
	struct openfc_softc *openfcp = fsp->openfcp;
	struct fc_frame *fp;
	struct fc_seq  *sp;

	fsp->timer.data = (ulong)fsp;
	sp = fc_sess_seq_alloc(fsp->rp->rp_sess,
			       openfc_scsi_recv, openfc_scsi_error,
			       (void *) fsp);
	if (!sp)
		goto retry;
	fp = fc_frame_alloc(openfcp->fcs_port, sizeof(fsp->cdb_cmd));
	if (!fp) {
		openfcp->outstandingcmd[fsp->idx].ptr = (void *)NULL;
		fc_seq_exch_complete(sp);
		goto retry;
	}
	fc_seq_hold(sp);
	fsp->seq_ptr = sp;
	memcpy(fc_frame_payload_get(fp, sizeof(fsp->cdb_cmd)),
	       &fsp->cdb_cmd, sizeof(fsp->cdb_cmd));
	fc_seq_send_req(sp, fp, FC_RCTL_DD_UNSOL_CMD,
			FC_TYPE_FCP, 0);
	openfc_timer_set(openfc_scsi_timeout, fsp,
			 (fsp->tgt_flags & OPENFC_TGT_REC_SUPPORTED) ?
			 OPENFC_SCSI_REC_TOV : OPENFC_SCSI_ER_TIMEOUT);
	return 0;
retry:
	openfc_timer_set(openfc_scsi_timeout, fsp, OPENFC_SCSI_REC_TOV);
	return 0;
}

/*
 * transport error handler
 */
static void openfc_scsi_error(enum fc_event event, void *fsp_arg)
{
	struct fc_scsi_pkt *fsp = (struct fc_scsi_pkt *) fsp_arg;
	struct openfc_softc *openfcp = fsp->openfcp;
	struct openfc_cmdq *qp = NULL;

	if (openfcp->state == OPENFC_GOING_DOWN) {
		return;
	}
	qp = openfc_scsi_lock(fsp);
	if (!qp)
		return;
	ASSERT(fsp != NULL);
	switch (event) {
	case FC_EV_TIMEOUT:
		SA_LOG("exchange level timeout error"); /* can't happen */
		openfc_scsi_complete(fsp);
		break;

	case FC_EV_CLOSED:
		fsp->status_code = OPENFC_CMD_PLOGO;
		openfc_scsi_complete(fsp);
		break;

	case FC_EV_ACC:
		/*
		 * this is abort accepted case
		 */
		SA_LOG("BLS_ACC event");
		if (fsp->state & OPENFC_SRB_ABORT_PENDING) {
			fsp->state &= ~OPENFC_SRB_ABORT_PENDING;
			fsp->state |= OPENFC_SRB_ABORTED;
			if (fsp->wait_for_comp)
				complete(&fsp->tm_done);
		}
		break;

	case FC_EV_RJT:
		/*
		 * this is abort rejected case
		 */
		SA_LOG("BLS reject event");
		if (fsp->state & OPENFC_SRB_ABORT_PENDING) {
			fsp->state &= ~OPENFC_SRB_ABORT_PENDING;
			if (fsp->wait_for_comp)
				complete(&fsp->tm_done);
			break;
		}
		break;
	default:
		SA_LOG("unknown event %d", event);
		fsp->status_code = OPENFC_CMD_PLOGO;
		openfc_scsi_complete(fsp);
		break;
	}
	openfc_scsi_unlock(qp);
}

/*
 * Abort a command due to an error detected on an incoming transfer ready
 * or received data frame.
 * This is called with the fc_scsi_pkt lock held.
 * If the abort is sent, the error callback will handle completion.
 */
static void openfc_abort_internal(struct fc_scsi_pkt *fsp)
{
	fsp->state |= OPENFC_SRB_ABORT_PENDING;
	fsp->cdb_status = -1;
	if (fc_seq_abort_exch(fsp->seq_ptr))
		openfc_scsi_complete(fsp);	/* abort could not be sent */
}

/*
 * Scsi abort handler- calls fcs to send an abort
 * and then wait for abort completion
 */
int openfc_abort_cmd(struct fcdev *dev, struct fc_scsi_pkt *fsp)
{
	int		rc = FAILED;

	if (!fsp->seq_ptr)
		return rc;
	if (fc_seq_abort_exch(fsp->seq_ptr))
		return rc;

	fsp->idx = 0;
	fsp->state |= OPENFC_SRB_ABORT_PENDING;

	init_completion(&fsp->tm_done);
	fsp->wait_for_comp = 1;
	if (!wait_for_completion_timeout(&fsp->tm_done,
					 msecs_to_jiffies(FCOE_TM_TIMEOUT))) {
		SA_LOG("target abort cmd  failed");
		rc = FAILED;
	} else if (fsp->state & OPENFC_SRB_ABORTED) {
		SA_LOG("target abort cmd passed");
		rc = SUCCESS;
	}
	if (fsp->seq_ptr) {
		fc_seq_exch_complete(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	if (timer_pending(&fsp->timer))
		del_timer_sync(&fsp->timer);
	return rc;
}

/*
 * Retry LUN reset after resource allocation failed.
 */
static void openfc_lun_reset_send(struct fc_scsi_pkt *fsp)
{
	struct openfc_softc *openfcp = fsp->openfcp;
	const size_t len = sizeof(fsp->cdb_cmd);
	struct fc_frame *fp;
	struct fc_seq  *sp;

	fp = fc_frame_alloc(openfcp->fcs_port, len);
	if (!fp)
		goto retry;
	sp = fc_sess_seq_alloc(fsp->rp->rp_sess, openfc_tm_done,
			       openfc_scsi_error, (void *) fsp);
	if (!sp)
		goto free_retry;
	fc_seq_hold(sp);
	fsp->seq_ptr = sp;
	memcpy(fc_frame_payload_get(fp, len), &fsp->cdb_cmd, len);
	fc_seq_send_req(sp, fp, FC_RCTL_DD_UNSOL_CMD, FC_TYPE_FCP, 0);
	return;

	/*
	 * Exchange or frame allocation failed.  Set timer and retry.
	 */
free_retry:
	fc_frame_free(fp);
retry:
	openfc_timer_set(openfc_lun_reset_send, fsp, OPENFC_SCSI_REC_TOV);
}

/*
 * Scsi target reset handler- send a LUN RESET to the device
 * and wait for reset reply
 */
int openfc_target_reset(struct fcdev *dev, struct fc_scsi_pkt *fsp)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	unsigned long	flags;
	u_int32_t	idx;
	struct fc_scsi_pkt *ofsp;
	struct scsi_cmnd *sc_cmd;
	unsigned int tid, otid, lun, olun;

	fsp->idx = 0;
	fsp->openfcp = openfcp;
	net32_put(&fsp->cdb_cmd.fc_dl, fsp->data_len);
	fsp->cdb_cmd.fc_tm_flags = FCP_TMF_LUN_RESET;
	net64_put((net64_t *) fsp->cdb_cmd.fc_lun, (fsp->lun << 48));
	fsp->wait_for_comp = 1;
	init_completion(&fsp->tm_done);

	openfc_lun_reset_send(fsp);

	/*
	 * wait for completion of reset
	 * after that make sure all commands are terminated
	 */
	if (!wait_for_completion_timeout(&fsp->tm_done,
					 msecs_to_jiffies(FCOE_TM_TIMEOUT))) {
		struct fc_seq *sp;

		sp = fsp->seq_ptr;
		if (sp) {
			fsp->seq_ptr = NULL;
			fc_seq_abort_exch(sp);
			fc_seq_exch_complete(sp);
		}
		SA_LOG("target reset failed");
		return FAILED;
	}

	/*
	 * Acquire the list lock
	 */
	tid = fsp->id;
	lun = fsp->lun;
	spin_lock_irqsave(&openfcp->outstandingcmd_lock, flags);
	for (idx = 1; idx < OPENFC_MAX_OUTSTANDING_COMMANDS; idx++) {
		spin_lock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
		ofsp = openfcp->outstandingcmd[idx].ptr;
		if (ofsp) {
			/*
			 * if the cmd is internal then no scsi cmd 
			 * associated with it, so ignore that scsi pkt
			 */
			if (!ofsp->cmd) {
				openfcp->outstandingcmd[idx].ptr = NULL;
				SA_LOG("non scsi cmd in the array %d idx\n",
				       idx);
				goto out;
			}
			sc_cmd = (struct scsi_cmnd *) ofsp->cmd;
			ASSERT(sc_cmd != NULL);
			otid = sc_cmd->device->id;
			olun = sc_cmd->device->lun;

			if ((otid == tid) && (olun == lun)) {

				/*
				 * aborted cmd 
				 */
				if (ofsp->status_code) {
					goto out;
				}
				ofsp->status_code = OPENFC_CMD_ABORTED;
				ofsp->cdb_status = -1;
				if (ofsp->seq_ptr)
					fc_seq_exch_complete(ofsp->seq_ptr);
				ofsp->seq_ptr = NULL;
				openfcp->outstandingcmd[idx].ptr = NULL;
				if (ofsp->done)
					(*ofsp->done)(ofsp);
			}
		}
out:
		spin_unlock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
	}

	spin_unlock_irqrestore(&openfcp->outstandingcmd_lock, flags);
	fsp->state = OPENFC_SRB_FREE;

	return SUCCESS;
}

/*
 * this routine will assume that flogo has happened
 * end we are cleaning all the commands.
 */
int openfc_inf_reset(struct fcdev *dev, struct fc_scsi_pkt *fsp)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	int		idx;
	unsigned long	flags;
	struct fc_scsi_pkt *old_fsp;

	spin_lock_irqsave(&openfcp->outstandingcmd_lock, flags);
	for (idx = 1; idx < OPENFC_MAX_OUTSTANDING_COMMANDS; idx++) {
		spin_lock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
		old_fsp = openfcp->outstandingcmd[idx].ptr;
		if (old_fsp) {
			if (timer_pending(&old_fsp->timer)) {
				del_timer_sync(&old_fsp->timer);
			}
			openfcp->outstandingcmd[idx].ptr = NULL;
			old_fsp->state = OPENFC_SRB_FREE;
			openfc_free_scsi_pkt(old_fsp);
		}
		spin_unlock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
	}
	spin_unlock_irqrestore(&openfcp->outstandingcmd_lock, flags);
	return 0;
}

/*
 * Task Managment response handler
 */
static void openfc_tm_done(struct fc_seq *sp, struct fc_frame *fp, void *arg)
{
	struct fc_scsi_pkt *fsp = (struct fc_scsi_pkt *) arg;
	struct fc_frame_header *fh;
	struct fcp_resp *fc_rp;
	u_int		r_ctl;

	fh = fc_frame_header_get(fp);
	r_ctl = fh->fh_r_ctl;
	if (r_ctl == FC_RCTL_DD_CMD_STATUS) {
		fc_rp = fc_frame_payload_get(fp, sizeof(*fc_rp));
		fsp->cdb_status = fc_rp->fr_status;
		fc_seq_exch_complete(sp);
		fc_frame_free(fp);
		if (fsp->wait_for_comp) 
			complete(&fsp->tm_done);
	}
}

void openfc_scsi_cleanup(struct fc_scsi_pkt *sp)
{
	struct openfc_softc *openfcp = sp->openfcp;
	int		idx;
	unsigned long	flags;
	struct fc_scsi_pkt *old_fsp;

	spin_lock_irqsave(&openfcp->outstandingcmd_lock, flags);
	for (idx = 1; idx < OPENFC_MAX_OUTSTANDING_COMMANDS; idx++) {
		spin_lock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
		old_fsp = openfcp->outstandingcmd[idx].ptr;
		if (old_fsp) {
			old_fsp->status_code = OPENFC_HRD_ERROR;
			openfc_scsi_complete(old_fsp);
		}
		spin_unlock(&openfcp->outstandingcmd[idx].scsi_pkt_lock);
	}
	spin_unlock_irqrestore(&openfcp->outstandingcmd_lock, flags);
	if (sp) {
		if (sp->wait_for_comp) 
			complete(&sp->tm_done);
	}
}

/*
 * openfc_scsi_timeout: called by the OS timer function.
 *
 * The timer has been inactivated and must be reactivated if desired
 * using openfc_timer_set().
 *
 * Algorithm:
 *
 * If REC is supported, just issue it, and return.  The REC exchange will
 * complete or time out, and recovery can continue at that point.
 *
 * Otherwise, if the response has been received without all the data,
 * it has been ER_TIMEOUT since the response was received.
 *
 * If the response has not been received,
 * we see if data was received recently.  If it has been, we continue waiting,
 * otherwise, if it is a simple read or write, we abort the command.
 */
static void openfc_scsi_timeout(struct fc_scsi_pkt *fsp)
{
	struct openfc_cmdq *qp;
	u_int8_t        cdb_op;

	qp = openfc_scsi_lock(fsp);
	if (!qp)
		return;

	if (fsp->disk->tgtp->flags & OPENFC_TGT_REC_SUPPORTED) {
		openfc_scsi_rec(fsp);
	} else {

		/*
		 * If data is still arriving, continue waiting.
		 */
		if (jiffies - fsp->last_pkt_time < OPENFC_SCSI_ER_TIMEOUT / 2) {
			openfc_timer_set(openfc_scsi_timeout, fsp,
					 OPENFC_SCSI_ER_TIMEOUT);
		} else if (fsp->state & OPENFC_SRB_RCV_STATUS) {
			/*
			 * Data is not arriving and we've received status.
			 * Complete the command and let SCSI handle it.
			 */
			openfc_scsi_complete(fsp);
		} else {

			/*
			 * Data is not arriving.  If this is a read or write,
			 * abort the command, otherwise let SCSI time it out.
			 */
			cdb_op = fsp->cdb_cmd.fc_cdb[0];
			if (cdb_op == READ_10 || cdb_op == READ_6 ||
			    cdb_op == WRITE_10 || cdb_op == WRITE_6)
				openfc_timeout_error(fsp);
		}
	}
	openfc_scsi_unlock(qp);
}

/*
 * Send a REC ELS request
 * Called with fsp->scsi_pkt_lock held.
 */
void openfc_scsi_rec(struct fc_scsi_pkt *fsp)
{
	struct fc_seq  *sp;
	struct fc_frame *fp;
	struct fc_els_rec *rp;
	struct fc_sess *sess;
	fc_xid_t ox_id;
	fc_xid_t rx_id;

	sess = fsp->rp->rp_sess;
	sp = fsp->seq_ptr;
	if (unlikely(!sess || !sp || !fsp->rp->rp_sess_ready)) {
		fsp->status_code = OPENFC_HRD_ERROR;
		fsp->io_status = (SUGGEST_RETRY << 24);
		openfc_scsi_complete(fsp);
		return;
	}
	fc_seq_get_xids(sp, &ox_id, &rx_id);
	sp = fc_sess_seq_alloc(sess, openfc_scsi_rec_resp,
			       openfc_scsi_rec_error, fsp);
	if (!sp)
		goto retry;

	fp = fc_frame_alloc(fsp->openfcp->fcs_port, sizeof(*rp));
	if (!fp)
		goto seq_retry;

	rp = fc_frame_payload_get(fp, sizeof(*rp));
	ASSERT(rp);
	memset(rp, 0, sizeof(*rp));
	rp->rec_cmd = ELS_REC;
	net24_put(&rp->rec_s_id, fc_sess_get_sid(sess));
	net16_put(&rp->rec_ox_id, ox_id);
	net16_put(&rp->rec_rx_id, rx_id);

	if (fc_seq_send_req(sp, fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS, 0))
		goto frame_retry;
	openfc_scsi_pkt_hold(fsp);	/* hold while REC outstanding */
	fc_exch_timer_set_recover(fc_seq_exch(sp), OPENFC_SCSI_REC_TOV);
	return;

frame_retry:
	fc_frame_free(fp);
seq_retry:
	fc_seq_exch_complete(sp);
retry:
	/*
	 * Wait a bit, then retry sending the REC.
	 */
	if (fsp->recov_retry++ < OPENFC_MAX_RECOV_RETRY)
		openfc_timer_set(openfc_scsi_timeout, fsp, OPENFC_SCSI_REC_TOV);
	else
		openfc_timeout_error(fsp);
}

/*
 * Receive handler for REC ELS response frame
 */
static void openfc_scsi_rec_resp(struct fc_seq *sp, struct fc_frame *fp,
				 void *arg)
{
	struct fc_scsi_pkt *fsp = (struct fc_scsi_pkt *) arg;
	struct openfc_cmdq *qp;
	struct fc_els_rec_acc *recp;
	struct fc_els_ls_rjt *rjt;
	uint32_t	e_stat;
	uint32_t	offset;
	uint8_t		opcode;
	enum dma_data_direction data_dir;
	enum fc_rctl r_ctl;

	qp = openfc_scsi_lock(fsp);
	if (!qp)
		goto out;
	opcode = fc_frame_payload_op(fp);
	if (opcode == ELS_LS_RJT) {
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		switch (rjt->er_reason) {
		default:
			if (openfc_debug)
				SA_LOG("device %x unexpected REC reject "
				       "reason %d expl %d",
				       fsp->rp->rp_fid, rjt->er_reason,
				       rjt->er_explan);
			/* fall through */

		case ELS_RJT_UNSUP:
			if (openfc_debug)
				SA_LOG("device %x does not support REC",
				       fsp->rp->rp_fid);
			fsp->disk->tgtp->flags &= ~OPENFC_TGT_REC_SUPPORTED;
			openfc_timeout_error(fsp);
			break;

		case ELS_RJT_LOGIC:
		case ELS_RJT_UNAB:
			/*
			 * If no data transfer, the command frame got dropped
			 * so we just retry.  If data was transferred, we
			 * lost the response but the target has no record,
			 * so we abort and retry.
			 */
			if (rjt->er_explan == ELS_EXPL_OXID_RXID &&
			   fsp->xfer_len == 0) {
				openfc_scsi_retry_cmd(fsp);
				break;
			}
			openfc_timeout_error(fsp);
			break;
		}
	} else if (opcode == ELS_LS_ACC) {
		if (qp->ptr != fsp || (fsp->state & OPENFC_SRB_ABORTED))
			goto unlock_out;

		data_dir = fsp->cmd->sc_data_direction;
		recp = fc_frame_payload_get(fp, sizeof(*recp));
		offset = net32_get(&recp->reca_fc4value);
		e_stat = net32_get(&recp->reca_e_stat);

		if (openfc_debug) {
			SA_LOG("REC ACC target %x op %2x "
			       "oxid %x offset %x e_stat %x "
			       "%scomplete. %s has seq_init",
			       net24_get(&recp->reca_rfid),
			       fsp->cdb_cmd.fc_cdb[0],
			       net16_get(&recp->reca_ox_id), offset, e_stat,
			       (e_stat & ESB_ST_COMPLETE) ? "" : "in",
			       (e_stat & ESB_ST_SEQ_INIT) ? "target" : "init");
		}

		if (e_stat & ESB_ST_SEQ_INIT) {

			/*
			 * The remote port has the initiative, so just
			 * keep waiting for it to complete.
			 */
			openfc_timer_set(openfc_scsi_timeout, fsp,
					 OPENFC_SCSI_REC_TOV);
		} else if (e_stat & ESB_ST_COMPLETE) {

			/*
			 * The exchange is complete and we have seq. initiative.
			 *
			 * For output, we must've lost the response.
			 * For input, all data must've been sent.
			 * We lost may have lost the response
			 * (and a confirmation was requested) and maybe
			 * some data.
			 *
			 * If all data received, send SRR
			 * asking for response.  If partial data received,
			 * or gaps, SRR requests data at start of gap.
			 * Recovery via SRR relies on in-order-delivery.
			 */
			if (data_dir == DMA_TO_DEVICE) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else if (fsp->xfer_contig_end == offset) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else {
				offset = fsp->xfer_contig_end;
				r_ctl = FC_RCTL_DD_SOL_DATA;
			}
			openfc_scsi_srr(fsp, r_ctl, offset);
		} else {
			/*
			 * The exchange is incomplete, we have seq. initiative.
			 * Lost response with requested confirmation,
			 * lost confirmation, lost transfer ready or
			 * lost write data.
			 *
			 * For output, if not all data was received, ask
			 * for transfer ready to be repeated.
			 *
			 * If we received or sent all the data, send SRR to
			 * request response.
			 *
			 * If we lost a response, we may have lost some read
			 * data as well.
			 */
			r_ctl = FC_RCTL_DD_SOL_DATA;
			if (data_dir == DMA_TO_DEVICE) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				if (offset < fsp->data_len)
					r_ctl = FC_RCTL_DD_DATA_DESC;
			} else if (offset == fsp->xfer_contig_end) {
				r_ctl = FC_RCTL_DD_CMD_STATUS;
			} else if (fsp->xfer_contig_end < offset) {
				offset = fsp->xfer_contig_end;
			}
			openfc_scsi_srr(fsp, r_ctl, offset);
		}
	}
unlock_out:
	openfc_scsi_unlock(qp);
out:
	openfc_scsi_pkt_release(fsp);	/* drop hold for outstanding REC */
	fc_frame_free(fp);
}

/*
 * Handle error response or timeout for REC exchange.
 */
static void openfc_scsi_rec_error(enum fc_event event, void *fsp_arg)
{
	struct fc_scsi_pkt *fsp = (struct fc_scsi_pkt *) fsp_arg;
	struct openfc_softc *openfcp = fsp->openfcp;
	struct openfc_cmdq *qp;

	if (openfcp->state == OPENFC_GOING_DOWN)	/* XXX handle CLOSED? */
		goto out;
	qp = openfc_scsi_lock(fsp);
	if (!qp)
		goto out;

	switch (event) {
	case FC_EV_CLOSED:
		openfc_timeout_error(fsp);
		break;

	default:
		SA_LOG("REC fid %x error unexpected event %d",
		       fsp->rp->rp_fid, event);
		fsp->status_code = OPENFC_CMD_PLOGO;
		/* fall through */

	case FC_EV_TIMEOUT:
		/*
		 * Assume REC or LS_ACC was lost.
		 * The exchange manager will have aborted REC, so retry.
		 */
		SA_LOG("REC fid %x error event %d", fsp->rp->rp_fid, event);
		if (fsp->recov_retry++ < OPENFC_MAX_RECOV_RETRY)
			openfc_scsi_rec(fsp);
		else
			openfc_timeout_error(fsp);
		break;
	}
	openfc_scsi_unlock(qp);
out:
	openfc_scsi_pkt_release(fsp);	/* drop hold for outstanding REC */
}

/*
 * Time out error routine: 
 * abort's the I/O close the exchange and
 * send completion notification to scsi layer
 */
static void openfc_timeout_error(struct fc_scsi_pkt *fsp)
{
	struct os_lun  *lun;
	struct openfc_cmdq *qp;

	qp = &fsp->openfcp->outstandingcmd[fsp->idx];
	ASSERT(qp->ptr);
	ASSERT(qp->ptr == fsp);
	qp->ptr = NULL;

	fsp->state |= OPENFC_SRB_ABORT_PENDING;
	if (fsp->seq_ptr) {
		fc_seq_abort_exch(fsp->seq_ptr);
		fc_seq_exch_complete(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	fsp->status_code = OPENFC_CMD_TIME_OUT;
	fsp->cdb_status = 0;
	fsp->io_status = 0;
	lun = fsp->disk;
	lun->error_cnt++;
	/*
	 * if we get 5 consecutive errors 
	 * for the same command then offline the device.
	 */
	if (lun->error_cnt >= OPENFC_MAX_ERROR_CNT) {
		lun->state = OPENFC_LUN_ERR;
		fsp->status_code = OPENFC_HRD_ERROR;
	} else {
		fsp->io_status = (SUGGEST_RETRY << 24);
	}
	if (fsp->done)
		(*fsp->done)(fsp);
}

/*
 * Retry command.
 * An abort isn't needed.  This is presumably due to cmd packet loss.
 *
 * If we deliver an error persistently on the same LUN, then report it
 * as a hard error.
 */
static void openfc_scsi_retry_cmd(struct fc_scsi_pkt *fsp)
{
	struct os_lun  *lun;

	if (fsp->seq_ptr) {
		fc_seq_exch_complete(fsp->seq_ptr);
		fsp->seq_ptr = NULL;
	}
	lun = fsp->disk;
	lun->error_cnt++;

	/*
	 * if we get 5 consecutive errors
	 * for the same command then offline the device.
	 */
	if (lun->error_cnt >= OPENFC_MAX_ERROR_CNT) {
		fsp->status_code = OPENFC_HRD_ERROR;
		openfc_scsi_complete(fsp);
	} else {
		openfc_scsi_send_cmd(fsp);
	}
}

/*
 * Sequence retransmission request.
 * This is called after receiving status but insufficient data, or
 * when expecting status but the request has timed out.
 */
static void openfc_scsi_srr(struct fc_scsi_pkt *fsp,
	enum fc_rctl r_ctl, uint32_t offset)
{
	struct openfc_softc *openfcp = fsp->openfcp;
	struct fc_remote_port *rp;
	struct fc_seq *sp;
	struct fcp_srr *srr;
	struct fc_frame *fp;
	uint cdb_op;
	fc_xid_t ox_id;
	fc_xid_t rx_id;

	rp = fsp->rp;
	cdb_op = fsp->cdb_cmd.fc_cdb[0];
	fc_seq_get_xids(fsp->seq_ptr, &ox_id, &rx_id);

	if (!(rp->rp_fcp_parm & FCP_SPPF_RETRY) ||
	    !rp->rp_sess || !rp->rp_sess_ready)
		goto retry;			/* shouldn't happen */
	fp = fc_frame_alloc(openfcp->fcs_port, sizeof(*srr));
	if (!fp)
		goto retry;

	sp = fc_sess_seq_alloc(rp->rp_sess, openfc_scsi_srr_resp,
				openfc_scsi_srr_error, fsp);
	if (!sp)
		goto free_retry;

	srr = fc_frame_payload_get(fp, sizeof(*srr));
	ASSERT(srr);
	memset(srr, 0, sizeof(*srr));
	srr->srr_op = ELS_SRR;
	net16_put(&srr->srr_ox_id, ox_id);
	net16_put(&srr->srr_rx_id, rx_id);
	srr->srr_r_ctl = r_ctl;
	net32_put(&srr->srr_rel_off, offset);
	fc_seq_hold(sp);		/* hold while I/O in progress */

	if (fc_seq_send_req(sp, fp, FC_RCTL_ELS4_REQ, FC_TYPE_FCP, 0)) {
		fc_seq_exch_complete(sp);
		fc_seq_release(sp);	/* release hold just above */
		goto retry;		/* fc_seq_send has freed frame */
	}
	fsp->xfer_len = offset;
	fsp->state &= ~OPENFC_SRB_RCV_STATUS;
	fc_exch_timer_set_recover(fc_seq_exch(sp), OPENFC_SCSI_REC_TOV);
	openfc_scsi_pkt_hold(fsp);	/* hold for outstanding SRR */
	return;

free_retry:
	fc_frame_free(fp);
retry:
	openfc_scsi_retry(fsp);
}

/*
 * Handle response from SRR.
 */
static void openfc_scsi_srr_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *arg)
{
	struct fc_scsi_pkt *fsp = arg;
	fc_xid_t ox_id;
	fc_xid_t rx_id;
	struct openfc_cmdq *qp;

	qp = openfc_scsi_lock(fsp);
	if (!qp)
		goto out;

	fc_seq_get_xids(fsp->seq_ptr, &ox_id, &rx_id);
	switch (fc_frame_payload_op(fp)) {
	case ELS_LS_ACC:
		fsp->recov_retry = 0;
		openfc_timer_set(openfc_scsi_timeout, fsp, OPENFC_SCSI_REC_TOV);
		break;
	case ELS_LS_RJT:
	default:
		openfc_timeout_error(fsp);
		break;
	}
	openfc_scsi_unlock(qp);

	/*
	 * The exchange manager marks most ELS exchanges complete,
	 * but SRR is special, since it has FC-4 type FCP.
	 */
	fc_seq_exch_complete(sp);
out:
	fc_frame_free(fp);
	openfc_scsi_pkt_release(fsp);	/* drop hold for outstanding SRR */
}

static void openfc_scsi_srr_error(enum fc_event event, void *arg)
{
	struct fc_scsi_pkt *fsp = arg;
	struct openfc_cmdq *qp;

	qp = openfc_scsi_lock(fsp);
	if (!qp)
		goto out;
	switch (event) {
	case FC_EV_CLOSED:			/* e.g., link failure */
		openfc_timeout_error(fsp);
		break;
	case FC_EV_TIMEOUT:
		if (fsp->recov_retry++ < OPENFC_MAX_RECOV_RETRY)
			openfc_scsi_rec(fsp);
		else
			openfc_timeout_error(fsp);
		break;
	default:
		openfc_scsi_retry(fsp);
		break;
	}
	openfc_scsi_unlock(qp);
out:
	openfc_scsi_pkt_release(fsp);	/* drop hold for outstanding SRR */
}
