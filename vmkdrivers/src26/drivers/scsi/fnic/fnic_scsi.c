/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2008 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fnic_tx.c 17851 2008-08-29 06:00:05Z jre $
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "kcompat.h"

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>

#include "sa_assert.h"

#include "fc_types.h"
#include "fnic_io.h"
#include "fnic.h"

#include "fc_frame.h"
#include "fc_els.h"
#include "fc_fcoe.h"
#include "fc_types.h"

#include "fcp_hdr.h"

#include "openfc_scsi_pkt.h"
#include "openfc.h"

#include "fc_sess_impl.h"
#include "fc_remote_port.h"

char *fnic_state_str[] = {
	"FNIC_IN_FC_MODE",
	"FNIC_IN_FC_TRANS_ETH_MODE",
	"FNIC_IN_ETH_MODE",
	"FNIC_IN_ETH_TRANS_FC_MODE",
};

static const char *fcpio_status_str[] =  {
	[FCPIO_SUCCESS] = "FCPIO_SUCCESS", /*0x0*/
	[FCPIO_INVALID_HEADER] = "FCPIO_INVALID_HEADER",
	[FCPIO_OUT_OF_RESOURCE] = "FCPIO_OUT_OF_RESOURCE",
	[FCPIO_INVALID_PARAM] = "FCPIO_INVALID_PARAM]",
	[FCPIO_REQ_NOT_SUPPORTED] = "FCPIO_REQ_NOT_SUPPORTED",
	[FCPIO_IO_NOT_FOUND] = "FCPIO_IO_NOT_FOUND",
	[FCPIO_ABORTED] = "FCPIO_ABORTED", /*0x41*/
	[FCPIO_TIMEOUT] = "FCPIO_TIMEOUT",
	[FCPIO_SGL_INVALID] = "FCPIO_SGL_INVALID",
	[FCPIO_MSS_INVALID] = "FCPIO_MSS_INVALID",
	[FCPIO_DATA_CNT_MISMATCH] = "FCPIO_DATA_CNT_MISMATCH",
	[FCPIO_FW_ERR] = "FCPIO_FW_ERR",
	[FCPIO_ITMF_REJECTED] = "FCPIO_ITMF_REJECTED",
	[FCPIO_ITMF_FAILED] = "FCPIO_ITMF_FAILED",
	[FCPIO_ITMF_INCORRECT_LUN] = "FCPIO_ITMF_INCORRECT_LUN",
	[FCPIO_CMND_REJECTED] = "FCPIO_CMND_REJECTED",
	[FCPIO_NO_PATH_AVAIL] = "FCPIO_NO_PATH_AVAIL",
	[FCPIO_PATH_FAILED] = "FCPIO_PATH_FAILED",
	[FCPIO_LUNMAP_CHNG_PEND] = "FCPIO_LUNHMAP_CHNG_PEND",
};

static void fnic_release_ioreq_buf(struct fnic *fnic, 
				   struct fnic_io_req *io_req)
{
	struct scsi_cmnd *sc = NULL;
	struct scatterlist *sg = NULL;
	
	/* 
	 * Unmap the buffer that contained the SGL 
	 * passed to device
	 */
	if (io_req->sgl_list_pa)
		pci_unmap_single
			(fnic->pdev,io_req->sgl_list_pa,
			 sizeof(io_req->sgl_list[0]) * io_req->sgl_cnt +
			 SCSI_SENSE_BUFFERSIZE,
			 PCI_DMA_TODEVICE);
	
	/* Unmap the SCSI buffer SGL from device*/
	sc = io_req->fsp->cmd;
	if (sc) {
		if (sc->use_sg) {
			sg = (struct scatterlist*)
				(sc->request_buffer);
			/* unmap the Scatter list*/
			pci_unmap_sg(fnic->pdev, 
				     sg, sc->use_sg,
				     sc->sc_data_direction);
		} else if (sc->request_bufflen)
			pci_unmap_single
				(fnic->pdev,
				 io_req->sgl_list[0].addr,
				 sc->request_bufflen,
				 sc->sc_data_direction);
	}
}

static inline void fnic_free_io_info(struct fnic *fnic, 
				     struct fnic_io_info *io_info)
{
	unsigned long flags;
	
	ASSERT(io_info->io_state == FNIC_IO_UNUSED);
	spin_lock_irqsave(&fnic->free_io_list_lock, flags);
	list_add(&io_info->free_io, &fnic->free_io_list);
	spin_unlock_irqrestore(&fnic->free_io_list_lock, flags);
}

struct fnic_io_info* fnic_get_io_info(struct fnic* fnic)
{
	unsigned long flags;
	struct fnic_io_info *io_info = NULL;

	spin_lock_irqsave(&fnic->free_io_list_lock, flags);
	if (!list_empty(&fnic->free_io_list)){
		io_info = (struct fnic_io_info*)fnic->free_io_list.next;
		list_del(fnic->free_io_list.next);
	}
	spin_unlock_irqrestore(&fnic->free_io_list_lock, flags);

	return io_info;
}

/* This function is called with the copy wq lock held and interrupts
 * disabled
 */
static int free_wq_copy_descs(struct fnic* fnic, struct vnic_wq_copy *wq)
{
	/* if no Ack received from firmware, then nothing to clean*/
	if (!fnic->fw_ack_recd[0])
		return 1;

	/* Update desc_available count based on number of freed descriptors
	 * Account for wraparound
	 */
	if (wq->to_clean_index <= fnic->fw_ack_index[0])
		wq->ring.desc_avail += (fnic->fw_ack_index[0]
					- wq->to_clean_index + 1);
	else
		wq->ring.desc_avail += (wq->ring.desc_count
					- wq->to_clean_index
					+ fnic->fw_ack_index[0] + 1);

	/* just bump clean index to ack_index+1 accounting for wraparound
	 * this will essentially free up all descriptors between
	 * to_clean_index and fw_ack_index, both inclusive
	 */
	wq->to_clean_index =
		(fnic->fw_ack_index[0] + 1) % wq->ring.desc_count;

	/* we have processed the acks received so far */
	fnic->fw_ack_recd[0] = 0;

	return 0;
}

int fnic_fw_reset_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	/* if avail descr less than threshold, free some */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);
	
	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto fw_reset_ioreq_end;
	}

	fnic_queue_wq_copy_desc_fw_reset(wq, IO_INDEX_INVALID);
	
 fw_reset_ioreq_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	return ret;
}

/* 
 * fnic_flogi_reg_handler
 * Routine to send flogi register msg to fw
 */
