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
 * $Id: fnic_rx.c 16305 2008-07-27 04:05:18Z ajoglekar $
 */


#ident "$Id: fnic_rx.c 16305 2008-07-27 04:05:18Z ajoglekar $"

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include "kcompat.h"

#include "fc_types.h"
#include "fnic_io.h"
#include "fnic.h"

#include "sa_assert.h"
#include "fc_frame.h"
#include "fc_els.h"
#include "fc_fcoe.h"
#include "fcp_hdr.h"

#include "cq_enet_desc.h"
#include "cq_exch_desc.h"

struct task_struct *fnic_thread;
LIST_HEAD(fnic_eventlist);
spinlock_t	fnic_eventlist_lock;

extern char *fnic_state_str[];
/* 
 * This thread polls a list for incoming FCS frames. If it gets a frame,
 * it upcalls into OpenFC to handle the frame. 
 */
int fnic_fc_thread(void *arg)
{
	struct fnic_event *ev;
	unsigned long flags;
	
	set_user_nice(current, -20);
	while (!kthread_should_stop()) {
		spin_lock_irqsave(&fnic_eventlist_lock, flags);
		
		if (!list_empty(&fnic_eventlist)){
			/* Get the first element from list*/
			ev = (struct fnic_event*) fnic_eventlist.next;
			list_del(fnic_eventlist.next);
			spin_unlock_irqrestore(&fnic_eventlist_lock, flags);

			if(ev->ev_type == EV_TYPE_FRAME) {
				if (ev->is_flogi_resp_frame) {
					struct fnic *fnic = ev->fnic;
					
					spin_lock_irqsave(&fnic->fnic_lock,
							  flags);
					vnic_dev_add_addr(fnic->vdev,
							  fnic->data_src_addr);
					spin_unlock_irqrestore
					  (&fnic->fnic_lock, flags);
				}
				openfc_rcv(ev->fnic->fc_dev, ev->fp);
			} else if (ev->ev_type == EV_TYPE_LINK_UP)
				openfc_linkup(ev->fnic->fc_dev);
			else if (ev->ev_type == EV_TYPE_LINK_DOWN)
				openfc_linkdown(ev->fnic->fc_dev);
			
			ev->fp = NULL;
			ev->fnic = NULL;
			/* OpenFC releases the FC frame, we just release
			   the event
			 */
			kmem_cache_free(fnic_ev_cache, ev);
			
		} else { /* no frame enqueued*/
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&fnic_eventlist_lock, flags);
			schedule();
			set_current_state(TASK_RUNNING);
		}
	}
	return 0;
}


static inline void fnic_import_rq_fc_frame( struct fc_frame *fp, 
					    unsigned int len, uint8_t sof, 
					    u_int8_t eof)
{
	/* In FC Lif mode, we get FC frames with all other headers stripped*/
	fp->fr_hdr = (struct fc_frame_header *)(fp + 1);
	fp->fr_len = (u_int16_t)len;
	fp->fr_eof = eof;
	fp->fr_sof = FC_FCOE_DECAPS_SOF(sof);

	return;
}


static inline void fnic_import_rq_eth_pkt( struct fc_frame *fp, 
					   unsigned int len, 
					   u_int16_t *ret_ox_id)
{
	struct ether_header *eth_hdr;
	struct fcoe_vlan_hdr *vlan_hdr;
	struct fcoe_hdr *fcoe_hdr;
	u_int16_t ether_type;
	struct fcoe_crc_eof *fcoe_crc_eof;
	u_int32_t payload_len;
	void* data_buf;
	void* trans_hdr = fp + 1;

	parse_fcoe_hdr(trans_hdr, len, &eth_hdr, &vlan_hdr, &fcoe_hdr, 
		       &fp->fr_hdr, &ether_type, &data_buf, &payload_len,
		       &fcoe_crc_eof, ret_ox_id, (u_int8_t *)&fp->fr_sof, 
		       (u_int8_t *)&fp->fr_eof);
	
	/* fcs expects frm_len to be the FC frame without FC CRC*/
	fp->fr_len = payload_len + sizeof(struct fc_frame_header);
	return;
}

