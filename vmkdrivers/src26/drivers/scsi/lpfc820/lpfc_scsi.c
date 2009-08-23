/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#if defined(__VMKLNX__)
#include <vmklinux26/vmklinux26_scsi.h>
#endif

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define LPFC_RESET_WAIT  2
#define LPFC_ABORT_WAIT  2

#define TGT_BYTES 	82	/* Bytes consumed per target display in proc */
#define SAN_BYTES	300	/* Bytes consumed by the SAN display in proc */
#define VPORT_BYTES	117	/* Bytes consumed by the Vport display in proc */
#define INFO_BUF    	8192	/* Max bytes per proc node dump. */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/*
 * This function is called with no lock held when there is a resource
 * error in driver or in firmware.
 */
void
lpfc_adjust_queue_depth(struct lpfc_hba *phba)
{
	unsigned long flags;
	uint32_t evt_posted;

	spin_lock_irqsave(&phba->hbalock, flags);
	atomic_inc(&phba->num_rsrc_err);
	phba->last_rsrc_error_time = jiffies;

	if ((phba->last_ramp_down_time + QUEUE_RAMP_DOWN_INTERVAL) > jiffies) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	phba->last_ramp_down_time = jiffies;

	spin_unlock_irqrestore(&phba->hbalock, flags);

	spin_lock_irqsave(&phba->pport->work_port_lock, flags);
	evt_posted = phba->pport->work_port_events & WORKER_RAMP_DOWN_QUEUE;
	if (!evt_posted)
		phba->pport->work_port_events |= WORKER_RAMP_DOWN_QUEUE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, flags);

	if (!evt_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/*
 * This function is called with no lock held when there is a successful
 * SCSI command completion.
 */
static inline void
lpfc_rampup_queue_depth(struct lpfc_vport  *vport,
			uint32_t queue_depth)
{
	unsigned long flags;
	struct lpfc_hba *phba = vport->phba;
	uint32_t evt_posted;
	atomic_inc(&phba->num_cmd_success);

	if (vport->cfg_lun_queue_depth <= queue_depth)
		return;
	spin_lock_irqsave(&phba->hbalock, flags);
	if (((phba->last_ramp_up_time + QUEUE_RAMP_UP_INTERVAL) > jiffies) ||
	 ((phba->last_rsrc_error_time + QUEUE_RAMP_UP_INTERVAL ) > jiffies)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}
	phba->last_ramp_up_time = jiffies;
	spin_unlock_irqrestore(&phba->hbalock, flags);

	spin_lock_irqsave(&phba->pport->work_port_lock, flags);
	evt_posted = phba->pport->work_port_events & WORKER_RAMP_UP_QUEUE;
	if (!evt_posted)
		phba->pport->work_port_events |= WORKER_RAMP_UP_QUEUE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, flags);

	if (!evt_posted)
		lpfc_worker_wake_up(phba);
	return;
}

void
lpfc_ramp_down_queue_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	unsigned long new_queue_depth;
	unsigned long num_rsrc_err, num_cmd_success;
	int i;

	num_rsrc_err = atomic_read(&phba->num_rsrc_err);
	num_cmd_success = atomic_read(&phba->num_cmd_success);

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				new_queue_depth =
					sdev->queue_depth * num_rsrc_err /
					(num_rsrc_err + num_cmd_success);
				if (!new_queue_depth)
					new_queue_depth = sdev->queue_depth - 1;
				else
					new_queue_depth = sdev->queue_depth -
								new_queue_depth;
				if (sdev->ordered_tags)
					scsi_adjust_queue_depth(sdev,
							MSG_ORDERED_TAG,
							new_queue_depth);
				else
					scsi_adjust_queue_depth(sdev,
							MSG_SIMPLE_TAG,
							new_queue_depth);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

void
lpfc_ramp_up_queue_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				if (vports[i]->cfg_lun_queue_depth <=
				    sdev->queue_depth)
					continue;
				if (sdev->ordered_tags)
					scsi_adjust_queue_depth(sdev,
							MSG_ORDERED_TAG,
							sdev->queue_depth+1);
				else
					scsi_adjust_queue_depth(sdev,
							MSG_SIMPLE_TAG,
							sdev->queue_depth+1);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

void
lpfc_scsi_dev_block(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	struct fc_rport *rport;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				rport = starget_to_rport(scsi_target(sdev));
				fc_remote_port_delete(rport);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

void
lpfc_scsi_dev_rescan(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vpi && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_scan_host(shost);
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

/*
 * This routine allocates a scsi buffer, which contains all the necessary
 * information needed to initiate a SCSI I/O.  The non-DMAable buffer region
 * contains information to build the IOCB.  The DMAable region contains
 * memory for the FCP CMND, FCP RSP, and the inital BPL.  In addition to
 * allocating memeory, the FCP CMND and FCP RSP BDEs are setup in the BPL
 * and the BPL BDE is setup in the IOCB.
 */
static struct lpfc_scsi_buf *
lpfc_new_scsi_buf(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_scsi_buf *psb;
	struct ulp_bde64 *bpl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys;
	uint16_t iotag;

	psb = kzalloc(sizeof(struct lpfc_scsi_buf), GFP_KERNEL);
	if (!psb)
		return NULL;

	/*
	 * Get memory from the pci pool to map the virt space to pci bus space
	 * for an I/O.  The DMA buffer includes space for the struct fcp_cmnd,
	 * struct fcp_rsp and the number of bde's necessary to support the
	 * sg_tablesize.
	 */
	psb->data = pci_pool_alloc(phba->lpfc_scsi_dma_buf_pool, GFP_KERNEL,
							&psb->dma_handle);
	if (!psb->data) {
		kfree(psb);
		return NULL;
	}

	/* Initialize virtual ptrs to dma_buf region. */
	memset(psb->data, 0, phba->cfg_sg_dma_buf_size);

	/* Allocate iotag for psb->cur_iocbq. */
	iotag = lpfc_sli_next_iotag(phba, &psb->cur_iocbq);
	if (iotag == 0) {
		pci_pool_free(phba->lpfc_scsi_dma_buf_pool,
			      psb->data, psb->dma_handle);
		kfree (psb);
		return NULL;
	}
	psb->cur_iocbq.iocb_flag |= LPFC_IO_FCP;

	psb->fcp_cmnd = psb->data;
	psb->fcp_rsp = psb->data + sizeof(struct fcp_cmnd);
	psb->fcp_bpl = psb->data + sizeof(struct fcp_cmnd) +
							sizeof(struct fcp_rsp);

	/* Initialize local short-hand pointers. */
	bpl = psb->fcp_bpl;
	pdma_phys = psb->dma_handle;

	/*
	 * The first two bdes are the FCP_CMD and FCP_RSP.  The balance are sg
	 * list bdes.  Initialize the first two and leave the rest for
	 * queuecommand.
	 */
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (struct fcp_cmnd);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	/* Setup the physical region for the FCP RSP */
	pdma_phys += sizeof (struct fcp_cmnd);
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (struct fcp_rsp);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	/*
	 * Since the IOCB for the FCP I/O is built into this lpfc_scsi_buf,
	 * initialize it with all known data now.
	 */
	pdma_phys += (sizeof (struct fcp_rsp));
	iocb = &psb->cur_iocbq.iocb;
	iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
	iocb->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys);
	iocb->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys);
	iocb->un.fcpi64.bdl.bdeSize = (2 * sizeof (struct ulp_bde64));
	iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	iocb->ulpBdeCount = 1;
	iocb->ulpClass = CLASS3;

	return psb;
}

