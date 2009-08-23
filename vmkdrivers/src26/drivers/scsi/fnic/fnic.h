/*
 * Copyright 2008 Nuova Systems, Inc.  All rights reserved.
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
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
 * $Id: fnic.h 21944 2008-12-16 20:34:07Z jre $
 */

#ifndef _FNIC_H_
#define _FNIC_H_

#include "sa_hash.h"
#include "fc_types.h"
#include "fnic_io.h"
#include "fcdev.h"
#include "fnic_res.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_scsi.h"

#define DRV_NAME		"fnic"
#define DRV_DESCRIPTION		"Cisco Systems Fibre Channel Driver"
#define DRV_VERSION		__stringify(BUILD_VERSION)
#define DRV_COPYRIGHT		"Copyright 2008 Cisco Systems"
#define PFX			DRV_NAME ": "
#define DFX			DRV_NAME "%d: "

#define DESC_CLEAN_LOW_WATERMARK 8

#define FNIC_TRANS_COMMON_CAP (TRANS_C_QUEUE | TRANS_C_CRC | TRANS_C_DIF | \
	TRANS_C_SG | TRANS_C_WSO | TRANS_C_DDP)

#define FNIC_TRANS_SPECIAL_CAP 0

#define FNIC_TRANS_CAP (FNIC_TRANS_COMMON_CAP | FNIC_TRANS_SPECIAL_CAP)

#define MAX_IO_REQ       2048
#define FNIC_LUN_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_HOST_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_RMDEVICE_TIMEOUT        1000       /* mSec */

#define FNIC_FCS_XID_COUNT	256
#define FNIC_FCS_XID_START	FCPIO_HOST_EXCH_RANGE_START
#define FNIC_FCS_XID_END	(FNIC_FCS_XID_START + FNIC_FCS_XID_COUNT - 1)

#if FNIC_FCS_XID_END > FCPIO_HOST_EXCH_RANGE_END
#error FNIC requested XID range exceeds firmware-supplied range
#endif

enum fnic_intx_intr_index {
	FNIC_INTX_WQ_RQ_COPYWQ,
	FNIC_INTX_ERR,
	FNIC_INTX_NOTIFY,
	FNIC_INTX_INTR_MAX,
};

enum fnic_msix_intr_index {
	FNIC_MSIX_RQ,
	FNIC_MSIX_WQ,
	FNIC_MSIX_WQ_COPY,
	FNIC_MSIX_ERR_NOTIFY,
	FNIC_MSIX_INTR_MAX,
};

struct fnic_msix_entry {
	int requested;
	char devname[IFNAMSIZ];
	irqreturn_t (*isr)(int, void *);
	void *devid;
};

enum fnic_state {
	FNIC_IN_FC_MODE,
	FNIC_IN_FC_TRANS_ETH_MODE,
	FNIC_IN_ETH_MODE,
	FNIC_IN_ETH_TRANS_FC_MODE
};

#define FNIC_WQ_COPY_MAX 1
/* This is the default value that scsi_transport_fc uses for dev_loss_tmo
 * Min value is 1, maximum is SCSI_DEVICE_BLOCK_MAX_TIMEOUT
 */
#define FNIC_DFLT_DEVLOSS_TMO 60

/* Per-instance private data structure */
struct fnic {
	struct vnic_dev_bar bar0;

	struct msix_entry msix_entry[FNIC_MSIX_INTR_MAX];
	struct fnic_msix_entry msix[FNIC_MSIX_INTR_MAX];

	struct vnic_stats *stats;
	struct vnic_nic_cfg *nic_cfg;
	char name[IFNAMSIZ];

	struct class_device	fnic_class_device;
	struct list_head list;           /* node in list of fnics */
	uint32_t	host_no;
	uint32_t	fnic_no;
	struct timer_list notify_timer; /* used for MSI interrupts */

	unsigned int err_intr_offset;
	unsigned int link_intr_offset;

	unsigned int wq_count;
	unsigned int cq_count;

	u_int32_t fcoui_mode:1;		/* use fcoui address*/
	u_int32_t vlan_hw_insert:1;	/* let hw insert the tag */
	u_int32_t ack_recd:1;		/* fw ack recd */
	u_int32_t enable_srr:1;         /* enable SRR for this vnic */
	u_int32_t in_remove:1;          /* fnic being removed */

	struct completion *remove_wait;  /* device remove thread blocks */ 
	struct completion *reset_wait; /* host reset thread blocks */
	struct fc_frame *flogi;
	struct fc_frame *flogi_resp;
	u_int16_t flogi_oxid;
	unsigned long s_id;
	u_int32_t link_down_cnt;
	enum fnic_state state;
	spinlock_t fnic_lock;

	u_int16_t vlan_id;	/* VLAN tag including priority */
	u_int8_t  mac_addr[ETH_ALEN];
	u_int8_t  dest_addr[ETH_ALEN];
	u_int8_t  data_src_addr[ETH_ALEN];

