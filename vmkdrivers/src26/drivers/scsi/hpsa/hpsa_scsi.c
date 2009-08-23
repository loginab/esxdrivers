/*
 *    Disk Array driver for HP Smart Array Controllers, 
 *    Copyright 2008, Hewlett Packard Company
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *    
 */

#if defined CONFIG_SCSI_HPSA || defined CONFIG_SCSI_HPSA_MODULE || defined __VMKLNX__

/* This file contains scsi-specific driver code. */

#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/atomic.h>

#include "scsi.h" 
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h> 

#include "hpsa_scsi.h"

#if defined(__VMKLNX__)
extern struct scsi_transport_template *hpsa_transport_template;
#endif

/* some prototypes... */ 
static int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int use_unit_num, /* 0: address the controller,
				      1: address logical volume log_unit, 
				      2: address is in scsi3addr */
	unsigned int log_unit,
	__u8	page_code,
	unsigned char *scsi3addr,
	int cmd_type);

static void cmd_free(ctlr_info_t *h, CommandList_struct *c, int got_from_pool);
static CommandList_struct * cmd_alloc(ctlr_info_t *h, int get_from_pool); 

static int hpsa_scsi_proc_info(
		struct Scsi_Host *sh,
		char *buffer, /* data buffer */
		char **start, 	   /* where data in buffer starts */
		off_t offset,	   /* offset from start of imaginary file */
		int length, 	   /* length of data in buffer */
		int func);	   /* 0 == read, 1 == write */

static int hpsa_scsi_queue_command (struct scsi_cmnd *cmd,
		void (* done)(struct scsi_cmnd *));
static int hpsa_eh_abort_handler(struct scsi_cmnd *scsicmd);
static int hpsa_eh_device_reset_handler(struct scsi_cmnd *scsicmd);
static int hpsa_eh_bus_reset_handler(struct scsi_cmnd *scsicmd);
static int hpsa_eh_host_reset_handler(struct scsi_cmnd *scsicmd);
static int hpsa_slave_alloc(struct scsi_device *);

static struct hpsa_scsi_hba_t hpsascsi[MAX_CTLR] = {
	{ .name = "hpsa0", .ndevices = 0 },
	{ .name = "hpsa1", .ndevices = 0 },
	{ .name = "hpsa2", .ndevices = 0 },
	{ .name = "hpsa3", .ndevices = 0 },
	{ .name = "hpsa4", .ndevices = 0 },
	{ .name = "hpsa5", .ndevices = 0 },
	{ .name = "hpsa6", .ndevices = 0 },
	{ .name = "hpsa7", .ndevices = 0 },
};

static struct scsi_host_template hpsa_driver_template = {
	.module			= THIS_MODULE,
	.name			= "hpsa",
	.proc_name		= "hpsa",
	.proc_info		= hpsa_scsi_proc_info,
	.queuecommand		= hpsa_scsi_queue_command,
	.can_queue		= 512,
	.this_id		= 7,
	.sg_tablesize		= MAXSGENTRIES,
	.cmd_per_lun		= 512,
	.use_clustering		= DISABLE_CLUSTERING,
	/* Can't have eh_bus_reset_handler or eh_host_reset_handler for hpsa */
	.eh_device_reset_handler= hpsa_eh_device_reset_handler,
	.eh_abort_handler	= hpsa_eh_abort_handler,
	.eh_bus_reset_handler	= hpsa_eh_bus_reset_handler,
	.eh_host_reset_handler	= hpsa_eh_host_reset_handler,
	.ioctl			= hpsa_ioctl,
	.compat_ioctl		= hpsa_compat_ioctl,
	.slave_alloc		= hpsa_slave_alloc,
};


struct hpsa_scsi_adapter_data_t {
	struct Scsi_Host *scsi_host;
	//struct hpsa_scsi_cmd_stack_t cmd_stack;
	int registered;
	spinlock_t lock; // to protect hpsascsi[ctlr]; 
};



/* scsi_device_types comes from scsi.h */
#define DEVICETYPE(n) (n<0 || n>MAX_SCSI_DEVICE_CODE) ? \
	"Unknown" : scsi_device_types[n]


static int 
hpsa_scsi_add_entry(int ctlr, int hostno, 
		unsigned char *scsi3addr, int devtype, 
		int bus, int target, int lun)
{
	/* assumes hba[ctlr]->scsi_ctlr->lock is held */ 
	int n = hpsascsi[ctlr].ndevices;
	struct hpsa_scsi_dev_t *sd;

	if (n >= MAX_DEVS) {
		printk("hpsa%d: Too many devices, "
			"some will be inaccessible.\n", ctlr);
		return -1;
	}
	sd = &hpsascsi[ctlr].dev[n];
	memcpy(&sd->scsi3addr[0], scsi3addr, 8);
	sd->devtype = devtype;

	// bus, target, and lun are set to -1 for tape operations.
	if ( bus != -1 ) {
		sd->bus = bus;
		sd->target = target;
		sd->lun = lun;
	}
	hpsascsi[ctlr].ndevices++;

	/* initially, (before registering with scsi layer) we don't 
	   know our hostno and we don't want to print anything first 
	   time anyway (the scsi layer's inquiries will show that info) */
	if (hostno != -1)
		printk("hpsa%d: %s device c%db%dt%dl%d added.\n", 
			ctlr, DEVICETYPE(sd->devtype), hostno, 
			sd->bus, sd->target, sd->lun);
	return 0;
}

static void
hpsa_scsi_remove_entry(int ctlr, int hostno, int entry)
{
	/* assumes hba[ctlr]->scsi_ctlr->lock is held */ 
	int i;
	struct hpsa_scsi_dev_t sd;

	if (entry < 0 || entry >= MAX_DEVS) return;
	sd = hpsascsi[ctlr].dev[entry];
	for (i=entry;i<hpsascsi[ctlr].ndevices-1;i++)
		hpsascsi[ctlr].dev[i] = hpsascsi[ctlr].dev[i+1];
	hpsascsi[ctlr].ndevices--;
	printk("hpsa%d: %s device c%db%dt%dl%d removed.\n",
		ctlr, DEVICETYPE(sd.devtype), hostno, 
			sd.bus, sd.target, sd.lun);
}


#define SCSI3ADDR_EQ(a,b) ( \
	(a)[7] == (b)[7] && \
	(a)[6] == (b)[6] && \
	(a)[5] == (b)[5] && \
	(a)[4] == (b)[4] && \
	(a)[3] == (b)[3] && \
	(a)[2] == (b)[2] && \
	(a)[1] == (b)[1] && \
	(a)[0] == (b)[0])

static int
adjust_hpsa_scsi_table(int ctlr, int hostno,
	struct hpsa_scsi_dev_t sd[], int nsds)
{
	/* sd contains scsi3 addresses and devtypes, but
	   bus target and lun are not filled in.  This funciton
	   takes what's in sd to be the current and adjusts
	   hpsascsi[] to be in line with what's in sd. */ 

	int i,j, found, changes=0;
	struct hpsa_scsi_dev_t *csd;
	unsigned long flags;

	spin_lock_irqsave( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);

	/* find any devices in hpsascsi[] that are not in 
	   sd[] and remove them from hpsascsi[] */

	i = 0;
	while(i<hpsascsi[ctlr].ndevices) {
		csd = &hpsascsi[ctlr].dev[i];
		found=0;
		for (j=0;j<nsds;j++) {
			if (SCSI3ADDR_EQ(sd[j].scsi3addr,
				csd->scsi3addr)) {
				if (sd[j].devtype == csd->devtype)
					found=2;
				else
					found=1;
				break;
			}
		}

		if (found == 0) { /* device no longer present. */ 
			changes++;
			printk("hpsa%d: %s device c%db%dt%dl%d removed.\n",
				ctlr, DEVICETYPE(csd->devtype), hostno, 
					csd->bus, csd->target, csd->lun);
			hpsa_scsi_remove_entry(ctlr, hostno, i);
			/* note, i not incremented */
		} 
		else if (found == 1) { /* device is different kind */
			changes++;
			printk("hpsa%d: device c%db%dt%dl%d type changed "
				"(device type now %s).\n",
				ctlr, hostno, csd->bus, csd->target, csd->lun,
					DEVICETYPE(csd->devtype));
			csd->devtype = sd[j].devtype;
			i++;	/* so just move along. */
		} else 		/* device is same as it ever was, */
			i++;	/* so just move along. */
	}