static struct lpfc_scsi_buf*
lpfc_get_scsi_buf(struct lpfc_hba * phba)
{
	struct  lpfc_scsi_buf * lpfc_cmd = NULL;
	struct list_head *scsi_buf_list = &phba->lpfc_scsi_buf_list;
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_lock, iflag);
	list_remove_head(scsi_buf_list, lpfc_cmd, struct lpfc_scsi_buf, list);
	if (lpfc_cmd) {
		lpfc_cmd->seg_cnt = 0;
		lpfc_cmd->nonsg_phys = 0;
	}
	spin_unlock_irqrestore(&phba->scsi_buf_list_lock, iflag);
	return  lpfc_cmd;
}

static void
lpfc_release_scsi_buf(struct lpfc_hba *phba, struct lpfc_scsi_buf *psb)
{
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_lock, iflag);
	psb->pCmd = NULL;
	list_add_tail(&psb->list, &phba->lpfc_scsi_buf_list);
	spin_unlock_irqrestore(&phba->scsi_buf_list_lock, iflag);
}

static int
lpfc_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_scsi_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = lpfc_cmd->fcp_bpl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t vpi = (lpfc_cmd->cur_iocbq.vport
			? lpfc_cmd->cur_iocbq.vport->vpi
			: 0);
	dma_addr_t physaddr;
	uint32_t i, num_bde = 0;
	int datadir = scsi_cmnd->sc_data_direction;
	int dma_error;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	bpl += 2;
	if (scsi_cmnd->use_sg) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		sgel = (struct scatterlist *)scsi_cmnd->request_buffer;
		lpfc_cmd->seg_cnt = dma_map_sg(&phba->pcidev->dev, sgel,
						scsi_cmnd->use_sg, datadir);
		if (lpfc_cmd->seg_cnt == 0)
			return 1;

		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			printk(KERN_ERR "%s: Too many sg segments from "
			       "dma_map_sg.  Config %d, seg_cnt %d",
			       __func__, phba->cfg_sg_seg_cnt,
			       lpfc_cmd->seg_cnt);
			dma_unmap_sg(&phba->pcidev->dev, sgel,
				     lpfc_cmd->seg_cnt, datadir);
			return 1;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the bde's.
		 */
		for (i = 0; i < lpfc_cmd->seg_cnt; i++) {
			physaddr = sg_dma_address(sgel);
			bpl->tus.f.bdeSize = sg_dma_len(sgel);
			bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
			if (datadir == DMA_TO_DEVICE)
				bpl->tus.f.bdeFlags = 0;
			else
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);
			bpl++;
#if defined(__VMKLNX__)
			sgel = sg_next(sgel);
#else /* !defined(__VMKLNX__) */
			sgel++;
#endif /* defined(__VMKLNX__) */
			num_bde++;
		}
	} else if (scsi_cmnd->request_buffer && scsi_cmnd->request_bufflen) {
		physaddr = dma_map_single(&phba->pcidev->dev,
					  scsi_cmnd->request_buffer,
					  scsi_cmnd->request_bufflen,
					  datadir);
		dma_error = dma_mapping_error(physaddr);
		if (dma_error) {
			lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
					"0718 Unable to dma_map_single "
					"request_buffer: vpi %d x%x\n",
					vpi, dma_error);
			return 1;
		}

		lpfc_cmd->nonsg_phys = physaddr;
		bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
		bpl->tus.f.bdeSize = scsi_cmnd->request_bufflen;
		if (datadir == DMA_TO_DEVICE)
			bpl->tus.f.bdeFlags = 0;
		else
			bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		num_bde = 1;
		bpl++;
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that the bdeSize is explicitly
	 * reinitialized since all iocb memory resources are used many times
	 * for transmit, receive, and continuation bpl's.
	 */
	iocb_cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (struct ulp_bde64));
	iocb_cmd->un.fcpi64.bdl.bdeSize +=
		(num_bde * sizeof (struct ulp_bde64));
	iocb_cmd->ulpBdeCount = 1;
	iocb_cmd->ulpLe = 1;
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_cmnd->request_bufflen);
	return 0;
}

static void
lpfc_scsi_unprep_dma_buf(struct lpfc_hba * phba, struct lpfc_scsi_buf * psb)
{
	/*
	 * There are only two special cases to consider.  (1) the scsi command
	 * requested scatter-gather usage or (2) the scsi command allocated
	 * a request buffer, but did not request use_sg.  There is a third
	 * case, but it does not require resource deallocation.
	 */
	if ((psb->seg_cnt > 0) && (psb->pCmd->use_sg)) {
		dma_unmap_sg(&phba->pcidev->dev, psb->pCmd->request_buffer,
				psb->seg_cnt, psb->pCmd->sc_data_direction);
	} else {
		 if ((psb->nonsg_phys) && (psb->pCmd->request_bufflen)) {
			dma_unmap_single(&phba->pcidev->dev, psb->nonsg_phys,
						psb->pCmd->request_bufflen,
						psb->pCmd->sc_data_direction);
		 }
	}
}

