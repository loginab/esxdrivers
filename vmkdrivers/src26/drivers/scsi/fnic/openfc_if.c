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
 * $Id: openfc_if.c 24088 2009-01-16 00:24:57Z jre $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/atomic.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>

#include "fc_types.h"
#include "sa_assert.h"
#include "fc_port.h"
#include "fc_event.h"
#include "fc_remote_port.h"
#include "fcdev.h"
#include "fc_frame.h"
#include "fc_sess.h"
#include "sa_log.h"
#include "openfc.h"
#include "openfc_scsi_pkt.h"
#include "fcs_state.h"

MODULE_AUTHOR("Cisco Systems");
MODULE_DESCRIPTION("OpenFC driver");
MODULE_LICENSE("GPL");		/* unreleased, Nuova Systems proprietary */

#if defined(__VMKLNX__)
#include "build_version.h"
#endif /* __VMKLNX__ */

#ifdef BUILD_VERSION
#define OPENFC_VERSION "OPENFC HBA " __stringify(BUILD_VERSION)
#else
#define OPENFC_VERSION "OPENFC HBA (TBD)"
#endif

#define OPENFC_MAX_FCP_TARGET	256
#define OPENFC_DFLT_QUEUE_DEPTH 32
#define OPENFC_DFLT_SG_TABLESIZE 4
#define OPENFC_HOST_RESET_SETTLE_TIME 10 /* secs */

/*
 * global link list of HBA's
 */
LIST_HEAD(openfc_hostlist);
static spinlock_t openfc_hostlist_lock = SPIN_LOCK_UNLOCKED;
DECLARE_MUTEX(ofc_drv_tmpl_mutex);
uint32_t	max_host_no = 0;
ulong		openfc_boot_time = 0;

/*
 * SCSI host template entry points
 */
static int	openfc_slave_configure(struct scsi_device *);
static int	openfc_slave_alloc(struct scsi_device *);
static void	openfc_slave_destroy(struct scsi_device *);
static int	openfc_queuecommand(struct scsi_cmnd *,
				    void (*fn) (struct scsi_cmnd *));
static int	openfc_eh_abort(struct scsi_cmnd *);
static int	openfc_eh_device_reset(struct scsi_cmnd *);
static int	openfc_eh_host_reset(struct scsi_cmnd *);
static void	openfc_remote_port_state_change(void *,
						struct fc_remote_port *);
static void	openfc_discovery_done(void *);
static int	openfc_change_queue_depth(struct scsi_device *, int);
static int	openfc_change_queue_type(struct scsi_device *, int);
extern void	openfc_fc_attr_init(struct openfc_softc *);
extern struct fc_function_template openfc_transport_function;
extern struct class_device_attribute *openfc_host_attrs[];
extern int	openfc_scsi_send(struct fcdev *, struct fc_scsi_pkt *);
extern int	openfc_abort_cmd(struct fcdev *, struct fc_scsi_pkt *);
extern int	openfc_target_reset(struct fcdev *, struct fc_scsi_pkt *);
extern int	openfc_host_reset(struct fcdev *);
extern void 	openfc_scsi_cleanup(struct fc_scsi_pkt * );
char		openfc_version_str[] = OPENFC_VERSION;
static struct scsi_transport_template *openfc_transport_template = NULL;

static struct fcs_create_args openfc_fcs_args = {
	.fca_remote_port_state_change = openfc_remote_port_state_change,
	.fca_disc_done = openfc_discovery_done,
	.fca_service_params = (FCP_SPPF_INIT_FCN | FCP_SPPF_RD_XRDY_DIS |
			       FCP_SPPF_RETRY),
	.fca_e_d_tov = 2 * 1000,	/* FC-FS default */
	.fca_plogi_retries = 3,
};

static const char * openfc_get_info(struct Scsi_Host *shost)
{
	return (const char *)THIS_MODULE;
}

static struct scsi_host_template openfc_driver_template = {
	.module = THIS_MODULE,
	.name = "openfc Driver",
	.queuecommand = openfc_queuecommand,
	.info = openfc_get_info,
	.eh_abort_handler = openfc_eh_abort,
	.eh_device_reset_handler = openfc_eh_device_reset,
	.eh_host_reset_handler = openfc_eh_host_reset,
	.slave_configure = openfc_slave_configure,
	.slave_alloc = openfc_slave_alloc,
	.slave_destroy = openfc_slave_destroy,
	.change_queue_depth = openfc_change_queue_depth,
	.change_queue_type = openfc_change_queue_type,
	.this_id = -1,
	.cmd_per_lun = 3,
	.can_queue = OPENFC_MAX_OUTSTANDING_COMMANDS,
	.use_clustering = ENABLE_CLUSTERING,
	.sg_tablesize = OPENFC_DFLT_SG_TABLESIZE,
	.max_sectors = 0xffff,
	.shost_attrs = openfc_host_attrs,
};
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
static struct attribute_group *openfc_groups[] = {
	&fcs_exch_attr_group,
	&fcs_local_port_attr_group,
	NULL
};
#endif

/**
 * openfc_io_compl: -  Handle responses for completed  commands
 *
 * Translates a openfc error to a Linux SCSI error
 */