	/* Now, make sure every device listed in sd[] is also
 	   listed in hpsascsi[], adding them if they aren't found */

	for (i=0;i<nsds;i++) {
		found=0;
		for (j=0;j<hpsascsi[ctlr].ndevices;j++) {
			csd = &hpsascsi[ctlr].dev[j];
			if (SCSI3ADDR_EQ(sd[i].scsi3addr,
				csd->scsi3addr)) {
				if (sd[i].devtype == csd->devtype)
					found=2;	/* found device */
				else
					found=1; 	/* found a bug. */
				break;
			}
		}
		if (!found) {
			changes++;
			if (hpsa_scsi_add_entry(ctlr, hostno, 
				&sd[i].scsi3addr[0], sd[i].devtype,
				sd[i].bus, sd[i].target, sd[i].lun) != 0)
				break;
		} else if (found == 1) {
			/* should never happen... */
			changes++;
			printk("hpsa%d: device unexpectedly changed type\n",
				ctlr);
			/* but if it does happen, we just ignore that device */
		}
	}
	spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);

#ifdef HPSA_DEBUG
	if (!changes) 
		printk("hpsa%d: No device changes detected.\n", ctlr);
#endif

	return 0;
}

static int
lookup_scsi3addr(int ctlr, int bus, int target, int lun, char *scsi3addr)
{
	int i;
	struct hpsa_scsi_dev_t *sd;
	unsigned long flags;

	spin_lock_irqsave( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);

	for (i=0;i<hpsascsi[ctlr].ndevices;i++) {
		sd = &hpsascsi[ctlr].dev[i];
		if (sd->bus == bus &&
		    sd->target == target &&
		    sd->lun == lun) {
			memcpy(scsi3addr, &sd->scsi3addr[0], 8);
			spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
			return 0;
		}
	}
	spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
	return -1;
}

static void 
hpsa_scsi_setup(int cntl_num)
{
	struct hpsa_scsi_adapter_data_t * shba;

	hpsascsi[cntl_num].ndevices = 0;
	shba = (struct hpsa_scsi_adapter_data_t *)
		kmalloc(sizeof(*shba), GFP_KERNEL);	
	if (shba == NULL)
		return;
	shba->scsi_host = NULL;
	spin_lock_init(&shba->lock);
	shba->registered = 0;
	hba[cntl_num]->scsi_ctlr = (void *) shba;
	return;
}

static void
complete_scsi_command( CommandList_struct *cp, int timeout, __u32 tag)
{
	struct scsi_cmnd *cmd;
	ctlr_info_t *ctlr;
	u64bit addr64;
	ErrorInfo_struct *ei;

	unsigned char sense_key;
	unsigned char asc;      //additional sense code
	unsigned char ascq;     //additional sense code qualifier
	struct task_struct *rescan_thread; 

	ei = cp->err_info;

	/* First, see if it was a message rather than a command */
	if (cp->Request.Type.Type == TYPE_MSG)  {
		cp->cmd_type = CMD_MSG_DONE;
		return;
	}

	cmd = (struct scsi_cmnd *) cp->scsi_cmd;	
	ctlr = hba[cp->ctlr];
	rescan_thread = ctlr->rescan_thread;

	/* undo the DMA mappings */

	if (cmd->use_sg) {
		pci_unmap_sg(ctlr->pdev,
			cmd->request_buffer, cmd->use_sg,
				cmd->sc_data_direction); 
	}
	else if (cmd->request_bufflen) {
		addr64.val32.lower = cp->SG[0].Addr.lower;
		addr64.val32.upper = cp->SG[0].Addr.upper;
		pci_unmap_single(ctlr->pdev, (dma_addr_t) addr64.val,
			cmd->request_bufflen, 
				cmd->sc_data_direction);
	}

	cmd->result = (DID_OK << 16); 		/* host byte */
	cmd->result |= (COMMAND_COMPLETE << 8);	/* msg byte */
	/* cmd->result |= (GOOD < 1); */		/* status byte */

	cmd->result |= (ei->ScsiStatus);

	/* copy the sense data whether we need to or not. */

	memcpy(cmd->sense_buffer, ei->SenseInfo, 
		ei->SenseLen > SCSI_SENSE_BUFFERSIZE ?
			SCSI_SENSE_BUFFERSIZE : 
			ei->SenseLen);
	cmd->resid = ei->ResidualCnt;

	if(ei->CommandStatus != 0) 
	{ /* an error has occurred */ 
		switch(ei->CommandStatus)
		{
			case CMD_TARGET_STATUS:
				if ( ei->ScsiStatus  ) {
					sense_key = 0xf & ei->SenseInfo[2];     //Get sense key
					asc = ei->SenseInfo[12];                //Get additional sense code
					ascq = ei->SenseInfo[13];               //Get additional sense code qualifier
				}

				if ( ei->ScsiStatus == SAM_STAT_CHECK_CONDITION ) {

					if (sense_key == UNIT_ATTENTION ) {
						cmd->resid = cmd->request_bufflen;
						// If ASC/ASCQ indicate LUN DATA CHANGE,
						// wake rescan thread to update scsi devices.
						if ( ( asc == 0x3F)  && (ascq == 0xE) ) {
							printk(KERN_WARNING "hpsa%d: reported LUNs data has changed: "
										" waking rescan thread\n", cp->ctlr);
							wake_up_process(rescan_thread);
							break;
						}
					}

					if (sense_key == ILLEGAL_REQUEST ) {
						cmd->resid = cmd->request_bufflen;
						// If ASC/ASCQ indicate Logical Unit Not Supported condition,
						// Report this as NO CONNECT for VMware multipathing to work.
						if ( ( asc == 0x25)  && (ascq == 0x0) ) {
							cmd->result = DID_NO_CONNECT << 16;
							printk(KERN_WARNING "hpsa%d has check condition: "
									"path problem: "
									"returning no connection\n", cp->ctlr);
							break;
						}
					}

					if (sense_key == NOT_READY ) {
						// If Sense is Not Ready, Logical Unit Not ready, Manual Intervention required,
						// probably need to report this as NO CONNECT also for multipathing to work.
						if ( ( asc == 0x04)  && (ascq == 0x03) ) {
							cmd->result = DID_NO_CONNECT << 16;
							printk(KERN_WARNING "hpsa%d has check condition: "
								"unit not ready, manual intervention required: "
								"returning no connection\n", cp->ctlr);
							break;
						}
					}


					//Must be some other type of check condition
					cmd->result |= (ei->ScsiStatus < 1);
#ifdef HPSA_DEBUG
/*
 * Some device does not support REPORT LUNS command.
 * So, put it in a debugging print to avoid 0x5/0x20/0x00 log spews. 
 */
					printk(KERN_WARNING "hpsa%d has check condition: "
							"unknown type: "
							"Sense: 0x%x, ASC: 0x%x, ASCQ: 0x%x, "
							"Returning result: 0x%x, "
							"cmd=[%02x %02x %02x %02x %02x "
							"%02x %02x %02x %02x %02x]\n",
							cp->ctlr, sense_key, asc, ascq,
							cmd->result,
							cmd->cmnd[0], cmd->cmnd[1],
							cmd->cmnd[2], cmd->cmnd[3],
							cmd->cmnd[4], cmd->cmnd[5],
							cmd->cmnd[6], cmd->cmnd[7],
							cmd->cmnd[8], cmd->cmnd[9]);
#endif
					break;
				}


				//Problem was not a check condition.
				/* Pass it up to the upper layers... */
				if( ei->ScsiStatus) {

					cmd->result |= (ei->ScsiStatus < 1);
					printk(KERN_WARNING "hpsa%d: has status 0x%x "
							"Sense: 0x%x, ASC: 0x%x, ASCQ: 0x%x, "
							"Returning result: 0x%x\n",
							cp->ctlr, ei->ScsiStatus,
							sense_key, asc, ascq,
							cmd->result);
				}
				else {  /* scsi status is zero??? How??? */
					printk(KERN_WARNING "hpsa%d: SCSI status was 0. "
							"Returning no connection.\n", cp->ctlr),

					/* Ordinarily, this case should never happen, but there is a bug
					in some released firmware revisions that allows it to happen
					if, for example, a 4100 backplane loses power and the tape
					drive is in it.  We assume that it's a fatal error of some
					kind because we can't show that it wasn't. We will make it
					look like selection timeout since that is the most common
					reason for this to occur, and it's severe enough. */
	
					cmd->result = DID_NO_CONNECT << 16;
				}
				break;

			case CMD_DATA_UNDERRUN: /* let mid layer handle it. */
			break;
			case CMD_DATA_OVERRUN:
				printk(KERN_WARNING "hpsa%d has"
					" completed with data overrun "
					"reported\n", cp->ctlr);
			break;
			case CMD_INVALID: {
				/* print_bytes(cp, sizeof(*cp), 1, 0);
				print_cmd(cp); */
     /* We get CMD_INVALID if you address a non-existent device instead
	of a selection timeout (no response).  You will see this if you yank 
	out a drive, then try to access it. This is kind of a shame
	because it means that any other CMD_INVALID (e.g. driver bug) will
	get interpreted as a missing target. */
				cmd->result = DID_NO_CONNECT << 16;
				}
			break;
			case CMD_PROTOCOL_ERR:
				printk(KERN_WARNING "hpsa%d has "
					"protocol error \n", cp->ctlr);
			break;
			case CMD_HARDWARE_ERR:
				cmd->result = DID_ERROR << 16;
				printk(KERN_WARNING "hpsa%d had " 
					" hardware error\n", cp->ctlr);
			break;
			case CMD_CONNECTION_LOST:
				cmd->result = DID_ERROR << 16;
				printk(KERN_WARNING "hpsa%d had "
					"connection lost\n", cp->ctlr);
			break;
			case CMD_ABORTED:
				cmd->result = DID_ABORT << 16;
				printk(KERN_WARNING "hpsa%d was "
					 "aborted with status 0x%x\n", cp->ctlr, ei->ScsiStatus);
			break;
			case CMD_ABORT_FAILED:
				cmd->result = DID_ERROR << 16;
				printk(KERN_WARNING "hpsa%d reports "
					"abort failed\n", cp->ctlr);
			break;
			case CMD_UNSOLICITED_ABORT:
				cmd->result = DID_ABORT << 16;
				printk(KERN_WARNING "hpsa%d aborted "
					"do to an unsolicited abort\n", cp->ctlr);
			break;
			case CMD_TIMEOUT:
				cmd->result = DID_TIME_OUT << 16;
				printk(KERN_WARNING "hpsa%d timedout\n",
					cp->ctlr);
			break;
			default:
				cmd->result = DID_ERROR << 16;
				printk(KERN_WARNING "hpsa%d returned "
					"unknown status %x\n", cp->ctlr, 
						ei->CommandStatus); 
		}
	}
	cmd->scsi_done(cmd);
	cmd_free(ctlr, cp, 1); 
}

