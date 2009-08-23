/*
 * ****************************************************************
 * Portions Copyright 2008 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

#if defined(__VMKLNX__)

#include <linux/kernel.h>
#include <linux/libata.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "kcompat.h"

/*
 * This function is defined to help free the memory allocated by libata module
 * In VMware ESX, only the module allocating the memory can free it.
 */
void ata_kfree(const void *p)
{
	kfree(p);
}
EXPORT_SYMBOL_GPL(ata_kfree);

static int ata_cmd_completed(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	int ret1 = 0, ret2 = 0, ret3 = 0, ret4 = 0;

	DPRINTK("%s: ap %p, sactive %x, active_tag %d, qc_active %x, qc %p, "
		"tag %d, %sflags %lx\n",
		__FUNCTION__, ap, ap->sactive, ap->active_tag, ap->qc_active,
		qc, qc->tag, (qc->tf.protocol == ATA_PROT_NCQ)?"NCQ, ":"", 
		qc->flags & ATA_QCFLAG_ACTIVE);

	if (qc->tag == ATA_TAG_POISON)
		ret1 = 1;
	else {
		if (qc->tf.protocol == ATA_PROT_NCQ) {
			if (!(ap->sactive & (1 << qc->tag)))
				ret2 = 1;
		}
		else {
			if (ap->active_tag == ATA_TAG_POISON)
				ret2 = 1;
		}

		if (!(qc->flags & ATA_QCFLAG_ACTIVE))
			ret3 = 1;

		if (!(ap->qc_active & (1 << qc->tag)))
			ret4 = 1;
	}

	DPRINTK("%s: ap %p, qc %p, rets %d, %d, %d, %d\n", 
		__FUNCTION__, ap, qc, ret1, ret2, ret3, ret4);
	if (ret1 || ret2 || ret3 || ret4)
		return 1;
	else
		return 0;
}

int ata_scsi_abort(struct scsi_cmnd *cmd)
{
	struct ata_port *ap;
	struct scsi_device *scsidev;
	struct Scsi_Host *shost;
	struct ata_queued_cmd *qc;
	int rc;
	int i; 

	BUG_ON(cmd == NULL);
	scsidev = cmd->device;
	BUG_ON(scsidev == NULL);
	shost = scsidev->host;
	BUG_ON(shost == NULL);

	/* If no port available, no way to abort, fail */
	ap = ata_shost_to_port(shost);
	if (ap == NULL) {
		printk("%s: cmd %p, ap not found\n", __FUNCTION__, cmd);
		rc = FAILED;
		goto exit_abort;
	}

	/* If the cmd is not found, return abort failed */
	for (i = 0; i < ATA_MAX_QUEUE; i++) {
		if ((qc = __ata_qc_from_tag(ap, i))) {
			if (qc->scsicmd == cmd)
				break;
		}
	}
	if (i == ATA_MAX_QUEUE) {
		printk("%s: ap %p, cmd %p not found\n", __FUNCTION__, ap, cmd);
		rc = FAILED;
		goto exit_abort;
	}

	printk("%s: ap %p, qc %p, cmd %p, tag %d, flags %x, "
		"waiting for completion\n", 
		__FUNCTION__, ap, qc, cmd, i, (unsigned int)qc->flags);
	rc = FAILED;
	for (i = 0; i < 5; i++) {
		if (ata_cmd_completed(ap, qc)) {
			printk("%s: ap %p, cmd %p completed in %d seconds\n",
				__FUNCTION__, ap, cmd, i);
			/* wait to make sure scsi_done is called */
			msleep(1000);
			rc = SUCCESS;
			break;
		}
		msleep(1000);
	}

exit_abort:
	printk("%s: ap %p, cmd %p, %s\n", 
		__FUNCTION__, ap, cmd, (rc==SUCCESS)?"SUCCEEDED":"FAILED");
	return rc;
}

int ata_scsi_device_reset(struct scsi_cmnd *cmd)
{
	printk("%s: cmd %p\n", __FUNCTION__, cmd);
	return SUCCESS;
}

int ata_scsi_bus_reset(struct scsi_cmnd *cmd)
{
	printk("%s: cmd %p\n", __FUNCTION__, cmd);
	return SUCCESS;
}

int ata_scsi_host_reset(struct scsi_cmnd *cmd)
{
	printk("%s: cmd %p\n", __FUNCTION__, cmd);
return SUCCESS;
}

#endif /* defined(__VMLNX__) */