void openfc_io_compl(struct fc_scsi_pkt *sp)
{
	struct scsi_cmnd *sc_cmd;
	struct openfc_softc *openfcp;
	struct openfc_cmdq *qp;

	openfcp = sp->openfcp;

	if (openfcp == NULL) {
		SA_LOG("error could not find openfc hba softc");
		return;
	}
	if (timer_pending(&sp->timer)) {
		del_timer_sync(&sp->timer);
	}

	sc_cmd = sp->cmd;

	if (sc_cmd == NULL) {
		SA_LOG("command already returned back to OS");
		return;
	}
	sp->cmd = NULL;
	/*
	 * we NULL out the openfc_cmdq pointer
	 * if it is already NULL that means command is getting aborted
	 * let the abort code handle this command
	 */
	qp = (struct openfc_cmdq *) xchg(&(CMD_SP(sc_cmd)), NULL);
	if (qp == NULL)
		return;
	CMD_SCSI_STATUS(sc_cmd) = sp->cdb_status;
	switch (sp->status_code) {
	case OPENFC_COMPLETE:
		if (sp->cdb_status == 0) {
			/*
			 * good I/O status
			 */
			sc_cmd->result = DID_OK << 16;
			if (sp->scsi_resid)
				CMD_RESID_LEN(sc_cmd) = sp->scsi_resid;
		} else if (sp->cdb_status == QUEUE_FULL) {
			struct scsi_device *tmp_sdev;
			struct scsi_device *sdev = sc_cmd->device;

			shost_for_each_device(tmp_sdev, sdev->host) {
				if (tmp_sdev->id != sdev->id) {
					continue;
				}
				if (tmp_sdev->queue_depth > 1) {
					scsi_track_queue_full(tmp_sdev,
							      tmp_sdev->
							      queue_depth - 1);
				}
			}
			sc_cmd->result = (DID_OK << 16) | sp->cdb_status;
		} else {
			/*
			 * transport level I/O was ok but scsi
			 * has non zero status
			 */
			sc_cmd->result = (DID_OK << 16) | sp->cdb_status;
		}
		break;
	case OPENFC_ERROR:
		if (sp->io_status & (SUGGEST_RETRY << 24)) 
			sc_cmd->result = DID_IMM_RETRY << 16;
		else 
			sc_cmd->result = (DID_ERROR << 16) | sp->io_status;
		break;
	case OPENFC_DATA_UNDRUN:
		/*
		 * Transport underrun.
		 */
		CMD_RESID_LEN(sc_cmd) = sp->scsi_resid;
		sc_cmd->result = (DID_ERROR << 16) | sp->cdb_status;
		break;
	case OPENFC_DATA_OVRRUN:
		/*
		 * overrun is an error
		 */
		sc_cmd->result = (DID_ERROR << 16) | sp->cdb_status;
		break;
	case OPENFC_CMD_ABORTED:
		sc_cmd->result = (DID_ABORT << 16) | sp->io_status;
		break;
	case OPENFC_CMD_TIME_OUT:
		sc_cmd->result = (DID_TIME_OUT << 16) | sp->io_status;
		break;
	case OPENFC_CMD_RESET:
		sc_cmd->result = (DID_RESET << 16);
		break;
	case OPENFC_HRD_ERROR:
		sc_cmd->result = (DID_NO_CONNECT << 16);
		break;
	default:
		sc_cmd->result = (DID_ERROR << 16);
		break;
	}
	/*
	 * call the scsi layer for i/o completion
	 */
	if (sc_cmd->scsi_done) {
		(*sc_cmd->scsi_done) (sc_cmd);
	}
	/*
	 * call the openfc pkt free routine
	 */
	fc_sess_release(sp->rp->rp_sess);
	sp->state = OPENFC_SRB_FREE;
	openfc_free_scsi_pkt(sp);
}


/**
 * openfc_queuecommand: - The queuecommand function of the scsi template
 * @cmd:	struct scsi_cmnd to be executed
 * @done:	Callback function to be called when cmd is completed
 *
 * this is the i/o strategy routine, called by the scsi layer
 * this routine is called with holding the host_lock.
 */