static inline int fnic_handle_flogi_resp(struct fnic *fnic, 
					 struct fc_frame *fp)
{
	u_int64_t mac;
	struct ether_header *eth_hdr = (struct ether_header *) (fp + 1);
	int ret = 0;
	unsigned long flags;
	struct fc_frame *old_flogi_resp = NULL;
	struct fc_frame_header *fh;

	fh = fc_frame_header_get(fp);

	spin_lock_irqsave(&fnic->fnic_lock, flags);

	if (fnic->state == FNIC_IN_ETH_MODE) {

		/* Check if oxid matches on taking the lock. A new Flogi
		 * issued might have changed the fnic cached oxid
		 */
		if (fnic->flogi_oxid != net16_get(&fh->fh_ox_id)) {
			printk(KERN_DEBUG PFX "Flogi response oxid not"
			       " matching cached oxid, dropping frame\n");
			ret = -1;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			fc_frame_free(fp);
			goto handle_flogi_resp_end;
		}

		/* Drop older cached flogi response frame, cache this frame */
		old_flogi_resp = fnic->flogi_resp;
		fnic->flogi_resp = fp;
		fnic->flogi_oxid = FC_XID_UNKNOWN;

		/*
		 * this frame is part of flogi get the src mac addr from this
		 * frame if the src mac is fcoui based then we mark the
		 * address mode flag to use fcoui base for dst mac addr
		 * otherwise we have to store the fcoe gateway addr
		 */
		mac = net48_get((net48_t *) eth_hdr->ether_shost);
		
		if ((mac >> 24) == FC_FCOE_OUI)
			fnic->fcoui_mode = 1;
		else {
			fnic->fcoui_mode = 0;
			net48_put((net48_t *)fnic->dest_addr, mac);
		}

		/* Except for Flogi frame, all outbound frames from us have the
		 * Eth Src address as FC_FCOE_OUI"our_sid". Flogi frame uses
		 * the vnic MAC address as the Eth Src address
		 */
		fc_fcoe_set_mac(fnic->data_src_addr,
				(net24_t *)&fp->fr_hdr->fh_d_id);

		/* We get our s_id from the d_id of the flogi resp frame */
		fnic->s_id = net24_get(&fp->fr_hdr->fh_d_id);
		
		/* Change state to flogi reg issued */
		fnic->state = FNIC_IN_ETH_TRANS_FC_MODE;

	} else {
		printk(KERN_DEBUG PFX "Unexpected fnic state %s while"
		       " processing flogi resp\n",
		       fnic_state_str[fnic->state]);
		ret = -1;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		fc_frame_free(fp);
		goto handle_flogi_resp_end;
	}

	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	/* Drop older cached frame */
	if (old_flogi_resp)
		fc_frame_free(old_flogi_resp);
	
	/* 
	 * send flogi reg request to firmware, this will put the lif in
	 * FC mode
	 */
	ret = fnic_flogi_reg_handler(fnic);
	
	if (ret < 0) {
		int free_fp = 1;
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		/* free the frame is some other thread is not
		 * pointing to it
		 */
		if (fnic->flogi_resp != fp)
			free_fp = 0;
		else
			fnic->flogi_resp = NULL;

		if (fnic->state == FNIC_IN_ETH_TRANS_FC_MODE)
			fnic->state = FNIC_IN_ETH_MODE;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		if (free_fp)
			fc_frame_free(fp);
	}

 handle_flogi_resp_end:
	return ret;
}