int fnic_flogi_reg_handler(struct fnic *fnic)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	/* if avail descr less than threshold, free some */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);
	
	if (!vnic_wq_copy_desc_avail(wq)) {
		ret = -EAGAIN;
		goto flogi_reg_ioreq_end;
	}
	
	fnic_queue_wq_copy_desc_flogi_reg(
		wq, 
		IO_INDEX_INVALID,
		FCPIO_FLOGI_REG_GW_DEST,
		fnic->s_id,		/* FC source id */
		net48_get((net48_t *) fnic->dest_addr) /* gateway mac */
		);
	
 flogi_reg_ioreq_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	return ret;
}

/* 
 * fnic_queue_wq_copy_desc
 * Routine to enqueue a wq copy desc
 */
static inline int fnic_queue_wq_copy_desc(struct fnic *fnic, 
					  struct vnic_wq_copy *wq, 
					  struct fc_scsi_pkt *fsp, 
					  unsigned int indx,
					  u_int8_t exch_flags)
{
	struct fnic_io_req *io_req = fsp->private;
	struct scsi_cmnd *sc = fsp->cmd;
	struct scatterlist *sg;
	u_int8_t flags, pri_tag = 0;
	unsigned int i;
	struct host_sg_desc *desc;
	u_int32_t sg_size = 0;
	int ret = 0;
	unsigned long intr_flags;
	char msg[2];
	struct scsi_lun fc_lun;

	if (sc->use_sg) {
		sg = (struct scatterlist*) (sc->request_buffer);
		/* map the Scatter list*/
		sg_size = pci_map_sg(fnic->pdev, sg, sc->use_sg,
				     sc->sc_data_direction);
		
		/* For each SGE, create a device desc entry */
		desc = io_req->sgl_list;
		for_each_sg(scsi_sglist(sc), sg, sg_size, i) {
			desc->addr = cpu_to_le64(sg_dma_address(sg));
			desc->len = cpu_to_le32(sg_dma_len(sg));
			desc->_resvd = 0;
			desc++;
		}

	} else if (sc->request_bufflen) {
		/* single buffer, no scatterlist*/
		host_sg_desc_enc(&io_req->sgl_list[0],
				 pci_map_single(
					 fnic->pdev,
					 sc->request_buffer, 
					 sc->request_bufflen,
					 sc->sc_data_direction),
				 sc->request_bufflen);
		sg_size = 1;
	}
	io_req->sgl_cnt = sg_size;
	sg_size *= sizeof(io_req->sgl_list[0]);

	io_req->sgl_list_pa = pci_map_single(fnic->pdev, &io_req->sgl_list[0], 
					     sg_size + SCSI_SENSE_BUFFERSIZE,
					     PCI_DMA_BIDIRECTIONAL);
	
	flags = (fsp->req_flags == OPENFC_SRB_READ) ? 
		FCPIO_ICMND_RDDATA : FCPIO_ICMND_WRDATA;
	int_to_scsilun(sc->device->lun, &fc_lun);
	pri_tag = FCPIO_ICMND_PTA_SIMPLE;
	if (scsi_populate_tag_msg(fsp->cmd, msg) && msg[0] == MSG_ORDERED_TAG)
		pri_tag = FCPIO_ICMND_PTA_ORDERED;
	
	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	/* if avail descr less than threshold, free some */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);

	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);
		fnic_release_ioreq_buf(fnic, io_req);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* Todo: We should not be using session params here without taking
	 * a lock. Alternative is to register handler with OpenFC for
	 * remote port events, and cache the port and session params like
	 * max payload, edtov, ratov in driver local cached port/sess. Then
	 * use the cached values to fill up the io_req. When the remote
	 * port/session goes down, make the local cached copy of the port/
	 * session as down
	 */
	fnic_queue_wq_copy_desc_icmnd_16(
		wq, 
		indx,                           /* IO req id */
		0,	                        /* lun map id */
		exch_flags,	                /* exch flags */
		io_req->sgl_cnt,	        /* SGL count */
		SCSI_SENSE_BUFFERSIZE,	        /* Sense len */
		io_req->sgl_list_pa,		/* ptr to sgl list */
	        io_req->sgl_list_pa + sg_size,	/* sense buffer */
		0,				/* scsi cmd ref, always 0 */
		pri_tag,			/* scsi pri and tag */
		flags,			        /* command flags */
		sc->cmnd,		        /* CDB */
		sc->request_bufflen,		/* data buf len */
		fc_lun.scsi_lun,		/* FCP 8 byte lun */
		fsp->d_id,			/* d_id */
		fsp->rp->rp_sess->fs_max_payload, /* max frame size */
		fsp->rp->rp_sess->fs_r_a_tov,  /* ratov */
		fsp->rp->rp_sess->fs_e_d_tov   /* edtov */
		);
	
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);

	return ret;
}

/* 
 * fnic_send_scsi
 * Routine to send a scsi cdb
 */
int fnic_send_scsi(struct fcdev *fc_dev, struct fc_scsi_pkt *fsp)
{
	struct fnic *fnic = fc_dev->drv_priv;
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct fnic_io_req *io_req = fsp->private;
	int ret;
	struct fnic_io_info* io_info = NULL;
	struct scsi_cmnd *sc = fsp->cmd;
	u_int8_t exch_flags = 0;
	void *p;
	unsigned long flags;

	memset(io_req, 0, sizeof(struct fnic_io_req));
	io_req->fsp = fsp;

	io_info = fnic_get_io_info(fnic);
	if (!io_info) {
		ret = SCSI_MLQUEUE_HOST_BUSY;
		goto send_scsi_end;
	}
	
	spin_lock_irqsave(&io_info->io_info_lock, flags);
	io_info->io_state = FNIC_IO_CMD_PENDING;
	io_info->fw_io_completed = 0;
	io_info->io_req = io_req;
	io_req->io_info = io_info;
	p = xchg(&((CMD_SP(sc))), io_req->fsp);
	spin_unlock_irqrestore(&io_info->io_info_lock, flags);
	
	if (fnic->enable_srr && (fsp->rp->rp_fcp_parm & FCP_SPPF_RETRY))
		exch_flags |= FCPIO_ICMND_SRFLAG_RETRY;

	io_req->sgl_list_type = 
		(sc->use_sg <= FNIC_DFLT_SG_DESC_CNT)? 
		(FNIC_SGL_CACHE_DFLT): (FNIC_SGL_CACHE_MAX);
	
	io_req->sgl_list = 
		kmem_cache_alloc(fnic_sgl_cache[io_req->sgl_list_type],
				 GFP_ATOMIC|GFP_DMA);

       	if (!io_req->sgl_list) {
		ret = SCSI_MLQUEUE_HOST_BUSY;
		spin_lock_irqsave(&io_info->io_info_lock, flags);
		io_req->io_info = NULL;
		io_info->io_req = NULL;
		io_info->io_state = FNIC_IO_UNUSED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		fnic_free_io_info(fnic, io_info);
		goto send_scsi_end;
	}
#if defined(__VMKLNX__)
	io_req->sgl_list_alloc = io_req->sgl_list;	/* address to free */
	{
		u_long ptr = (u_long) io_req->sgl_list;

		if (ptr % PALO_SG_DESC_ALIGN) {
			io_req->sgl_list = (struct host_sg_desc *)
			    (((u_long) ptr + PALO_SG_DESC_ALIGN - 1)
			    & ~(PALO_SG_DESC_ALIGN - 1));
		}
	}
#endif /* __VMKLNX */
	ASSERT((u_long) io_req->sgl_list % PALO_SG_DESC_ALIGN == 0);

	/* create copy wq desc and enqueue it*/
	ret = fnic_queue_wq_copy_desc(fnic, wq, fsp, io_info->indx,
				      exch_flags);
	if (ret) {
		kmem_cache_free(fnic_sgl_cache[io_req->sgl_list_type],
				io_req->sgl_list_alloc);
		spin_lock_irqsave(&io_info->io_info_lock, flags);
		io_req->io_info = NULL;
		io_info->io_req = NULL;
		io_info->io_state = FNIC_IO_UNUSED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		fnic_free_io_info(fnic, io_info);
	}

 send_scsi_end:
	return ret;
}