static void
lpfc_handle_fcp_err(struct lpfc_hba *phba, struct lpfc_vport *vport,
		    struct lpfc_scsi_buf *lpfc_cmd, struct lpfc_iocbq *rsp_iocb)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t *lp;
	uint32_t host_status = DID_OK;
	uint32_t rsplen = 0;
	uint32_t logit = LOG_FCP | LOG_FCP_ERROR;

	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with this lpfc_cmd.  The driver
	 *  consumes it.
	 */
	if (fcpcmd->fcpCntl2) {
		scsi_status = 0;
		goto out;
	}

	if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen) {
		uint32_t snslen = be32_to_cpu(fcprsp->rspSnsLen);
		if (snslen > SCSI_SENSE_BUFFERSIZE)
			snslen = SCSI_SENSE_BUFFERSIZE;

		if (resp_info & RSP_LEN_VALID)
		  rsplen = be32_to_cpu(fcprsp->rspRspLen);
		memcpy(cmnd->sense_buffer, &fcprsp->rspInfo0 + rsplen, snslen);
	}
	lp = (uint32_t *)cmnd->sense_buffer;

	if (!scsi_status && (resp_info & RESID_UNDER))
		logit = LOG_FCP;

	lpfc_printf_vlog(vport, KERN_WARNING, logit,
			"0730 FCP command x%x failed: x%x SNS x%08x x%08x "
			"Data: x%x x%x x%x x%x x%x\n",
			cmnd->cmnd[0], scsi_status,
			be32_to_cpu(*lp), be32_to_cpu(*(lp + 3)), resp_info,
			be32_to_cpu(fcprsp->rspResId),
			be32_to_cpu(fcprsp->rspSnsLen),
			be32_to_cpu(fcprsp->rspRspLen),
			fcprsp->rspInfo3);

	if (resp_info & RSP_LEN_VALID) {
		rsplen = be32_to_cpu(fcprsp->rspRspLen);
		if ((rsplen != 0 && rsplen != 4 && rsplen != 8) ||
		    (fcprsp->rspInfo3 != RSP_NO_FAILURE)) {
			host_status = DID_ERROR;
			goto out;
		}
	}

	cmnd->resid = 0;
	if (resp_info & RESID_UNDER) {
		cmnd->resid = be32_to_cpu(fcprsp->rspResId);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				"0716 FCP Read Underrun, expected %d, "
				"residual %d Data: x%x x%x x%x\n",
				be32_to_cpu(fcpcmd->fcpDl),
				cmnd->resid, fcpi_parm, cmnd->cmnd[0],
				cmnd->underflow);
		/*
		 * If there is an under run check if under run reported by
		 * storage array is same as the under run reported by HBA.
		 * If this is not same, there is a dropped frame.
		 */
		if ((cmnd->sc_data_direction == DMA_FROM_DEVICE) &&
			fcpi_parm &&
			(cmnd->resid != fcpi_parm)) {
			lpfc_printf_vlog(vport, KERN_WARNING,
					LOG_FCP | LOG_FCP_ERROR,
					"0735 FCP Read Check Error "
					"and Underrun Data: x%x x%x x%x x%x\n",
					be32_to_cpu(fcpcmd->fcpDl),
					cmnd->resid, fcpi_parm,
					cmnd->cmnd[0]);
			cmnd->resid = cmnd->request_bufflen;
			host_status = DID_ERROR;
		}
		/*
		 * The cmnd->underflow is the minimum number of bytes that must
		 * be transfered for this command.  Provided a sense condition
		 * is not present, make sure the actual amount transferred is at
		 * least the underflow value or fail.
		 */
		if (!(resp_info & SNS_LEN_VALID) &&
		    (scsi_status == SAM_STAT_GOOD) &&
		    (cmnd->request_bufflen - cmnd->resid) < cmnd->underflow) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
					"0717 FCP command x%x residual "
					"underrun converted to error "
					"Data: x%x x%x x%x\n",
					cmnd->cmnd[0], cmnd->request_bufflen,
					cmnd->resid, cmnd->underflow);
			host_status = DID_ERROR;
		}
	} else if (resp_info & RESID_OVER) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				"0720 FCP command x%x residual "
				"overrun error. Data: x%x x%x \n",
				cmnd->cmnd[0], cmnd->request_bufflen,
				cmnd->resid);
		host_status = DID_ERROR;

	/*
	 * Check SLI validation that all the transfer was actually done
	 * (fcpi_parm should be zero). Apply check only to reads.
	 */
	} else if ((scsi_status == SAM_STAT_GOOD) && fcpi_parm &&
			(cmnd->sc_data_direction == DMA_FROM_DEVICE)) {
		lpfc_printf_vlog(vport, KERN_WARNING,
				LOG_FCP | LOG_FCP_ERROR,
				"0734 FCP Read Check Error Data: "
				"x%x x%x x%x x%x\n",
				be32_to_cpu(fcpcmd->fcpDl),
				be32_to_cpu(fcprsp->rspResId),
				fcpi_parm, cmnd->cmnd[0]);
		host_status = DID_ERROR;
		cmnd->resid = cmnd->request_bufflen;
	}

 out:
	cmnd->result = ScsiResult(host_status, scsi_status);
}

static void
lpfc_scsi_cmd_iocb_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) pIocbIn->context1;
	struct lpfc_vport      *vport = pIocbIn->vport;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct Scsi_Host *shost = cmd->device->host;
	int result;
	struct scsi_device *tmp_sdev;
	int depth = 0;
	unsigned long flags;
	uint32_t queue_depth, scsi_id;

	lpfc_cmd->result = pIocbOut->iocb.un.ulpWord[4];
	lpfc_cmd->status = pIocbOut->iocb.ulpStatus;
	if (pnode && NLP_CHK_NODE_ACT(pnode))
		atomic_dec(&pnode->cmd_pending);

	if (lpfc_cmd->status) {
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
		    (lpfc_cmd->result & IOERR_DRVR_MASK))
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		else if (lpfc_cmd->status >= IOSTAT_CNT)
			lpfc_cmd->status = IOSTAT_DEFAULT;

		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0729 FCP cmd x%x failed <%d/%d> "
				 "status: x%x result: x%x Data: x%x x%x\n",
				 cmd->cmnd[0], cmd->device->id,
				 cmd->device->lun, lpfc_cmd->status,
				 lpfc_cmd->result, pIocbOut->iocb.ulpContext,
				 lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		switch (lpfc_cmd->status) {
		case IOSTAT_FCP_RSP_ERROR:
			/* Call FCP RSP handler to determine result */
			lpfc_handle_fcp_err(phba, vport, lpfc_cmd, pIocbOut);
			break;
		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			cmd->result = ScsiResult(DID_TRANSPORT_DISRUPTED, 0);
			break;
		case IOSTAT_LOCAL_REJECT:
			/*
			* The only requeue case is a resource condition
			* in the port.  All other local rejects are a
			* DID_ERROR condition and fall through to default.
			*/
			if (lpfc_cmd->result == IOERR_NO_RESOURCES) {
				cmd->result = ScsiResult(DID_REQUEUE, 0);
				break;
			}
		default:
			cmd->result = ScsiResult(DID_ERROR, 0);
			break;
		}

		if (!pnode || !NLP_CHK_NODE_ACT(pnode)
		    || (pnode->nlp_state != NLP_STE_MAPPED_NODE))
			cmd->result = ScsiResult(DID_TRANSPORT_DISRUPTED,
			SAM_STAT_BUSY);
	} else {
		cmd->result = ScsiResult(DID_OK, 0);
	}

	if (cmd->result || lpfc_cmd->fcp_rsp->rspSnsLen) {
		uint32_t *lp = (uint32_t *)cmd->sense_buffer;

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0710 Iodone <%d/%d> cmd %p, error "
				 "x%x SNS x%08x x%08x Data: x%x x%x\n",
				 cmd->device->id, cmd->device->lun, cmd,
				 cmd->result, be32_to_cpu(*lp),
				 be32_to_cpu(*(lp + 3)), cmd->retries,
				 cmd->resid);
	}

	result = cmd->result;

	/*
	 * Throttle number of outstanding I/O to a target when the I/O is
	 * outstanding for too long. When the I/O takes longer than
	 * "lpfc_max_scsicmpl_time" to complete, lower tgt qdepth, and start a
	 * LPFC_TGTQ_INTERVAL "recovery cycle". After it, start to bump up tgt
	 * qdepth by LPFC_TGTQ_RAMPUP_PCENT% per I/O completion. For any new
	 * violations, repeat the process.
	 */
	if (vport->cfg_max_scsicmpl_time &&
	   time_after(jiffies, lpfc_cmd->start_time +
		msecs_to_jiffies(vport->cfg_max_scsicmpl_time))) {
		spin_lock_irqsave(shost->host_lock, flags);
		if (pnode && NLP_CHK_NODE_ACT(pnode)) {
		    if (pnode->cmd_qdepth > atomic_read(&pnode->cmd_pending) &&
			(atomic_read(&pnode->cmd_pending) > 
			 LPFC_MIN_TGT_QDEPTH) &&
			((cmd->cmnd[0] == READ_10) ||
			 (cmd->cmnd[0] == WRITE_10))) {
			pnode->cmd_qdepth = atomic_read(&pnode->cmd_pending);
		    }

		    pnode->last_change_time = jiffies;
		}
		spin_unlock_irqrestore(shost->host_lock, flags);
	} else if (pnode && NLP_CHK_NODE_ACT(pnode)) {
		if ((pnode->cmd_qdepth < LPFC_MAX_TGT_QDEPTH) &&
		   time_after(jiffies, pnode->last_change_time +
			      msecs_to_jiffies(LPFC_TGTQ_INTERVAL))) {
			spin_lock_irqsave(shost->host_lock, flags);
			pnode->cmd_qdepth += pnode->cmd_qdepth *
				LPFC_TGTQ_RAMPUP_PCENT / 100;
			if (pnode->cmd_qdepth > LPFC_MAX_TGT_QDEPTH)
				pnode->cmd_qdepth = LPFC_MAX_TGT_QDEPTH;
			pnode->last_change_time = jiffies;
			spin_unlock_irqrestore(shost->host_lock, flags);
		}
	}

	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	spin_lock_irqsave(shost->host_lock, flags);

	/* The sdev is not guaranteed to be valid post scsi_done upcall. */
	lpfc_cmd->pCmd = NULL;	/* This must be done before scsi_done */
	queue_depth = cmd->device->queue_depth;
	scsi_id = cmd->device->id;
	spin_unlock_irqrestore(shost->host_lock, flags);
	cmd->scsi_done(cmd);

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		/*
		 * If there is a thread waiting for command completion
		 * wake up the thread.
		 */
		spin_lock_irqsave(shost->host_lock, flags);
		if (lpfc_cmd->waitq)
			wake_up(lpfc_cmd->waitq);
		spin_unlock_irqrestore(shost->host_lock, flags);
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return;
	}

	if (!result)
		lpfc_rampup_queue_depth(vport, queue_depth);

	if (!result && pnode && NLP_CHK_NODE_ACT(pnode) &&
	   ((jiffies - pnode->last_ramp_up_time) >
		LPFC_Q_RAMP_UP_INTERVAL * HZ) &&
	   ((jiffies - pnode->last_q_full_time) >
		LPFC_Q_RAMP_UP_INTERVAL * HZ) &&
	   (vport->cfg_lun_queue_depth > queue_depth)) {
		shost_for_each_device(tmp_sdev, shost) {
			if (vport->cfg_lun_queue_depth > tmp_sdev->queue_depth){
				if (tmp_sdev->id != scsi_id)
					continue;
				if (tmp_sdev->ordered_tags)
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_ORDERED_TAG,
						tmp_sdev->queue_depth+1);
				else
					scsi_adjust_queue_depth(tmp_sdev,
						MSG_SIMPLE_TAG,
						tmp_sdev->queue_depth+1);

				pnode->last_ramp_up_time = jiffies;
			}
		}
	}

	/*
	 * Check for queue full.  If the lun is reporting queue full, then
	 * back off the lun queue depth to prevent target overloads.
	 */
	if (result == SAM_STAT_TASK_SET_FULL && pnode &&
	    NLP_CHK_NODE_ACT(pnode)) {
		pnode->last_q_full_time = jiffies;

		shost_for_each_device(tmp_sdev, shost) {
			if (tmp_sdev->id != scsi_id)
				continue;
			depth = scsi_track_queue_full(tmp_sdev,
					tmp_sdev->queue_depth - 1);
		}
		/*
		 * The queue depth cannot be lowered any more.
		 * Modify the returned error code to store
		 * the final depth value set by
		 * scsi_track_queue_full.
		 */
		if (depth == -1)
			depth = shost->cmd_per_lun;

		if (depth) {
			lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
					 "0711 detected queue full - lun queue "
					 "depth adjusted to %d.\n", depth);
		}
	}

	/*
	 * If there is a thread waiting for command completion
	 * wake up the thread.
	 */
	spin_lock_irqsave(shost->host_lock, flags);
	if (lpfc_cmd->waitq)
		wake_up(lpfc_cmd->waitq);
	spin_unlock_irqrestore(shost->host_lock, flags);

	lpfc_release_scsi_buf(phba, lpfc_cmd);
}