/* Returns 1 for a response that matches cached flogi oxid */
static inline int is_matching_flogi_resp_frame(struct fnic *fnic,
					       struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	int ret = 0;
	u32 f_ctl;

	fh = fc_frame_header_get(fp);
	f_ctl = net24_get(&fh->fh_f_ctl);

	if (fnic->flogi_oxid == net16_get(&fh->fh_ox_id) &&
	    fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    (f_ctl & (FC_FC_EX_CTX | FC_FC_SEQ_CTX)) == FC_FC_EX_CTX &&
	    fh->fh_type == FC_TYPE_ELS &&
	    fc_frame_payload_op(fp) == ELS_LS_ACC)
		ret = 1;

	return ret;
}

static void fnic_rq_cmpl_frame_recv(struct vnic_rq *rq, struct cq_desc 
				    *cq_desc, struct vnic_rq_buf *buf,
				    int skipped __attribute__((unused)),
				    void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(rq->vdev);
	struct fc_frame *fp;
	unsigned int eth_hdrs_stripped;
	u_int8_t list_was_empty;
	struct fnic_event *ev = NULL;
	u_int8_t type, color, eop, sop, ingress_port, vlan_stripped;
	u_int8_t fcoe=0, fcoe_sof, fcoe_eof;
	u_int8_t fcoe_fc_crc_ok = 1, fcoe_enc_error = 0;
	u_int8_t tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u_int8_t ipv6, ipv4, ipv4_fragment, rss_type, csum_not_calc;
	u_int8_t fcs_ok = 1, packet_error = 0;
	u_int16_t q_number, completed_index, bytes_written=0, vlan, checksum;
	u_int32_t rss_hash; 
	u_int16_t exchange_id, tmpl;
	u_int8_t sof=0, eof=0;
	u_int32_t fcp_bytes_written=0;
	unsigned long flags;
	
	/* unmap buffer from pci space, get frame pointer*/
	pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len, 
			 PCI_DMA_FROMDEVICE);
	fp = buf->os_buf;

	/* We will now reuse this rq_buf for another frame*/
	buf->os_buf = NULL;

	cq_desc_dec(cq_desc, &type, &color, &q_number, &completed_index);
	if (type == CQ_DESC_TYPE_RQ_FCP) {
		/* lif in FC mode*/
		cq_fcp_rq_desc_dec((struct cq_fcp_rq_desc *)cq_desc,
				   &type, &color, &q_number, &completed_index,
				   &eop, &sop, &fcoe_fc_crc_ok, &exchange_id,
				   &tmpl, &fcp_bytes_written, &sof, &eof,
				   &ingress_port, &packet_error, 
				   &fcoe_enc_error, &fcs_ok, &vlan_stripped, 
				   &vlan);
		eth_hdrs_stripped = 1;
		
	} else if (type == CQ_DESC_TYPE_RQ_ENET) {
		/* lif in eth mode*/
		cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc,
				    &type, &color, &q_number, &completed_index,
				    &ingress_port, &fcoe, &eop, &sop,&rss_type,
				    &csum_not_calc, &rss_hash, &bytes_written,
				    &packet_error, &vlan_stripped, &vlan, 
				    &checksum, &fcoe_sof, &fcoe_fc_crc_ok, 
				    &fcoe_enc_error, &fcoe_eof, 
				    &tcp_udp_csum_ok, &udp, &tcp,
				    &ipv4_csum_ok, &ipv6, &ipv4,&ipv4_fragment,
				    &fcs_ok);
		eth_hdrs_stripped = 0;
		
	} else {
		/* wrong CQ type*/
		printk(KERN_ERR DFX "fnic rq_cmpl wrong cq type x%x\n", 
		       fnic->fnic_no, type);
		goto drop;
	}
	
	if (!fcs_ok || packet_error || !fcoe_fc_crc_ok || fcoe_enc_error) {
		printk(KERN_DEBUG DFX "fnic rq_cmpl fcoe x%x fcsok x%x"
		       " pkterr x%x fcoe_fc_crc_ok x%x, fcoe_enc_err x%x\n", 
		       fnic->fnic_no, fcoe, fcs_ok, packet_error,
		       fcoe_fc_crc_ok, fcoe_enc_error);
		goto drop;
	}
	
	if (eth_hdrs_stripped)
		fnic_import_rq_fc_frame(fp, fcp_bytes_written, sof, eof);
	else
		fnic_import_rq_eth_pkt(fp, bytes_written, &exchange_id);

	/* If frame is an ELS response that matches the cached FLOGI OX_ID,
	 * issue flogi_reg_request copy wq request to firmware
	 * to register the S_ID and determine whether FC_OUI mode or GW mode.
	 */
	if (is_matching_flogi_resp_frame(fnic, fp)) {
		if (!eth_hdrs_stripped) {
			fnic_handle_flogi_resp(fnic, fp);
			return;
		}
		goto drop;
	} 

	if (!eth_hdrs_stripped)
		goto drop;

	/* Pass this frame up to FCS, enqueue the frame for Rx 
	 * thread to pick it up for FCS processing
	 */
	ev = kmem_cache_alloc(fnic_ev_cache, GFP_ATOMIC);
	if (ev == NULL) {
		printk(KERN_DEBUG DFX "Cannot allocate an event, "
		       "dropping the FCS Rx frame\n", fnic->fnic_no);
		goto drop;
	}
	
	memset(ev,0,sizeof(struct fnic_event));
	
	/* initialize the frame structure*/
	ev->fp = fp;
	ev->fp->fr_dev = fnic->fc_dev;
	ev->fnic = fnic;
	ev->ev_type = EV_TYPE_FRAME;
	ev->is_flogi_resp_frame = 0;
	
	/* The driver has one single list for all FC vnics 
	 * to queue Receive frames. This queue is also accessed
	 * for the same fnic in the Flogi registration completion
	 * copy WQ processing to queue the Flogi response frame.
	 * So turn off all interrupts before enqueuing
	 */
	spin_lock_irqsave(&fnic_eventlist_lock, flags);
	list_was_empty = list_empty(&fnic_eventlist);
	list_add_tail(&ev->list, &fnic_eventlist);
	spin_unlock_irqrestore(&fnic_eventlist_lock, flags);
	if (list_was_empty)
		wake_up_process(fnic_thread);

	return;
 drop:
	fc_frame_free(fp);
}