static int openfc_queuecommand(struct scsi_cmnd *sc_cmd,
			       void (*done) (struct scsi_cmnd * sc_cmd))
{
	struct openfc_softc *openfcp;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_scsi_pkt *sp;
	struct os_tgt  *tgtp;
	struct os_lun  *disk;
	int		rval;
	int		rc = 0;
	struct fcoe_dev_stats *stats;

	ASSERT(done != NULL);

	openfcp = (struct openfc_softc *) sc_cmd->device->host->hostdata;

	if (openfcp->state != OPENFC_RUNNING) {
		if (openfcp->status & OPENFC_PAUSE) {
			rc = SCSI_MLQUEUE_HOST_BUSY;
			goto out;
		} else {
			sc_cmd->result = DID_NO_CONNECT << 16;
			done(sc_cmd);
			goto out;
		}
	} else {
		if (openfcp->dev->fd_link_status == TRANS_LINK_DOWN) {
			sc_cmd->result = DID_NO_CONNECT << 16;
			done(sc_cmd);
			goto out;
		}
	}
		
	if (atomic_read(&openfcp->fcs_status) != OPENFC_FCS_ONLINE) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}
	rval = fc_remote_port_chkready(rport);
	if (rval) {
		sc_cmd->result = rval;
		done(sc_cmd);
		goto out;
	}
	tgtp = rport->dd_data;
	if (!tgtp) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}
	if (tgtp->fcs_rport == NULL) {
		printk("fcs NULL\n");
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}

	if (!(tgtp->fcs_rport->rp_fcp_parm & FCP_SPPF_TARG_FCN)) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}
	disk = sc_cmd->device->hostdata;
	if (!disk) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}
	if (disk->state == OPENFC_LUN_OFFLINE) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		done(sc_cmd);
		goto out;
	}

	/* after finding target we wil release the lock */
	spin_unlock_irq(openfcp->host->host_lock);

	sp = openfc_alloc_scsi_pkt(openfcp);
	if (sp == NULL) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto busy_lock;
	}

	/*
	 * build the openfc request pkt
	 */
	sp->cmd = sc_cmd;	/* save the cmd */
	sp->openfcp = openfcp;	/* save the softc ptr */
	sp->rp = tgtp->fcs_rport;	/* set the remote port ptr */
	sp->d_id = tgtp->fcid;	  /* remote DID*/
	sp->disk = (void *) disk;
	sc_cmd->scsi_done = done;
	fc_sess_hold(sp->rp->rp_sess);

	/*
	 * set up the transfer length
	 */
	sp->data_len = sc_cmd->request_bufflen;
	sp->xfer_len = 0;

	/*
	 * setup the lun number and data direction
	 */
	sp->lun = sc_cmd->device->lun;
	sp->id = sc_cmd->device->id;
	stats = openfcp->dev->dev_stats[smp_processor_id()];
	if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		sp->req_flags = OPENFC_SRB_READ;
		stats->InputRequests++;
		stats->InputMegabytes = sp->data_len;
	} else if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
		sp->req_flags = OPENFC_SRB_WRITE;
		stats->OutputRequests++;
		stats->OutputMegabytes = sp->data_len;
	} else {
		sp->req_flags = 0;
		stats->ControlRequests++;
	}

	sp->done = openfc_io_compl;
	sp->cmd_len = sc_cmd->cmd_len;
	sp->tgt_flags = tgtp->flags;
	/*
	 * send it to the lower layer
	 * if we get -1 return then put the request in the pending
	 * queue.
	 */
	rval = openfcp->dev->port_ops.send_scsi(openfcp->dev, sp);
	if (rval != 0) {
		fc_sess_release(sp->rp->rp_sess);
		sp->state = OPENFC_SRB_FREE;
		openfc_free_scsi_pkt(sp);
		rc = SCSI_MLQUEUE_HOST_BUSY;
	}
busy_lock:
	spin_lock_irq(openfcp->host->host_lock);
out:
	return rc;
}

/**
 * openfc_eh_abort: Abort a command...from scsi host template
 * send ABTS to the target device  and wait for the response
 * sc_cmd is the pointer to the command to be aborted.
 */
static int openfc_eh_abort(struct scsi_cmnd *sc_cmd)
{
	struct openfc_cmdq *qp;
	struct fc_scsi_pkt *sp;
	struct openfc_softc *openfcp;
	int		rc = FAILED;
	openfcp = (struct openfc_softc *) sc_cmd->device->host->hostdata;

	if (openfcp->state != OPENFC_RUNNING) {
		return rc;
	} else if (openfcp->dev->fd_link_status == TRANS_LINK_DOWN) {
		return rc;
	}

	if ((openfcp->dev->capabilities & TRANS_C_QUEUE) == 0) {
		qp = (struct openfc_cmdq *) xchg(&(CMD_SP(sc_cmd)), NULL);
		if (qp) {
			spin_lock(&qp->scsi_pkt_lock);
			sp = (struct fc_scsi_pkt *) qp->ptr;

			if (!sp) {
				spin_unlock(&qp->scsi_pkt_lock);
				return rc;
			}
			/*
			 * if there is our timer pending then 
			 * delete that first
			 */
			if (timer_pending(&sp->timer)) {
				del_timer_sync(&sp->timer);
			}
			sp->state |= OPENFC_SRB_ABORT_PENDING;
			qp->ptr = NULL;

			rc = openfcp->dev->port_ops.abort_cmd(openfcp->dev, sp);
			sp->state = OPENFC_SRB_FREE;
			spin_unlock(&qp->scsi_pkt_lock);
			openfc_free_scsi_pkt(sp);
		}
	} else {
		/*
		 * this is for Palo 
		 */
		sp =  (struct fc_scsi_pkt *) xchg(&(CMD_SP(sc_cmd)), NULL);
		if (sp) {
			/*
			 * if there is our timer pending then 
			 * delete that first
			 */
			if (timer_pending(&sp->timer)) {
				del_timer_sync(&sp->timer);
			}

			sp->state |= OPENFC_SRB_ABORT_PENDING;
			rc = openfcp->dev->port_ops.abort_cmd
				(openfcp->dev, sp);
			if (rc == SUCCESS) {
#if defined(__VMKLNX__)
				sc_cmd->result = DID_ABORT << 16;
				(*sc_cmd->scsi_done)(sc_cmd);
#endif /* __VMKLNX__ */
				sp->cmd = NULL;
				sp->state = OPENFC_SRB_FREE;
				openfc_free_scsi_pkt(sp);
			} else {
				if (sp->state & OPENFC_SRB_ABORTED) {
#if defined(__VMKLNX__)
					sc_cmd->result = DID_ABORT << 16;
					(*sc_cmd->scsi_done)(sc_cmd);
#endif /* __VMKLNX__ */
					sp->cmd = NULL;
					sp->state = OPENFC_SRB_FREE;
					openfc_free_scsi_pkt(sp);
				} else {
					void * p;
					/* put back the fsp
					 * It will get cleaned up during higher
					 * error recovery levels like lun reset
					 * or host reset
					 */
					p = xchg(&(CMD_SP(sc_cmd)), sp);
				}
			}
		} else {
                       /* if no sp associated with cmd, then the command
			* must have completed to SCSI mid-layer. Thus, lower
			* layers and target have already forgotten about the 
			* command, so abort is successful
			*/
			rc = SUCCESS;
		}
	}
	return rc;
}