static int
hpsa_scsi_detect(int ctlr)
{
	struct Scsi_Host *sh;
	int error;
#if defined(__VMKLNX__)
	int target;
#endif

	sh = scsi_host_alloc(&hpsa_driver_template, sizeof(struct ctlr_info *));
	if (sh == NULL)
		goto fail;
#if defined(__VMKLNX__)
        // Since there is no block device IOCTL support in ESX 4.0, create
        // a character device for this controller
        char_major[ctlr] = register_chrdev(0, hba[ctlr]->devname, &hpsa_char_fops); 
        if (char_major[ctlr] < 0) {
                printk(KERN_WARNING "hpsa: failed to register char device %s\n",
			 hba[ctlr]->devname);

                return 0;
        }
        else
                printk(KERN_WARNING "hpsa: Created character device %s, major %d"
			" for host_no %d\n",
			hba[ctlr]->devname, char_major[ctlr], sh->host_no);
#endif
	sh->io_port = 0;	// not used
	sh->n_io_port = 0;	// I don't think we use these two...
	sh->this_id = SELF_SCSI_ID;  

	sh->max_lun = MAX_DEVS;
	sh->max_id = MAX_DEVS;
	((struct hpsa_scsi_adapter_data_t *) 
		hba[ctlr]->scsi_ctlr)->scsi_host = (void *) sh;
	sh->hostdata[0] = (unsigned long) hba[ctlr];
	sh->irq = hba[ctlr]->intr[SIMPLE_MODE_INT];
	sh->unique_id = sh->irq;
#if defined(__VMKLNX__)
        sh->transportt = hpsa_transport_template;	
#endif
	error = scsi_add_host(sh, &hba[ctlr]->pdev->dev);
	if (error)
		goto fail_host_put;
#if defined(__VMKLNX__)
        for (target = 0; target < sh->max_id; target++) {
                /*
                 * With scsi_scan_host, vmklinux will not be able to scanned and                 
                 * reported targets because real SAS infrastructure information
                 * is not available during scanning.
                 * So, change to scsi_scan_target as a workaround to avoid that
                 * situation to be able to register as a SAS capable driver.
                 *
                 * Current workaround assume that only channel 0 is supported
                 * to avoid long duration scan time.
                 */
                scsi_scan_target(&sh->shost_gendev, 0, target, ~0, 0);
        }
#else
	scsi_scan_host(sh);
#endif
	return 1;

 fail_host_put:
	printk("hpsa_scsi_detect: scsi_add_host failed for controller %d\n", ctlr);
	scsi_host_put(sh);
 fail:
	printk("hpsa_scsi_detect: scsi_host_alloc failed for controller %d\n", ctlr);
	return 0;
}

static void
hpsa_unmap_one(struct pci_dev *pdev,
		CommandList_struct *cp,
		size_t buflen,
		int data_direction)
{
	u64bit addr64;

	addr64.val32.lower = cp->SG[0].Addr.lower;
	addr64.val32.upper = cp->SG[0].Addr.upper;
	pci_unmap_single(pdev, (dma_addr_t) addr64.val, buflen, data_direction);
}

static void
hpsa_map_one(struct pci_dev *pdev,
		CommandList_struct *cp,
		unsigned char *buf,
		size_t buflen,
		int data_direction)
{
	__u64 addr64;

	addr64 = (__u64) pci_map_single(pdev, buf, buflen, data_direction);
	cp->SG[0].Addr.lower = 
	  (__u32) (addr64 & (__u64) 0x00000000FFFFFFFF);
	cp->SG[0].Addr.upper =
	  (__u32) ((addr64 >> 32) & (__u64) 0x00000000FFFFFFFF);
	cp->SG[0].Len = buflen;
	cp->Header.SGList = (__u8) 1;   /* no. SGs contig in this cmd */
	cp->Header.SGTotal = (__u16) 1; /* total sgs in this cmd list */
}

static int
hpsa_scsi_do_simple_cmd(ctlr_info_t *c,
			CommandList_struct *cp,
			unsigned char *scsi3addr, 
			unsigned char *cdb,
			unsigned char cdblen,
			unsigned char *buf, int bufsize,
			int direction)
{
	unsigned long flags;
	DECLARE_COMPLETION_ONSTACK(wait);

	cp->cmd_type = CMD_IOCTL_PEND;		// treat this like an ioctl 
	cp->scsi_cmd = NULL;
	cp->Header.ReplyQueue = 0;  // unused in simple mode
	memcpy(&cp->Header.LUN, scsi3addr, sizeof(cp->Header.LUN));
	cp->Header.Tag.lower = cp->busaddr;  // Use k. address of cmd as tag
	// Fill in the request block...

	memset(cp->Request.CDB, 0, sizeof(cp->Request.CDB));
	memcpy(cp->Request.CDB, cdb, cdblen);
	cp->Request.Timeout = 0;
	cp->Request.CDBLen = cdblen;
	cp->Request.Type.Type = TYPE_CMD;
	cp->Request.Type.Attribute = ATTR_SIMPLE;
	cp->Request.Type.Direction = direction;

	/* Fill in the SG list and do dma mapping */
	hpsa_map_one(c->pdev, cp, (unsigned char *) buf,
			bufsize, DMA_FROM_DEVICE); 

	cp->waiting = &wait;

	/* Put the request on the tail of the request queue */
	spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
	addQ(&c->reqQ, cp);
	c->Qdepth++;
	start_io(c);
	spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);

	wait_for_completion(&wait);

	/* undo the dma mapping */
	hpsa_unmap_one(c->pdev, cp, bufsize, DMA_FROM_DEVICE);
	return(0);
}