/* Service the RQ indicated by the queue number */
static int fnic_rq_cmpl_handler_cont(struct vnic_dev *vdev, 
				     struct cq_desc *cq_desc, u8 type, 
				     u16 q_number, u16 completed_index, 
				     void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(vdev);

	/* This RQ is not modified by any other thread or interrupt context,
	 * no lock needed here
	 */
	vnic_rq_service(&fnic->rq[q_number], cq_desc, completed_index, 
			VNIC_RQ_RETURN_DESC, fnic_rq_cmpl_frame_recv, 
			NULL);
	return 0;
}

int fnic_rq_cmpl_handler(struct fnic *fnic, int rq_work_to_do)
{
	unsigned int tot_rq_work_done = 0, cur_work_done;
	unsigned int i;
	int err;
	
	/* Look through all RQs for incoming frames */
	for (i = 0; i < fnic->rq_count; i++) {
		cur_work_done = vnic_cq_service(&fnic->cq[i]
						,rq_work_to_do, 
						fnic_rq_cmpl_handler_cont, 
						NULL); 
		/* allocate and fill in new RQ buffers */
		if (cur_work_done) {
			err = vnic_rq_fill(&fnic->rq[i], 
					   fnic_alloc_rq_frame);
			if (err)
				printk(KERN_ERR DFX "fnic_alloc_rq_frame"
				       " cant alloc frame\n", fnic->fnic_no);
		}
		tot_rq_work_done += cur_work_done;
	}

	return tot_rq_work_done;
}

void fnic_rx_frame_free(struct fc_frame *fp)
{
	fc_frame_free_static(fp);
	kmem_cache_free(fnic_fc_frame_cache,fp);
}

/* 
 * This function is called once at init time to allocate and fill RQ
 * buffers. Subsequently, it is called in the interrupt context after RQ
 * buffer processing to replenish the buffers in the RQ
 */
