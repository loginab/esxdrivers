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
 * $Id: fnic_main.c 22660 2009-01-16 00:24:57Z jre $
 */

#include <linux/module.h>
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
#include <asm/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>

#include "sa_kernel.h"
#include "fc_types.h"
#include "fcdev.h"
#include "sa_assert.h"
#include "fc_frame.h"
#include "fcp_hdr.h"
#include "fc_remote_port.h"
#include "openfc_ioctl.h"

#include "kcompat.h"
#include "pci_ids.h"

#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "fnic_io.h"
#include "fnic.h"
#if !defined(__VMKLNX__)
#include "fnic_ioctl.h"
#else
#include "build_version.h"
#endif /* __VMKLNX__ */

#include "openfc.h"

/* timer to poll notification area for events. Used in case of MSI
 * interrupts being used by the device
 */
#define FNIC_NOTIFY_TIMER_PERIOD	(2 * HZ)

/* Cache for FCS Frames*/
struct kmem_cache         *fnic_fc_frame_cache;

/* Cache for frame for passing link events/frames from ISR to Thread*/
struct kmem_cache         *fnic_ev_cache;

/* sgl_list cache*/
struct kmem_cache         *fnic_sgl_cache[FNIC_SGL_NUM_CACHES];

/* fnic count */
atomic_t fnic_no;

/* global list of fnic devices handled by driver */
LIST_HEAD(fnic_hostlist);
spinlock_t fnic_hostlist_lock;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) && !defined(__VMKLNX__)
extern struct class   *fnic_class;
extern struct attribute_group fnic_attr_group;

static struct attribute_group *fnic_groups[] = {
	&fnic_attr_group,
	NULL
};
#endif

static struct fc_frame *fnic_tx_frame_alloc(size_t len);

/* Supported devices by fnic module*/
static struct pci_device_id fnic_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CISCO, PCI_DEVICE_ID_CISCO_PALO_FC) },
	{ 0, }	/* end of table */
};

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Cisco Systems");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, fnic_id_table);

/* Operations exposed to the OpenFC module*/
static struct openfc_port_operations fnic_port_ops = {
	.owner = THIS_MODULE,
	.send = fnic_send,  
	.send_scsi = fnic_send_scsi, 
	.abort_cmd = fnic_abort_cmd,
	.target_reset = fnic_device_reset, /* LUN Reset */
	.host_reset = fnic_host_reset,
	.cleanup_scsi = fnic_cleanup_scsi,
	.frame_alloc = fnic_tx_frame_alloc,
	.get_stats = fnic_get_stats,
	.ext_fsp_size = sizeof(struct fnic_io_req),
	.sg_tablesize = FNIC_MAX_SG_DESC_CNT,
};

void fnic_tx_frame_free(struct fc_frame *fp) 
{
	fc_frame_free_static(fp);
	kmem_cache_free(fnic_fc_frame_cache, fp);
}

struct fc_frame *fnic_tx_frame_alloc(size_t len)
{    
	struct fc_frame *fp = NULL;
	size_t tot_len = len + sizeof(struct fcp_hdr) + 
		sizeof(struct fcoe_crc_eof) + sizeof(struct fc_frame);

	fp = (struct fc_frame*)kmem_cache_alloc(fnic_fc_frame_cache, 
						GFP_DMA|GFP_ATOMIC);
	
	if (!fp) {
		printk(KERN_DEBUG PFX "fnic_frame_alloc failed: len %lu\n",
		       (unsigned long)tot_len);
		return NULL;
	}

	memset(fp,0,tot_len);

	fc_frame_init_static(fp);
	fp->fr_free = fnic_tx_frame_free;
	fp->fr_hdr = (struct fc_frame_header *)(((unsigned char*)fp + 
						 sizeof(*fp)) + 
						offsetof(struct fcp_hdr, 
							 fc_hdr));
	fp->fr_len = (u_int16_t)(len + sizeof(struct fc_frame_header));
	return fp;
}

void fnic_get_stats(struct fcdev *fc_dev)
{
	int ret;
	struct fnic *fnic = fc_dev->drv_priv;
	struct fcoe_dev_stats *stats = fc_dev->dev_stats[0];
	
	/* OpenFC expects that the transport maintains per CPU stats.
	 * For the HBA driver, currently there are no per CPU stats. So, we 
	 * get the stats from the device and dump it in CPU 0 stats
	 */

	ret = vnic_dev_stats_dump(fnic->vdev, &fnic->stats);
	if (ret) {
		printk(KERN_DEBUG DFX "fnic: Get vnic stats failed"
		       " 0x%x", fnic->fnic_no, ret);
		return;
	}
	stats->TxFrames = fnic->stats->tx.tx_unicast_frames_ok;
	stats->TxWords  = (fnic->stats->tx.tx_unicast_bytes_ok / 4);
	stats->RxFrames = fnic->stats->rx.rx_unicast_frames_ok;
	stats->RxWords  = (fnic->stats->rx.rx_unicast_bytes_ok / 4);
	stats->ErrorFrames = (fnic->stats->tx.tx_errors + 
			      fnic->stats->rx.rx_errors);
	stats->DumpedFrames = (fnic->stats->tx.tx_drops + 
			       fnic->stats->rx.rx_drop);
	stats->InvalidCRCCount = fnic->stats->rx.rx_crc_errors;
}