/* 
 * fnic_fcpio_fw_reset_cmpl_handler
 * Routine to handle fw reset completion
 */
static int fnic_fcpio_fw_reset_cmpl_handler(struct fnic *fnic,
					    struct fcpio_fw_req *desc)
{
	u_int8_t type;
	u_int8_t hdr_status;
	struct fcpio_tag tag;
	int ret = 0;
	struct fc_frame *flogi;
	unsigned long flags;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	
	/* Clean up all outstanding io requests */
	fnic_cleanup_io(fnic, IO_INDEX_INVALID);

	/* Update fnic state based on reset cmpl status */
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	flogi = fnic->flogi;
	fnic->flogi = NULL;

	/* fnic should be in FC_TRANS_ETH_MODE */
	if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE) {
		/* Check status of reset completion */
		if (!hdr_status) {
			printk(KERN_DEBUG DFX "reset cmpl success\n",
			       fnic->fnic_no);
			/* Ready to send flogi out */
			fnic->state = FNIC_IN_ETH_MODE;
		} else {
			printk(KERN_DEBUG DFX "fnic fw_reset : failed %s\n",
			       fnic->fnic_no, fcpio_status_str[hdr_status]);

			/* Unable to change to eth mode, cannot send out flogi
			 * Change state to fc mode, so that subsequent Flogi
			 * requests from libFC will cause more attempts to
			 * reset the firmware. Free the cached flogi
			 */
			fnic->state = FNIC_IN_FC_MODE;
			ret = -1;
		}
	} else {
		printk(KERN_DEBUG PFX "Unexpected state %s while processing"
		       " reset cmpl\n", fnic_state_str[fnic->state]);
		ret = -1;
	}

	/* Thread issuing host reset blocks till firmware reset is complete */
	if (fnic->reset_wait)
		complete(fnic->reset_wait);

	/* Thread removing device blocks till firmware reset is complete */
	if (fnic->remove_wait)
		complete(fnic->remove_wait);

	/* If fnic is in host reset or being removed, or fw reset failed
	 * free the flogi frame. Else, send it out
	 */
	if (fnic->remove_wait || fnic->reset_wait || ret) {
		fnic->flogi_oxid = FC_XID_UNKNOWN;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (flogi)
			fc_frame_free(flogi);
		goto reset_cmpl_handler_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (flogi)
		ret = fnic_send_frame(fnic, flogi);

 reset_cmpl_handler_end:
	return ret;
}

/* 
 * fnic_fcpio_flogi_reg_cmpl_handler
 * Routine to handle flogi register completion
 */
static int fnic_fcpio_flogi_reg_cmpl_handler(struct fnic *fnic,
						      struct fcpio_fw_req 
						      *desc)
{
	u_int8_t type;
	u_int8_t hdr_status;
	struct fcpio_tag tag;
	u_int8_t list_was_empty;
	int ret = 0;
	struct fc_frame *flogi_resp = NULL;
	unsigned long flags;
	struct fnic_event *event;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);

	/* Update fnic state based on status of flogi reg completion */
	spin_lock_irqsave(&fnic->fnic_lock, flags);

	flogi_resp = fnic->flogi_resp;
	fnic->flogi_resp = NULL;

	if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE) {

		/* Check flogi registration completion status */
		if (!hdr_status) {
			printk(KERN_DEBUG DFX "flog reg succeeded\n",
			       fnic->fnic_no);
			fnic->state = FNIC_IN_FC_MODE;
		} else {
			printk(KERN_DEBUG DFX "fnic flogi reg :failed %s\n",
			       fnic->fnic_no, fcpio_status_str[hdr_status]);
			fnic->state = FNIC_IN_ETH_MODE;
			ret = -1;
		}
	} else {
		printk(KERN_DEBUG PFX "Unexpected fnic state %s while"
		       " processing flogi reg completion\n",
		       fnic_state_str[fnic->state]);
		ret = -1;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Successful flogi reg cmpl, pass frame to LibFC */
	if (!ret && flogi_resp) {
		event = kmem_cache_alloc(fnic_ev_cache, GFP_ATOMIC);

		if (!event) {
			fc_frame_free(flogi_resp);
			ret = -1;
			goto reg_cmpl_handler_end;
		}

		memset(event, 0, sizeof(struct fnic_event));

		/* initialize the event structure */
		event->fp = flogi_resp;
		event->fnic = fnic;
		event->ev_type = EV_TYPE_FRAME;
		event->is_flogi_resp_frame = 1;

		/* insert it into event queue */
		spin_lock_irqsave(&fnic_eventlist_lock, flags);
		list_was_empty = list_empty(&fnic_eventlist);
		list_add_tail(&event->list, &fnic_eventlist);
		spin_unlock_irqrestore(&fnic_eventlist_lock, flags);
		if (list_was_empty)
			wake_up_process(fnic_thread);
	} else
		if (flogi_resp)
			fc_frame_free(flogi_resp);

 reg_cmpl_handler_end:
	return ret;
}

static inline int is_ack_index_in_range(struct vnic_wq_copy *wq,
					u_int16_t request_out)
{
	if (wq->to_clean_index <= wq->to_use_index) {
		if ((request_out < wq->to_clean_index) ||
		    (request_out >= wq->to_use_index)) {
			/* out of range, stale request_out index*/
			return 0;
		}
	} else {
		if ((request_out < wq->to_clean_index) &&
		    (request_out >= wq->to_use_index)) {
			/*out of range, stale request_out index*/
			return 0;
		} 
	}
	/* request_out index is in range*/
	return 1;
}