int fnic_alloc_rq_frame(struct vnic_rq *rq)
{
	struct fnic *fnic = vnic_dev_priv(rq->vdev);
	struct fc_frame *fp;
	u16 len;
	dma_addr_t pa;

	len = sizeof(*fp) + sizeof(struct fcp_hdr) +
		FC_MAX_PAYLOAD + sizeof(struct fcoe_crc_eof);
	fp = (struct fc_frame*)kmem_cache_alloc(fnic_fc_frame_cache, 
						GFP_DMA|GFP_ATOMIC);
	if (!fp) {
		printk(KERN_DEBUG DFX "Unable to allocate RQ frame\n",
		       fnic->fnic_no);
		return -ENOMEM;
	}

	memset(fp,0,len);
	
	fc_frame_init_static(fp);
	fp->fr_free = fnic_rx_frame_free;
	/* Initialize fr_hdr. When a FCS frame is actually recevied, fr_hdr
	 * gets updated to point to the correct place in the frame,
	 * depending on whether the lif was in FC mode or in eth mode
	 */
	fp->fr_hdr = (struct fc_frame_header *)(fp + 1);
	pa = pci_map_single(fnic->pdev, (fp + 1), 
			    (len-sizeof(*fp)),PCI_DMA_FROMDEVICE);
	fnic_queue_rq_desc(rq, fp, pa, (len-sizeof(*fp)));
	return 0;
}

void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf* buf)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(rq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr, buf->len, 
			 PCI_DMA_FROMDEVICE);

	fc_frame_free(fp);
	buf->os_buf = NULL;
}

/* returns 1 for flogi frame, 0 otherwise*/
static inline int is_flogi_frame(struct fc_frame_header *fh)
{
	return fh->fh_r_ctl == FC_RCTL_ELS_REQ &&
		*(u_int8_t *)(fh + 1) == ELS_FLOGI;
}


int fnic_send_frame(struct fnic *fnic, struct fc_frame* fp)
{
	/* for now just the first WQ for raw send*/
	struct vnic_wq *wq = &fnic->wq[0];
	dma_addr_t pa;
	struct fcp_hdr *fcp_hdr;
	struct ether_header *eth_hdr;
	struct fcoe_vlan_hdr *vlan_hdr;
	struct fcoe_hdr *fcoe_hdr;
	struct fc_frame_header *fh;
	u_int32_t tot_len, eth_hdr_len;
	int ret = 0;
	unsigned long flags;

	fh = fc_frame_header_get(fp);

	/* Either host sets up vlan header or hardware */
	if (!fnic->vlan_hw_insert) {
		fcp_hdr = (struct fcp_hdr *)(fp + 1);
		eth_hdr = &fcp_hdr->eth_hdr;
		vlan_hdr = &fcp_hdr->vlan_hdr;
		fcoe_hdr = &fcp_hdr->fcoe_hdr;
		eth_hdr_len = offsetof(struct fcp_hdr, fc_hdr);
		eth_hdr->ether_type = htons(ETH_P_8021Q);
		vlan_hdr->vlan_tag.net_data = htons(fnic->vlan_id);
		vlan_hdr->vlan_ethertype.net_data = htons(ETH_P_FCOE);
	} else {
		/* skip vlan header, hw will generate it */
		eth_hdr = (struct ether_header *)
			((unsigned char *)(fp + 1) + 
			 sizeof(*vlan_hdr));
		vlan_hdr = NULL;
		fcoe_hdr = (struct fcoe_hdr *)(eth_hdr + 1);
		eth_hdr_len = sizeof(*eth_hdr) + sizeof(*fcoe_hdr);
		eth_hdr->ether_type = htons(ETH_P_FCOE);
	}

	/* set ethernet src and dst MAC addresses */
	if(is_flogi_frame(fh)) {
		fc_fcoe_set_mac(eth_hdr->ether_dhost, (net24_t *)&fh->fh_d_id);
		memcpy(eth_hdr->ether_shost, fnic->mac_addr, ETH_ALEN);
	} else {
		/* insert dst addr of target in fcoui mode */
		if (fnic->fcoui_mode)
			fc_fcoe_set_mac(eth_hdr->ether_dhost, 
					(net24_t *)&fh->fh_d_id);
		else /* gw addr*/
			memcpy(eth_hdr->ether_dhost, fnic->dest_addr, 
			       ETH_ALEN);

		/* insert src addr*/
		memcpy(eth_hdr->ether_shost, fnic->data_src_addr, ETH_ALEN);
	}

	/* find total len to program the desc
	   Total len = eth + [vlan] + fcoe + fchdr + fcpayload + pad
	*/
	tot_len = (eth_hdr_len + fp->fr_len);
	/* Include pad bytes to pad to next word boundary */
	tot_len = ((tot_len + 3) / 4) * 4;
	
	/* Set SOF and version in the fcoe header */
	fcoe_hdr->fcoe_sof = fp->fr_sof;
	FC_FCOE_ENCAPS_VER(fcoe_hdr, FC_FCOE_VER);

	/* map the frame*/
	pa = pci_map_single(fnic->pdev, eth_hdr, tot_len, PCI_DMA_TODEVICE);
	
	/* get the wq lock*/
	spin_lock_irqsave(&fnic->wq_lock[0], flags);

	if (!vnic_wq_desc_avail(wq)) {
		pci_unmap_single(fnic->pdev, pa,
				 tot_len, PCI_DMA_TODEVICE);
		ret = -1;
		goto fnic_send_frame_end;
	}

	/* queue the descriptor to hardware, hardware generates FC CRC,
	 * eof, vlan header. Pass vlan id to use to hw
	 */
	fnic_queue_wq_desc(wq, fp, pa, tot_len, fp->fr_eof, 
			   fnic->vlan_hw_insert, fnic->vlan_id, 1, 1, 1);
 fnic_send_frame_end:
	/* release the wq lock*/
	spin_unlock_irqrestore(&fnic->wq_lock[0], flags);

	if (ret)
		fc_frame_free(fp);
	return ret;
}