void fnic_log_q_error(struct fnic *fnic)
{
	unsigned int i;
	u32 error_status;
	
	for (i = 0; i < fnic->raw_wq_count; i++) {
		error_status = ioread32(&fnic->wq[i].ctrl->error_status);
		if (error_status)
			printk(KERN_ERR DFX "WQ[%d] error_status"
			       " %d\n", fnic->fnic_no, i, error_status);
	}

	for (i = 0; i < fnic->rq_count; i++) {
		error_status = ioread32(&fnic->rq[i].ctrl->error_status);
		if (error_status)
			printk(KERN_ERR DFX "RQ[%d] error_status"
			       " %d\n", fnic->fnic_no, i, error_status);
	}

	for (i = 0; i < fnic->wq_copy_count; i++) {
		error_status = ioread32(&fnic->wq_copy[i].ctrl->error_status);
		if (error_status)
			printk(KERN_ERR DFX "CWQ[%d] error_status"
			       " %d\n", fnic->fnic_no, i, error_status);
	}
}

void fnic_handle_link_event(struct fnic *fnic)
{
	int link_status = vnic_dev_link_status(fnic->vdev);
	u_int32_t link_down_cnt = vnic_dev_link_down_cnt(fnic->vdev);
	struct fnic_event *event = NULL;
	struct fnic_event *down_event = NULL;
	u_int8_t list_was_empty = 0;
	unsigned long flags;

	/*
	 * Filter out non-events due to polling or imprecise interrupts.
	 */
	if (fnic->fc_dev->fd_link_status == TRANS_LINK_UP) {
		if (link_status && fnic->link_down_cnt == link_down_cnt)
			return;
	} else if (!link_status) {
		return;
	}

	printk(KERN_DEBUG DFX "link %s\n", fnic->fnic_no,
	       (link_status ? "up" : "down"));

	if (fnic->in_remove)
		return;

	event = kmem_cache_alloc(fnic_ev_cache, GFP_ATOMIC);
	if (!event) {
		printk(KERN_DEBUG DFX "Cannot allocate a event, "
		       "cannot indicate link down to FCS\n", fnic->fnic_no);
		return;
	}

	/* Pass the event to thread */
	memset(event, 0, sizeof(struct fnic_event));
	event->fnic = fnic;
	event->ev_type = EV_TYPE_LINK_UP;
	fnic->fc_dev->fd_link_status = TRANS_LINK_UP;

	if (!link_status) {
		event->ev_type = EV_TYPE_LINK_DOWN;
		fnic->fc_dev->fd_link_status = TRANS_LINK_DOWN;
	} else if (fnic->link_down_cnt != link_down_cnt) {
		/* Missed a down event, insert one here */
		down_event = kmem_cache_alloc(fnic_ev_cache, GFP_ATOMIC);
		if (!down_event) {
			memset(down_event, 0, sizeof(struct fnic_event));
			down_event->fnic = fnic;
			down_event->ev_type = EV_TYPE_LINK_DOWN;

			spin_lock_irqsave(&fnic_eventlist_lock, flags);
			list_was_empty |= list_empty(&fnic_eventlist);
			list_add_tail(&down_event->list, &fnic_eventlist);
			spin_unlock_irqrestore(&fnic_eventlist_lock, flags);
		} else {
			printk(KERN_DEBUG DFX "Cannot allocate a down_event, "
			       "cannot indicate link down to FCS\n",
			       fnic->fnic_no);
		}
	}

	fnic->link_down_cnt = link_down_cnt;
	spin_lock_irqsave(&fnic_eventlist_lock, flags);
	list_was_empty |= list_empty(&fnic_eventlist);
	list_add_tail(&event->list, &fnic_eventlist);
	spin_unlock_irqrestore(&fnic_eventlist_lock, flags);
	if (list_was_empty)
		wake_up_process(fnic_thread);
}

void fnic_notify_check(struct fnic *fnic)
{
	/*Todo: Check for dbg msglevel change*/
	fnic_handle_link_event(fnic);
}

