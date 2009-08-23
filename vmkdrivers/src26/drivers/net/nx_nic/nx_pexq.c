/*
 * Copyright (C) 2003 - 2007 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    licensing@netxen.com
 * NetXen, Inc.
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/version.h>

#include "nx_errorcode.h"
#include "nxplat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
#include <linux/wrapper.h>
#endif
#ifndef _LINUX_MODULE_PARAMS_H
#include <linux/moduleparam.h>
#endif

#include "kernel_compatibility.h"
#include "unm_nic.h"
#include "unm_version.h"
#include "nx_pexq.h"


/* This forms a xdma host-to-host copy from body[3] of 
 * the message to the completion status. This dma acts 
 * as a sync signal for the pexq 
 */

#define NX_PEXQ_SYNC_BODY_INDEX 3

//TODO: Include Header as param
static inline void
form_xdma_sync_msg(unm_msg_t *msg,
		   U16 msg_idx,
		   U16 pci_fn,
		   U64 xdma_hdr_word,
		   U64 msg_base_paddr,
		   U64 compl_in_paddr)
{
	dma_addr_t paddr;

	msg->hdr.word = xdma_hdr_word;
	msg->body.values[0] = ((((U64)pci_fn & 
				 0x3f) << 56) |   /* pci_fn */
			       (3ULL << 40)   |   /* both host */
			       (1ULL << 34)   |   /* 1 dma */
			       (1ULL << 32)   |   /* disable compl */
			       (8ULL));           /* 8 bytes */

	/* Make source point to the body.values[3] below */
	paddr = (msg_base_paddr + 
		 NX_PEXQ_DBELL_GET_OFFSET(msg_idx) +
		 (NX_PEXQ_SYNC_BODY_INDEX+1)*8);
	msg->body.values[1] = paddr;                 /* src */
	msg->body.values[2] = compl_in_paddr;        /* dst */
	msg->body.values[3] = NX_PEXQ_DBELL_INCR_IDX(msg_idx, 1);	

	nx_nic_print7(NULL, "Gen compl update req idx %d\n",
		      msg_idx);
}


/* Schedule a message for sending via pexq doorbell
 *   Failure: msg could not be queued, retry later 
 *            (not enough host space)
 */

nx_rcode_t 
nx_schedule_pexqdb(nx_pexq_dbell_t *pexq,
		   unm_msg_t *user_msg)
{
        unm_msg_t *msg;
	U16 msg_idx;

	spin_lock_bh(&pexq->lock);
 get_another_slot:

	if (pexq->qbuf_free_cnt == 0) {
		nx_nic_print5(NULL, "%s: retAddr=%p, No free slots\n",
			      __FUNCTION__, __builtin_return_address(0));
		spin_unlock_bh(&pexq->lock);
		return NX_RCODE_NO_HOST_RESOURCE;
	}

	msg_idx = pexq->qbuf_new_idx;
	msg = &pexq->qbuf_vaddr[msg_idx];
	pexq->qbuf_new_idx = NX_PEXQ_DBELL_INCR_IDX(msg_idx, 1);
	pexq->qbuf_free_cnt --;
	
	if ((pexq->qbuf_free_cnt & (NX_PEXQ_DBELL_COMPL_THRES - 1)) == 0) {
		form_xdma_sync_msg(msg,
				   msg_idx,
				   pexq->pci_fn,
				   pexq->xdma_hdr.word,
				   pexq->qbuf_paddr,
				   pexq->compl_in_paddr);		
		goto get_another_slot;
	}
	
	memcpy(msg, user_msg, sizeof(*msg));
	
	nx_nic_print7(NULL, "%s: retAddr=%p, qbuf_new %d free %d\n", 
		      __FUNCTION__, __builtin_return_address(0), 
		      msg_idx,
		      pexq->qbuf_free_cnt);

	spin_unlock_bh(&pexq->lock);

        return NX_RCODE_SUCCESS;
}


/* Send scheduled doorbells as resources allow
 *   Failure: could not complete all scheduled dbs, retry later
 *            (back pressure from card)
 */