/**
 * openfc_eh_device_reset: Reset a single LUN...from scsi host
 * template send tm cmd to the target and wait for the
 * response
 */
static int openfc_eh_device_reset(struct scsi_cmnd *sc_cmd)
{
	struct openfc_softc *openfcp;
	struct fc_scsi_pkt *sp;
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	int		rc = FAILED;
	struct os_tgt  *tgtp;
	int		rval;

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		goto out;
	}
	tgtp = rport->dd_data;
	if (tgtp != NULL) {
		openfcp =
		    (struct openfc_softc *) sc_cmd->device->host->hostdata;


		if (openfcp->state != OPENFC_RUNNING) {
			return rc;
		}
		sp = openfc_alloc_scsi_pkt(openfcp);
		if (sp == NULL) {
			SA_LOG("could not allocate scsi_pkt");
			sc_cmd->result = DID_NO_CONNECT << 16;
			goto out;
		}

		/*
		 * build the openfc request pkt
		 */
		sp->cmd = sc_cmd;	/* save the cmd */
		sp->openfcp = openfcp;	/* save the softc ptr */
		sp->rp = tgtp->fcs_rport;	/* set the remote port ptr */


		sp->id = sc_cmd->device->id;
		sp->lun = sc_cmd->device->lun;
		sp->d_id = tgtp->fcid;	  /* remote DID*/

		/*
		 * flush outstanding commands
		 */
		rc = openfcp->dev->port_ops.target_reset(openfcp->dev, sp);
		sp->cmd = NULL;
		sp->state = OPENFC_SRB_FREE;
		openfc_free_scsi_pkt(sp);
	}

      out:
	return rc;
}

/*
 * The reset function will reset the Adapter.
 * not supported for FC0E , only supported for
 * Palo
 */

static int openfc_eh_host_reset(struct scsi_cmnd *sc_cmd)
{
	struct openfc_softc *openfcp;
	int rc;

	openfcp = (struct openfc_softc *) sc_cmd->device->host->hostdata;
	if ((rc = openfc_reset_if(openfcp)) == SUCCESS)
		ssleep(OPENFC_HOST_RESET_SETTLE_TIME);

	return rc;
}

int openfc_reset_if(struct openfc_softc *openfcp)
{
	struct fc_scsi_pkt *sp;
	int		rc = FAILED;
	int		rval;


	if (atomic_read(&openfcp->fcs_status) != OPENFC_FCS_ONLINE) {
		SA_LOG("FCS Offline can not reset"); 
		goto out1;
	}
	if (openfcp->state != OPENFC_RUNNING) {
		return rc;
	}
	openfcp->state = OPENFC_FCS_RESET;
	fcs_quiesce(openfcp->fcs_state);
	sp = openfc_alloc_scsi_pkt(openfcp);
	if (sp == NULL) {
		SA_LOG("could not allocate scsi_pkt");
		goto out1;
	}
	/*
	 * flush outstanding commands
	 */
	rval = openfcp->dev->port_ops.host_reset(openfcp->dev, sp);

	if (rval) {
		goto out;
	}
	fcs_start(openfcp->fcs_state);
	rc = SUCCESS;
	openfcp->state = OPENFC_RUNNING;
out:
	sp->state = OPENFC_SRB_FREE;
	openfc_free_scsi_pkt(sp);
out1:
	return rc;
}
/*
 * function to configure queue depth
 */
static int openfc_slave_configure(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct os_tgt  *tgtp;
	struct openfc_softc *openfcp;
	int		queue_depth;

	tgtp = rport->dd_data;
	openfcp = (struct openfc_softc *) tgtp->hba;

	if (openfcp->qdepth)
		queue_depth = openfcp->qdepth;
	else
		queue_depth = OPENFC_DFLT_QUEUE_DEPTH;

	if (sdev->tagged_supported) {
		scsi_activate_tcq(sdev, queue_depth);
	} else {
		scsi_deactivate_tcq(sdev, sdev->host->hostt->cmd_per_lun);
	}

	rport->dev_loss_tmo = OPENFC_DFLT_DEVLOSS_TMO;
	if (openfcp->dev->dev_loss_tmo)
		rport->dev_loss_tmo = openfcp->dev->dev_loss_tmo;

	return 0;
}

static int openfc_slave_alloc(struct scsi_device *sdev)
{
	struct os_tgt  *tgtp;
	struct os_lun  *disk;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport)) {
		return -ENXIO;
	}
	tgtp = rport->dd_data;
	disk = kzalloc(sizeof(struct os_lun), GFP_KERNEL);
	if (!disk) {
		SA_LOG("could not allocate lun structure");
		return -ENOMEM;
	}
	disk->lun_number = sdev->lun;
	disk->state = OPENFC_LUN_READY;
	sdev->hostdata = disk;
	disk->tgtp = tgtp;
	disk->sdev = sdev;
	return 0;
}

