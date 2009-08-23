/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_compat.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "hbaapi.h"
#include "lpfc_ioctl.h"
#include "lpfc_crtn.h"

/* Define forward prototypes. */
static int lpfc_ioctl_lip(struct lpfc_hba *, struct lpfcCmdInput *, void *);
static int lpfc_ioctl_reset(struct lpfc_hba *, struct lpfcCmdInput *);

int
lpfc_process_ioctl_dfc(struct lpfc_hba * phba, struct lpfcCmdInput * cip)
{
	int rc = -1;
	uint32_t total_mem;
	void   *dataout;

	if (cip->lpfc_outsz >= 4096) {
		/*
		 * Allocate memory for ioctl data. If buffer is bigger than
		 * 64k, then we allocate 64k and re-use that buffer over and
		 * over to xfer the whole block. This is because Linux kernel
		 * has a problem allocating more than 120k of kernel space
		 * memory. Saw problem with GET_FCPTARGETMAPPING.
		 */
		if (cip->lpfc_outsz <= (64 * 1024))
			total_mem = cip->lpfc_outsz;
		else
			total_mem = 64 * 1024;		
	} else {
		total_mem = 4096;
	}

	dataout = kmalloc(total_mem, GFP_KERNEL);
	if (!dataout)
		return ENOMEM;

	switch (cip->lpfc_cmd) {
	case LPFC_LIP:
		rc = lpfc_ioctl_lip(phba, cip, dataout);
		break;
	case LPFC_RESET:
		rc = lpfc_ioctl_reset(phba, cip);
		break;
	}

	if (rc == 0) {
		if (cip->lpfc_outsz) {
			if (copy_to_user
			    ((uint8_t *) cip->lpfc_dataout,
			     (uint8_t *) dataout, (int)cip->lpfc_outsz)) {
				rc = EIO;
			}
		}
	}

	kfree(dataout);
	return rc;
}

int
lpfc_ioctl_lip(struct lpfc_hba *phba, struct lpfcCmdInput *cip, void *dataout)
{
	struct lpfc_sli_ring *pring;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i;
	struct lpfc_vport *vport = phba->pport;

	mbxstatus = MBXERR_ERROR;
	if (vport->port_state == LPFC_HBA_READY) {

		pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (pmboxq == NULL)
			return ENOMEM;

		i = 0;
		pring = &phba->sli.ring[phba->sli.fcp_ring];
		while (pring->txcmplq_cnt) {
			if (i++ > 500)
				break;
			mdelay(10);
		}

		lpfc_init_link(phba, pmboxq, phba->cfg_topology,
			       phba->cfg_link_speed);

		mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     phba->fc_ratov * 2);
		if (mbxstatus == MBX_TIMEOUT)
			pmboxq->mbox_cmpl = 0;
		else
			mempool_free(pmboxq, phba->mbox_mem_pool);
	}

	memcpy(dataout, (char *)&mbxstatus, sizeof (uint16_t));
	return 0;
}

int
lpfc_ioctl_reset (struct lpfc_hba * phba, struct lpfcCmdInput * cip)
{
        uint32_t offset;
        int rc = 0;

        offset = (ulong) cip->lpfc_arg1;
        switch (offset) {
        case 1:
		/* Selective reset */
		rc = lpfc_selective_reset(phba);
                break;
	case 2:
        default:
		/* The driver only supports selective reset. */
                rc = ERANGE;
                break;
        }

        return rc;
}