static void 
hpsa_scsi_interpret_error(CommandList_struct *cp)
{
	ErrorInfo_struct *ei;

	ei = cp->err_info; 
	switch(ei->CommandStatus)
	{
		case CMD_TARGET_STATUS:
			printk(KERN_WARNING "hpsa: cmd %p has "
				"completed with errors\n", cp);
			printk(KERN_WARNING "hpsa: cmd %p "
				"has SCSI Status = %x\n",
					cp,  
					ei->ScsiStatus);
			if (ei->ScsiStatus == 0)
				printk(KERN_WARNING 
				"hpsa:SCSI status is abnormally zero.  "
				"(probably indicates selection timeout "
				"reported incorrectly due to a known "
				"firmware bug, circa July, 2001.)\n");
		break;
		case CMD_DATA_UNDERRUN: /* let mid layer handle it. */
			printk("UNDERRUN\n");
		break;
		case CMD_DATA_OVERRUN:
			printk(KERN_WARNING "hpsa: cp %p has"
				" completed with data overrun "
				"reported\n", cp);
		break;
		case CMD_INVALID: {
			/* controller unfortunately reports SCSI passthru's */
			/* to non-existent targets as invalid commands. */
			printk(KERN_WARNING "hpsa: cp %p is "
				"reported invalid (probably means "
				"target device no longer present)\n", 
				cp); 
			/* print_bytes((unsigned char *) cp, sizeof(*cp), 1, 0);
			print_cmd(cp);  */
			}
		break;
		case CMD_PROTOCOL_ERR:
			printk(KERN_WARNING "hpsa: cp %p has "
				"protocol error \n", cp);
		break;
		case CMD_HARDWARE_ERR:
			/* cmd->result = DID_ERROR << 16; */
			printk(KERN_WARNING "hpsa: cp %p had " 
				" hardware error\n", cp);
		break;
		case CMD_CONNECTION_LOST:
			printk(KERN_WARNING "hpsa: cp %p had "
				"connection lost\n", cp);
		break;
		case CMD_ABORTED:
			printk(KERN_WARNING "hpsa: cp %p was "
				"aborted\n", cp);
		break;
		case CMD_ABORT_FAILED:
			printk(KERN_WARNING "hpsa: cp %p reports "
				"abort failed\n", cp);
		break;
		case CMD_UNSOLICITED_ABORT:
			printk(KERN_WARNING "hpsa: cp %p aborted "
				"do to an unsolicited abort\n", cp);
		break;
		case CMD_TIMEOUT:
			printk(KERN_WARNING "hpsa: cp %p timedout\n",
				cp);
		break;
		default:
			printk(KERN_WARNING "hpsa: cp %p returned "
				"unknown status %x\n", cp, 
					ei->CommandStatus); 
	}
}

static int
hpsa_scsi_do_inquiry(ctlr_info_t *c, unsigned char *scsi3addr, 
			 unsigned char *buf, unsigned char bufsize)
{
	int rc;
	CommandList_struct *cp;
	char cdb[6];
	ErrorInfo_struct *ei;
	unsigned long flags;

	spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
	cp = cmd_alloc(c, 0);
	spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);

	if (cp == NULL) {			/* trouble... */
		printk("cmd_alloc returned NULL!\n");
		return -1;
	}

	ei = cp->err_info; 

	cdb[0] = HPSA_INQUIRY;
	cdb[1] = 0;
	cdb[2] = 0;
	cdb[3] = 0;
	cdb[4] = bufsize;
	cdb[5] = 0;
	rc = hpsa_scsi_do_simple_cmd(c, cp, scsi3addr, cdb, 
				6, buf, bufsize, XFER_READ);

	if (rc != 0) return rc; /* something went wrong */

	if (ei->CommandStatus != 0 && 
	    ei->CommandStatus != CMD_DATA_UNDERRUN) {
		hpsa_scsi_interpret_error(cp);
		rc = -1;
	}
	spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
	cmd_free(c, cp, 0);
	spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);
	return rc;	
}

static int
hpsa_sense_subsystem_info(ctlr_info_t *c,
                SenseSubsystem_info_struct *buf, int bufsize)
{
        int rc;
        CommandList_struct *cp;
        unsigned char cdb[12];
        unsigned char scsi3addr[8];
        ErrorInfo_struct *ei;
        unsigned long flags;

        spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
        cp = cmd_alloc(c, 0);
        spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);
        if (cp == NULL) {                       /* trouble... */
                printk("cmd_alloc returned NULL!\n");
                return -1;
        }

        memset(&scsi3addr[0], 0, 8); /* address the controller */
        cdb[0] = 0x26;
        cdb[1] = 0;
        cdb[2] = 0;
        cdb[3] = 0;
        cdb[4] = 0;
        cdb[5] = 0;
        cdb[6] = 0x66;
        cdb[7] = (bufsize >> 8) & 0xFF;
        cdb[8] = bufsize & 0xFF;
        cdb[9] = 0;
        cdb[10] = 0;
        cdb[11] = 0;

        rc = hpsa_scsi_do_simple_cmd(c, cp, scsi3addr,
                                cdb, 12,
                                (unsigned char *) buf,
                                bufsize, XFER_READ);

        if (rc != 0) return rc; /* something went wrong */

        ei = cp->err_info;
        if (ei->CommandStatus != 0 &&
            ei->CommandStatus != CMD_DATA_UNDERRUN) {
                hpsa_scsi_interpret_error(cp);
                rc = -1;
        }
        spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
        cmd_free(c, cp, 0);
        spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);
        return rc;
}

static int
hpsa_scsi_do_report_luns(ctlr_info_t *c, int logical,
		ReportLunData_struct *buf, int bufsize, int extended_response)
{
	int rc;
	CommandList_struct *cp;
	unsigned char cdb[12];
	unsigned char scsi3addr[8]; 
	ErrorInfo_struct *ei;
	unsigned long flags;

	spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
	cp = cmd_alloc(c, 0);
	spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);
	if (cp == NULL) {			/* trouble... */
		printk("cmd_alloc returned NULL!\n");
		return -1;
	}

	memset(&scsi3addr[0], 0, 8); /* address the controller */
	cdb[0] = logical ? HPSA_REPORT_LOG : HPSA_REPORT_PHYS;
 	if (extended_response)
                cdb[1] = extended_response;
        else
                cdb[1] = 0; 	
	cdb[2] = 0;
	cdb[3] = 0;
	cdb[4] = 0;
	cdb[5] = 0;
	cdb[6] = (bufsize >> 24) & 0xFF;  //MSB
	cdb[7] = (bufsize >> 16) & 0xFF;
	cdb[8] = (bufsize >> 8) & 0xFF;
	cdb[9] = bufsize & 0xFF;
	cdb[10] = 0;
	cdb[11] = 0;

	rc = hpsa_scsi_do_simple_cmd(c, cp, scsi3addr, 
				cdb, 12, 
				(unsigned char *) buf, 
				bufsize, XFER_READ);

	if (rc != 0) return rc; /* something went wrong */

	ei = cp->err_info; 
	if (ei->CommandStatus != 0 && 
	    ei->CommandStatus != CMD_DATA_UNDERRUN) {
		hpsa_scsi_interpret_error(cp);
		rc = -1;
	}
	spin_lock_irqsave(HPSA_LOCK(c->ctlr), flags);
	cmd_free(c, cp, 0);
	spin_unlock_irqrestore(HPSA_LOCK(c->ctlr), flags);
	return rc;	
}

static inline int
hpsa_scsi_do_report_phys_luns(ctlr_info_t *c, 
		ReportLunData_struct *buf, int bufsize, int extended_response)
{
	return hpsa_scsi_do_report_luns(c, 0, buf, bufsize, extended_response);
}
static inline int
hpsa_scsi_do_report_log_luns(ctlr_info_t *c, 
		ReportLunData_struct *buf, int bufsize)
{
	return hpsa_scsi_do_report_luns(c, 1, buf, bufsize, 0); 
}