static int fnic_notify_set(struct fnic *fnic)
{
	int err;
	
	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		err = vnic_dev_notify_set(fnic->vdev, FNIC_INTX_NOTIFY);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		err = vnic_dev_notify_set(fnic->vdev, -1);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		err = vnic_dev_notify_set(fnic->vdev, FNIC_MSIX_ERR_NOTIFY);
		break;
	default:
		printk(KERN_ERR DFX "Interrupt mode should be set up"
		       " before devcmd notify set %d\n", fnic->fnic_no,
		       vnic_dev_get_intr_mode(fnic->vdev));
		err = -1;
		BUG_ON(1);
		break;
	}
	
	return err;
}

static void fnic_notify_timer(unsigned long data)
{
	struct fnic *fnic = (struct fnic *)data;

	fnic_notify_check(fnic);
	mod_timer(&fnic->notify_timer, jiffies + FNIC_NOTIFY_TIMER_PERIOD);
}

static void fnic_notify_timer_start(struct fnic *fnic)
{
	switch (vnic_dev_get_intr_mode(fnic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
		mod_timer(&fnic->notify_timer, jiffies);
		break;
	default:
		/* Using intr for notification for INTx/MSI-X */
		break;
	};
}

static int fnic_dev_wait(struct vnic_dev *vdev,
			 int (*start)(struct vnic_dev *, int),
			 int (*finished)(struct vnic_dev *, int *),
			 int arg)
{
	unsigned long time;
	int done;
	int err;
	
	BUG_ON(in_interrupt());
	
	err = start(vdev, arg);
	if (err)
		return err;

	/* Wait for func to complete...2 seconds max
	 */

	time = jiffies + (HZ * 2);
	do {

		err = finished(vdev, &done);
		if (err)
			return err;

		if (done)
			return 0;

		schedule_timeout_uninterruptible(HZ / 10);

	} while (time_after(time, jiffies));
	
	return -ETIMEDOUT;
}

static int fnic_dev_open(struct fnic *fnic)
{
	int err;
	
	err = fnic_dev_wait(fnic->vdev, vnic_dev_open,
			    vnic_dev_open_done, 0);
	if (err)
		printk(KERN_ERR PFX
		       "vNIC device open failed, err %d.\n", err);
	
	return err;
}

static int fnic_cleanup(struct fnic *fnic)
{
	unsigned int i;
	int err;
	unsigned long flags;
	enum fnic_state old_state;
	struct openfc_softc *openfcp;
	struct fc_frame *flogi = NULL;
	struct fc_frame *flogi_resp = NULL;
	DECLARE_COMPLETION_ONSTACK(remove_wait);

	/* Stop OpenFC module from issuing more SCSI IOs */
	openfcp = openfc_get_softc(fnic->fc_dev);
	spin_lock_irqsave(openfcp->host->host_lock, flags);
	openfcp->state = OPENFC_GOING_DOWN;
	spin_unlock_irqrestore(openfcp->host->host_lock, flags);

	/* Issue firmware reset for fnic, wait for reset to complete */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->fc_dev->fd_link_status = TRANS_LINK_DOWN;
	fnic->in_remove = 1;
	fnic->remove_wait = &remove_wait;
	old_state = fnic->state;
	fnic->state = FNIC_IN_FC_TRANS_ETH_MODE;
	vnic_dev_del_addr(fnic->vdev, fnic->data_src_addr);
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	err = fnic_fw_reset_handler(fnic);
	if (err) {
		spin_lock_irqsave(&fnic->fnic_lock, flags);
		if (fnic->state == FNIC_IN_FC_TRANS_ETH_MODE)
			fnic->state = old_state;
		fnic->remove_wait = NULL;
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		goto continue_cleanup;
	}

	/* Wait for firmware reset to complete */
	wait_for_completion_timeout(&remove_wait,
				    msecs_to_jiffies(FNIC_RMDEVICE_TIMEOUT));

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	fnic->remove_wait = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

 continue_cleanup:
	/* Reset might or might not have completed succesfully, in any case
	 * we continue the cleanup
	 */
	del_timer_sync(&fnic->notify_timer);

	vnic_dev_disable(fnic->vdev);

	for (i = 0; i < fnic->intr_count; i++) 
		vnic_intr_mask(&fnic->intr[i]);

	for (i = 0; i < fnic->rq_count; i++) {
		err = vnic_rq_disable(&fnic->rq[i]);
		if (err) 
			return err;
	}
	for (i = 0; i < fnic->raw_wq_count; i++) {
		err = vnic_wq_disable(&fnic->wq[i]);
		if (err)
			return err;
	}
	for (i = 0; i < fnic->wq_copy_count; i++) {
		err = vnic_wq_copy_disable(&fnic->wq_copy[i]);
		if (err) 
			return err;
	}

	/* Clean up completed IOs and FCS frames */
	fnic_wq_copy_cmpl_handler(fnic, -1);
	fnic_wq_cmpl_handler(fnic, -1);
	fnic_rq_cmpl_handler(fnic, -1);

	/* Clean up the IOs and FCS frames that have not completed */
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_clean(&fnic->wq[i], fnic_free_wq_buf);
	for (i = 0; i < fnic->rq_count; i++)
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);
	for (i = 0; i < fnic->wq_copy_count; i++)
		vnic_wq_copy_clean(&fnic->wq_copy[i], 
				   fnic_wq_copy_cleanup_handler);

	for (i = 0; i < fnic->cq_count; i++)
		vnic_cq_clean(&fnic->cq[i]);
	for (i = 0; i < fnic->intr_count; i++)
		vnic_intr_clean(&fnic->intr[i]);

	/* Remove cached flogi and flogi resp frames if any */
	spin_lock_irqsave(&fnic->fnic_lock, flags);
	flogi = fnic->flogi;
	fnic->flogi = NULL;
	flogi_resp = fnic->flogi_resp;
	fnic->flogi_resp = NULL;
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (flogi) {
		printk(KERN_DEBUG DFX "freeing flogi during remove\n",
		       fnic->fnic_no);
		fc_frame_free(flogi);
	}
	if (flogi_resp) {
		printk(KERN_DEBUG DFX "freeing flogi_resp during remove\n",
		       fnic->fnic_no);
		fc_frame_free(flogi_resp);
	}

	/* Free the free_list and IO info array */
	while(!list_empty(&fnic->free_io_list))
		list_del(fnic->free_io_list.next);
	kfree(fnic->outstanding_io_info_list);
	fnic->outstanding_io_info_list = NULL;

	/* Clean stats structures */
	for_each_online_cpu(i) {
		kfree(fnic->fc_dev->dev_stats[i]);
		fnic->fc_dev->dev_stats[i] = NULL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) && !defined(__VMKLNX__)
	/* delete this device from sysfs tree */
	if (!list_empty(&fnic->fnic_class_device.node))
		class_device_del(&fnic->fnic_class_device);
#endif /* LINUX_VERSION */
	
	/* delete this device from global fnic list */
	spin_lock_irqsave(&fnic_hostlist_lock, flags);
	list_del(&fnic->list);
	spin_unlock_irqrestore(&fnic_hostlist_lock, flags);

	return 0;
}