/* Mark that ack received and store the Ack index. If there are multiple
 * acks received before Tx thread cleans it up, the latest value will be
 * used which is correct behavior. This state should be in the copy Wq
 * instead of in the fnic
 */
static inline int fnic_fcpio_ack_handler(struct fnic *fnic, 
					 unsigned int cq_index, 
					 struct fcpio_fw_req *desc)
{
	struct vnic_wq_copy *wq;
	u_int16_t request_out =	 desc->u.ack.request_out;
	unsigned long flags;

	/* mark the ack state */
	wq = &fnic->wq_copy[cq_index - fnic->raw_wq_count - fnic->rq_count];
	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);

	if(is_ack_index_in_range(wq, request_out)) {
		fnic->fw_ack_index[0] = request_out;
		fnic->fw_ack_recd[0] = 1;
	}
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	return 0;
}

static inline void fnic_copy_sense(struct fc_scsi_pkt *fsp,
				   struct fnic_io_req *io_req, size_t len)
{
	fsp->sense_len = len;
	if (len) {
		if (len > SCSI_SENSE_BUFFERSIZE)
			len = SCSI_SENSE_BUFFERSIZE;
		memcpy(fsp->cmd->sense_buffer,
		       &io_req->sgl_list[io_req->sgl_cnt], len);
	}
}

/* 
 * fnic_fcpio_icmnd_cmpl_handler
 * Routine to handle icmnd completions
 */
static int fnic_fcpio_icmnd_cmpl_handler(struct fnic *fnic, 
					 struct fcpio_fw_req *desc)
{
	u_int8_t type;
	u_int8_t hdr_status;
	struct fcpio_tag tag;
	u_int32_t id;
	struct fnic_io_info *io_info;
	struct fc_scsi_pkt *fsp = NULL;
	struct fcpio_icmnd_cmpl *icmnd_cmpl;
	struct fnic_io_req *io_req = NULL;
	unsigned long flags;

	/* Decode the cmpl description to get the io_info id */
	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);
	if ((id & IO_INDEX_MASK) >= MAX_IO_REQ)
		return 0;

	io_info = &fnic->outstanding_io_info_list[id];

	spin_lock_irqsave(&io_info->io_info_lock, flags);

	/* If io_info is already free, then ignore completion */
	if (io_info->io_state == FNIC_IO_UNUSED) {
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto icmnd_cmpl_handler_end;
	}

	/* io_info is completed by firmware, whether succ or fail */
	io_info->fw_io_completed = 1;

	io_req = io_info->io_req;
	WARN_ON_ONCE(!io_req);
	if (!io_req) {
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto icmnd_cmpl_handler_end;
	}

	if (io_info->io_state == FNIC_IO_ABTS_PENDING) {
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto icmnd_cmpl_handler_end;
	}

	io_info->io_state = FNIC_IO_CMD_COMPLETE;

	icmnd_cmpl = &desc->u.icmnd_cmpl;

	fsp = io_req->fsp;
	BUG_ON(!fsp);

	switch (hdr_status) {
	case FCPIO_SUCCESS:
		fsp->cdb_status = icmnd_cmpl->scsi_status;
		fsp->scsi_resid = icmnd_cmpl->residual;
		fnic_copy_sense(fsp, io_req, icmnd_cmpl->sense_len);
		fsp->status_code = OPENFC_COMPLETE;
		break;

	case FCPIO_TIMEOUT:     /* request was timed out */
		fsp->status_code = OPENFC_CMD_TIME_OUT;
		break;

	case FCPIO_ABORTED:          /* request was aborted */
		fsp->status_code = OPENFC_CMD_ABORTED;
		break;

	case FCPIO_DATA_CNT_MISMATCH: /* recv/sent more/less data than exp. */
		fsp->cdb_status = icmnd_cmpl->scsi_status;
		fsp->scsi_resid = icmnd_cmpl->residual;
		fnic_copy_sense(fsp, io_req, icmnd_cmpl->sense_len);
		fsp->status_code = OPENFC_DATA_UNDRUN;
		break;

	case FCPIO_OUT_OF_RESOURCE: /* out of resources to complete request */
		printk(KERN_ERR DFX "hdr status = %s\n", fnic->fnic_no, 
		       fcpio_status_str[hdr_status]);
		fsp->io_status |= (SUGGEST_RETRY << 24);
		fsp->status_code = OPENFC_ERROR;
		break;

	case FCPIO_INVALID_HEADER:  /* header contains invalid data */
	case FCPIO_INVALID_PARAM:   /* some parameter in request is invalid */
	case FCPIO_REQ_NOT_SUPPORTED: /* request type is not supported */
	case FCPIO_IO_NOT_FOUND:    /* requested I/O was not found */
	case FCPIO_SGL_INVALID: /* request was aborted due to sgl error */
	case FCPIO_MSS_INVALID: /* request was aborted due to mss error */
	case FCPIO_FW_ERR:     /* request was terminated due to fw error */
	default:
		printk(KERN_ERR DFX "hdr status = %s\n", fnic->fnic_no, 
		       fcpio_status_str[hdr_status]);
		fsp->status_code = OPENFC_ERROR;
		break;
	}

	/* We are completing this IO, so break the link betn io_info, io_req*/
	io_info->io_req = NULL;
	io_req->io_info = NULL;
 	io_info->io_state = FNIC_IO_UNUSED;
	spin_unlock_irqrestore(&io_info->io_info_lock, flags);

	fnic_free_io_info(fnic, io_info);
	fnic_release_ioreq_buf(fnic, io_req);
	kmem_cache_free(fnic_sgl_cache[io_req->sgl_list_type], 
			io_req->sgl_list_alloc);
	io_req->sgl_list = NULL;

	/* Call OpenFC completion function to complete the IO */
	fsp->done(fsp);

 icmnd_cmpl_handler_end:
	return 0;
}

/* fnic_fcpio_itmf_cmpl_handler
 *
 * Routine to handle itmf completions
 */
static int fnic_fcpio_itmf_cmpl_handler(struct fnic *fnic, 
					struct fcpio_fw_req *desc)
{
	u_int8_t type;
	u_int8_t hdr_status;
	struct fcpio_tag tag;
	u_int32_t id;
	struct fnic_io_info *io_info;
	int ret = 0;
	unsigned long flags;

	fcpio_header_dec(&desc->hdr, &type, &hdr_status, &tag);
	fcpio_tag_id_dec(&tag, &id);
	if ((id & IO_INDEX_MASK) >= MAX_IO_REQ)
		return 0;

	io_info = &fnic->outstanding_io_info_list[id];
	