/* 
 * fnic_send
 * Routine to send a raw frame 
 */
int fnic_send(struct fcdev *fc_dev, struct fc_frame *fp)
{
	struct fnic *fnic = fc_dev->drv_priv;
	struct fc_frame_header *fh;
	int ret = 0;
	unsigned long flags;
	enum fnic_state old_state;
	struct fc_frame *old_flogi = NULL;
	struct fc_frame *old_flogi_resp = NULL;

	if (fnic->in_remove) {
		fc_frame_free(fp);
		ret = -1;
		goto fnic_send_end;
	}

	fh = fc_frame_header_get(fp);

	/* if not an Flogi frame, send it out, this is the common case */
	if(!is_flogi_frame(fh))
		return fnic_send_frame(fnic, fp);

	/* Flogi frame, now enter the state machine */

	spin_lock_irqsave(&fnic->fnic_lock, flags);
 again:
	/* Get any old cached frames, free them after dropping lock */
	old_flogi = fnic->flogi;
	fnic->flogi = NULL;
	old_flogi_resp = fnic->flogi_resp;
	fnic->flogi_resp = NULL;

	fnic->flogi_oxid = FC_XID_UNKNOWN;

	old_state = fnic->state;
	switch (fnic->state) {
	case FNIC_IN_FC_MODE:
	case FNIC_IN_ETH_TRANS_FC_MODE:
	default:
		fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
		vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);

		if (old_flogi) {
			fc_frame_free(old_flogi);
			old_flogi = NULL;
		}
		if (old_flogi_resp) {
			fc_frame_free(old_flogi_resp);
			old_flogi_resp = NULL;
		}

		ret = fnic_fw_reset_handler(fnic);

		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state != FNIC_IN_FC_TRANS_ETH_MODE)
			goto again;
		if (ret) {
			fnic->state = old_state;
			spin_unlock_irqrestore(&fnic->fnic_lock, flags);
			fc_frame_free(fp);
			goto fnic_send_end;
		}
		old_flogi = fnic->flogi;
		fnic->flogi = fp;
		fnic->flogi_oxid = net16_get(&fh->fh_ox_id);
		old_flogi_resp = fnic->flogi_resp;
		fnic->flogi_resp = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		break;

	case FNIC_IN_FC_TRANS_ETH_MODE:
		/* A reset is pending with the firmware. Store the flogi
		 * and its oxid. The transition out of this state happens
		 * only when Firmware completes the reset, either with
		 * success or failed. If success, transition to
		 * FNIC_IN_ETH_MODE, if fail, then transition to
		 * FNIC_IN_FC_MODE
		 */
		fnic->flogi = fp;
		fnic->flogi_oxid = net16_get(&fh->fh_ox_id);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		break;

	case FNIC_IN_ETH_MODE:
		/* The fw/hw is already in eth mode. Store the oxid,
		 * and send the flogi frame out. The transition out of this
		 * state happens only we receive flogi response from the
		 * network, and the oxid matches the cached oxid when the
		 * flogi frame was sent out. If they match, then we issue
		 * a flogi_reg request and transition to state
		 * FNIC_IN_ETH_TRANS_FC_MODE
		 */
		fnic->flogi_oxid = net16_get(&fh->fh_ox_id);
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		ret = fnic_send_frame(fnic, fp);
		break;
	}

 fnic_send_end:
	if (old_flogi)
		fc_frame_free(old_flogi);
	if (old_flogi_resp)
		fc_frame_free(old_flogi_resp);

	return ret;
}