static void openfc_slave_destroy(struct scsi_device *sdev)
{
	struct os_tgt  *tgtp;
	struct os_lun  *disk;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (rport) {
		tgtp = rport->dd_data;
		if (!tgtp) {
			/*
			 * we should not be here
			 * Most likely RSCN cleaned up the 
			 * target structure. so return;
			 */
			sdev->hostdata = NULL;
			return;
		}
		disk = sdev->hostdata;
		if (!disk) {
			return;
		}
		sdev->hostdata = NULL;
		kfree(disk);
	}
}

static int openfc_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	return sdev->queue_depth;
}

static int openfc_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}

/*
 * FCS callback routine for port change notification
 */
static void openfc_remote_port_state_change(void *arg,
					    struct fc_remote_port *port)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) arg;
	struct fc_rport *rport;
	struct fc_rport_identifiers rport_ids;
	struct os_tgt  *tgt;

	/*
	 * this routine is called for any port state change
	 * if the rp_sess_ready is set and rp_client_priv is not NULL
	 * then we have the transport structure
	 * if the rp_sess_ready is set and rp_client_priv is NULL
	 * then we do not have the transport structure and we will create it
	 * if the rp_sess_ready is not set and rp_client_priv is not NULL
	 */
	if (port->rp_sess_ready) {
		if (openfcp->state == OPENFC_GOING_DOWN)
			return;
		/*
		 * Remote port has reappeared. Re-register w/ FC transport
		 */
		rport_ids.node_name = port->rp_node_wwn;
		rport_ids.port_name = port->rp_port_wwn;
		rport_ids.port_id = port->rp_fid;
		rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;
#if defined(__VMKLNX__) /* ESX doesn't handle role change */
		if (port->rp_fcp_parm & FCP_SPPF_INIT_FCN)
			rport_ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (port->rp_fcp_parm & FCP_SPPF_TARG_FCN)
			rport_ids.roles |= FC_RPORT_ROLE_FCP_TARGET;
#endif /* __VMKLNX__ */
		rport = fc_remote_port_add(openfcp->host, 0, &rport_ids);
		if (!rport) {
			SA_LOG("fc_remote_port_add failed");
			return;
		}
		if (!port->rp_client_priv)
			fc_sess_hold(port->rp_sess);

		/*
		 * added to the list of discovered target port
		 */
		tgt = rport->dd_data;
		tgt->rport = rport;
		tgt->fcid = port->rp_fid;
		tgt->hba = openfcp;
		tgt->fcs_rport = port;
		tgt->node_name = port->rp_node_wwn;
		tgt->port_name = port->rp_port_wwn;

		port->rp_client_priv = tgt;
		tgt->flags = OPENFC_TGT_REC_SUPPORTED;
		if (port->rp_fcp_parm & FCP_SPPF_RETRY) {
			tgt->flags |= OPENFC_TGT_RETRY_OK;
		}

		if (port->rp_fcp_parm & FCP_SPPF_INIT_FCN) {
			rport->dev_loss_tmo = 50;
			rport->supported_classes = FC_COS_CLASS3;
#if !defined(__VMKLNX__)
			rport_ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;
			fc_remote_port_rolechg(rport, rport_ids.roles);
#endif /* not __VMKLNX__ */
		}
		if (port->rp_fcp_parm & FCP_SPPF_TARG_FCN) {
			rport->dev_loss_tmo = 50;
			rport->supported_classes = FC_COS_CLASS3;
#if !defined(__VMKLNX__)
			rport_ids.roles |= FC_RPORT_ROLE_FCP_TARGET;
			fc_remote_port_rolechg(rport, rport_ids.roles);
#endif /* not __VMKLNX__ */
			if ((rport->scsi_target_id != -1) &&
			    (rport->scsi_target_id < OPENFC_MAX_FCP_TARGET)) {
				tgt->tid = rport->scsi_target_id;
			}
		}
		atomic_inc(&openfcp->discover_devs);
		if (openfcp->dev->port_ops.remote_port_state_change)
			openfcp->dev->port_ops.remote_port_state_change
				(openfcp->dev, port, 1);
	} else {
		if (port->rp_client_priv) {
			/*
			 * delete the port form port database
			 */
			spin_lock_irq(openfcp->host->host_lock);
			tgt = port->rp_client_priv;
			rport = tgt->rport;
			port->rp_client_priv = NULL;
			tgt->rport = NULL;
			tgt->fcid = 0;
			tgt->fcs_rport = NULL;
			spin_unlock_irq(openfcp->host->host_lock);
			fc_remote_port_delete(rport);
			atomic_dec(&openfcp->discover_devs);
			if (openfcp->dev->port_ops.remote_port_state_change)
				openfcp->dev->port_ops.remote_port_state_change
					(openfcp->dev, port, 0);
			fc_sess_release(port->rp_sess);
		}
	}
}

/*
 * after FCS completes discovery
 */
static void openfc_discovery_done(void *arg)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) arg;

	if (openfcp->state != OPENFC_FCS_INITIALIZATION) {
		/*
		 * this is the case for link down and link up
		 * in this case we do not need to do scsi_scan_bus again
		 */
		return;
	}

	openfcp->state = OPENFC_RUNNING;
	scsi_scan_host(openfcp->host);
}

/*
 * called by the lower layer receive function
 */
void openfc_rcv(struct fcdev *dev, struct fc_frame *fp)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	struct fc_port *portp = openfcp->fcs_port;
	if (openfcp->dev->fd_link_status == TRANS_LINK_UP)
		fc_port_ingress(portp, fp);
	else
		fc_frame_free(fp);
}