nx_rcode_t
nx_ring_pexqdb(nx_pexq_dbell_t *pexq)
{
        dma_addr_t qbuf_paddr;
        U64 post_cmd = 0;
        I32 ii;
	U32 rv = NX_RCODE_SUCCESS;

	spin_lock_bh(&pexq->lock);

        for (ii = pexq->qbuf_needs_db_idx; 
	     ii != pexq->qbuf_new_idx; 
	     ii = NX_PEXQ_DBELL_INCR_IDX(ii, 1)) {

		if (pexq->qbuf_pending_cnt >= NX_PEXQ_DBELL_MAX_PENDING) {
			I16 in, cnt;

			in = *pexq->compl_in_vaddr;
			if (in == pexq->qbuf_pending_compl_idx) {
				nx_nic_print5(NULL,
					      "%s: max pending cnt %d reached. "
					      "pend idx %d in idx %d db idx %d\n",
					      __FUNCTION__,
					      pexq->qbuf_pending_cnt,
					      pexq->qbuf_pending_compl_idx, 
					      in, ii);
				rv = NX_RCODE_NOT_READY;
				break;
			}
	
			/* One or more pexq ops completed */
			cnt = (in - pexq->qbuf_pending_compl_idx);
			if (in < pexq->qbuf_pending_compl_idx) {
				cnt += NX_PEXQ_DBELL_BUF_QMSGS;
			}

			nx_nic_print7(NULL, "%s: compl %d idx %d\n",
				      __FUNCTION__, cnt, in);

			pexq->qbuf_pending_compl_idx = in;
			pexq->qbuf_pending_cnt -= cnt;
			pexq->qbuf_free_cnt += cnt;
			if (pexq->qbuf_pending_cnt < 0) {
				nx_nic_print3(NULL,
					      "%s: pending_cnt %d out of range\n",
					      __FUNCTION__,
					      pexq->qbuf_pending_cnt);
				break;
			}
			if (pexq->qbuf_free_cnt > NX_PEXQ_DBELL_BUF_QMSGS) {
				nx_nic_print3(NULL,
					      "%s: free_cnt %d out of range\n",
					      __FUNCTION__,
					      pexq->qbuf_free_cnt);
				break;
			}
		}

                qbuf_paddr = pexq->qbuf_paddr + NX_PEXQ_DBELL_GET_OFFSET(ii);
		post_cmd = (PEXQ_DB_FIELD_ADDR_40(qbuf_paddr) |
			    PEXQ_DB_FIELD_NUM_MSGS(1) |
			    PEXQ_DB_FIELD_8_QW_PER_MSG |
			    PEXQ_DB_FIELD_DISABLE_COMPL |
			    PEXQ_DB_FIELD_COMPL_HDR_FROM_REG |
			    PEXQ_DB_FIELD_DATA_IN_HOST);

		pexq->qbuf_pending_cnt ++;

		writeq(*(u64 *)&post_cmd, (void *)pexq->dbell_vaddr);

                nx_nic_print7(NULL, "%s: ringing. new %d ii %d "
			      "qbuf_paddr 0x%llx: 0x%llx=0x%llx\n",
			      __FUNCTION__, 
			      pexq->qbuf_new_idx, ii,
			      qbuf_paddr, pexq->dbell_vaddr, 
			      *(u64 *)&post_cmd);
        }
	pexq->qbuf_needs_db_idx = ii;

	spin_unlock_bh(&pexq->lock);
	return rv;
}


/* Testing only
 * Enqueue a series of test messages via pexq
 */

static void
pexq_db_test(nx_pexq_dbell_t *pexq, 
	     U64 qhdr)
{
        I32 i;
        I32 j;
	
        for (j=0; j<2; j++) {
                for (i=0; i<8; i++) {
                        unm_msg_t p;
			
                        p.hdr.word = qhdr;
                        p.body.values[0] = 0x00000000000000 + (j<<4) +i;
                        p.body.values[1] = 0x11111111111100 + (j<<4) +i;
                        p.body.values[2] = 0x22222222222200 + (j<<4) +i;
                        p.body.values[3] = 0x33333333333300 + (j<<4) +i;
                        p.body.values[4] = 0x44444444444400 + (j<<4) +i;
                        p.body.values[5] = 0x55555555555500 + (j<<4) +i;
                        p.body.values[6] = qhdr;
			
                        nx_schedule_pexqdb(pexq, &p);
                }
		nx_ring_pexqdb(pexq);
        }

	udelay(1000);
	nx_ring_pexqdb(pexq);
}