static void
lpfc_scsi_prep_cmnd(struct lpfc_vport *vport, struct lpfc_scsi_buf *lpfc_cmd,
		    struct lpfc_nodelist *pnode)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);
	int datadir = scsi_cmnd->sc_data_direction;
	char tag[2];

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return;

	lpfc_cmd->fcp_rsp->rspSnsLen = 0;
	/* clear task management bits */
	lpfc_cmd->fcp_cmnd->fcpCntl2 = 0;

	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);

	memcpy(&fcp_cmnd->fcpCdb[0], scsi_cmnd->cmnd, 16);

	if (scsi_populate_tag_msg(scsi_cmnd, tag)) {
		switch (tag[0]) {
		case HEAD_OF_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = HEAD_OF_Q;
			break;
		case ORDERED_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = ORDERED_Q;
			break;
		default:
			fcp_cmnd->fcpCntl1 = SIMPLE_Q;
			break;
		}
	} else
		fcp_cmnd->fcpCntl1 = 0;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_cmnd->use_sg) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->un.fcpi.fcpi_parm = 0;
			iocb_cmd->ulpPU = 0;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			phba->fc4OutputRequests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			iocb_cmd->un.fcpi.fcpi_parm =
				scsi_cmnd->request_bufflen;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			phba->fc4InputRequests++;
		}
	} else if (scsi_cmnd->request_buffer && scsi_cmnd->request_bufflen) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->un.fcpi.fcpi_parm = 0;
			iocb_cmd->ulpPU = 0;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			phba->fc4OutputRequests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			iocb_cmd->un.fcpi.fcpi_parm =
				scsi_cmnd->request_bufflen;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			phba->fc4InputRequests++;
		}
	} else {
		iocb_cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		iocb_cmd->un.fcpi.fcpi_parm = 0;
		iocb_cmd->ulpPU = 0;
		fcp_cmnd->fcpCntl3 = 0;
		phba->fc4ControlRequests++;
	}

	/*
	 * Finish initializing those IOCB fields that are independent
	 * of the scsi_cmnd request_buffer
	 */
	piocbq->iocb.ulpContext = pnode->nlp_rpi;
	if (pnode->nlp_fcp_info & NLP_FCP_2_DEVICE)
		piocbq->iocb.ulpFCP2Rcvy = 1;
	else
		piocbq->iocb.ulpFCP2Rcvy = 0;

	piocbq->iocb.ulpClass = (pnode->nlp_fcp_info & 0x0f);
	piocbq->context1  = lpfc_cmd;
	piocbq->iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	piocbq->iocb.ulpTimeout = lpfc_cmd->timeout;
	piocbq->vport = vport;
}

static int
lpfc_scsi_prep_task_mgmt_cmd(struct lpfc_vport *vport,
			     struct lpfc_scsi_buf *lpfc_cmd,
			     unsigned int lun,
			     uint8_t task_mgmt_cmd)
{
	struct lpfc_iocbq *piocbq;
	IOCB_t *piocb;
	struct fcp_cmnd *fcp_cmnd;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ndlp->nlp_state != NLP_STE_MAPPED_NODE)
		return 0;

	piocbq = &(lpfc_cmd->cur_iocbq);
	piocbq->vport = vport;

	piocb = &piocbq->iocb;

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	int_to_scsilun(lun, &lpfc_cmd->fcp_cmnd->fcp_lun);
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;

	piocb->ulpCommand = CMD_FCP_ICMND64_CR;

	piocb->ulpContext = ndlp->nlp_rpi;
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
		piocb->ulpFCP2Rcvy = 1;
	}
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = lpfc_cmd->timeout;
	}

	return 1;
}