static void fnic_wq_complete_frame_send( struct vnic_wq *wq,
					 struct cq_desc *cq_desc, 
					 struct vnic_wq_buf *buf, void *opaque)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	/* Frame transmission complete, unmap frame from PCI space
	 * and delink from buffer pointer. Release frame
	 */
	pci_unmap_single(fnic->pdev, buf->dma_addr,
			 buf->len, PCI_DMA_TODEVICE);
	fc_frame_free(fp);
	buf->os_buf = NULL;
}
    

/* Service the WQ indicated by the queue number */
static int fnic_wq_cmpl_handler_cont(struct vnic_dev *vdev, 
				     struct cq_desc *cq_desc, u8 type, 
				     u16 q_number, u16 completed_index, 
				     void *opaque)
{
	struct fnic *fnic = vnic_dev_priv(vdev);
	unsigned long flags;

	/* Raw WQ is modified in two other contexts.
	 * 1) Thread context for sending FC frame out
	 * 2) In interrupt context during copy WQ processing of fw reset
	 *    completion, resulting in sending out Flogi
	 * Block interrupts so that (2) does not kick in while we are
	 * modifying the WQ here
	 */
	spin_lock_irqsave(&fnic->wq_lock[q_number], flags);
	vnic_wq_service(&fnic->wq[q_number], cq_desc, completed_index, 
			fnic_wq_complete_frame_send, NULL);
	spin_unlock_irqrestore(&fnic->wq_lock[q_number], flags);

	return 0;
}

int fnic_wq_cmpl_handler(struct fnic *fnic, int work_to_do)
{
	unsigned int wq_work_done = 0;
	unsigned int i;

	/* Look through all Raw WQs */
	for (i = 0; i < fnic->raw_wq_count; i++) {
		wq_work_done  += vnic_cq_service(&fnic->cq[fnic->rq_count+i],
						 work_to_do,
						 fnic_wq_cmpl_handler_cont, 
						 NULL); 
	}

	return wq_work_done;
}


void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf)
{
	struct fc_frame *fp = buf->os_buf;
	struct fnic *fnic = vnic_dev_priv(wq->vdev);

	pci_unmap_single(fnic->pdev, buf->dma_addr,
			 buf->len, PCI_DMA_TODEVICE);
	fc_frame_free(fp);
	buf->os_buf = NULL;
}