static void
pexq_db_do_test(nx_pexq_dbell_t *pexq)
{
	nx_nic_print5(NULL,
		      "%s: Starting\n",
		      __FUNCTION__);
	
        /* Gratuitous doorbell ringging. */
        pexq_db_test(pexq, 0x5400000000440003ULL);
        pexq_db_test(pexq, 0x5400000000450003ULL);
        pexq_db_test(pexq, 0x5400000000460003ULL);
        pexq_db_test(pexq, 0x5400000000470003ULL);

	nx_nic_print5(NULL,
		      "%s: free %d pending %d : compl %d in %d db %d new %d\n",
		      __FUNCTION__,
		      pexq->qbuf_free_cnt,
		      pexq->qbuf_pending_cnt,
		      pexq->qbuf_pending_compl_idx,
		      (U16)*pexq->compl_in_vaddr,
		      pexq->qbuf_needs_db_idx,
		      pexq->qbuf_new_idx);
}


/* Perform a single doorbell ctrl write
 */

static void
db_ctrl_write(struct unm_adapter_s *adapter,
	      U32 db_addr_val,
	      U32 db_data1_val,
	      U32 db_data2_val)
{
	U32 ctl_val;

        nx_nic_print5(adapter,
		      "%s: DBCTL 0x%x <= 0x%x 0x%x\n",
		      __FUNCTION__, db_addr_val,
		      db_data1_val, db_data2_val);

        adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_DB_ADDR), 
				     &db_addr_val, 4);
        adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_DB_DATA), 
				     &db_data1_val, 4);
        adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_DB_DATA2), 
				     &db_data2_val, 4);

	ctl_val = 0x2;
        adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_DB_CTRL), 
				     &ctl_val, 4);
	ctl_val = 0x0;
        adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_DB_CTRL), 
				     &ctl_val, 4);
}


/* Enable and configure PexQ support on the card
 */

static nx_rcode_t 
pexq_dbell_hw_init(struct unm_adapter_s *adapter,
		   nx_pexq_dbell_t *pexq)
{
	U32 reg_val;
	U32 hdr_val;

        /* Enable pexq doorbell. */
        /* TODO: Should be done in crbinit */
	adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(PCIE_CHICKEN3), 
				    &reg_val, 4);
        reg_val |= 0x40;
	adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_CHICKEN3), 
				     &reg_val, 4);


        /* Setup mapping pexq to doorbell and host address
	 */
	db_ctrl_write(adapter,
		      NX_PEXQ_PF2DBELL_NUMBER(pexq->pci_fn),
		      (((pexq->qbuf_paddr >> 40) & 0xffffff) |
		       ((pexq->db_number & 0xff) << 24)),
		      ((pexq->db_number >> 8) & 0xff));

        /* Load PEXQ msg header low word and high word
	 * Must follow doorbell/host addr mapping above
	 * This should match the programmed pexq req hdr
	 */

	adapter->unm_nic_hw_read_wx(adapter,
				    UNM_PEXQ_REQ_HDR_LO,
				    &hdr_val, 4);
	
	db_ctrl_write(adapter,
		      NX_PEXQ_DMA_REG_QW0_LO,
		      hdr_val, 0);
	
	adapter->unm_nic_hw_read_wx(adapter,
				    UNM_PEXQ_REQ_HDR_HI,
				    &hdr_val, 4);
	
	db_ctrl_write(adapter,
		      NX_PEXQ_DMA_REG_QW0_HI,
		      hdr_val, 0);
	
        return NX_RCODE_SUCCESS;
}

/* Initialize pexq for this pci function
 *   Failure: could no allocate needed host resources
 */