	spin_lock_irqsave(&io_info->io_info_lock, flags);

	switch(io_info->io_state) {
	
	case FNIC_IO_ABTS_PENDING:
		printk(KERN_DEBUG DFX "itmf_cmpl_handler: abts status = %s\n", 
		       fnic->fnic_no, fcpio_status_str[hdr_status]);

		io_info->io_state = FNIC_IO_ABTS_COMPLETE;
		io_info->abts_status = hdr_status;
		if (io_info->io_req->abts_done)
			complete(io_info->io_req->abts_done);

		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		break;
	
	case FNIC_IO_CMD_PENDING:
		printk(KERN_DEBUG DFX "itmf_cmpl_handler: lr status = %s\n",
		       fnic->fnic_no, fcpio_status_str[hdr_status]);
		
		io_info->io_state = FNIC_IO_CMD_COMPLETE;
		io_info->lr_status = hdr_status;
		if (io_info->io_req->dr_done)
			complete(io_info->io_req->dr_done);

		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		break;
	default:
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		break;
	}
	
	return ret;
}

/* 
 * fnic_fcpio_cmpl_handler
 * Routine to service the cq for wq_copy
 */
static int fnic_fcpio_cmpl_handler(struct vnic_dev *vdev, 
				   unsigned int cq_index, 
				   struct fcpio_fw_req *desc)
{
	struct fnic *fnic = vnic_dev_priv(vdev);
	int ret = 0;

	switch (desc->hdr.type) {
	case FCPIO_ACK: /* fw copied copy wq desc to its queue*/
		ret = fnic_fcpio_ack_handler(fnic, cq_index, desc);
		break;
	case FCPIO_ICMND_CMPL: /* fw completed a command */
		ret = fnic_fcpio_icmnd_cmpl_handler(fnic, desc);
		break;
	case FCPIO_ITMF_CMPL: /* fw completed itmf (abort cmd, lun reset)*/
		ret = fnic_fcpio_itmf_cmpl_handler(fnic, desc);
		break;
	case FCPIO_FLOGI_REG_CMPL: /* fw completed flogi_reg */
		ret = fnic_fcpio_flogi_reg_cmpl_handler(fnic, desc);
		break;
	case FCPIO_RESET_CMPL: /* fw completed reset */
		ret = fnic_fcpio_fw_reset_cmpl_handler(fnic, desc);
		break;
	default:
		printk(KERN_DEBUG DFX "firmware completion type %d\n",
		       fnic->fnic_no, desc->hdr.type);
		break;
	}

	return ret;
}

/* 
 * fnic_wq_copy_cmpl_handler
 * Routine to process wq copy
 */
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int copy_work_to_do)
{
	unsigned int wq_work_done = 0;
	unsigned int i, cq_index;
	unsigned int cur_work_done;

	for (i = 0; i < fnic->wq_copy_count; i++) {
		cq_index = i + fnic->raw_wq_count + fnic->rq_count;
		cur_work_done = vnic_cq_copy_service(&fnic->cq[cq_index], 
						     fnic_fcpio_cmpl_handler,
						     copy_work_to_do);
		wq_work_done += cur_work_done;
	}
	return wq_work_done;
}

void fnic_cleanup_io(struct fnic *fnic, int exclude_id)
{
	unsigned int i;
	struct fnic_io_info *io_info;
	struct fnic_io_req *io_req;
	unsigned long flags = 0;

	for (i = 0; i < MAX_IO_REQ; i++) {
		if (i == exclude_id)
			continue;

		io_info = &fnic->outstanding_io_info_list[i];
		if (io_info->io_state == FNIC_IO_UNUSED)
			continue;
		
		spin_lock_irqsave(&io_info->io_info_lock, flags);
		if (io_info->io_state == FNIC_IO_UNUSED) {
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		} 

		io_req = io_info->io_req;
		io_info->io_req = NULL;
		io_info->io_state = FNIC_IO_UNUSED;
	
		if (io_req)
			io_req->io_info = NULL;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
	
		fnic_free_io_info(fnic, io_info);

		/* If there is a io_req associated with this io_info, then
		 * free the corresponding state
		 */
		if (io_req) {
			/* if io_req has fsp, then complete it upto SCSI*/
			if (io_req->fsp) {
				fnic_release_ioreq_buf(fnic, io_req);
				kmem_cache_free
					(fnic_sgl_cache[io_req->sgl_list_type],
					 io_req->sgl_list_alloc);
				io_req->sgl_list = NULL;
				io_req->fsp->status_code = OPENFC_ERROR;
				io_req->fsp->cdb_status = -1;
				printk(KERN_DEBUG DFX "fnic_cleanup_io:"
				       " DID_ERROR\n", fnic->fnic_no);
				io_req->fsp->done(io_req->fsp);
			} 
		} /* if io_req */
	}
	return;
}

void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq, 
				  struct fcpio_host_req *desc)
{
	u_int32_t id;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);
	struct fnic_io_info *io_info;
	struct fnic_io_req *io_req;
	unsigned long flags;
	
	/* get the tag reference*/
	fcpio_tag_id_dec(&desc->hdr.tag, &id);
	if ((id & IO_INDEX_MASK) >= MAX_IO_REQ)
		return;

	/* Get the IO context which this desc refers to*/
	io_info = &fnic->outstanding_io_info_list[id];
	
	/* fnic interrupts are turned off by now */
	spin_lock_irqsave(&io_info->io_info_lock, flags);

	if(io_info->io_state == FNIC_IO_UNUSED) {
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		return;
	}
	io_req = io_info->io_req;
	io_info->io_req = NULL;
	if (io_req)
		io_req->io_info = NULL;
	io_info->io_state = FNIC_IO_UNUSED;

	spin_unlock_irqrestore(&io_info->io_info_lock, flags);

	fnic_free_io_info(fnic, io_info);

	/* If there is a io_req associated with this io_info, then
	 * free the corresponding state
	 */
	if (io_req) {
		/* if io_req has fsp, then complete it upto SCSI*/
		if (io_req->fsp) {
			fnic_release_ioreq_buf(fnic, io_req);
			kmem_cache_free
				(fnic_sgl_cache[io_req->sgl_list_type],
				 io_req->sgl_list_alloc);
			io_req->sgl_list = NULL;
			io_req->fsp->status_code = OPENFC_HRD_ERROR;
			io_req->fsp->cdb_status = -1;
			io_req->fsp->done(io_req->fsp);
		}
	} /* if io_req */
}

void fnic_cleanup_scsi(struct fc_scsi_pkt *cleanup_fsp)
{
	struct fnic *fnic = cleanup_fsp->openfcp->dev->drv_priv;
	unsigned long flags;
	enum fnic_state old_state;

	/* issue fw reset */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (fnic_fw_reset_handler(fnic)) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
	}

	return;
}