static void fnic_iounmap(struct fnic *fnic)
{
	if (fnic->bar0.vaddr)
		iounmap(fnic->bar0.vaddr);
}

static int __devinit fnic_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct fcdev *fc_dev;
	struct fnic *fnic;
	int err;
	int i;
	unsigned long flags;

	/* Allocate FC device */
	fc_dev = openfc_alloc_dev(&fnic_port_ops, sizeof(struct fnic));
	if (!fc_dev) {
		printk(KERN_ERR PFX "Unable to alloc openfc dev\n");
		err = -ENOMEM;
		goto err_out;
	}
	fnic = fc_dev->drv_priv;
	fnic->fc_dev = fc_dev;

	/* Setup PCI resources */
	pci_set_drvdata(pdev, fnic);

	fnic->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable PCI device, aborting.\n");
		goto err_out_free_fcdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable PCI resources,aborting\n");
		goto err_out_disable_device;
	}

	pci_set_master(pdev);

	/* Query PCI controller on system for DMA addressing
	 * limitation for the device.  Try 40-bit first, and
	 * fail to 32-bit.
	 */

	err = pci_set_dma_mask(pdev, DMA_40BIT_MASK);
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_ERR PFX "No usable DMA configuration "
			       "aborting\n");
			goto err_out_release_regions;
		}
		err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_ERR PFX "Unable to obtain 32-bit DMA "
			       "for consistent allocations, aborting.\n");
			goto err_out_release_regions;
		}
	} else {
		err = pci_set_consistent_dma_mask(pdev, DMA_40BIT_MASK);
		if (err) {
			printk(KERN_ERR PFX "Unable to obtain 40-bit DMA "
			       "for consistent allocations, aborting.\n");
			goto err_out_release_regions;
		}
	}

	/* Map vNIC resources from BAR0 
	 */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "BAR0 not memory-map'able, aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	fnic->bar0.vaddr = pci_iomap(pdev, 0, 0);
	fnic->bar0.bus_addr = pci_resource_start(pdev, 0);
	fnic->bar0.len = pci_resource_len(pdev, 0);

	if (!fnic->bar0.vaddr) {
		printk(KERN_ERR PFX "Cannot memory-map BAR0 res hdr, "
		       "aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	/* Register this vNIC device */
	fnic->vdev = vnic_dev_register(NULL, fnic, pdev, &fnic->bar0);
	if (!fnic->vdev) {
		printk(KERN_ERR PFX "vNIC registration failed, "
		       "aborting.\n");
		err = -ENODEV;
		goto err_out_iounmap;
	}

	/* Issue devcmd open to get device in known state */
	err = fnic_dev_open(fnic);
	if (err) {
		printk(KERN_ERR PFX
		       "vNIC dev open failed, aborting.\n");
		goto err_out_vnic_unregister;
	}
	
	/* Issue device init to initialize the vnic-to-switch link. 
	 * Do not wait for the negotiation with switch to complete. Continue
	 * loading the driver and enable the device. Once switch link is
	 * up, driver will get a link up for the device
	 */
	err = vnic_dev_init(fnic->vdev, 0);
	if (err) {
		printk(KERN_ERR PFX
		       "vNIC dev init failed, aborting.\n");
		goto err_out_dev_close;
	}

	/* Get the fnic mac address */
	err = vnic_dev_mac_addr(fnic->vdev, fnic->mac_addr);
	if (err) {
		printk(KERN_ERR PFX "vNIC get MAC addr failed \n");
		goto err_out_dev_close;
	}
	
	/* Get vNIC configuration */
	err = fnic_get_vnic_config(fnic);
	if (err) {
		printk(KERN_ERR PFX "Get vNIC configuration failed, "
		       "aborting.\n");
		goto err_out_dev_close;
	}

	/* Get resource counts for this fnic */
	fnic_get_res_counts(fnic);

	/* Set interrupt mode Legacy/MSI/MSIX */
	err = fnic_set_intr_mode(fnic);
	if (err) {
		printk(KERN_ERR PFX "Failed to set intr mode, "
		  "aborting.\n");
		goto err_out_dev_close;
	}

	/* Get interrupt resources for this fnic device */
	err = fnic_request_intr(fnic);
	if (err) {
		printk(KERN_ERR PFX "Unable to request irq.\n");
		goto err_out_clear_intr;
	}

	/* Allocate the fnic resources */
	err = fnic_alloc_vnic_resources(fnic);
	if (err) {
		printk(KERN_ERR PFX "Failed to alloc vNIC resources, "
		       "aborting.\n");
		goto err_out_free_intr;
	}

	/* initialize fnic state lock */
	spin_lock_init(&fnic->fnic_lock);

	for(i = 0; i < FNIC_WQ_MAX; i++) 
		spin_lock_init(&fnic->wq_lock[i]);

	for(i = 0; i < FNIC_WQ_COPY_MAX; i++) {
		spin_lock_init(&fnic->wq_copy_lock[i]);
		fnic->wq_copy_desc_low[i] = DESC_CLEAN_LOW_WATERMARK;
		fnic->fw_ack_recd[i] = 0;
		fnic->fw_ack_index[i] = -1;
	}

	/* allocate the outstanding io context array */
	fnic->outstanding_io_info_list = (struct fnic_io_info*)
		kzalloc(MAX_IO_REQ * sizeof(struct fnic_io_info),
			GFP_KERNEL);
	if (!fnic->outstanding_io_info_list) {
		printk(KERN_ERR PFX "Unable to allocate memory for io context"
		       " \n");
		err = -1;
		goto err_out_fnic_free_resources;
	}
	
	/* initialize the outstanding io context arrary */
	for (i = 0; i < MAX_IO_REQ; i++) {
		spin_lock_init
			(&fnic->outstanding_io_info_list[i].io_info_lock);
		fnic->outstanding_io_info_list[i].io_req = NULL;
		fnic->outstanding_io_info_list[i].indx = i;
	}

	/* initialize the free io list */
	spin_lock_init(&fnic->free_io_list_lock);
	INIT_LIST_HEAD(&fnic->free_io_list);
	for (i = MAX_IO_REQ-1; i >= 0; i--)
		list_add(&fnic->outstanding_io_info_list[i].free_io,
			 &fnic->free_io_list);

	/* allocate per cpu stats block
	*/
	for_each_online_cpu(i) {
		fnic->fc_dev->dev_stats[i] = kzalloc
			(sizeof(struct fcoe_dev_stats), GFP_KERNEL);
		if(!fnic->fc_dev->dev_stats[i])
			goto err_out_free_cpu_stats;
	}

	/* fnic number starts from 0 onwards */
	fnic->fnic_no = atomic_add_return(1, &fnic_no);
	fc_dev->fd_speed = OFC_SPEED_10GBIT;
	fc_dev->fd_speed_support = OFC_SPEED_10GBIT;

	/* Initialize fc_dev */
	snprintf(fc_dev->ifname, 64, "%s%d", DRV_NAME, fnic->fnic_no);
	snprintf(fc_dev->drv_info.model, 64, "fcoe");
	snprintf(fc_dev->drv_info.model_desc, 64, DRV_DESCRIPTION);
	snprintf(fc_dev->drv_info.drv_version, 64, DRV_VERSION);
	snprintf(fc_dev->drv_info.drv_name, 64, DRV_NAME);
	snprintf(fc_dev->drv_info.vendor, 64, "Nuova Systems Inc.");
	fc_dev->fd_wwnn = fnic->config.node_wwn;
	fc_dev->fd_wwpn = fnic->config.port_wwn;
	fc_dev->framesize = fnic->config.maxdatafieldsize + 
		sizeof(struct fc_frame_header);
	fc_dev->capabilities = FNIC_TRANS_CAP;
	fc_dev->options |= TRANS_O_FCS_AUTO;
	fc_dev->fd_link_status = TRANS_LINK_DOWN;
	fc_dev->min_xid = FNIC_FCS_XID_START;
	fc_dev->max_xid = FNIC_FCS_XID_END;
	fc_dev->luns_per_tgt = fnic->config.luns_per_tgt;
	fc_dev->dev_loss_tmo = FNIC_DFLT_DEVLOSS_TMO;
	fc_dev->dev = &pdev->dev;

	/*setup vlan config, hw inserts vlan header */
	fnic->vlan_hw_insert = 1;
	fnic->vlan_id = 0;

	/* Todo: This will come from the devspec area. Pass to OpenFC
	 * and disable/enable the Retry flag in the fcs args
	 */
	fnic->enable_srr = 1;

	fnic->flogi_oxid = FC_XID_UNKNOWN;
	fnic->flogi = NULL;
	fnic->flogi_resp = NULL;
	fnic->link_down_cnt = 0;


	/* Enable hardware stripping of vlan header on ingress */
	fnic_set_nic_cfg(fnic, 0, 0, 0, 0, 0, 0, 1);

	/* Setup notification buffer area */
	err = fnic_notify_set(fnic);
	if (err) {
		printk(KERN_ERR DFX
		       "Failed to alloc notify buffer, aborting.\n", 
		       fnic->fnic_no);
		goto err_out_free_cpu_stats;
	}
	
	/* Setup notify timer when using MSI interrupts */
	init_timer(&fnic->notify_timer);
	fnic->notify_timer.function = fnic_notify_timer;
	fnic->notify_timer.data = (unsigned long)fnic;

	/* allocate RQ buffers and post them to RQ*/
	for (i = 0; i < fnic->rq_count; i++) {
		err = vnic_rq_fill(&fnic->rq[i], fnic_alloc_rq_frame);
		if (err) {
			printk(KERN_ERR DFX "fnic_alloc_rq_frame can't alloc "
			       "frame\n", fnic->fnic_no);
			goto err_out_free_rq_buf;
		}
	}

	/* Register FC device*/
	if (openfc_register(fc_dev) < 0) {
		printk(KERN_ERR DFX "openfc_register failed\n", fnic->fnic_no);
		goto err_out_free_rq_buf;
	}

	/* Get the host number for this fnic */
	fnic->host_no = container_of(fc_dev, struct openfc_softc, fd)->host_no;

	printk(KERN_ERR DFX "host no %d bus addr 0x%llx\n", fnic->fnic_no, 
	       fnic->host_no, fnic->bar0.bus_addr);
	
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) && !defined(__VMKLNX__)
	{
		struct class_device *cp;

		/* Register with SysFS */
		cp = &fnic->fnic_class_device;
		class_device_initialize(cp);
		cp->class = fnic_class;
		cp->class_data = fnic;
		cp->groups = fnic_groups;

		snprintf(cp->class_id, sizeof(cp->class_id),
			 "host%d", fnic->host_no);
		err = class_device_add(cp);
	}
#endif /* LINUX_VERSION_CODE */

	/* Add this fnic device to driver global list of devices */
	spin_lock_irqsave(&fnic_hostlist_lock, flags);
	list_add_tail(&fnic->list, &fnic_hostlist);
	spin_unlock_irqrestore(&fnic_hostlist_lock, flags);

	/* fnic starts with fc mode */
	fnic->state = FNIC_IN_FC_MODE;

	/* Enable queues*/
	for (i = 0; i < fnic->raw_wq_count; i++)
		vnic_wq_enable(&fnic->wq[i]);
	for (i = 0; i < fnic->rq_count; i++)
		vnic_rq_enable(&fnic->rq[i]);
	for (i = 0; i < fnic->wq_copy_count; i++) 
		vnic_wq_copy_enable(&fnic->wq_copy[i]);

	/* Ready for action */
	vnic_dev_enable(fnic->vdev);
	for (i = 0; i < fnic->intr_count; i++) 
		vnic_intr_unmask(&fnic->intr[i]);
	/* Start notification timer */
	fnic_notify_timer_start(fnic);

	return 0;

 err_out_free_rq_buf:
	/* Clean up RQs for which buffers already created and posted */
	for (; i >=0; i--)
		vnic_rq_clean(&fnic->rq[i], fnic_free_rq_buf);		
	vnic_dev_notify_unset(fnic->vdev);
 err_out_free_cpu_stats:
	for_each_online_cpu(i) {
		kfree(fnic->fc_dev->dev_stats[i]);
		fnic->fc_dev->dev_stats[i] = NULL;
	}

	/* Empty the free list, then release the io context state */
	while(!list_empty(&fnic->free_io_list))
		list_del(fnic->free_io_list.next);
	kfree(fnic->outstanding_io_info_list);
	fnic->outstanding_io_info_list = NULL;
 err_out_fnic_free_resources:
	fnic_free_vnic_resources(fnic);
 err_out_free_intr:
	fnic_free_intr(fnic);
 err_out_clear_intr:
	fnic_clear_intr_mode(fnic);
 err_out_dev_close:
	vnic_dev_close(fnic->vdev);
 err_out_vnic_unregister:
	vnic_dev_unregister(fnic->vdev);
 err_out_iounmap:
	fnic_iounmap(fnic);
 err_out_release_regions:
	pci_release_regions(pdev);
 err_out_disable_device:
	pci_disable_device(pdev);
 err_out_free_fcdev:
	openfc_put_dev(fc_dev);
 err_out:
	return err;
}