nx_rcode_t 
nx_init_pexq_dbell(struct unm_adapter_s *adapter,
		   nx_pexq_dbell_t *pexq)
{
	struct pci_dev *pdev = adapter->pdev;
	nx_rcode_t rv = NX_RCODE_SUCCESS;

	nx_nic_print7(adapter,
		      "%s: pci_fn %d\n",
		      __FUNCTION__, adapter->ahw.pci_func);

	memset(pexq, 0, sizeof (*pexq));
	pexq->qbuf_free_cnt = NX_PEXQ_DBELL_BUF_QMSGS;
	pexq->pci_fn = adapter->ahw.pci_func;
	pexq->db_number = PEXQ_DB_NUMBER;

        spin_lock_init(&pexq->lock);

	/* Allocate 1MB space for general pexq msg use 
	 */
        pexq->qbuf_vaddr = ((unm_msg_t *)
			    __get_free_pages(GFP_KERNEL, 
					     NX_PEXQ_DBELL_BUF_ORDER));
        if (pexq->qbuf_vaddr == NULL) {
		nx_nic_print3(adapter,
			      "Could not allocate pexq_qbuf_buf\n");
                rv = NX_RCODE_NO_HOST_MEM;
		goto failure;
        }
	
        pexq->qbuf_paddr = pci_map_single(pdev, 
                                          (void *)pexq->qbuf_vaddr, 
                                          NX_PEXQ_DBELL_BUF_SIZE,
                                          PCI_DMA_TODEVICE); 
        if (pexq->qbuf_paddr == (dma_addr_t)0) {
		nx_nic_print3(adapter,
			      "Could not map qbuf_vaddr\n");
                rv = NX_RCODE_NO_HOST_MEM;
		goto failure;
        }

	pexq->compl_in_vaddr = ((U64 *)
				__get_free_pages(GFP_KERNEL, 0));
        if (pexq->compl_in_vaddr == NULL) {
		nx_nic_print3(adapter,
			      "Could not allocate compl_in_vaddr\n");
                rv = NX_RCODE_NO_HOST_MEM;
		goto failure;
        }
	memset(pexq->compl_in_vaddr, 0, PAGE_SIZE);
	
        pexq->compl_in_paddr = pci_map_single(pdev, 
					      (void *)pexq->compl_in_vaddr,
					      PAGE_SIZE,
					      PCI_DMA_FROMDEVICE); 
        if (pexq->compl_in_paddr == (dma_addr_t)0) {
		nx_nic_print3(adapter,
			      "Could not map compl_in_vaddr\n");
                rv = NX_RCODE_NO_HOST_MEM;
		goto failure;
        }

	rv = pexq_dbell_hw_init(adapter, pexq);
	if (rv != NX_RCODE_SUCCESS) {
		goto failure;
	}

	/* Setup doorbell */
        pexq->dbell_paddr = (pci_resource_start(pdev,4) +
			     (pexq->db_number << 12));
        pexq->dbell_vaddr = ((unsigned long)
			     ioremap_nocache(pexq->dbell_paddr, 8));

	/* Setup xdmaq header for pexq sync ops */
	memset(&pexq->xdma_hdr, 0, sizeof(pexq->xdma_hdr));
	pexq->xdma_hdr.dst_major = 4;
	pexq->xdma_hdr.dst_minor = 14;
	pexq->xdma_hdr.dst_subq = 1;

#if 1
	pexq_db_do_test(pexq);
#endif

        return NX_RCODE_SUCCESS;
	
 failure:
	nx_free_pexq_dbell(adapter, pexq);
	return rv;
}


/* Destroy pexq setup for this pci function
 */

void  
nx_free_pexq_dbell(struct unm_adapter_s *adapter,
		   nx_pexq_dbell_t *pexq)
{
	struct pci_dev *pdev = adapter->pdev;
	
        nx_nic_print7(adapter,
		      "%s:\n", __FUNCTION__);
	
	if (pexq->qbuf_paddr != (dma_addr_t)0) {
		pci_unmap_single(pdev, pexq->qbuf_paddr,
				 NX_PEXQ_DBELL_BUF_SIZE,
				 PCI_DMA_TODEVICE);
	}

	if (pexq->qbuf_vaddr != NULL) {
		free_pages((unsigned long) (pexq->qbuf_vaddr), 
			   NX_PEXQ_DBELL_BUF_ORDER);
	}

	if (pexq->compl_in_paddr != (dma_addr_t)0) {
		pci_unmap_single(pdev, pexq->compl_in_paddr,
				 PAGE_SIZE,
				 PCI_DMA_FROMDEVICE);
	}
	
	if (pexq->compl_in_vaddr != NULL) {
		free_pages((unsigned long) (pexq->compl_in_vaddr), 0);
	}
	
	if (pexq->dbell_vaddr != 0x0) {
		iounmap((void *)pexq->dbell_vaddr);
	}
	
	memset(pexq, 0, sizeof (*pexq));
}