static inline int fnic_queue_abort_io_req(struct fnic *fnic, 
					  struct fnic_io_req *io_req,
					  u_int32_t task_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct scsi_lun fc_lun;
	unsigned long flags;
	struct fc_scsi_pkt *fsp = io_req->fsp;
	struct fnic_io_info *io_info = io_req->io_info;

	/* Now queue the abort command to firmware */
	spin_lock_irqsave(&fnic->wq_copy_lock[0], flags);
	
	/* if avail descr less than threshold, free some */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);
	
	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);
		return 1;
	} 

	int_to_scsilun(fsp->cmd->device->lun, &fc_lun);

	fnic_queue_wq_copy_desc_itmf(
		wq,
		io_info->indx,          /* id for abts ioinfo*/
		0,                      /* lun map id */
		task_req,               /* task mgmt type */
		io_info->indx,          /* id for aborted IO */
		fc_lun.scsi_lun,        /* FCP lun addr */
		fsp->d_id,              /* d_id */
		fnic->config.ra_tov,    /* RATOV in msecs */
		fnic->config.ed_tov     /* EDTOV in msecs */
		);
	
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], flags);

	return 0;
}

static void fnic_block_error_handler(struct scsi_cmnd *sc)
{
	struct Scsi_Host *shost = sc->device->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc->device));
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	while (rport->port_state == FC_PORTSTATE_BLOCKED) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		msleep(1000);
		spin_lock_irqsave(shost->host_lock, flags);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	return;
}

/* 
 * This function is exported to OpenFC for sending abort cmnds. 
 * The fsp parameter points to the original command which is to be
 * aborted. A SCSI IO is represented by a io_req and io_info in the driver.
 * The ioreq is part of the fsp and is a link to the SCSI Cmd, thus a link
 * with the ULP's IO. The io_info is local to the driver, and is used to keep
 * state with the firmware, thus a link to the IO below the driver.
 * 
 */
int fnic_abort_cmd(struct fcdev *fc_dev, struct fc_scsi_pkt *fsp)
{
	struct fnic *fnic;
	struct vnic_wq_copy *wq;
	struct fnic_io_req *io_req;
	struct fnic_io_info *io_info;
	unsigned long flags;
	int ret = SUCCESS;
	u_int32_t task_req;
	struct scsi_cmnd *sc;
	struct fc_rport *rport;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	
	BUG_ON(!fc_dev);
	BUG_ON(!fsp);

	fnic = fc_dev->drv_priv;
	wq = &fnic->wq_copy[0];
	
	io_req = fsp->private;
	sc  = fsp->cmd;
	io_info = io_req->io_info;

	printk(KERN_DEBUG DFX "fnic_abort_cmd called lun 0x%llx, d_id 0x%x\n", 
	       fnic->fnic_no, fsp->lun, fsp->d_id);

	fnic_block_error_handler(sc);

	/* if io_req points to a NULL io_info, cmd has already completed. 
	 * There could be race between the two events - the command times
	 * out at the SCSI layer, and so SCSI eh thread issues an abort,
	 * at the same time the firmware completes the command. If the
	 * command is already completed by the fw/driver, just return
	 * SUCCESS from here. This means that the abort succeeded. In the SCSI
	 * ML, since the timeout for command has happened, the completion
	 * wont actually complete the command above SCSI, and it will be
	 * considered as an aborted command
	 */
	if (!io_info) {
		fsp->state |= OPENFC_SRB_ABORTED;
		goto fnic_abort_cmd_end;
	} 

	spin_lock_irqsave(&io_info->io_info_lock, flags);
	
	/* SCSI eh thread has kicked off the abort. But, its possible
	 * that before we could grab the io_info lock, the command
	 * completed, and it got reused for a different IO request
	 * by the driver. So make sure we are still refering to the 
	 * same SCSI IO for which this abort is issued
	 */
	if (io_info->io_req != io_req) {
		fsp->state |= OPENFC_SRB_ABORTED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto fnic_abort_cmd_end;
	}
	/* Command is completed already */
	if ((io_info->io_state == FNIC_IO_CMD_COMPLETE) ||
	    (io_info->io_state == FNIC_IO_UNUSED)) {
		fsp->state |= OPENFC_SRB_ABORTED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto fnic_abort_cmd_end;
	}
	if (io_info->io_state == FNIC_IO_ABTS_COMPLETE)
		goto after_wait;
	if (io_info->io_state == FNIC_IO_ABTS_PENDING) {
		io_req->abts_done = &tm_done;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		goto wait_pending;
	}
	
	/* If the firmware completes the command after this point,
	 * the completion wont be done till mid-layer, since abort
	 * has already started. In the completion of the command
	 * from the firmware, io_state is checked to see if abts_pending
	 */
	io_info->io_state = FNIC_IO_ABTS_PENDING;
	
	io_req->abts_done = &tm_done;
	
	spin_unlock_irqrestore(&io_info->io_info_lock, flags);
	
	/* Check if the target is still connected to fabric or not */
	rport = starget_to_rport(scsi_target(sc->device));
	if (fc_remote_port_chkready(rport) == 0) {
		printk(KERN_DEBUG DFX "Abort task and send abts\n",
		       fnic->fnic_no);
		task_req = FCPIO_ITMF_ABT_TASK;
	} else {
		printk(KERN_DEBUG DFX "Abort task and terminate\n",
		       fnic->fnic_no);
		task_req = FCPIO_ITMF_ABT_TASK_TERM;
	}

	printk(KERN_DEBUG DFX "io id %d, io_info state %d"
	       " FCID 0x%x, lun %llx\n",
	       fnic->fnic_no, io_info->indx, io_info->io_state, 
	       fsp->d_id, fsp->lun);
	
	if (sc->cmnd)
		printk(KERN_DEBUG DFX "lba 0x%.2x%.2x%.2x%.2x\n", 
		       fnic->fnic_no, sc->cmnd[2],
		       sc->cmnd[3], sc->cmnd[4],
		       sc->cmnd[5]); 
	
	/* Now queue the abort command to firmware */
	if (fnic_queue_abort_io_req(fnic, io_req, task_req)) {
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}

	/* Wait for completion of abort io req to fw
	 * SCSI Error Handling does all aborts synchronously.
	 * If the driver returns back with success, then that
	 * means the command is completely forgotten by
	 * the target, the firmware, hardware, and driver. If
	 * the driver returns failure, then one or more of the
	 * bottom layers did not forget about the command
	 */
	
	/* Wait for the abort to complete. Once the firmware
	 * completes the abort command, it will wake up this
	 * thread. 
	 * Timeout: 2*fnic->config.ra_tov + fnic->config.ed_tov
	 */
wait_pending:
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies
				    (2*fnic->config.ra_tov +
				     fnic->config.ed_tov));
	
	spin_lock_irqsave(&io_info->io_info_lock, flags);
	if (io_info->io_state == FNIC_IO_UNUSED) {
		printk(KERN_DEBUG DFX "abort info freed while waiting\n",
		       fnic->fnic_no);
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		ret = SUCCESS;
		goto fnic_abort_cmd_end;
	}
	io_req->abts_done = NULL;