static void
hpsa_update_scsi_devices(int cntl_num, int hostno)
{
	/* the idea here is we could get notified 
	   that some devices have changed, so we do a report 
	   physical luns cmd, and adjust our list of devices 
	   accordingly.  (We can't rely on the scsi-mid layer just
	   doing inquiries, because the "busses" that the scsi 
	   mid-layer probes are totally fabricated by this driver,
	   so new devices wouldn't show up.

	   The scsi3addr's of devices won't change so long as the 
	   adapter is not reset.  That means we can rescan and 
	   tell which devices we already know about, vs. new 
	   devices, vs.  disappearing devices.
	 */
#define OBDR_TAPE_INQ_SIZE 49
#define OBDR_TAPE_SIG "$DR-10"
	ReportLunData_struct *device_list = NULL;
	ReportLunData_struct *logdev_list = NULL;
	ReportExtendedLunData_struct *extended_phys_list = NULL;
	SenseSubsystem_info_struct *sense_subsystem_info = NULL;
	unsigned char *inq_buff = NULL;

	unsigned char scsi3addr[8];
	ctlr_info_t *c;
	__u32 nphysicals=0;
	unsigned char *ch;
	struct hpsa_scsi_dev_t *currentsd;
	int ncurrent=0;
	int reportlunsize = sizeof(*device_list) + HPSA_MAX_PHYS_LUN * 8;
	int extended_phys_size = sizeof(*extended_phys_list) + HPSA_MAX_PHYS_LUN * 24;
	int nlogicals = 0;
	int i, j;

	int lun;
	int target;
	int hpsa_targets=0;	//number of targets registered

	u64 sas_id = 0;
        int sas_target = 0;
        char *ptr = (char *)&sas_id;

	char lun0_processing[MAX_CTLR][MAX_TARGETS_PER_CTLR];
	memset(&lun0_processing[0][0], 0, sizeof(lun0_processing));	

	currentsd = kmalloc(sizeof(*currentsd) * MAX_DEVS, GFP_KERNEL);
	if (currentsd == NULL)
		goto out;	

	c = (ctlr_info_t *) hba[cntl_num];	
	device_list = kzalloc(reportlunsize * 2, GFP_KERNEL);
	if (device_list == NULL) {
		printk(KERN_ERR "hpsa: out of memory\n");
		goto out;
	}
        extended_phys_list = kzalloc(extended_phys_size, GFP_KERNEL);
        if (extended_phys_list == NULL) {
                printk(KERN_ERR "hpsa: out of memory on extended_phys_list\n");
                goto out;
        }
        sense_subsystem_info = kzalloc(sizeof(SenseSubsystem_info_struct), GFP_KERNEL);
        if (sense_subsystem_info == NULL) {
                printk(KERN_ERR "hpsa: out of memory on sense_subsystem_info\n");
                goto out;
        }
	logdev_list = (ReportLunData_struct *) (((char *) device_list) + reportlunsize);

	inq_buff = kmalloc(OBDR_TAPE_INQ_SIZE, GFP_KERNEL);
	if (inq_buff == NULL) {
		printk(KERN_ERR "hpsa: out of memory\n");
		goto out;
	}
        //
        // First do the extended report_phys_luns to get the target SAS IDs
        // and the sense_subsystem call to get the controller SAS ID
        //
        if (hpsa_scsi_do_report_phys_luns(c, (ReportLunData_struct *)extended_phys_list, extended_phys_size, 2) == 0) {
                ch = &extended_phys_list->LUNListLength[0];
                nphysicals = ((ch[0]<<24) | (ch[1]<<16) | (ch[2]<<8) | ch[3]) / 24;
                if (nphysicals > HPSA_MAX_PHYS_LUN) {
                        printk(KERN_WARNING
                                "hpsa: Maximum physical LUNs (%d) exceeded.  "
                                "%d LUNs ignored.\n", HPSA_MAX_PHYS_LUN,
                                nphysicals - HPSA_MAX_PHYS_LUN);
                        nphysicals = HPSA_MAX_PHYS_LUN;
                }
                // Process each device looking for controller devices
                for (i = 0; i < nphysicals; i++)
                {
                        sas_target = (int)extended_phys_list->LUN[i][3] & 0x3f;

                        //
                        // Now reverse the endianness
                        //
                        ptr = (char *)&sas_id;
                        // int j;
                        for (j = 15; j > 7; j--)
                                *ptr++ = extended_phys_list->LUN[i][j];

                        // Is it a controller device?
                        if (extended_phys_list->LUN[i][16] == CONTROLLER_DEVICE)
                        {
                                if (sas_target > MAX_PATHS)
                                {
                                        printk(KERN_ERR "hpsa: sas_target of %d is greater than %d\n", sas_target, MAX_PATHS);
                                        goto out;
                                }
                                else
                                {
                                        // Save it off by cntl_num
                                        target_sas_id[cntl_num][sas_target] = sas_id;
                                }
                        }
                }
                //
                // Now do the sense_subsystem call to get the controller SAS id
                //
                if (hpsa_sense_subsystem_info(c, sense_subsystem_info, sizeof(SenseSubsystem_info_struct)) == 0)
                {
                        ptr = (char *)&sas_id;
                        // Reverse endianess
                        for (j = 7; j >= 0; j--)
                                *ptr++ = sense_subsystem_info->portname[j];
                        // Save off controller sas_id by cntl_num
                        cntl_sas_id[cntl_num] = sas_id;
			target_sas_id[cntl_num][0] = sas_id & 0x00ffffffffffffff;
                }
                else
                {
                        printk(KERN_ERR  "hpsa: sense subsystem info failed.\n");
                        goto out;
                }
        }
        else {
                printk(KERN_ERR  "hpsa: Report extended physical LUNs failed.\n");
                goto out;
        }

        //
        // Now do the regular report_phys_luns
        //
        if (hpsa_scsi_do_report_phys_luns(c, device_list, reportlunsize, 0) == 0) {	
		ch = &device_list->LUNListLength[0];
		nphysicals = ((ch[0]<<24) | (ch[1]<<16) | (ch[2]<<8) | ch[3]) / 8;
#ifdef DEBUG
                printk(KERN_INFO "hpsa: number of physical luns is %d\n", nphysicals);
#endif
		if (nphysicals > HPSA_MAX_PHYS_LUN) {
			printk(KERN_WARNING 
				"hpsa: Maximum physical LUNs (%d) exceeded.  "
				"%d LUNs ignored.\n", HPSA_MAX_PHYS_LUN, 
				nphysicals - HPSA_MAX_PHYS_LUN);
			nphysicals = HPSA_MAX_PHYS_LUN;
		}
	}
	else {
		printk(KERN_ERR  "hpsa: Report physical LUNs failed.\n");
		goto out;
	}

        //
        // Now do the report_log_luns
        //
	if (hpsa_scsi_do_report_log_luns(c, logdev_list, reportlunsize) == 0 ) {

		char temp[8 * MAX_DEVS];
		ch = &logdev_list->LUNListLength[0];
		nlogicals = ((ch[0]<<24) | (ch[1]<<16) | (ch[2]<<8) | ch[3]) / 8;
#ifdef DEBUG
                printk(KERN_INFO "hpsa: number of logical luns is %d\n", nlogicals);
#endif

		// Reject Logicals in excess of our max capability.
		if (nlogicals > HPSA_MAX_LUN ) {
			printk(KERN_WARNING
				"hpsa: Maximum logical LUNs (%d) exceeded.  "
				"%d LUNs ignored.\n", HPSA_MAX_LUN,
				nlogicals - HPSA_MAX_LUN);
				nlogicals = HPSA_MAX_LUN;
		}
	
		// If Logicals + Physicals is greater than our max combined capability,
		// reject enough logicals to reduce total to within supported maximum.
		if (nlogicals + nphysicals > HPSA_MAX_PHYS_LUN) {
			printk(KERN_WARNING
				"hpsa: Maximum logical + physical LUNs (%d) exceeded. "
				"%d LUNs ignored.\n", HPSA_MAX_PHYS_LUN,
				nphysicals + nlogicals - HPSA_MAX_PHYS_LUN);
			nlogicals = HPSA_MAX_PHYS_LUN - nphysicals;
		}

		//append logical list to physical list.
		ch = &device_list->LUN[nphysicals][0];
		memcpy(temp, &logdev_list->LUN[0][0], 8 * nlogicals);
		memcpy(ch, temp, 8 * nlogicals);

	} else {
		printk(KERN_ERR "hpsa: Report logical LUNs failed.\n");
		goto out;
	}
	c->num_luns = nlogicals; 

	// Get the firmware version and save it off for this cntl_num
	memset(inq_buff, 0, OBDR_TAPE_INQ_SIZE);
	memset(&scsi3addr[0], 0, 8); /* address the controller */
	
	if (hpsa_scsi_do_inquiry(hba[cntl_num], scsi3addr, inq_buff,
		(unsigned char) OBDR_TAPE_INQ_SIZE) != 0) {
		/* Inquiry failed (msg printed already) */
		printk("hpsa_update_scsi_devices: inquiry for firmware version failed\n");
	} else {
		// Save off firmware version
		hba[cntl_num]->firm_ver[0] = inq_buff[32];
		hba[cntl_num]->firm_ver[1] = inq_buff[33];
		hba[cntl_num]->firm_ver[2] = inq_buff[34];
		hba[cntl_num]->firm_ver[3] = inq_buff[35];
#ifdef HPSA_DEBUG
	printk(KERN_INFO "hpsa: Saved off Firmware Version of %c%c%c%c for controller number %d\n",
		hba[cntl_num]->firm_ver[0], hba[cntl_num]->firm_ver[1], hba[cntl_num]->firm_ver[2],
		hba[cntl_num]->firm_ver[3], cntl_num);
#endif
	}

	/* adjust our table of devices */	
	int logical_drive_number = 0;
	for (i=0; i<nphysicals + nlogicals; i++ )
	{
		int devtype;
		unsigned int lunid = 0; //SST

		/* for each physical lun, do an inquiry */
		if (device_list->LUN[i][3] & 0xC0 && i < nphysicals) {
#ifdef DEBUG
                        printk(KERN_INFO "hpsa: Physical device found, buffer is"
				" 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				device_list->LUN[i][0], device_list->LUN[i][1],
				device_list->LUN[i][2], device_list->LUN[i][3],
				device_list->LUN[i][4], device_list->LUN[i][5],
				device_list->LUN[i][6], device_list->LUN[i][7]);
#endif
			continue; //Skip "masked" physical devices.
		}
		
		lunid = (0xff & (unsigned int)(device_list->LUN[i][3])) << 24;
		lunid |= (0xff & (unsigned int)(device_list->LUN[i][2])) << 16;
		lunid |= (0xff & (unsigned int)(device_list->LUN[i][1])) << 8;
		lunid |= (0xff & (unsigned int)(device_list->LUN[i][0]));

		lun = lunid & 0x00ff;
		target = ( lunid >> 16) & 0x3fff;

		if (lun0_processing[cntl_num][target] == 0)
		{

			// If this is first LUN of a target, and LUN id is not 0,
			// add a LUN id of 0, which is enclosure on MSA2012sa.
			// This allows scanning code to work properly, as it requires
			// a LUN 0 on every target.
			if ( lun != 0 ) {
#ifdef HPSA_DEBUG
				printk(KERN_INFO "hpsa: In lun0 processing, setting up enclosure device as lun 0\n");
#endif
				/* bus (channel) should be 0 */
				currentsd[ncurrent].bus        = 0;
				currentsd[ncurrent].target     = target;
				currentsd[ncurrent].lun        = 0;
				currentsd[ncurrent].devtype    = TYPE_ENCLOSURE;
				memset(currentsd[ncurrent].scsi3addr, 0, 8);
				currentsd[ncurrent].scsi3addr[3] = target;
				ncurrent++;
				hpsa_targets++;
			}
			lun0_processing[cntl_num][target] = 1;
		}


		c->drv[logical_drive_number].LunID = lunid;
		logical_drive_number++;
 
		memset(inq_buff, 0, OBDR_TAPE_INQ_SIZE);
		memcpy(&scsi3addr[0], &device_list->LUN[i][0], 8);

		if (hpsa_scsi_do_inquiry(hba[cntl_num], scsi3addr, inq_buff,
			(unsigned char) OBDR_TAPE_INQ_SIZE) != 0) {
			/* Inquiry failed (msg printed already) */
			printk("hpsa_update_scsi_devices: inquiry failed\n");
			devtype = 0; /* so we will skip this device. */
		} else /* what kind of device is this? */
			devtype = (inq_buff[0] & 0x1f);

		switch (devtype)
		{
		  case 0x05: /* CD-ROM */ {

			/* We don't *really* support actual CD-ROM devices,
			 * just "One Button Disaster Recovery" tape drive
			 * which temporarily pretends to be a CD-ROM drive.
			 * So we check that the device is really an OBDR tape
			 * device by checking for "$DR-10" in bytes 43-48 of
			 * the inquiry data.
			 */
				char obdr_sig[7];

				strncpy(obdr_sig, &inq_buff[43], 6);
				obdr_sig[6] = '\0';
				if (strncmp(obdr_sig, OBDR_TAPE_SIG, 6) != 0)
					/* Not OBDR device, ignore it. */
					break;
			}
			/* fall through . . . */

		  case TYPE_DISK: 
#ifdef HPSA_DEBUG
			printk(KERN_INFO "hpsa: Found a logical drive\n");
#endif
			if (i < nphysicals)
				break; /*skip the physical disks, only expose logicals. */ 	
			
		  case TYPE_RAID_CONTROLLER: 
			if (devtype == 0x0C)
				printk(KERN_ERR "hpsa: ncurrent = %d, RAID Controller found.\n", ncurrent); 

		  case TYPE_TAPE: 
		  case TYPE_MEDIUM_CHANGER:
			//Too many devices
			if (ncurrent >= MAX_DEVS) {
				printk(KERN_INFO "hpsa%d: %s ignored, "
					"too many devices.\n", cntl_num,
					DEVICETYPE(devtype));
				break;
			}
			//Too many LUNs
			if ((ncurrent - hpsa_targets)  >= HPSA_MAX_LUN) {
				printk(KERN_INFO "hpsa%d: %s ignored, "
					"too many LUNs.\n", cntl_num,
					DEVICETYPE(devtype));
				break;
			}
			memcpy(&currentsd[ncurrent].scsi3addr[0], 
				&scsi3addr[0], 8);
			currentsd[ncurrent].devtype = devtype;
			/* bus (channel) should be 0 */
			currentsd[ncurrent].bus = 0;
			currentsd[ncurrent].target = target;
			currentsd[ncurrent].lun = lun;
			ncurrent++;
			break;
		  default: 
			break;
		}
	}

	adjust_hpsa_scsi_table(cntl_num, hostno, currentsd, ncurrent);
out:
	kfree(currentsd);
	kfree(inq_buff);
	kfree(device_list);
        kfree(extended_phys_list);
        kfree(sense_subsystem_info);
	return;
}