	/* outstanding IO list head */
	struct fnic_io_info *outstanding_io_info_list;
	____cacheline_aligned struct list_head free_io_list;
	spinlock_t free_io_list_lock;
	struct fcdev *fc_dev;
	struct pci_dev *pdev;  /* fast path */
	struct vnic_fc_config config;	 /* fast path */
	struct vnic_dev *vdev; /* fast path */
	unsigned int raw_wq_count;
	unsigned int wq_copy_count;
	unsigned int rq_count;
	int fw_ack_index[FNIC_WQ_COPY_MAX];
	unsigned short fw_ack_recd[FNIC_WQ_COPY_MAX];
	unsigned short wq_copy_desc_low[FNIC_WQ_COPY_MAX];
	unsigned int intr_count;
	u32 *legacy_pba;		/* memory-mapped */


	/* copy work queue cache line section */
	____cacheline_aligned struct vnic_wq_copy wq_copy[FNIC_WQ_COPY_MAX];
	/* completion queue cache line section */
	____cacheline_aligned struct vnic_cq cq[FNIC_CQ_MAX];

	spinlock_t wq_copy_lock[FNIC_WQ_COPY_MAX];

	/* work queue cache line section */
	____cacheline_aligned struct vnic_wq wq[FNIC_WQ_MAX];

	spinlock_t wq_lock[FNIC_WQ_MAX];

	/* receive queue cache line section */
	____cacheline_aligned struct vnic_rq rq[FNIC_RQ_MAX];

	/* interrupt resource cache line section */
	____cacheline_aligned struct vnic_intr intr[FNIC_MSIX_INTR_MAX];
};

enum fnic_thread_event_type {
	EV_TYPE_LINK_DOWN = 0,
	EV_TYPE_LINK_UP,
	EV_TYPE_FRAME,
};

/* ISR allocates and inserts an event into a list
 * Fnic thread processes the frame, and then deallocates the entry
 */
struct fnic_event {
	/* list head has to the first field*/
	struct list_head list;             /* link on list of frames */
	struct fc_frame* fp;               /* FC frame pointer */
	struct fnic *fnic;                 /* fnic on which event received */
	enum fnic_thread_event_type ev_type;/* ev type: frame or link event*/
	u_int32_t  is_flogi_resp_frame:1;
};

/* Fnic Thread for handling FCS events  and link events */
extern struct task_struct *fnic_thread;
extern struct list_head   fnic_eventlist;
extern spinlock_t         fnic_eventlist_lock;

/* global driver caches across all fnics */
extern struct kmem_cache	*fnic_fc_frame_cache;
extern struct kmem_cache	*fnic_ev_cache;
extern struct kmem_cache	*fnic_ioreq_cache;
extern struct kmem_cache	*fnic_sgl_cache[FNIC_SGL_NUM_CACHES];

/* fnic_isr.c prototypes */
void fnic_clear_intr_mode(struct fnic *fnic);
int fnic_set_intr_mode(struct fnic *fnic);
void fnic_free_intr(struct fnic *fnic);
int fnic_request_intr(struct fnic *fnic);

/* fnic_scsi.c prototypes */
int fnic_send(struct fcdev *fcdev, struct fc_frame *frame);
int fnic_send_scsi (struct fcdev *fcdev, struct fc_scsi_pkt *fsp);
int fnic_abort_cmd (struct fcdev *fcdev, struct fc_scsi_pkt *fsp);
int fnic_device_reset (struct fcdev *fc_dev, struct fc_scsi_pkt * fsp);
int fnic_host_reset (struct fcdev *fc_dev, struct fc_scsi_pkt * fsp);
void fnic_cleanup_scsi(struct fc_scsi_pkt *fsp);
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int);
int fnic_wq_cmpl_handler(struct fnic *fnic, int);
struct fnic_io_info* fnic_get_io_info(struct fnic* fnic);
int fnic_flogi_reg_handler(struct fnic *fnic);
void fnic_cleanup_io(struct fnic *fnic, int);
void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq, 
				  struct fcpio_host_req *desc);
int fnic_fw_reset_handler(struct fnic *fnic);
void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf);

/* fnic_fcs.c prototypes */
int fnic_fc_thread(void *arg);
int fnic_rq_cmpl_handler(struct fnic *fnic, int);
int fnic_alloc_rq_frame(struct vnic_rq* rq);
void fnic_rx_frame_free(struct fc_frame *fp);
void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf);
int fnic_send_frame(struct fnic *, struct fc_frame *);

/* fnic_main.c prototypes */
void fnic_handle_link_event(struct fnic *fnic);
void fnic_get_stats (struct fcdev *fc_dev);
void fnic_log_q_error(struct fnic *fnic);
void fnic_notify_check(struct fnic *fnic);

#endif /* _FNIC_H_ */