after_wait:
	/* Check the abort status*/
	if (io_info->io_state == FNIC_IO_ABTS_PENDING) {
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		ret = FAILED;
		goto fnic_abort_cmd_end;
	}
	
	if (io_info->abts_status != FCPIO_SUCCESS)
		ret = FAILED;

	/* firmware completed the abort - succ/fail */
	fsp->state |= OPENFC_SRB_ABORTED;
	fsp->status_code = OPENFC_CMD_ABORTED;
	io_req->io_info = NULL;
	io_info->io_req = NULL;
	io_info->io_state = FNIC_IO_UNUSED;
	spin_unlock_irqrestore(&io_info->io_info_lock, flags);
	
	fnic_free_io_info(fnic, io_info);
	fnic_release_ioreq_buf(fnic, io_req);
	if (io_req->sgl_list) {
		kmem_cache_free(fnic_sgl_cache[io_req->sgl_list_type], 
				io_req->sgl_list_alloc);
		io_req->sgl_list = NULL;
	}

fnic_abort_cmd_end:
	printk(KERN_DEBUG DFX "Returning from abort cmd %s\n",
	       fnic->fnic_no, ((ret == SUCCESS)? "SUCCESS" : "FAILED"));
	return ret;
}

/* fnic_queue_lr_io_req
 * lun reset helper routine
 */
static inline int fnic_queue_lr_io_req(struct fnic *fnic, 
				       struct fnic_io_req *lr_req)
{
	struct vnic_wq_copy *wq = &fnic->wq_copy[0];
	struct scsi_lun fc_lun;
	int ret = 0;
	unsigned long intr_flags;
	struct fc_scsi_pkt * fsp = lr_req->fsp;
	
	spin_lock_irqsave(&fnic->wq_copy_lock[0], intr_flags);

	/* if avail descr less than threshold, free some */
	if (vnic_wq_copy_desc_avail(wq) <= fnic->wq_copy_desc_low[0])
		free_wq_copy_descs(fnic, wq);
	
	if (unlikely(!vnic_wq_copy_desc_avail(wq))) {
		ret = -EAGAIN;
		goto lr_io_req_end;
	}

	/* fill in the lun info*/
	int_to_scsilun(fsp->cmd->device->lun, &fc_lun);

	fnic_queue_wq_copy_desc_itmf(
		wq, 
		lr_req->io_info->indx,  /* host request id */
		0,                      /* lun map id */
		FCPIO_ITMF_LUN_RESET,   /* logical unit reset task management*/
		IO_INDEX_INVALID,       /* id not specified */
		fc_lun.scsi_lun,        /* FCP 8 byte lun addr */
		lr_req->fsp->d_id,      /* d_id */
		fnic->config.ra_tov,    /* RATOV in msecs */
		fnic->config.ed_tov     /* EDTOV in msecs */
		);
	
 lr_io_req_end:
	spin_unlock_irqrestore(&fnic->wq_copy_lock[0], intr_flags);

	return ret;
}

/* Wait for pending aborts on a LUN to complete */
static int fnic_clean_pending_aborts(struct fnic *fnic,
				     struct fc_scsi_pkt *lr_fsp)
{
	unsigned int i;
	struct fnic_io_info *io_info;
	unsigned long flags;
	struct fc_scsi_pkt *fsp = NULL;
	struct fnic_io_req *io_req = NULL;
	int ret = 0; /* all commands aborted on the LUN */
	DECLARE_COMPLETION_ONSTACK(tm_done);
	
	for (i = 0; i < MAX_IO_REQ; i++) {
		io_info = &fnic->outstanding_io_info_list[i];
		
		if (io_info->io_state == FNIC_IO_UNUSED)
			continue;
		
		spin_lock_irqsave(&io_info->io_info_lock, flags);

		if (io_info->io_state == FNIC_IO_UNUSED) {
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		}
		
		/* Ignore the IO corresponding to Lun reset */
		if(io_info->io_req->fsp == lr_fsp) {
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		}

		io_req = io_info->io_req;
		fsp = io_req->fsp;
		if (!fsp) {
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		}
		/* Check if cmd belongs to this lun */
		if ((fsp->id != lr_fsp->id) || (fsp->lun != lr_fsp->lun)) {
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		}

		io_req->abts_done = &tm_done;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);

		if (fnic_queue_abort_io_req(fnic, io_req,
					    FCPIO_ITMF_ABT_TASK_TERM)) {
			ret = 1;
			break;
		}

		wait_for_completion_timeout(&tm_done,
					    msecs_to_jiffies
					    (fnic->config.ed_tov));
		
		spin_lock_irqsave(&io_info->io_info_lock, flags);
		if (io_info->io_state == FNIC_IO_UNUSED) {
			printk(KERN_DEBUG DFX "clean pending info "
			       "freed while waiting\n", fnic->fnic_no);
			spin_unlock_irqrestore(&io_info->io_info_lock, flags);
			continue;
		}
		io_req->abts_done = NULL;
		if (io_info->io_state == FNIC_IO_ABTS_PENDING)
			ret = 1; /* fail lun reset */
		else
			io_info->io_state = FNIC_IO_UNUSED;
		
		spin_unlock_irqrestore (&io_info->io_info_lock, flags);
		if (ret)
			break;

		/* Clean up all driver and OpenFC state for the cmd */
		fnic_free_io_info(fnic, io_info);
		fnic_release_ioreq_buf(fnic, io_req);
		kmem_cache_free(fnic_sgl_cache[io_req->sgl_list_type], 
				io_req->sgl_list_alloc);
		io_req->sgl_list = NULL;
		fsp->status_code = OPENFC_CMD_ABORTED;
		fsp->state = OPENFC_SRB_FREE;
		if (fsp->cmd)
			CMD_SP(fsp->cmd) = NULL;
		fsp->cmd = NULL;
		openfc_free_scsi_pkt(fsp);
	}

	return ret;
}

/* SCSI Eh thread issues a Lun Reset when one or more commands on a LUN
 * fail to get aborted. It calls OpenFC's eh_device_reset with a SCSI command
 * on the LUN. OpenFC allocates a new FSP, points that to the SCSI command
 * and calls the transport driver to issue the LUN Reset
 */