static void
lpfc_tskmgmt_def_cmpl(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct lpfc_scsi_buf *lpfc_cmd =
		(struct lpfc_scsi_buf *) cmdiocbq->context1;
	if (lpfc_cmd)
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	return;
}

static int
lpfc_scsi_tgt_reset(struct lpfc_vport *vport, unsigned int tgt_id,
		    unsigned int lun, struct lpfc_rport_data *rdata)
{
	struct lpfc_scsi_buf *lpfc_cmd;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocbqrsp;
	int ret;
	int status;

	if (!rdata->pnode || !NLP_CHK_NODE_ACT(rdata->pnode))
		return FAILED;

	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (!lpfc_cmd)
		return FAILED;

	lpfc_cmd->timeout = 60;
	lpfc_cmd->rdata = rdata;
	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd, lun,
					      FCP_TARGET_RESET);
	if (!status) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	iocbq = &lpfc_cmd->cur_iocbq;
	iocbqrsp = lpfc_sli_get_iocbq(phba);
	if (!iocbqrsp) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	/* Issue Target Reset to TGT <num> */
	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0702 Issue Target Reset to TGT %d Data: x%x x%x\n",
			 tgt_id, rdata->pnode->nlp_rpi, rdata->pnode->nlp_flag);

	status = lpfc_sli_issue_iocb_wait(phba,
					  &phba->sli.ring[phba->sli.fcp_ring],
					  iocbq, iocbqrsp, lpfc_cmd->timeout);
	if (status != IOCB_SUCCESS) {
		if (status == IOCB_TIMEDOUT) {
			iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;
			ret = TIMEOUT_ERROR;
		} else
			ret = FAILED;

		lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
	} else {
		ret = SUCCESS;
		lpfc_cmd->result = iocbqrsp->iocb.un.ulpWord[4];
		lpfc_cmd->status = iocbqrsp->iocb.ulpStatus;
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
			(lpfc_cmd->result & IOERR_DRVR_MASK))
				lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
	}

	if (ret != IOCB_TIMEDOUT)
		lpfc_release_scsi_buf(phba, lpfc_cmd);

	lpfc_sli_release_iocbq(phba, iocbqrsp);
	return ret;
}

const char *
lpfc_info(struct Scsi_Host *host)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int len;
	static char  lpfcinfobuf[384];

	memset(lpfcinfobuf,0,384);
	if (phba && phba->pcidev){
		strncpy(lpfcinfobuf, phba->ModelDesc, 256);
		len = strlen(lpfcinfobuf);
		snprintf(lpfcinfobuf + len,
			384-len,
			" on PCI bus %02x device %02x irq %d",
			phba->pcidev->bus->number,
			phba->pcidev->devfn,
			phba->pcidev->irq);
		len = strlen(lpfcinfobuf);
		if (phba->Port[0]) {
			snprintf(lpfcinfobuf + len,
				 384-len,
				 " port %s",
				 phba->Port);
		}
	}
	return lpfcinfobuf;
}