static int
is_keyword(char *ptr, int len, char *verb)  // Thanks to ncr53c8xx.c
{
	int verb_len = strlen(verb);
	if (len >= verb_len && !memcmp(verb,ptr,verb_len))
		return verb_len;
	else
		return 0;
}

static int
hpsa_scsi_user_command(int ctlr, int hostno, char *buffer, int length)
{
	int arg_len;

	if ((arg_len = is_keyword(buffer, length, "rescan")) != 0)
		hpsa_update_scsi_devices(ctlr, hostno); 
	else
		return -EINVAL;
	return length;
}


static int
hpsa_scsi_proc_info(struct Scsi_Host *sh,
		char *buffer, /* data buffer */
		char **start, 	   /* where data in buffer starts */
		off_t offset,	   /* offset from start of imaginary file */
		int length, 	   /* length of data in buffer */
		int func)	   /* 0 == read, 1 == write */
{

	int buflen, datalen;
	ctlr_info_t *ci;
	int cntl_num;
        int i;

	ci = (ctlr_info_t *) sh->hostdata[0];
	if (ci == NULL)  /* This really shouldn't ever happen. */
		return -EINVAL;

	cntl_num = ci->ctlr;	/* Get our index into the hba[] array */
	buflen = sprintf(buffer, "%s\n", DRIVER_NAME);
	if (func == 0) {	/* User is reading from /proc/scsi/hpsa*?/?*  */
		buflen += sprintf(&buffer[buflen], "hpsa%d: SCSI host: %d\n",
				cntl_num, sh->host_no);

		/* this information is needed by apps to know which hpsa
		   device corresponds to which scsi host number without
		   having to open a scsi target device node.  The device
		   information is not a duplicate of /proc/scsi/scsi because
		   the two may be out of sync due to scsi hotplug, rather
		   this info is for an app to be able to use to know how to
		   get them back in sync. */

		/* 
		 * Printing controller/bus/target/lun/devtype for every lun
		 * causes proc node to exceed 1 page when there are lots of LUNs.
		 * VMware has a 1 page limit.
                 */
		for (i=0;i<hpsascsi[cntl_num].ndevices;i++) {
			struct hpsa_scsi_dev_t *sd = &hpsascsi[cntl_num].dev[i];
#if defined(__VMKLNX__)
                /*
                 * The -80 is for the last line where we would put the
                 * vmkadapter name.
		 */
			int len = length - buflen - 80;
			if (len <= 0) { 
				break;
                        }
			buflen += snprintf(&buffer[buflen], len,
                                           "c%db%dt%dl%d %02d "
#else
			buflen += sprintf(&buffer[buflen], "c%db%dt%dl%d %02d "
#endif
				"0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				sh->host_no, sd->bus, sd->target, sd->lun,
				sd->devtype,
				sd->scsi3addr[0], sd->scsi3addr[1],
				sd->scsi3addr[2], sd->scsi3addr[3],
				sd->scsi3addr[4], sd->scsi3addr[5],
				sd->scsi3addr[6], sd->scsi3addr[7]);
		}
		buflen = min(buflen, length - 80);
		datalen = buflen - offset;
		if (datalen < 0) { 	/* they're reading past EOF. */
			datalen = 0;
			*start = buffer+buflen;	
		} else
			*start = buffer + offset;
		return(datalen);
	} else 	/* User is writing to /proc/scsi/hpsa*?/?*  ... */
		return hpsa_scsi_user_command(cntl_num, sh->host_no,
			buffer, length);	
} 

/* hpsa_scatter_gather takes a struct scsi_cmnd, (cmd), and does the pci 
   dma mapping  and fills in the scatter gather entries of the 
   hpsa command, cp. */

static void
hpsa_scatter_gather(struct pci_dev *pdev, 
		CommandList_struct *cp,	
		struct scsi_cmnd *cmd)
{
	unsigned int use_sg, nsegs=0, len;
	struct scatterlist *scatter = (struct scatterlist *) cmd->request_buffer;
	__u64 addr64;

	/* is it just one virtual address? */	
	if (!cmd->use_sg) {
		if (cmd->request_bufflen) {	/* anything to xfer? */

			addr64 = (__u64) pci_map_single(pdev, 
				cmd->request_buffer, 
				cmd->request_bufflen, 
				cmd->sc_data_direction); 
	
			cp->SG[0].Addr.lower = 
			  (__u32) (addr64 & (__u64) 0x00000000FFFFFFFF);
			cp->SG[0].Addr.upper =
			  (__u32) ((addr64 >> 32) & (__u64) 0x00000000FFFFFFFF);
			cp->SG[0].Len = cmd->request_bufflen;
			nsegs=1;
		}
	} /* else, must be a list of virtual addresses.... */
	else if (cmd->use_sg <= MAXSGENTRIES) {	/* not too many addrs? */

		use_sg = pci_map_sg(pdev, cmd->request_buffer, cmd->use_sg,
			cmd->sc_data_direction);

		for (nsegs=0; nsegs < use_sg; nsegs++) {
			addr64 = (__u64) sg_dma_address(&scatter[nsegs]);
			len  = sg_dma_len(&scatter[nsegs]);
			cp->SG[nsegs].Addr.lower =
			  (__u32) (addr64 & (__u64) 0x00000000FFFFFFFF);
			cp->SG[nsegs].Addr.upper =
			  (__u32) ((addr64 >> 32) & (__u64) 0x00000000FFFFFFFF);
			cp->SG[nsegs].Len = len;
			cp->SG[nsegs].Ext = 0;  // we are not chaining
		}
	} else BUG();

	cp->Header.SGList = (__u8) nsegs;   /* no. SGs contig in this cmd */
	cp->Header.SGTotal = (__u16) nsegs; /* total sgs in this cmd list */
	return;
}


static int
hpsa_scsi_queue_command (struct scsi_cmnd *cmd, void (* done)(struct scsi_cmnd *))
{
	ctlr_info_t **c;
	int ctlr, rc;
	unsigned char scsi3addr[8];
	CommandList_struct *cp;
	unsigned long flags;

	// Get the ptr to our adapter structure (hba[i]) out of cmd->host.
	// We violate cmd->host privacy here.  (Is there another way?)
	c = (ctlr_info_t **) &cmd->device->host->hostdata[0];	
	ctlr = (*c)->ctlr;

	rc = lookup_scsi3addr(ctlr, cmd->device->channel, cmd->device->id, 
			cmd->device->lun, scsi3addr);
	if (rc != 0) {
		/* the scsi nexus does not match any that we presented... */
		/* pretend to mid layer that we got selection timeout */
		cmd->result = DID_NO_CONNECT << 16;
		done(cmd);
		/* we might want to think about registering controller itself
		   as a processor device on the bus so sg binds to it. */
		return 0;
	}

	/* Ok, we have a reasonable scsi nexus, so send the cmd down, and
	   see what the device thinks of it. */

	spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	cp = cmd_alloc(*c, 1); 
	spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
	if (cp == NULL) {			/* trouble... */
		printk("scsi_cmd_alloc returned NULL!\n");
		cmd->result = DID_NO_CONNECT << 16;
		done(cmd);
		return 0;
	}

	// Fill in the command list header

	cmd->scsi_done = done;    // save this for use by completion code 

	// save cp in case we have to abort it 
	cmd->host_scribble = (unsigned char *) cp; 

	cp->cmd_type = CMD_SCSI;
	cp->scsi_cmd = cmd;
	cp->Header.ReplyQueue = 0;  // unused in simple mode
	memcpy(&cp->Header.LUN.LunAddrBytes[0], &scsi3addr[0], 8);
	cp->Header.Tag.lower = cp->busaddr;  // Use k. address of cmd as tag
	
	// Fill in the request block...

	cp->Request.Timeout = 0;
	memset(cp->Request.CDB, 0, sizeof(cp->Request.CDB));
	BUG_ON(cmd->cmd_len > sizeof(cp->Request.CDB));
	cp->Request.CDBLen = cmd->cmd_len;
	memcpy(cp->Request.CDB, cmd->cmnd, cmd->cmd_len);
	cp->Request.Type.Type = TYPE_CMD;
	cp->Request.Type.Attribute = ATTR_SIMPLE;
	switch(cmd->sc_data_direction)
	{
	  case DMA_TO_DEVICE: cp->Request.Type.Direction = XFER_WRITE; break;
	  case DMA_FROM_DEVICE: cp->Request.Type.Direction = XFER_READ; break;
	  case DMA_NONE: cp->Request.Type.Direction = XFER_NONE; break;
	  case DMA_BIDIRECTIONAL:
		// This can happen if a buggy application does a scsi passthru
		// and sets both inlen and outlen to non-zero. ( see
		// ../scsi/scsi_ioctl.c:scsi_ioctl_send_command() )

		cp->Request.Type.Direction = XFER_RSVD;
		// This is technically wrong, and hpsa controllers should
		// reject it with CMD_INVALID, which is the most correct 
		// response, but non-fibre backends appear to let it 
		// slide by, and give the same results as if this field
		// were set correctly.  Either way is acceptable for
		// our purposes here.

		break;

	  default: 
		printk(KERN_ERR "hpsa: unknown data direction: %d\n", 
			cmd->sc_data_direction);
		BUG();
		break;
	}

	hpsa_scatter_gather((*c)->pdev, cp, cmd); // Fill the SG list

	/* Put the request on the tail of the request queue */

	spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	addQ(&(*c)->reqQ, cp);
	(*c)->Qdepth++;
	start_io(*c);
	spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);

	/* the cmd'll come back via intr handler in complete_scsi_command()  */
	return 0;
}