int fnic_device_reset(struct fcdev *fc_dev, struct fc_scsi_pkt *fsp)
{
	struct fnic *fnic;
	struct fnic_io_req *io_req;
	struct fnic_io_info *io_info;
	int ret = FAILED;
	unsigned long flags;
	struct scsi_cmnd *sc;
	struct fc_rport *rport;
	DECLARE_COMPLETION_ONSTACK(tm_done);
	
	BUG_ON(!fc_dev);
	BUG_ON(!fsp);
	
	fnic = fc_dev->drv_priv;
	io_req = fsp->private;
	sc = fsp->cmd;

	printk(KERN_DEBUG DFX "Device reset called lun 0x%llx, d_id 0x%x\n", 
	       fnic->fnic_no, fsp->lun, fsp->d_id);
	
	fnic_block_error_handler(sc);

	/* Check for link down. If the link is down, then we cannot
	 * issue the Lun Reset. Fail the LUN reset
	 */
	if (fc_dev->fd_link_status == TRANS_LINK_DOWN)
		goto fnic_device_reset_end;

	/* Check if remote port ready for device reset */
	rport = starget_to_rport(scsi_target(sc->device));
	if (fc_remote_port_chkready(rport)) {
		printk(KERN_DEBUG PFX "remote port not ready\n");
		goto fnic_device_reset_end;
	}
	
	memset(io_req, 0, sizeof(struct fnic_io_req));
	io_req->fsp = fsp;

	io_info = fnic_get_io_info(fnic);
	if (!io_info)
		goto fnic_device_reset_end;

	spin_lock_irqsave(&io_info->io_info_lock, flags);
	io_info->io_state = FNIC_IO_CMD_PENDING;
	io_info->fw_io_completed = 0;
	io_info->io_req = io_req;
	io_req->io_info = io_info;
	io_req->dr_done = &tm_done;
	spin_unlock_irqrestore(&io_info->io_info_lock, flags);

	printk(KERN_DEBUG DFX "Device reset id %d\n", fnic->fnic_no, 
	       io_info->indx);

	/* issue the lun reset */
	if (fnic_queue_lr_io_req(fnic, io_req)) {
		spin_lock_irqsave(&io_info->io_info_lock, flags);
		io_req->io_info = NULL;
		io_info->io_req = NULL;
		io_info->io_state = FNIC_IO_UNUSED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		fnic_free_io_info(fnic, io_info);
		goto fnic_device_reset_end;
	}

	/* Issued the Lun Reset. Now wait for firmware to complete the 
	 * LUN Reset. When the firmware completes the Lun Reset, this
	 * thread gets woken up. Else it times out after FNIC_LUN_RESET_TO
	 */
	wait_for_completion_timeout(&tm_done,
				    msecs_to_jiffies(FNIC_LUN_RESET_TIMEOUT));

	spin_lock_irqsave(&io_info->io_info_lock, flags);
	if (io_info->io_state == FNIC_IO_UNUSED) {
		printk(KERN_DEBUG DFX "device reset info freed while waiting\n",
		       fnic->fnic_no);
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		ret = SUCCESS;
		goto fnic_device_reset_end;
	}
	io_req->dr_done = NULL;
	io_info->io_req = NULL;
	io_req->io_info = NULL;

	if (io_info->io_state == FNIC_IO_CMD_PENDING ||
	    io_info->lr_status != FCPIO_SUCCESS) {
		printk(KERN_DEBUG PFX "Device reset %s\n",
		       (io_info->io_state == FNIC_IO_CMD_PENDING) ?
		       "timed out" : "completed - failed");
		io_info->io_state = FNIC_IO_UNUSED;
		spin_unlock_irqrestore(&io_info->io_info_lock, flags);
		fnic_free_io_info(fnic, io_info);
		goto fnic_device_reset_end;
	}

	printk(KERN_DEBUG DFX "Device reset completed - success\n",
	       fnic->fnic_no);
	io_info->io_state = FNIC_IO_UNUSED;

	spin_unlock_irqrestore(&io_info->io_info_lock, flags);
	fnic_free_io_info(fnic, io_info);

	/* Clean up any aborts on this lun that have still not
	 * completed. If any of these fail, then LUN reset fails.
	 * clean_pending_aborts cleans all cmds on this lun except
	 * the lun reset cmd. If all cmds get cleaned, then clean
	 * lun reset, otherswise fail lun reset, and higher levels
	 * of EH will kick in
	 */
	if (!fnic_clean_pending_aborts(fnic, fsp))
		ret = SUCCESS;
	else
		printk(KERN_DEBUG DFX "Device reset failed -"
		       " cleaning all pending abort cmds failed\n",
		       fnic->fnic_no);

fnic_device_reset_end:
	printk(KERN_DEBUG DFX "Returning from device reset %s \n",
	       fnic->fnic_no, ((ret == SUCCESS)? "SUCCESS" : "FAILED"));
	return ret;
}

/* SCSI Error handling calls eh_host_reset in OpenFC if one of the Lun
 * Resets does not complete successfully. OpenFC calls fcs_reset to reset
 * sessions, local port, exchanges, then calls transport driver's host
 * reset function. If host reset completes successfully, and if link is up,
 * then Fabric login begins.
 *
 * Host Reset is the highest level of error recovery. If this fails, then
 * host is offlined by SCSI. Issue a firmware reset to clean up the firmware's
 * exchanges. After firmware reset completes, or times out, clean up driver
 * IO state. Return reset status to SCSI
 */
int fnic_host_reset(struct fcdev *fc_dev, struct fc_scsi_pkt * fsp)
{
	struct fnic *fnic = fc_dev->drv_priv;
	unsigned long flags;
	int ret = 0;
	enum fnic_state old_state;
	DECLARE_COMPLETION_ONSTACK(reset_wait);

	printk(KERN_DEBUG DFX "Host reset called\n", fnic->fnic_no);

	/* Issue firmware reset if not already in reset issued state*/
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->reset_wait = &reset_wait;
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Issue firmware reset */
	if (fnic_fw_reset_handler(fnic)) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		ret = -1;
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		fnic->reset_wait = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto fnic_reset_end;
	}

	/* fw reset is issued, now wait for it to complete*/
	wait_for_completion_timeout(&reset_wait,
				    msecs_to_jiffies(FNIC_HOST_RESET_TIMEOUT));


	/* Check for status */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->reset_wait = NULL;
	if (fnic->state != FNIC_IN_ETH_MODE)
		ret = -1;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

 fnic_reset_end:
	printk(KERN_DEBUG DFX "Returning from fnic reset %s\n",
	       fnic->fnic_no, (ret == 0) ? "SUCCESS" : "FAILED");

	return ret;
}