int
lpfc_proc_info(struct Scsi_Host *shost, char *buffer, char **start, off_t offset,
	       int length, int func)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	char fwrev[64];
	struct lpfc_nodelist *ndlp;
	uint8_t name[sizeof (struct lpfc_name)];
	int len = 0;
	uint32_t mrpi, arpi, mvpi, avpi, mxri, axri;
	uint32_t lpfc_log_setting;
	uint32_t san_display = 0;
	char log_cmd[32] = {0};
	int str_stat;
	char *info_buf;
	int byte_cnt;

	if (func) {
		/**
		 * Write path.  Only allow lpfc_log_verbose here.
		 * The log_cmd buffer is only 32 entries deep. Discount one
		 * for the NULL terminator and make sure the incoming buffer
		 * does not exceed the temporary driver buffer.
		 */
		sscanf(buffer, "%31s%x", log_cmd, &lpfc_log_setting);
		str_stat = strncmp(log_cmd, "lpfc_log_verbose",
				   sizeof("lpfc_log_verbose"));
		if (str_stat == 0) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"0701 Received %s value x%x\n",
					log_cmd, lpfc_log_setting);
			phba->pport->cfg_log_verbose = lpfc_log_setting;
			return length;
		}

	        str_stat = strncmp(log_cmd, "lpfc_vlog_verbose",
				   sizeof("lpfc_vlog_verbose"));
		if (str_stat == 0) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"0709 Received %s value x%x\n",
					log_cmd, lpfc_log_setting);
			phba->cfg_vport_log_override = lpfc_log_setting;
			return length;
		}

		return 0;
	} else {
		*start = buffer;
	}

	info_buf = kzalloc(INFO_BUF, GFP_KERNEL);
	if (info_buf == NULL) {
		lpfc_printf_log(phba, KERN_ERR, LOG_FCP,
				"0708 Memory alloc error in proc handler\n");
		return 0;
	}

	len += snprintf(info_buf, INFO_BUF, LPFC_MODULE_DESC "\n");

	len += snprintf(info_buf+len, INFO_BUF-len, "%s\n",
			lpfc_info(shost));

	len += snprintf(info_buf+len, INFO_BUF-len, "BoardNum: %d\n",
			phba->brd_no);

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	len += snprintf(info_buf+len, INFO_BUF-len, "Firmware Version: %s\n",
			fwrev);

	len += snprintf(info_buf+len, INFO_BUF-len, "Portname: ");
	memcpy (&name[0], &phba->pport->fc_portname, sizeof (struct lpfc_name));
	len += snprintf(info_buf+len, INFO_BUF-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			name[0], name[1], name[2],
			name[3], name[4], name[5],
			name[6], name[7]);

	len += snprintf(info_buf+len, INFO_BUF-len, "   Nodename: ");
	memcpy (&name[0], &phba->pport->fc_nodename,
		sizeof (struct lpfc_name));

	len += snprintf(info_buf+len, INFO_BUF-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			name[0], name[1], name[2],
			name[3], name[4], name[5],
			name[6], name[7]);

	if (phba->sli_rev == 3) {
		/*
		 * This operation needs to be synchronized as a vport could
		 * be getting destroyed while a proc script is running.
		 */
		mrpi = arpi = mvpi = avpi = 0;
		lpfc_get_hba_info(phba, &mxri, &axri, &mrpi, &arpi, &mvpi, &avpi);
		len += snprintf(info_buf+len, INFO_BUF-len, "\nSLI Rev: %d\n",
				phba->sli_rev);

		if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
			if (phba->link_flag & LS_NPIV_FAB_SUPPORTED) {
				len += snprintf(info_buf+len, INFO_BUF-len,
						"   NPIV Supported: ");
				len += snprintf(info_buf+len, INFO_BUF-len,
						"VPIs max %d  ", mvpi);
				len += snprintf(info_buf+len, INFO_BUF-len,
						"VPIs used %d\n", mvpi - avpi);
			} else {
				len += snprintf(info_buf+len, INFO_BUF-len,
					"   NPIV Unsupported by Fabric\n");
			}
		} else {
			if (phba->sli3_options & LPFC_SLI3_NPIV_ENABLED) {
				len += snprintf(info_buf+len, INFO_BUF-len,
					"   NPIV feature disabled.\n");
			} 
		}

		len += snprintf(info_buf+len, INFO_BUF-len, "   RPIs max %d  ",
				mrpi);
		len += snprintf(info_buf+len, INFO_BUF-len, "RPIs used %d\n\n",
				mrpi - arpi);

		len += snprintf(info_buf+len, INFO_BUF-len, "   Vport List:\n");

		/*
		 * The info_buf limit is INFO_BUF, 8192.  Size the number of
		 * vports by the number of targets and the fabric data so that
		 * the entire SAN is displayed along with the vports.  The
		 * assumption is that the number of actual targets is typically
		 * small allowing the code to optimize how many vports will fit.
		 */
		san_display = (TGT_BYTES * vport->fc_map_cnt) + SAN_BYTES;
		spin_lock_irq(&phba->hbalock);
		list_for_each_entry(vport, &phba->port_list, listentry) {
			if (vport->port_type == LPFC_PHYSICAL_PORT)
				continue;
			len += snprintf(info_buf+len, INFO_BUF-len,
				"   Vport DID 0x%x, vpi %d, state 0x%x\n",
				vport->fc_myDID, vport->vpi, vport->port_state);
			len += snprintf(info_buf+len, INFO_BUF-len,
				"      Portname: ");
			memcpy (&name[0], &vport->fc_portname,
				sizeof (struct lpfc_name));
			len += snprintf(info_buf+len, INFO_BUF-len,
				"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
				name[0], name[1], name[2],
				name[3], name[4], name[5],
				name[6], name[7]);
			len += snprintf(info_buf+len, INFO_BUF-len,
					"  Nodename: ");
			memcpy (&name[0], &vport->fc_nodename,
				sizeof (struct lpfc_name));
			len += snprintf(info_buf+len, INFO_BUF-len,
				"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				name[0], name[1], name[2],
				name[3], name[4], name[5],
				name[6], name[7]);

			/* Is there room to insert another vport display? */
			if (((INFO_BUF - len) - VPORT_BYTES) <= san_display)
				break;
		}
		spin_unlock_irq(&phba->hbalock);
	}


	switch (phba->link_state) {
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
		len += snprintf(info_buf + len, INFO_BUF-len, "\nLink Down\n");
		break;
	case LPFC_LINK_UP:
		len += snprintf(info_buf + len, INFO_BUF-len, "\nLink Up\n");
		break;
	case LPFC_HBA_READY:
		len += snprintf(info_buf + len, INFO_BUF-len,
				"\nLink Up - Ready:\n");
		len += snprintf(info_buf + len, INFO_BUF-len, "   PortID 0x%x\n",
				phba->pport->fc_myDID);
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->pport->fc_flag & FC_PUBLIC_LOOP)
				len += snprintf(info_buf + len, INFO_BUF-len,
						"   Public Loop\n");
			else
				len += snprintf(info_buf + len, INFO_BUF-len,
						"   Private Loop\n");
		} else {
			if (phba->pport->fc_flag & FC_FABRIC)
				len += snprintf(info_buf + len, INFO_BUF-len,
						"   Fabric\n");
			else
				len += snprintf(info_buf + len, INFO_BUF-len,
						"   Point-2-Point\n");
		}

		if (phba->fc_linkspeed == LA_8GHZ_LINK)
			len += snprintf(info_buf + len, INFO_BUF-len,
					"   Current speed 8G\n\n");
		else
		if (phba->fc_linkspeed == LA_4GHZ_LINK)
			len += snprintf(info_buf + len, INFO_BUF-len,
					"   Current speed 4G\n\n");
		else
		if (phba->fc_linkspeed == LA_2GHZ_LINK)
			len += snprintf(info_buf + len, INFO_BUF-len,
					"   Current speed 2G\n\n");
		else
		if (phba->fc_linkspeed == LA_1GHZ_LINK)
			len += snprintf(info_buf + len, INFO_BUF-len,
					"   Current speed 1G\n\n");
		else
			len += snprintf(info_buf + len, INFO_BUF-len,
					"   Current speed unknown\n\n");

		len += snprintf(info_buf + len, INFO_BUF-len,
				"Physical Port Discovered Nodes: Count %d\n",
				phba->pport->fc_map_cnt);

		spin_lock_irq(shost->host_lock);
		list_for_each_entry(ndlp, &phba->pport->fc_nodes, nlp_listp) {
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE) {
				len += snprintf(info_buf + len, INFO_BUF -len,
					"t%02x DID %06x State %02x WWPN ",
					ndlp->nlp_sid, ndlp->nlp_DID,
					ndlp->nlp_state);

				memcpy (&name[0], &ndlp->nlp_portname,
					sizeof (struct lpfc_name));
				len += snprintf(info_buf + len, INFO_BUF-len,
				"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
				name[0], name[1], name[2],
				name[3], name[4], name[5],
				name[6], name[7]);

				len += snprintf(info_buf + len, INFO_BUF-len,
						" WWNN ");

				memcpy (&name[0], &ndlp->nlp_nodename,
					sizeof (struct lpfc_name));
				len += snprintf(info_buf + len, INFO_BUF-len,
				"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				name[0], name[1], name[2],
				name[3], name[4], name[5],
				name[6], name[7]);
			}

			/* Is there room to insert another target display? */
			if (((INFO_BUF - len) - TGT_BYTES) <= TGT_BYTES)
				break;
		}
		spin_unlock_irq(shost->host_lock);
		break;

	default:
		len += snprintf(info_buf + len, INFO_BUF-len,
				"\nError: State is %d\n", phba->link_state);
		break;
	}

	/*
	 * The proc filesystem will repeatedly call this handler until
	 * handler returns zero bytes.  The MIN value and associated logic
	 * have to ensure this requirement is met.  Assume a byte_cnt of zero
	 * and produce the difference only when justified.
	 */
	byte_cnt = 0;
	if (len >= offset)
		byte_cnt = len - offset;

	byte_cnt = MIN(byte_cnt, length);
	len = 0;
	if (byte_cnt > 0) {
		memcpy(buffer, info_buf + offset, byte_cnt);
		len = byte_cnt;
	}

	kfree(info_buf);
	return len;
}


static __inline__ void lpfc_poll_rearm_timer(struct lpfc_hba * phba)
{
	unsigned long  poll_tmo_expires =
		(jiffies + msecs_to_jiffies(phba->cfg_poll_tmo));

	if (phba->sli.ring[LPFC_FCP_RING].txcmplq_cnt)
		mod_timer(&phba->fcp_poll_timer,
			  poll_tmo_expires);
}

void lpfc_poll_start_timer(struct lpfc_hba * phba)
{
	lpfc_poll_rearm_timer(phba);
}

void lpfc_poll_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba = (struct lpfc_hba *) ptr;

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring (phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}
}