EXPORT_SYMBOL(openfc_rcv);
/*
 * openfc_find_hba
 */
struct openfc_softc *openfc_find_hba(int host_no)
{
	struct openfc_softc *openfcp;

	spin_lock(&openfc_hostlist_lock);
	list_for_each_entry(openfcp, &openfc_hostlist, list) {
		if (openfcp->host_no == host_no) {
			spin_unlock(&openfc_hostlist_lock);
			return (openfcp);
		}
	}
	spin_unlock(&openfc_hostlist_lock);
	return NULL;
}


/**
 * openfc_alloc_dev:  create device structure 
 * @fctt:	pointer to fc host template
 * @privsize:	extra byte to allocate for the driver
 *
 * Note:
 *	Allocate a new fcdev and perform basic initialization.
 *	The fcdev is not usuable  until openfc_register
 *	is called.
 *
 * Return value:
 *	Pointer to a new fcdev
 *
 **/
struct fcdev   *openfc_alloc_dev(struct openfc_port_operations *fctt,
				 int privsize)
{
	struct fcdev   *fc_dev;
	struct openfc_softc *openfcp;
	struct Scsi_Host *host;

	/* 
	 * Modify driver template parameters based on parameters passed
	 * by transport. Then call scsi_alloc_host with the modified
	 * template. Global mutex used to ensure that only one transport 
	 * can use the template at a time to allocate a SCSI host
	 */
	if(down_interruptible(&ofc_drv_tmpl_mutex))
		return NULL;

	if (fctt->sg_tablesize)
		openfc_driver_template.sg_tablesize = fctt->sg_tablesize;
	else 
		openfc_driver_template.sg_tablesize = OPENFC_DFLT_SG_TABLESIZE;

	host = scsi_host_alloc(&openfc_driver_template,
			       sizeof(struct openfc_softc) +
			       privsize);

	up(&ofc_drv_tmpl_mutex);

	if (host == NULL) {
		SA_LOG("openfc: Could not allocate host structure\n");
		return NULL;
	}
	openfcp = (struct openfc_softc *) host->hostdata;
	memset(openfcp, 0, sizeof(struct openfc_softc));
	openfcp->dev = &openfcp->fd;
	openfcp->host = host;
	fc_dev = openfcp->dev;
	fc_dev->luns_per_tgt = OPENFC_MAX_LUN_COUNT;

	/*
	 * setup fsp allocation flags 
	 */
	if (fctt->alloc_flags) {
		openfcp->alloc_flags = fctt->alloc_flags;
	}

	/*
	 * setup extended fsp size
	 */
	if (fctt->ext_fsp_size) {
		openfcp->ext_fsp_size = fctt->ext_fsp_size;
	}

	openfcp->host_no = (uint32_t) host->host_no;
	if (fctt->send) {
		openfcp->dev->port_ops.send = fctt->send;
	}
	if (fctt->send_scsi == NULL) {
		openfcp->dev->port_ops.send_scsi = openfc_scsi_send;
	} else {
		openfcp->dev->port_ops.send_scsi = fctt->send_scsi;
	}
	if (fctt->abort_cmd == NULL) {
		openfcp->dev->port_ops.abort_cmd = openfc_abort_cmd;
	} else {
		openfcp->dev->port_ops.abort_cmd = fctt->abort_cmd;
	}
	if (fctt->target_reset == NULL) {
		openfcp->dev->port_ops.target_reset = openfc_target_reset;
	} else {
		openfcp->dev->port_ops.target_reset = fctt->target_reset;
	}

	if (fctt->host_reset == NULL) {
		openfcp->dev->port_ops.host_reset = openfc_inf_reset;
	} else {
		openfcp->dev->port_ops.host_reset = fctt->host_reset;
	}
	if (fctt->cleanup_scsi == NULL)
		openfcp->dev->port_ops.cleanup_scsi = openfc_scsi_cleanup;
	else
		openfcp->dev->port_ops.cleanup_scsi = fctt->cleanup_scsi;

	openfcp->dev->port_ops.frame_alloc = fctt->frame_alloc;
	openfcp->dev->port_ops.get_stats = fctt->get_stats;
	openfcp->dev->port_ops.remote_port_state_change
		= fctt->remote_port_state_change;
	if (privsize)
		fc_dev->drv_priv = (void *)(openfcp + 1);

	/*
	 * create slab for pkt buffer
	 */
	if (openfc_create_scsi_slab(openfcp) < 0) {
		SA_LOG("scsi pkt slab create failed");
		scsi_host_put(host);
		return NULL;
	}
	return fc_dev;
}

EXPORT_SYMBOL(openfc_alloc_dev);

/**
 * openfc_register - register a fcdev
 * @dev:	     fcdev pointer to register
 *
 * Return value:
 *	Host number on success / < 0 for error
 *
 **/