static void __devexit fnic_remove(struct pci_dev *pdev)
{
	struct fnic *fnic = pci_get_drvdata(pdev);

	if (fnic) {
		fnic_cleanup(fnic);
		openfc_unregister(fnic->fc_dev);
		vnic_dev_notify_unset(fnic->vdev);
		fnic_free_vnic_resources(fnic);
		fnic_free_intr(fnic);
		fnic_clear_intr_mode(fnic);
		vnic_dev_close(fnic->vdev);
		vnic_dev_unregister(fnic->vdev);
		fnic_iounmap(fnic);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		openfc_put_dev(fnic->fc_dev);
	}
}

static int fnic_suspend(struct pci_dev *pdev, pm_message_t state)
{
	printk(KERN_ERR PFX "fnic_suspend not implemented yet.\n");
	return 0;
}

static int fnic_resume(struct pci_dev *pdev)
{
	printk(KERN_ERR PFX "fnic_resume not implemented yet.\n");
	return 0;
}

static struct pci_driver fnic_driver = {
	.name = DRV_NAME,
	.id_table = fnic_id_table,
	.probe = fnic_probe,
	.remove = __devexit_p(fnic_remove),
	.suspend = fnic_suspend,
	.resume = fnic_resume,
};

static int __init fnic_init_module(void)
{
	size_t len;

	printk(KERN_INFO PFX "%s, ver %s\n", DRV_DESCRIPTION, DRV_VERSION);
	printk(KERN_INFO PFX "%s\n", DRV_COPYRIGHT);

#ifdef OPENFC_LIB
	openfc_init();
#endif /* OPENFC_LIB */

	/* total length includes the fc_frame datastructure, and
	 * the packet that will go on wire
	 */
	len = sizeof(struct fc_frame) + sizeof(struct fcp_hdr) + \
		FC_MAX_PAYLOAD + sizeof(struct fcoe_crc_eof);
	/* Create a cache for allocation of OpenFC/FCS frames*/
	fnic_fc_frame_cache = kmem_cache_create("fnic_fcs_frames", len,
						0, SLAB_CACHE_DMA,
#if defined(__VMKLNX__)
						NULL,
#endif
						NULL);
	if (!fnic_fc_frame_cache) {
		printk(KERN_ERR PFX "failed to create fnic fc frame slab");
		goto err_create_fc_frame_slab;
	} 
	
	/* Create a cache for allocation of default size sgls*/
	len = sizeof(struct fnic_dflt_sgl_list);
	fnic_sgl_cache[FNIC_SGL_CACHE_DFLT] = kmem_cache_create("fnic_sgl_dflt",
#if !defined(__VMKLNX__)
		 len, PALO_SG_DESC_ALIGN, SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA,
#else /* __VMKLNX__ */
		 len + PALO_SG_DESC_ALIGN, 0,	/* pad for alignment */
		 SLAB_CACHE_DMA, NULL,
#endif /* __VMKLNX__ */
		 NULL);
	if (!fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]) {
		printk(KERN_ERR PFX "failed to create fnic dflt sgl slab");
		goto err_create_fnic_sgl_slab_dflt;
	}

	/* Create a cache for allocation of max size sgls*/
	len = sizeof(struct fnic_sgl_list);
	fnic_sgl_cache[FNIC_SGL_CACHE_MAX] = kmem_cache_create("fnic_sgl_max",
#if !defined(__VMKLNX__)
		 len, PALO_SG_DESC_ALIGN, SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA,
#else /* __VMKLNX__ */
		 len + PALO_SG_DESC_ALIGN, 0,	/* pad for alignment */
		 SLAB_CACHE_DMA, NULL,
#endif /* __VMKLNX__ */
		 NULL);
	if (!fnic_sgl_cache[FNIC_SGL_CACHE_MAX]) {
		printk(KERN_ERR PFX "failed to create fnic max sgl slab");
		goto err_create_fnic_sgl_slab_max;
	}

	/* Create a cache of objects to wrap frames/events sent to openfc*/
	len = sizeof(struct fnic_event);
	fnic_ev_cache = kmem_cache_create("fnic_ev", len,
					  0, 0,
#if defined(__VMKLNX__)
		 			  NULL,
#endif /* __VMKLNX__ */
		 			  NULL);
	if (!fnic_ev_cache) {
		printk(KERN_ERR PFX "failed to create fnic event slab");
		goto err_create_fnic_fr_slab;
	}
	
	/* initialize the Inbound FC Frames list spinlock*/
	spin_lock_init(&fnic_eventlist_lock);

	/* initialize the global fnic host list lock */
	spin_lock_init(&fnic_hostlist_lock);

	/* Create a thread for handling incoming OpenFC/FCS frames*/
	fnic_thread = kthread_create(fnic_fc_thread,
				     NULL,
				     "fnicthread");

	if (likely(!IS_ERR(fnic_thread))) {
		wake_up_process(fnic_thread);
	} else {
		printk(KERN_ERR PFX "fnic thread create failed\n");
		goto err_create_fnic_thread;
	}