static int
lpfc_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli   *psli = &phba->sli;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct scsi_device *sdev;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	int err;

	err = fc_remote_port_chkready(rport);
	if (err) {
		cmnd->result = err;
		sdev = cmnd->device;
		if ((phba->link_state == LPFC_HBA_ERROR) &&
			(cmnd->result == ScsiResult(DID_NO_CONNECT, 0)) &&
			(sdev->queue_depth != 1)) {

			/* If we reach this point, the HBA is not responding
			 * and has been taken offline. If alot of SCSI IO
			 * was active, this could cause a massive amount of
			 * SCSI layer initiated logging. On some systems,
			 * this massive amount of logging has been known to
			 * cause CPU soft lockups. In an attempt to throttle
			 * the amount of logging, set the sdev queue depth to 1.
			 */
			if (sdev->ordered_tags)
				scsi_adjust_queue_depth(sdev,
					MSG_ORDERED_TAG, 1);
			else
				scsi_adjust_queue_depth(sdev,
					MSG_SIMPLE_TAG, 1);
		}
		goto out_fail_command;
	}

	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		cmnd->result = ScsiResult(DID_TRANSPORT_DISRUPTED, 0);
		goto out_fail_command;
	}
 	if (vport->cfg_max_scsicmpl_time &&
 	    (atomic_read(&ndlp->cmd_pending) >= ndlp->cmd_qdepth))
		goto out_host_busy;

	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == NULL) {
		lpfc_adjust_queue_depth(phba);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0707 driver's buffer pool is empty, "
				 "IO busied\n");
		goto out_host_busy;
	}

	/*
	 * Store the midlayer's command structure for the completion phase
	 * and complete the command initialization.  Also initialize the
	 * lpfc_cmd timeout value so that the FW will abort the scsi IO
	 * after timeout seconds.  This mechanism is a backup for vmkernel's
	 * scsi command error escalation policy.
	 */
	lpfc_cmd->pCmd  = cmnd;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->timeout = LPFC_SCSI_CMD_TMO;

	lpfc_cmd->start_time = jiffies;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;
	cmnd->scsi_done = done;

	err = lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
	if (err)
		goto out_host_busy_free_buf;

	lpfc_scsi_prep_cmnd(vport, lpfc_cmd, ndlp);

	atomic_inc(&ndlp->cmd_pending);
	err = lpfc_sli_issue_iocb(phba, &phba->sli.ring[psli->fcp_ring],
				  &lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);
	if (err) {
		atomic_dec(&ndlp->cmd_pending);
		goto out_host_busy_free_buf;
	}

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring(phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;

 out_host_busy_free_buf:
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	lpfc_release_scsi_buf(phba, lpfc_cmd);
 out_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

 out_fail_command:
	done(cmnd);
	return 0;
}

static void
lpfc_block_error_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));

	spin_lock_irq(shost->host_lock);
	while (rport->port_state == FC_PORTSTATE_BLOCKED) {
		spin_unlock_irq(shost->host_lock);
		msleep(1000);
		spin_lock_irq(shost->host_lock);
	}
	spin_unlock_irq(shost->host_lock);
	return;
}

static int
lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli_ring *pring = &phba->sli.ring[phba->sli.fcp_ring];
	struct lpfc_iocbq *iocb;
	struct lpfc_iocbq *abtsiocb;
	struct lpfc_scsi_buf *lpfc_cmd;
	IOCB_t *cmd, *icmd;
	int ret = SUCCESS;
#ifdef DECLARE_WAIT_QUEUE_HEAD_ONSTACK
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waitq);
#else
	DECLARE_WAIT_QUEUE_HEAD(waitq);
#endif

	lpfc_block_error_handler(cmnd);
	lpfc_cmd = (struct lpfc_scsi_buf *)cmnd->host_scribble;
#if defined(__VMKLNX__)
	if (!lpfc_cmd) {
		printk(KERN_ERR "%s: SCSI Layer try to abort a completed I/O %p "
			"ID %d LUN %d snum %#lx\n", __FUNCTION__, cmnd,
			cmnd->device->id, cmnd->device->lun, cmnd->serial_number);
		return FAILED;
	}
#else
	BUG_ON(!lpfc_cmd);
#endif

	/*
	 * If pCmd field of the corresponding lpfc_scsi_buf structure
	 * points to a different SCSI command, then the driver has
	 * already completed this command, but the midlayer did not
	 * see the completion before the eh fired.  Just return
	 * SUCCESS.
	 */
	iocb = &lpfc_cmd->cur_iocbq;
	if (lpfc_cmd->pCmd != cmnd)
		goto out;

	BUG_ON(iocb->context1 != lpfc_cmd);

	abtsiocb = lpfc_sli_get_iocbq(phba);
	if (abtsiocb == NULL) {
		ret = FAILED;
		goto out;
	}

	/* Prep the ABTS command and just send an abort to the FW. */
	cmd = &iocb->iocb;
	icmd = &abtsiocb->iocb;
	icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	icmd->un.acxri.abortContextTag = cmd->ulpContext;
	icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	icmd->ulpLe = 1;
	icmd->ulpClass = cmd->ulpClass;
	if (lpfc_is_link_up(phba))
		icmd->ulpCommand = CMD_ABORT_XRI_CN;
	else
		icmd->ulpCommand = CMD_CLOSE_XRI_CN;

	abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
	abtsiocb->vport = vport;
	if (lpfc_sli_issue_iocb(phba, pring, abtsiocb, 0) == IOCB_ERROR) {
		lpfc_sli_release_iocbq(phba, abtsiocb);
		ret = FAILED;
		goto out;
	}

	if (phba->cfg_poll & DISABLE_FCP_RING_INT)
		lpfc_sli_poll_fcp_ring (phba);

	lpfc_cmd->waitq = &waitq;
	wait_event_timeout(waitq,
			  (lpfc_cmd->pCmd != cmnd),
			   (2*vport->cfg_devloss_tmo*HZ));

	spin_lock_irq(shost->host_lock);
	lpfc_cmd->waitq = NULL;
	spin_unlock_irq(shost->host_lock);

	if (lpfc_cmd->pCmd == cmnd) {
		ret = FAILED;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0748 abort handler timed out waiting "
				 "for abort to complete: ret %#x, <%d:%d>, "
				 "snum %#lx\n",
				 ret, cmnd->device->id, cmnd->device->lun,
				 cmnd->serial_number);
	}

 out:
	lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "0749 SCSI Layer I/O Abort Request Status x%x <%d:%d> "
			 "snum %#lx\n", ret, cmnd->device->id,
			 cmnd->device->lun, cmnd->serial_number);
	return ret;
}

static int
lpfc_device_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct lpfc_iocbq *iocbq, *iocbqrsp;
	struct lpfc_rport_data *rdata = cmnd->device->hostdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	unsigned long later;
	int ret = SUCCESS;
	int status, cnt;
	char reset_type[16] = {"target"};
	lpfc_ctx_cmd reset_cntxt = LPFC_CTX_TGT;

	lpfc_block_error_handler(cmnd);

	/*
	 * If target is not in a MAPPED state, delay the reset until
	 * target is rediscovered or devloss timeout expires.
	 */
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies)) {
		if (!pnode || !NLP_CHK_NODE_ACT(pnode))
			return FAILED;
		if (pnode->nlp_state == NLP_STE_MAPPED_NODE)
			break;
		schedule_timeout_uninterruptible(msecs_to_jiffies(500));
		rdata = cmnd->device->hostdata;
		if (!rdata)
			break;
		pnode = rdata->pnode;
	}
	if (!rdata || pnode->nlp_state != NLP_STE_MAPPED_NODE) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0721 LUN Reset rport "
				 "failure: msec x%x rdata x%p\n",
				 jiffies_to_msecs(jiffies - later), rdata);
		return FAILED;
	}

	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == NULL)
		return FAILED;

	lpfc_cmd->timeout = 60;
	lpfc_cmd->rdata = rdata;