int openfc_register(struct fcdev *dev)
{
	struct openfc_softc *openfcp;
	struct Scsi_Host *host;
	struct fc_port *port;
	struct class_device *cp;
	int		i;
	int		rc;
	struct fcs_create_args ofc_fcs_args = openfc_fcs_args;
 
	openfcp = openfc_get_softc(dev);
	host = openfcp->host;
	openfcp->host_no = (uint32_t) host->host_no;

	if (host->host_no >= max_host_no)
		max_host_no = host->host_no;

	host->max_lun = min(OPENFC_MAX_LUN_COUNT, dev->luns_per_tgt);
	host->max_id = OPENFC_MAX_FCP_TARGET;
	host->max_channel = 0;

	host->transportt = openfc_transport_template;

	/*
	 * call the scsi add host to add the hba structure
	 */
	rc = scsi_add_host(openfcp->host, dev->dev);
	if (rc) {
		SA_LOG("error on scsi_add_host\n");
		goto out_host_put;
	}

	/*
	 * we will use openfc's scsi i/f only when lower level driver
	 * does not have a fast path.
	 */
	openfcp->qdepth = OPENFC_DFLT_QUEUE_DEPTH;
	if ((openfcp->dev->capabilities & TRANS_C_QUEUE) == 0) {	
		for (i = 0; i < OPENFC_MAX_OUTSTANDING_COMMANDS; i++) {
			spin_lock_init(&openfcp->outstandingcmd[i].scsi_pkt_lock);
			openfcp->outstandingcmd[i].ptr = NULL;
		}

		spin_lock_init(&openfcp->outstandingcmd_lock);
	}
	/*
	 * FCS initialization starts here
	 * create fcs structures here
	 */
	openfcp->state = OPENFC_INITIALIZATION;
	openfcp->status = OPENFC_LINK_UP;
	port = fc_port_alloc();
	if (!port) {
		SA_LOG("Could not create fc_port structure");
		goto out_host_rem;
	}
	openfcp->fcs_port = port;
	if (dev->port_ops.frame_alloc) {
		fc_port_set_frame_alloc(port, dev->port_ops.frame_alloc);
	}
	fc_port_set_egress(port,
			   (int (*)(void *, struct fc_frame *)) dev->port_ops.
			   send, dev);
	ofc_fcs_args.fca_port = port;

	if (dev->min_xid) {
		ofc_fcs_args.fca_min_xid = dev->min_xid;
	} else {
		ofc_fcs_args.fca_min_xid = OPENFC_MIN_XID;
	}
	if (dev->max_xid) {
		ofc_fcs_args.fca_max_xid = dev->max_xid;
	} else {
		ofc_fcs_args.fca_max_xid = OPENFC_MAX_XID;
	}
	if (dev->port_ops.send_scsi == openfc_scsi_send)
		ofc_fcs_args.fca_service_params |= FCP_SPPF_CONF_COMPL;

	fc_port_set_max_frame_size(port, dev->framesize);
	ofc_fcs_args.fca_cb_arg = (void *) openfcp;
	openfcp->fcs_state = fcs_create(&ofc_fcs_args);
	if (openfcp->fcs_state == NULL) {
		SA_LOG("Could not create fcs_state structure");
		fc_port_close_ingress(port);
		goto out_host_rem;
	}

	/*
	 * init local port fc attr
	 */
	openfc_fc_attr_init(openfcp);

	openfcp->state = OPENFC_FCS_INITIALIZATION;

	spin_lock(&openfc_hostlist_lock);
	list_add_tail(&openfcp->list, &openfc_hostlist);
	spin_unlock(&openfc_hostlist_lock);

	fcs_local_port_set(openfcp->fcs_state, dev->fd_wwnn, dev->fd_wwpn);

	/*
	 * Initialize sysfs device for the instance.
	 * Create /sys/class/openfc/host<n> groups and attributes.
	 */
	cp = &openfcp->openfc_class_device;
	class_device_initialize(cp);
	cp->class = openfc_class;
	cp->class_data = openfcp;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	cp->groups = openfc_groups;
#endif
	snprintf(cp->class_id, sizeof(cp->class_id), "host%d", host->host_no);
	rc = class_device_add(cp);
	if (rc) {
		SA_LOG("class_device_add failed rc %d", rc);
		goto out_fcs;
	}
	if (dev->fd_link_status	== TRANS_LINK_DOWN) {
		fc_port_send_event(openfcp->fcs_port, FC_EV_DOWN);
		openfcp->status &= ~OPENFC_LINK_UP;
	}
	if (dev->options & TRANS_O_FCS_AUTO) {	
		atomic_set(&openfcp->fcs_status, OPENFC_FCS_ONLINE);
		fcs_start(openfcp->fcs_state);
	} else {
		atomic_set(&openfcp->fcs_status, OPENFC_FCS_OFFLINE);
	}

	if (!openfc_boot_time)
		openfc_boot_time = jiffies;
	openfcp->state = OPENFC_RUNNING;
	return openfcp->host_no;

out_fcs:
	fcs_destroy(openfcp->fcs_state);	/* also closes port */
out_host_rem:
	scsi_remove_host(openfcp->host);
out_host_put:
	scsi_host_put(openfcp->host);
	return -ENOMEM;
}

EXPORT_SYMBOL(openfc_register);

/**
 * openfc_unregister - register a fcdev
 * @dev:	     fcdev pointer to unregister
 *
 * Return value:
 *	None
 * Note: 
 * exit routine for openfc instance
 * clean-up all the allocated memory
 * and free up other system resources.
 *
 **/
