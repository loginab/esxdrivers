/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: openfc.h 22660 2009-01-16 00:24:57Z jre $
 */
#ifndef _OPENFC_H_
#define _OPENFC_H_


#include "fc_fcp.h"

#define OPENFC_MAX_LUN_COUNT		1024U
#define OPENFC_MAX_OUTSTANDING_COMMANDS 2048
#define OPENFC_SRB_CACHEP_NAME 20
#define OPENFC_DFLT_DEVLOSS_TMO 30

struct os_tgt;
struct os_lun;
struct fc_scsi_pkt;


/*
 * openfc  hba state flages
 */
#define OPENFC_INITIALIZATION	    1
#define OPENFC_FCS_INITIALIZATION   2
#define OPENFC_FCS_RESET            3
#define OPENFC_DISCOVERY_DONE	    4
#define OPENFC_RUNNING		    5
#define OPENFC_GOING_DOWN	    6
/*
 * openfc hba status
 */
#define OPENFC_PAUSE		    (1 << 1)
#define OPENFC_LINK_UP		    (1 << 0)

#define OPENFC_FCS_ONLINE		1	
#define OPENFC_FCS_OFFLINE	        2

#define OPENFC_SRB_CACHEP_NAME 20

#define OPENFC_MIN_XID          0x0004
#define OPENFC_MAX_XID          0x07ef

#define CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)
#define CMD_ENTRY_STATUS(Cmnd)	((Cmnd)->SCp.have_data_in)
#define CMD_COMPL_STATUS(Cmnd)	((Cmnd)->SCp.this_residual)
#define CMD_SCSI_STATUS(Cmnd)	((Cmnd)->SCp.Status)
#define CMD_RESID_LEN(Cmnd)	((Cmnd)->SCp.buffers_residual)

#define OPENFC_SCSI_ER_TIMEOUT	(10 * HZ)
#define OPENFC_SCSI_REC_TOV	(2 * HZ)
#define OPENFC_TIMEOUT_SUCESS	0
#define OPENFC_TIMEOUT_FAILD	1

enum os_lun_state {
	OPENFC_LUN_READY,
	OPENFC_LUN_OFFLINE,		/* taken offline by user */
	OPENFC_LUN_ERR,
	OPENFC_LUN_UNKNW,
};

struct openfc_cmdq {
	spinlock_t	scsi_pkt_lock;
	void	       *ptr;
};

/*
 * openfc hba software structure
 */
struct openfc_softc {

	struct list_head list;
	/*
	 * pointer to scsi host structure
	 */
	struct Scsi_Host *host;
	struct timer_list timer;
	/*
	 * low level driver handle
	 */
	struct fcdev   *dev;		/* handle to lower level driver */
	uint16_t	state;		/* state flags */
	uint16_t	status;
	atomic_t	fcs_status;

	/*
	 * dchba_softc specific veriables
	 */
	uint32_t	host_no;
	uint32_t	instance;
	atomic_t	link_state;
	struct task_struct *dpc_scan_thread;

	/*
	 * array of pointers for outstanding cmds 
	 * (only used by openfc_scsi.c)
	 */
	struct openfc_cmdq outstandingcmd[OPENFC_MAX_OUTSTANDING_COMMANDS];
	int		current_cmd_indx;
	____cacheline_aligned spinlock_t outstandingcmd_lock;

	/*
	 * FCS parameters
	 */
	struct fcs_state *fcs_state;	/* fcs state handle */
	struct fc_port *fcs_port;	/* pointer to local port */
	int		login_timeout;
	int		login_retries;
	ulong		qdepth;
	atomic_t	discover_devs;
	/*
	 * scsi_fc packet
	 */
	char	scsi_pkt_cachep_name[OPENFC_SRB_CACHEP_NAME];

#if !defined(__VMKLNX__)
	struct kmem_cache *openfc_scsi_pkt_cachep;
#else
	kmem_cache_t	*openfc_scsi_pkt_cachep;
#endif /* __VMKLNX__ */
	int	ext_fsp_size;
	ulong	alloc_flags;

	struct scsi_transport_template *transport_templet;
	struct fc_host_statistics openfc_host_stat;
	struct class_device	openfc_class_device;
	/*
	 * fcdev struct starts here
	 */
	struct fcdev	fd;
};

#define OPENFC_TGT_REC_SUPPORTED      (1 << 1)
#define OPENFC_TGT_REC_NOT_SUPPORTED ~(OPENFC_TGT_REC_SUPPORTED)
#define OPENFC_TGT_RETRY_OK	      (1 << 0)
#define OPENFC_TGT_NO_RETRY	     ~(OPENFC_TGT_RETRY_OK)
/*
 * Openfc data struct per target port
 */
struct os_tgt {
	struct fc_remote_port *fcs_rport;
	struct fc_rport *rport;
	struct openfc_softc *hba;
	int		num_lun;
	uint16_t	resv;
	uint16_t	flags;
	fc_fid_t	fcid;
	/*
	 * assign by scs_transport_fc
	 */
	uint32_t	tid;

	/*
	 * Binding information
	 */
	fc_wwn_t	node_name;
	fc_wwn_t	port_name;

	struct list_head lun_list;
};

/*
 * Openfc data struct per lun 
 */
struct os_lun {
	struct scsi_device *sdev;
	struct os_tgt  *tgtp;		/* pointer to the target */
	uint32_t	lun_number;	/* lun number */
	enum os_lun_state state;
	int		error_cnt;	/* consecutive error count */
	unsigned long	avg_time;	/* average read/write time */
};


static inline struct openfc_softc *openfc_get_softc(struct fcdev *dev)
{
	return container_of(dev, struct openfc_softc, fd);
}

/*
 * function prototype defination
 */
int		openfc_ioctl_init(void);
int		openfc_ioctl_exit(void);
int		openfc_target_reset(struct fcdev *, struct fc_scsi_pkt *);
int		openfc_inf_reset(struct fcdev *, struct fc_scsi_pkt *);
int		openfc_abort_cmd(struct fcdev *, struct fc_scsi_pkt *);

struct fc_scsi_pkt *openfc_alloc_scsi_pkt(struct openfc_softc *);
void		openfc_scsi_abort_iocontext(struct fc_scsi_pkt *);
int		openfc_free_scsi_pkt(struct fc_scsi_pkt *);
void		openfc_scsi_pkt_hold(struct fc_scsi_pkt *);
void		openfc_scsi_pkt_release(struct fc_scsi_pkt *);
int		openfc_destroy_scsi_slab(struct openfc_softc *);
int		openfc_create_scsi_slab(struct openfc_softc *);
struct openfc_softc *openfc_find_hba(int host_no);
char	       *openfc_info(struct Scsi_Host *host);
void 		openfc_fcs_start(struct openfc_softc *);
void 		openfc_fcs_stop(struct openfc_softc *);
int             openfc_reset_if(struct openfc_softc *);

/*
 * sysfs attribute groups from FCS.
 */
extern struct attribute_group fcs_exch_attr_group;
extern struct attribute_group fcs_local_port_attr_group;
extern struct class *openfc_class;

#ifdef OPENFC_LIB
int openfc_init(void);
void openfc_exit(void);
#endif /* OPENFC_LIB */

#endif /* _OPENFC_H_ */