#ifdef __VMKLNX__
	/*
	 * The device reset strategy is set by vmkernel.  The reset desired
	 * by vmkernel is passed in the scsi command.  This code just reads
	 * what is wanted by vmkernel and executes it so that vmkernel's
	 * error escalation semantics are preserved.
	 */
	if (!(cmnd->vmkflags & VMK_FLAGS_USE_LUNRESET)) {
		status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd,
						      cmnd->device->lun,
						      FCP_TARGET_RESET);
	} else {
		status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd,
						      cmnd->device->lun,
						      FCP_LUN_RESET);
		strcpy(reset_type, "lun");
		reset_cntxt = LPFC_CTX_LUN;
	}
#else
	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd,
					      cmnd->device->lun,
					      FCP_TARGET_RESET);
#endif
		
	if (!status) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	iocbq = &lpfc_cmd->cur_iocbq;

	/* get a buffer for this IOCB command response */
	iocbqrsp = lpfc_sli_get_iocbq(phba);
	if (iocbqrsp == NULL) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0703 Issue %s reset to <%d:%d> "
			 "rpi x%x nlp_flag x%x\n", reset_type, cmnd->device->id,
			 cmnd->device->lun, pnode->nlp_rpi, pnode->nlp_flag);

	status = lpfc_sli_issue_iocb_wait(phba,
					  &phba->sli.ring[phba->sli.fcp_ring],
					  iocbq, iocbqrsp, lpfc_cmd->timeout);

	if (status == IOCB_TIMEDOUT) {
		iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;
		ret = TIMEOUT_ERROR;
	} else {
		if (status != IOCB_SUCCESS)
			ret = FAILED;
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	}

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0713 SCSI layer issued device %s reset <%d:%d> "
			 "return x%x status x%x result x%x\n", reset_type,
			 cmnd->device->id, cmnd->device->lun, ret,
			 iocbqrsp->iocb.ulpStatus,
			 iocbqrsp->iocb.un.ulpWord[4]);

	lpfc_sli_release_iocbq(phba, iocbqrsp);

	/*
	 * Ensure all remaining I/Os get aborted before returning to the
	 * scsi layer.
	 */
	cnt = lpfc_sli_sum_iocb(vport, cmnd->device->id, cmnd->device->lun,
				reset_cntxt);
	if (cnt) {
		lpfc_sli_abort_iocb(vport, &phba->sli.ring[phba->sli.fcp_ring],
				    cmnd->device->id, cmnd->device->lun,
				    reset_cntxt);
	}

	/* The loop time for aborting all remaining IO is 2*devloss. */
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, cmnd->device->id,
					cmnd->device->lun, reset_cntxt);
	}
	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0719 %s reset I/O flush failure: "
				 "cnt x%x\n", reset_type, cnt);
		ret = FAILED;
	}

	return ret;
}

static int
lpfc_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_rport_data *rdata;
	int match;
	int ret = SUCCESS, status, i;
	int cnt;
	unsigned long later;

	lpfc_block_error_handler(cmnd);

	/*
	 * Since the driver manages a single bus device, reset all
	 * targets known to the driver.  Final status to vmkernel
	 * depends just on a successful reset to all targets.
	 */
	for (i = 0; i < LPFC_MAX_TARGET; i++) {
		match = 0;
		spin_lock_irq(shost->host_lock);
		list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE &&
			    ndlp->nlp_sid == i && ndlp->rport) {
				match = 1;
				break;
			}
		}
		spin_unlock_irq(shost->host_lock);
		if (!match)
			continue;

		rdata = (struct lpfc_rport_data *) ndlp->rport->dd_data;
		status = lpfc_scsi_tgt_reset(vport, i, cmnd->device->lun, rdata);
		if (status != SUCCESS) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0700 Bus Reset on target %d failed\n",
					 i);
			ret = FAILED;
		}
	}

	/* Abort all remaining I/Os before returning to the scsi layer. */
	cnt = lpfc_sli_sum_iocb(vport, 0, 0, LPFC_CTX_HOST);
	if (cnt) {
		lpfc_sli_abort_iocb(vport, &phba->sli.ring[phba->sli.fcp_ring],
				    0, 0, LPFC_CTX_HOST);
	}

	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, 0, 0, LPFC_CTX_HOST);
	}

	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
				 "0715 Bus Reset I/O flush failure: "
				 "x%x IOs outstanding after %d secs\n",
				 cnt, vport->cfg_devloss_tmo * 2);
		ret = FAILED;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "0714 SCSI Layer Bus Reset Status: %d\n", ret);
	return ret;
}

static int
lpfc_slave_alloc(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_scsi_buf *scsi_buf = NULL;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	uint32_t total = 0, i;
	uint32_t num_to_alloc = 0;
	unsigned long flags;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = rport->dd_data;

	/*
	 * Populate the cmds_per_lun count scsi_bufs into this host's globally
	 * available list of scsi buffers.  Don't allocate more than the
	 * HBA limit conveyed to the midlayer via the host structure.  The
	 * formula accounts for the lun_queue_depth + error handlers + 1
	 * extra.  This list of scsi bufs exists for the lifetime of the driver.
	 */
	total = phba->total_scsi_bufs;
	num_to_alloc = vport->cfg_lun_queue_depth + 2;

	/* Allow some exchanges to be available always to complete discovery */
	if (total >= phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0704 At limitation of %d preallocated "
				 "command buffers\n", total);
		return 0;
	/* Allow some exchanges to be available always to complete discovery */
	} else if (total + num_to_alloc >
		phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0705 Allocation request of %d "
				 "command buffers will exceed max of %d.  "
				 "Reducing allocation request to %d.\n",
				 num_to_alloc, phba->cfg_hba_queue_depth,
				 (phba->cfg_hba_queue_depth - total));
		num_to_alloc = phba->cfg_hba_queue_depth - total;
	}

	for (i = 0; i < num_to_alloc; i++) {
		scsi_buf = lpfc_new_scsi_buf(vport);
		if (!scsi_buf) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
					 "0706 Failed to allocate "
					 "command buffer\n");
			break;
		}

		spin_lock_irqsave(&phba->scsi_buf_list_lock, flags);
		phba->total_scsi_bufs++;
		list_add_tail(&scsi_buf->list, &phba->lpfc_scsi_buf_list);
		spin_unlock_irqrestore(&phba->scsi_buf_list_lock, flags);
	}
	return 0;
}

static int
lpfc_slave_configure(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct fc_rport   *rport = starget_to_rport(sdev->sdev_target);

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, vport->cfg_lun_queue_depth);
	else
		scsi_deactivate_tcq(sdev, vport->cfg_lun_queue_depth);

	/*
	 * Initialize the fc transport attributes for the target
	 * containing this scsi device.  Also note that the driver's
	 * target pointer is stored in the starget_data for the
	 * driver's sysfs entry point functions.
	 */
	rport->dev_loss_tmo = vport->cfg_devloss_tmo;

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_poll_fcp_ring(phba);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;
}

static void
lpfc_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
	return;
}

/*
 * For ESX, there are two templates, lpfc_template and lpfc_vport_template.
 * The Linux Version check is eliminated because of the different meaning
 * of KERNEL_VERSION definition and the functionality in vmkernel.
*/
struct scsi_host_template lpfc_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler= lpfc_device_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.proc_info		= lpfc_proc_info,
	.proc_name		= LPFC_DRIVER_NAME,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFF,
};

struct scsi_host_template lpfc_vport_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler= lpfc_device_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING,
	.shost_attrs		= lpfc_vport_attrs,
	.max_sectors		= 0xFFFF,
};