void openfc_unregister(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	int		i;
	ulong		flags;
	struct fc_scsi_pkt *fsp;

	if (!list_empty(&openfcp->openfc_class_device.node))
		class_device_del(&openfcp->openfc_class_device);

	spin_lock_irqsave(openfcp->host->host_lock, flags);

	openfcp->state = OPENFC_GOING_DOWN;
	spin_unlock_irqrestore(openfcp->host->host_lock, flags);


	spin_lock(&openfc_hostlist_lock);
	list_del(&openfcp->list);
	spin_unlock(&openfc_hostlist_lock);

	/*
	 * do not need to hold any lock b'coz we will not start any new I/O.
	 * free all the pending scsi pkt and close all the sequence
	 */
	if ((openfcp->dev->capabilities & TRANS_C_QUEUE) == 0) {	
		for (i = 0; i < OPENFC_MAX_OUTSTANDING_COMMANDS; i++) {
			spin_lock(&openfcp->outstandingcmd[i].scsi_pkt_lock);
			fsp = openfcp->outstandingcmd[i].ptr;
			if (fsp) {
				openfc_scsi_abort_iocontext(fsp);
			}
			spin_unlock(&openfcp->outstandingcmd[i].scsi_pkt_lock);
		}
	}
	/*
	 * remove the fc transport and the host structure
	 * this will cleanup target/lun data structures
	 */
	fc_remove_host(openfcp->host);
	scsi_remove_host(openfcp->host);
	fcs_destroy(openfcp->fcs_state);
}

EXPORT_SYMBOL(openfc_unregister);

/**
 * openfc_put_dev -  dec the fcdev ref count
 * @dev:      Pointer to fcdev to dec.
 **/
void openfc_put_dev(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);

	openfc_destroy_scsi_slab(openfcp);
	scsi_host_put(openfcp->host);
}

EXPORT_SYMBOL(openfc_put_dev);

/**
 * openfc_linkup -  link up notification 
 * @dev:      Pointer to fcdev .
 **/
void openfc_linkup(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);

	if (!(openfcp->status & OPENFC_LINK_UP)) {
		fc_port_send_event(openfcp->fcs_port, FC_EV_READY);
		openfcp->status |= OPENFC_LINK_UP;
	}
}

EXPORT_SYMBOL(openfc_linkup);

/**
 * openfc_linkdown -  link down notification 
 * @dev:      Pointer to fcdev .
 **/
void openfc_linkdown(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);
	struct fc_scsi_pkt *fsp;

	if (openfcp->status & OPENFC_LINK_UP) {
		openfcp->status &= ~(OPENFC_LINK_UP);
		fc_port_send_event(openfcp->fcs_port, FC_EV_DOWN);
		fsp = openfc_alloc_scsi_pkt(openfcp);
		dev->port_ops.cleanup_scsi(fsp);
		fsp->state = OPENFC_SRB_FREE;
		openfc_free_scsi_pkt(fsp);
	}
}

EXPORT_SYMBOL(openfc_linkdown);

/**
 * openfc_pause -  pause driver instance
 * @dev:      Pointer to fcdev .
 **/
void openfc_pause(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);

	openfcp->status |= OPENFC_PAUSE;
}
EXPORT_SYMBOL(openfc_pause);

/**
 * openfc_unpause -  unpause driver instance
 * @dev:      Pointer to fcdev .
 **/
void openfc_unpause(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);

	openfcp->status &= ~(OPENFC_PAUSE);
}
EXPORT_SYMBOL(openfc_unpause);

/**
 * openfc_set_mfs -  per instance setting mfs
 * @dev:      Pointer to fcdev .
 **/
void openfc_set_mfs(struct fcdev *dev)
{
	struct openfc_softc *openfcp = openfc_get_softc(dev);

	fcs_set_mfs(openfcp->fcs_state, dev->framesize);
}
EXPORT_SYMBOL(openfc_set_mfs);

void openfc_fcs_start(struct openfc_softc *openfcp)
{
	if (atomic_read(&openfcp->fcs_status) == OPENFC_FCS_OFFLINE) {
		atomic_set(&openfcp->fcs_status, OPENFC_FCS_ONLINE);
		fcs_start(openfcp->fcs_state);
	}
}

void openfc_fcs_stop(struct openfc_softc *openfcp)
{
	struct fc_scsi_pkt *fsp;

	if (atomic_read(&openfcp->fcs_status) == OPENFC_FCS_ONLINE) {
		atomic_set(&openfcp->fcs_status, OPENFC_FCS_OFFLINE);
		fcs_stop(openfcp->fcs_state);
		fsp = openfc_alloc_scsi_pkt(openfcp);
		openfcp->dev->port_ops.cleanup_scsi(fsp);
		fsp->state = OPENFC_SRB_FREE;
		openfc_free_scsi_pkt(fsp);
	}
}

/*
 * initialize the fc_transport and fcs 
 */
#ifndef OPENFC_LIB
static int __init openfc_init(void)
#else
int openfc_init(void)
#endif /* OPENFC_LIB */
{
	fcs_module_init();

	openfc_transport_template =
	    fc_attach_transport(&openfc_transport_function);

	if (openfc_transport_template == NULL) {
		SA_LOG("fail to attach fc transport");
		return -1;
	}
	//openfc_transport_template->eh_timed_out = openfc_eh_timed_out;
	openfc_ioctl_init();

	return 0;
}

/*
 * destroy  fc_transport and fcs
 */
#ifndef OPENFC_LIB
static void __exit openfc_exit(void)
#else
void openfc_exit(void)
#endif /* OPENFC_LIB */
{
	fc_release_transport(openfc_transport_template);
	openfc_ioctl_exit();
	fcs_module_exit();
}

#ifndef OPENFC_LIB
module_init(openfc_init);
module_exit(openfc_exit);
#endif /* OPENFC_LIB */