static void 
hpsa_unregister_scsi(int ctlr)
{
	struct hpsa_scsi_adapter_data_t *sa;
	unsigned long flags;

	/* we are being forcibly unloaded, and may not refuse. */

	spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	sa = (struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr;

	/* if we weren't ever actually registered, don't unregister */ 
	if (sa->registered) {
		spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
		scsi_remove_host(sa->scsi_host);
		scsi_host_put(sa->scsi_host);
		spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	}

	/* set scsi_host to NULL so our detect routine will 
	   find us on register */
	sa->scsi_host = NULL;
	kfree(sa);
	spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
}

static int 
hpsa_register_scsi(int ctlr)
{
	unsigned long flags;
	int rc; 

	spin_lock_irqsave( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
	
	if (((struct hpsa_scsi_adapter_data_t *)
		hba[ctlr]->scsi_ctlr)->registered) {
		spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
		return ENXIO;
	}	
	spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
	hpsa_update_scsi_devices(ctlr, -1);

	spin_lock_irqsave( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);

	/* Register with the SCSI subsystem even if no SCSI devices detected. */	
	((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->registered = 1;
	spin_unlock_irqrestore( &(((struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr)->lock), flags);
	rc=hpsa_scsi_detect(ctlr);
	if (rc == 0) {
		printk(KERN_ERR "hpsa: hpsa_register_scsi: failed"
			" hpsa_scsi_detect(), rc is %d\n", rc);
	}
	return rc;
}

static int 
hpsa_engage_scsi(int ctlr)
{
	struct hpsa_scsi_adapter_data_t *sa;
	unsigned long flags;

	spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	sa = (struct hpsa_scsi_adapter_data_t *) hba[ctlr]->scsi_ctlr;

	if (((struct hpsa_scsi_adapter_data_t *) 
		hba[ctlr]->scsi_ctlr)->registered) {
		printk("hpsa%d: SCSI subsystem already engaged.\n", ctlr);
		spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
		return ENXIO;
	}
	spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
	hpsa_update_scsi_devices(ctlr, 0); 
	hpsa_register_scsi(ctlr);
	return 0;
}


/* Need at least one of these error handlers to keep ../scsi/hosts.c from 
 * complaining.  Doing a host- or bus-reset can't do anything good here. 
 */

static int hpsa_eh_device_reset_handler(struct scsi_cmnd *scsicmd)
{
	int rc;
	ctlr_info_t **c;
	int ctlr;
	unsigned char scsi3addr[8];

	/* find the controller to which the command to be aborted was sent */
	c = (ctlr_info_t **) &scsicmd->device->host->hostdata[0];	
	if (c == NULL) /* paranoia */
		return FAILED;
	ctlr = (*c)->ctlr;
	printk(KERN_WARNING "hpsa%d: resetting drive\n", ctlr);

	rc = lookup_scsi3addr(ctlr, scsicmd->device->channel, scsicmd->device->id, scsicmd->device->lun, scsi3addr);
	if (rc != 0) {
		printk("hpsa_eh_device_reset_handler: lookup_scsi3addr failed.\n");
		return FAILED;
	}

	/* send a reset to the SCSI LUN which the command was sent to */
	rc = sendcmd(HPSA_DEVICE_RESET_MSG, ctlr, NULL, 0, 1, scsicmd->device->id, 0, 
		(unsigned char *) NULL,
		1);

	/* sendcmd turned off interrputs on the board, turn 'em back on. */
	(*c)->access.set_intr_mask(*c, HPSA_INTR_ON);
	if (rc == 0) {
		return SUCCESS;
	}
	printk(KERN_WARNING "hpsa%d: resetting device failed.\n", ctlr);
	return FAILED;
}

static int hpsa_eh_bus_reset_handler(struct scsi_cmnd *scsicmd)
{
	int rc;
	ctlr_info_t **c;
	int ctlr;
	unsigned char scsi3addr[8];

	/* find the controller to which the command to be aborted was sent */
	c = (ctlr_info_t **) &scsicmd->device->host->hostdata[0];	
	if (c == NULL) /* paranoia */
		return FAILED;
	ctlr = (*c)->ctlr;
	printk(KERN_WARNING "hpsa%d: resetting bus\n", ctlr);

	//May not need this lookup
	rc = lookup_scsi3addr(ctlr, scsicmd->device->channel, scsicmd->device->id, scsicmd->device->lun, scsi3addr);
	if (rc != 0) {
		printk("hpsa_eh_bus_reset_handler: lookup_scsi3addr failed.\n");
		return FAILED;
	}

	/* send a bus reset to the SCSI LUN which the command was sent to */
	rc = sendcmd(HPSA_BUS_RESET_MSG, ctlr, NULL, 0, 1, scsicmd->device->id, 0, 
		(unsigned char *) NULL,
		1);

	/* sendcmd turned off interrputs on the board, turn 'em back on. */
	(*c)->access.set_intr_mask(*c, HPSA_INTR_ON);
	if (rc == 0) {
		return SUCCESS;
	}
	printk(KERN_WARNING "hpsa%d: resetting bus failed.\n", ctlr);
	return FAILED;
}
static int hpsa_eh_host_reset_handler(struct scsi_cmnd *scsicmd)
{
	int rc;
	ctlr_info_t **c;
	int ctlr;
	unsigned char scsi3addr[8];

	/* find the controller to which the command to be aborted was sent */
	c = (ctlr_info_t **) &scsicmd->device->host->hostdata[0];	
	if (c == NULL) /* paranoia */
		return FAILED;
	ctlr = (*c)->ctlr;
	printk(KERN_WARNING "hpsa%d: resetting host\n", ctlr);

	// May not need this lookup
	rc = lookup_scsi3addr(ctlr, scsicmd->device->channel, scsicmd->device->id, scsicmd->device->lun, scsi3addr);
	if (rc != 0) {
		printk("hpsa_eh_host_reset_handler: lookup_scsi3addr failed.\n");
		return FAILED;
	}

	/* send a host reset to the SCSI LUN which the command was sent to */
	rc = sendcmd(HPSA_HOST_RESET_MSG, ctlr, NULL, 0, 1, scsicmd->device->id, 0, 
		(unsigned char *) NULL,
		1);

	/* sendcmd turned off interrputs on the board, turn 'em back on. */
	(*c)->access.set_intr_mask(*c, HPSA_INTR_ON);
	if (rc == 0) {
		return SUCCESS;
	}
	printk(KERN_WARNING "hpsa%d: resetting host failed.\n", ctlr);
	return FAILED;
}
static int  hpsa_eh_abort_handler(struct scsi_cmnd *scsicmd)
{
	int rc;
	CommandList_struct *cmd_to_abort;
	ctlr_info_t **c;
	int ctlr;

	/* find the controller to which the command to be aborted was sent */
	c = (ctlr_info_t **) &scsicmd->device->host->hostdata[0];	
	if (c == NULL) /* paranoia */
		return FAILED;
	ctlr = (*c)->ctlr;
	printk(KERN_WARNING "hpsa%d: aborting tardy SCSI cmd\n", ctlr);

	/* find the command to be aborted */
	cmd_to_abort = (CommandList_struct *) scsicmd->host_scribble;
	if (cmd_to_abort == NULL) /* paranoia */
	{
		printk(KERN_WARNING "hpsa%d: aborting tardy SCSI cmd failed, no outstanding command to abort\n", ctlr);

		return FAILED;
	}

	rc = sendcmd(HPSA_ABORT_MSG, ctlr, cmd_to_abort, 0, 1, scsicmd->device->id, 0,
		(unsigned char *) NULL,
		1);
	/* sendcmd turned off interrputs on the board, turn 'em back on. */
	(*c)->access.set_intr_mask(*c, HPSA_INTR_ON);
	if (rc == 0)
		return SUCCESS;
	return FAILED;

}

static int hpsa_slave_alloc(struct scsi_device *dev)
{
	ctlr_info_t *ci;

	ci = (ctlr_info_t *) dev->host->hostdata[0];

#ifdef HPSA_DEBUG
	printk(KERN_INFO "slave_alloc host%d c%d t%d lun%d \n",
		dev->host->host_no, dev->channel, dev->id, dev->lun);
#endif
	if (dev->id <  MAX_TARGETS_PER_CTLR) {
		hpsa_update_scsi_devices(ci->ctlr, dev->host->host_no);
	}

	return 0;
}

#else /* no CONFIG_SCSI_HPSA */

/* If no tape support, then these become defined out of existence */

define hpsa_scsi_setup(cntl_num)
define hpsa_unregister_scsi(ctlr)
define hpsa_register_scsi(ctlr)
define hpsa_proc_tape_report(ctlr, buffer, pos, len)

#endif /* CONFIG_SCSI_HPSA */
