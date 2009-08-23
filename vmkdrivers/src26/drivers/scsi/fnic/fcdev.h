/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc. All Rights Reserved
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
 * $Id: fcdev.h 4554 2007-05-24 23:25:58Z jre $
 */
#ifndef _FCDEV_H_
#define _FCDEV_H_

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/mempool.h>


/*
 * This struct is created by an instance of a transport specific HBA driver.
 * The openfc driver and transport specific drivers use this structure.
 */
struct module;
struct fcdev;
struct fc_scsi_pkt;
struct fc_frame;
struct fc_remote_port;

/*
 * Ops vector for upper layer
 */
struct openfc_port_operations {
	struct module  *owner;

	/*
	 * interface to send FC frame
	 */
	int		(*send) (struct fcdev * hba, struct fc_frame * frame);

	/*
	 * interface to send scsi pkt to FCP State machine
	 */
	int		(*send_scsi) (struct fcdev * hba,
				      struct fc_scsi_pkt * fsp);

	/*
	 * i/f for abort, tgt reset, lun reset and hba reset
	 */

	int		(*abort_cmd) (struct fcdev * hba,
				      struct fc_scsi_pkt * fsp);
	int		(*target_reset) (struct fcdev * hba,
					 struct fc_scsi_pkt * fsp);
	int		(*bus_reset) (struct fcdev * hba);
	int		(*host_reset) (struct fcdev * hba,
					 struct fc_scsi_pkt * fsp);
	void            (*cleanup_scsi) (struct fc_scsi_pkt *fsp);
	void            (*get_stats) (struct fcdev *);
	void            (*remote_port_state_change) (struct fcdev *,
						     struct fc_remote_port *,
						     u_int8_t);
	/*
	 * frame allocation routine
	 */
	struct fc_frame *(*frame_alloc)(size_t);

	ulong		alloc_flags;
	int		ext_fsp_size;
	unsigned short  sg_tablesize;
};

#define TRANS_LINK_UP	    0x01
#define TRANS_LINK_DOWN     0x02
/*
 * destination address mode
 * 0) Gateway based address
 * 1) FC OUI based address
 */
#define FCOE_GW_ADDR_MODE      0x00
#define FCOE_FCOUI_ADDR_MODE   0x01

#define FC_DFLT_LOGIN_TIMEOUT 10   // 10sec
#define FC_DFLT_LOGIN_RETRY   5
#define FC_INTR_DELAY_OFF     0
#define FC_INTR_DELAY_ON      1

/*
 * fcoe stats structure
 */
struct fcoe_dev_stats {
	uint64_t	SecondsSinceLastReset;
	uint64_t	TxFrames;
	uint64_t	TxWords;
	uint64_t	RxFrames;
	uint64_t	RxWords;
	uint64_t	ErrorFrames;
	uint64_t	DumpedFrames;
	uint64_t	LinkFailureCount;
	uint64_t	LossOfSignalCount;
	uint64_t	InvalidTxWordCount;
	uint64_t	InvalidCRCCount;
	uint64_t	InputRequests;
	uint64_t	OutputRequests;
	uint64_t	ControlRequests;
	uint64_t	InputMegabytes;
	uint64_t	OutputMegabytes;
};
/*
 * device specific information
 */
struct fc_drv_info {
	char 		model[64];
	char 		vendor[64];
	char 		sn[64];
	char 		model_desc[256];
	char 		hw_version[256];
	char 		fw_version[256];
	char 		opt_rom_version[256];
	char 		drv_version[128];
	char 		drv_name[128];
};

/*
 * Transport Capabilities
 */
#define TRANS_C_QUEUE	(1 << 0)  /*cmd queuing */
#define TRANS_C_CRC	(1 << 1)  /* FC CRC */
#define TRANS_C_DIF	(1 << 2)  /* t10 DIF */
#define TRANS_C_SG	(1 << 3)  /* Scatter gather */
#define TRANS_C_WSO	(1 << 4)  /* write seg offload */
#define TRANS_C_DDP	(1 << 5)  /* direct data placement for read */
#define TRANS_C_NO_DISC	(1 << 6)  /* No FC discovery */
/*
 * Transport Options
 */
#define TRANS_O_FCS_AUTO	(1 << 0) /* Bringup FCS at registratio time */
 
/*
 * transport  driver structure
 * one per instance of the driver
 */
struct fcdev {
	unsigned long long fd_wwnn;	/* hba node name */
	unsigned long long fd_wwpn;	/* hba port name */
	int		fd_link_status;	/* link status */
	u16		fd_speed;       /* link speed */
	u16		fd_speed_support; /* supported link speeds */
	struct openfc_port_operations port_ops; /* transport op vector */
	struct device	*dev;		/* lower-level device if any */
	/*
	 * driver specific stuff
	 */
	void		*drv_priv; 	/* private data */
	uint32_t 	capabilities;	/* driver cap is defined here */
	uint32_t 	options;	/* driver options is defined here */
	/*
	 * protocol related stuff
	 */
	char		ifname[IFNAMSIZ];
	uint16_t	framesize;
	fc_xid_t        min_xid;
	fc_xid_t        max_xid;
	uint32_t        dev_loss_tmo;
	u32		luns_per_tgt;	/* max LUNs per target */

	/*
	 * Driver specific info used by HBA API
	 */
	struct fc_drv_info  drv_info;	
	/*
	 * per cpu fc stat block
	 */
	struct fcoe_dev_stats *dev_stats[NR_CPUS];

};

/*
 * used by lower layer drive (fcoe)
 */
struct fcdev *	openfc_alloc_dev(struct openfc_port_operations *, int);
void		openfc_put_dev(struct fcdev *);
int		openfc_register(struct fcdev *);
void		openfc_rcv(struct fcdev *, struct fc_frame *);
void		openfc_unregister(struct fcdev *);
void		openfc_linkup(struct fcdev *);
void		openfc_linkdown(struct fcdev *);
void		openfc_pause(struct fcdev *);
void		openfc_unpause(struct fcdev *);
void		openfc_set_mfs(struct fcdev *);

/*
 * Mask for per-CPU exchange pools.
 * The exchange ID can be ANDed with this mask to find the CPU that will
 * have the least lock contention in handling the exchange.
 */
extern u16	openfc_cpu_mask;

#endif /* _FCDEV_H_ */