#if !defined(__VMKLNX__)
	/* Initialize ioctl interface for the driver */
	if (fnic_ioctl_init())
		goto err_ioctl_init;
#endif /* __VMKLNX__ */

	/* initialize fnic_no to -1, the first device is numbered 0 */
	atomic_set(&fnic_no, -1);

	/* register the driver with PCI system*/
	if (pci_register_driver(&fnic_driver) < 0) {
		printk(KERN_ERR PFX "pci register error\n");
		goto err_pci_register;
	}

	return 0;

 err_pci_register:
#if !defined(__VMKLNX__)
	fnic_ioctl_exit();
 err_ioctl_init:
#endif /* __VMKLNX__ */
	kthread_stop(fnic_thread);
 err_create_fnic_thread:
	kmem_cache_destroy(fnic_ev_cache);
	fnic_ev_cache = NULL;
 err_create_fnic_fr_slab:
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	fnic_sgl_cache[FNIC_SGL_CACHE_MAX] = NULL;
 err_create_fnic_sgl_slab_max:
	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	fnic_sgl_cache[FNIC_SGL_CACHE_DFLT] = NULL;
 err_create_fnic_sgl_slab_dflt:
	kmem_cache_destroy(fnic_fc_frame_cache);
	fnic_fc_frame_cache = NULL;
 err_create_fc_frame_slab:
	return -1;
}

static void __exit fnic_cleanup_module(void)
{
	struct fnic_event *ev = NULL;
	unsigned long flags;

	pci_unregister_driver(&fnic_driver);

#if !defined(__VMKLNX__)
	fnic_ioctl_exit();
#endif /* __VMKLNX__ */

	kthread_stop(fnic_thread);

	/* Cleanup the event list */
	spin_lock_irqsave(&fnic_eventlist_lock,flags);
	while(!list_empty(&fnic_eventlist)){
		ev = (struct fnic_event*) 
			fnic_eventlist.next;
		if (ev->fp)
			fc_frame_free(ev->fp);
		list_del(fnic_eventlist.next);
		kmem_cache_free(fnic_ev_cache, ev);
		ev = NULL;
	}
	spin_unlock_irqrestore(&fnic_eventlist_lock,flags);


	/* release memory for all global caches */
	kmem_cache_destroy(fnic_ev_cache);
	fnic_ev_cache = NULL;

	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_MAX]);
	fnic_sgl_cache[FNIC_SGL_CACHE_MAX] = NULL;

	kmem_cache_destroy(fnic_sgl_cache[FNIC_SGL_CACHE_DFLT]);
	fnic_sgl_cache[FNIC_SGL_CACHE_DFLT] = NULL;

	kmem_cache_destroy(fnic_fc_frame_cache);
	fnic_fc_frame_cache = NULL;

#ifdef OPENFC_LIB
	openfc_exit();
#endif /* OPENFC_LIB */
}

module_init(fnic_init_module);
module_exit(fnic_cleanup_module);

