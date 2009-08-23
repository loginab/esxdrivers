/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/list.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <linux/delay.h>

void qla2x00_vp_stop_timer(scsi_qla_host_t *);

void
qla2x00_vp_stop_timer(scsi_qla_host_t *vha)
{
	if (vha->parent && vha->timer_active) {
		del_timer_sync(&vha->timer);
		vha->timer_active = 0;
	}
}

uint32_t
qla24xx_allocate_vp_id(scsi_qla_host_t *vha)
{
	uint32_t vp_id;
	scsi_qla_host_t *ha = vha->parent;
	unsigned long   flags;

	/* Find an empty slot and assign an vp_id */
	spin_lock_irqsave(&ha->vport_lock, flags);
	vp_id = find_first_zero_bit(ha->vp_idx_map, ha->max_npiv_vports + 1);
	if (vp_id > ha->max_npiv_vports) {
		DEBUG15(printk ("vp_id %d is bigger than max-supported %d.\n",
		    vp_id, ha->max_npiv_vports));
		spin_unlock_irqrestore(&ha->vport_lock, flags);
		return vp_id;
	}

	set_bit(vp_id, ha->vp_idx_map);
	ha->num_vhosts++;
	ha->cur_vport_count++;
	vha->vp_idx = vp_id;
	list_add_tail(&vha->vp_list, &ha->vp_list);
	spin_unlock_irqrestore(&ha->vport_lock, flags);
	return vp_id;
}

/*
 * qla2xxx_deallocate_vp_id(): Move vport to vp_del_list.
 * @vha: Vport context.
 *
 * Note: The caller of this function needs to ensure that
 * the vport_lock of the corresponding physical port is
 * held while making this call.
 */
void
qla24xx_deallocate_vp_id(scsi_qla_host_t *vha)
{
	uint16_t vp_id;
	scsi_qla_host_t *ha = vha->parent;
  
	vp_id = vha->vp_idx;
	ha->num_vhosts--;
	ha->cur_vport_count--;
	clear_bit(vp_id, ha->vp_idx_map);
	list_del(&vha->vp_list);
	/* Add vport to physical port's del list. */
	list_add_tail(&vha->vp_list, &ha->vp_del_list);
}

scsi_qla_host_t *
qla24xx_find_vhost_by_name(scsi_qla_host_t *ha, uint8_t *port_name)
{
	scsi_qla_host_t *vha;
	unsigned long	flags;

	/* Locate matching device in database. */
 	spin_lock_irqsave(&ha->vport_lock, flags);
	list_for_each_entry(vha, &ha->vp_list, vp_list) {
		if (!memcmp(port_name, vha->port_name, WWN_SIZE))
			goto found_vha;	
	}
	vha = NULL;
found_vha:
 	spin_unlock_irqrestore(&ha->vport_lock, flags);
	return vha;
}

/*
 * qla2x00_mark_vp_devices_dead
 *	Updates fcport state when device goes offline.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
static void
qla2x00_mark_vp_devices_dead(scsi_qla_host_t *vha)
{
	fc_port_t *fcport;
	struct list_head *fcpl, *fcptemp;

	list_for_each_safe(fcpl, fcptemp, &vha->fcports) {
		fcport = list_entry(fcpl, fc_port_t, list);

		DEBUG15(printk("scsi(%ld): Marking port dead, "
		    "loop_id=0x%04x :%x\n",
		    vha->host_no, fcport->loop_id, fcport->vp_idx));

		qla2x00_mark_device_lost(vha, fcport, 0, 0);
		atomic_set(&fcport->state, FCS_UNCONFIGURED);
	}
}

/**************************************************************************
* qla2x00_eh_wait_for_vp_pending_commands
*
* Description:
*    Waits for all the commands to come back from the specified host.
*
* Input:
*    ha - pointer to scsi_qla_host structure.
*
* Returns:
*    0 : SUCCESS
*    1 : FAILED
*
* Note:
**************************************************************************/
static int
qla2x00_eh_wait_for_vp_pending_commands(scsi_qla_host_t *vha)
{
	int cnt, ret = 0;
	srb_t *sp;
	struct scsi_cmnd *cmd;
	unsigned long flags;
	scsi_qla_host_t *pha = to_qla_parent(vha);
	/*
	 * Waiting for all commands for the designated target in the active
	 * array.
	 */
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		spin_lock_irqsave(&pha->hardware_lock, flags);
		sp = pha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock_irqrestore(&pha->hardware_lock, flags);
			if (vha->vp_idx == sp->fcport->ha->vp_idx) {
				if (!qla2x00_eh_wait_on_command(vha, cmd)) {
					ret = 1;
					break;
				}
			}
		} else {
			spin_unlock_irqrestore(&pha->hardware_lock, flags);
		}
	}
	return (ret);
}
/*
 * qla2400_disable_vp
 *	Disable Vport and logout all fcports.
 *
 * Input:
 *	ha = adapter block pointer.
 *
* Returns:
*    0 : SUCCESS
*    -1 : FAILED
 *
 * Context:
 */
int
qla24xx_disable_vp(scsi_qla_host_t *vha)
{
	int ret = 0;
	struct list_head *hal, *tmp_ha;
	scsi_qla_host_t *search_ha = NULL;

	/* 
	 * Logout all targets. After the logout, no further IO
	 * requests are entertained.
	 */
	ret = qla24xx_control_vp(vha, VCE_COMMAND_DISABLE_VPS_LOGO_ALL);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);

	/* Complete all outstanding commands on vha. */
	ret = qla2x00_eh_wait_for_vp_pending_commands(vha);

	/* 
	 * Remove vha from hostlist. No further IOCTLs can be 
	 * queued on the corresponding vha. 
	 */
	if (test_bit(VP_DELETE_ACTIVE, &vha->dpc_flags)) {
		mutex_lock(&instance_lock);
		list_for_each_safe(hal, tmp_ha, &qla_hostlist) {
			search_ha = list_entry(hal, scsi_qla_host_t, list);

			if (search_ha->instance == vha->instance) {
				list_del(hal);
				break;
			}
		}

		clear_bit(vha->instance, host_instance_map);
		num_hosts--;
		mutex_unlock(&instance_lock);
	}

	/* Indicate to FC transport & that rports are gone. */
	qla2x00_mark_vp_devices_dead(vha);
	atomic_set(&vha->vp_state, VP_FAILED);
	vha->flags.management_server_logged_in = 0;
	if (ret == QLA_SUCCESS) {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_DISABLED);
	} else {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		return -1;
	}
	return 0;
}

int
qla24xx_enable_vp(scsi_qla_host_t *vha)
{
	int ret;
	scsi_qla_host_t *ha = vha->parent;

	/* Check if physical ha port is Up. */
	if (atomic_read(&ha->loop_state) == LOOP_DOWN  ||
		atomic_read(&ha->loop_state) == LOOP_DEAD ) {
		vha->vp_err_state =  VP_ERR_PORTDWN;
		fc_vport_set_state(vha->fc_vport, FC_VPORT_LINKDOWN);
		goto enable_failed;
	}

	/* Initialize the new vport unless it is a persistent port. */
	ret = qla24xx_modify_vp_config(vha);

	if (ret != QLA_SUCCESS) {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		goto enable_failed;
	}

	DEBUG15(qla_printk(KERN_INFO, ha,
	    "Virtual port with id: %d - Enabled\n", vha->vp_idx));
	return 0;

enable_failed:
	DEBUG15(qla_printk(KERN_INFO, ha,
	    "Virtual port with id: %d - Disabled\n", vha->vp_idx));
	return 1;
}

static void
qla24xx_configure_vp(scsi_qla_host_t *vha)
{
	struct fc_vport *fc_vport;
	int ret;

	fc_vport = vha->fc_vport;

	DEBUG15(printk("scsi(%ld): %s: change request #3 for this host.\n",
	    vha->host_no, __func__));
	ret = qla2x00_send_change_request(vha, 0x3, vha->vp_idx);
	if (ret != QLA_SUCCESS) {
		DEBUG15(qla_printk(KERN_ERR, vha, "Failed to enable receiving"
		    " of RSCN requests: 0x%x\n", ret));
		return;
	} else {
		/* Corresponds to SCR enabled */
		clear_bit(VP_SCR_NEEDED, &vha->vp_flags);
	}

	vha->flags.online = 1;
	if (qla24xx_configure_vhba(vha))
		return;

	atomic_set(&vha->vp_state, VP_ACTIVE);
	fc_vport_set_state(fc_vport, FC_VPORT_ACTIVE);
}

void
qla2x00_alert_all_vps(scsi_qla_host_t *ha, uint16_t *mb)
{
	scsi_qla_host_t *vha, *tvha, *localvha = NULL;
	unsigned long   flags;

	if (ha->parent)
		return;

	spin_lock_irqsave(&ha->vport_lock, flags);
	list_for_each_entry_safe(vha, tvha, &ha->vp_list, vp_list) {

		/* Get vport reference. */
		qla2xxx_vha_get(vha);
		/* Skip if vport delete is in progress. */
		if (test_bit(VP_DELETE_ACTIVE, &vha->dpc_flags)) {
			/* Drop vport reference */
			qla2xxx_vha_put(vha);
			continue;
		}

		/* Get temporary vport reference. */
		qla2xxx_vha_get(tvha);
		spin_unlock_irqrestore(&ha->vport_lock, flags);

		switch (mb[0]) {
		case MBA_LIP_OCCURRED:
		case MBA_LOOP_UP:
		case MBA_LOOP_DOWN:
		case MBA_LIP_RESET:
		case MBA_POINT_TO_POINT:
		case MBA_CHG_IN_CONNECTION:
		case MBA_PORT_UPDATE:
		case MBA_RSCN_UPDATE:
			DEBUG15(printk("scsi(%ld)%s: Async_event for"
			    " VP[%d], mb = 0x%x, vha=%p\n",
			    vha->host_no, __func__,vha->vp_idx, *mb, vha));
			qla2x00_async_event(vha, mb);
			break;
		}

		spin_lock_irqsave(&ha->vport_lock, flags);
		/* Drop vport reference */
		qla2xxx_vha_put(vha);

		localvha = tvha;
		/* Skip current entry as it is in the process of deletion. */
		if (test_bit(VP_DELETE_ACTIVE, &tvha->dpc_flags))
			tvha = list_entry(tvha->vp_list.next, typeof(*tvha), vp_list); 
		/* Drop temporary vport reference */
		qla2xxx_vha_put(localvha);
	}
	spin_unlock_irqrestore(&ha->vport_lock, flags);
}

int
qla2x00_vp_abort_isp(scsi_qla_host_t *vha)
{

	if(!vha->parent)
		return 1;
	/*
	 * Physical port will do most of the abort and recovery work. We can
	 * just treat it as a loop down
	 */
	if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		qla2x00_mark_all_devices_lost(vha, 0);
	} else {
		if (!atomic_read(&vha->loop_down_timer))
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
	}

	/* To exclusively reset vport, we need to log it out first.*/
	if (!test_bit(ABORT_ISP_ACTIVE, &vha->parent->dpc_flags))
		qla24xx_control_vp(vha, VCE_COMMAND_DISABLE_VPS_LOGO_ALL);

	/* Host Statistics. */
	vha->qla_stats.total_isp_aborts++;
	DEBUG15(printk("scsi(%ld): Scheduling enable of Vport %d...\n",
	    vha->host_no, vha->vp_idx));
	return qla24xx_enable_vp(vha);
}

int
qla2x00_do_dpc_vp(scsi_qla_host_t *vha)
{
	scsi_qla_host_t *ha = vha->parent;

	/*
	 * Don't process anything on this vp if a deletion
	 * is in progress
	 */
	if (test_bit(VP_DELETE_ACTIVE, &vha->dpc_flags))
		return 0;

	if (test_and_clear_bit(VP_IDX_ACQUIRED, &vha->vp_flags)) {
		/* VP acquired. complete port configuration */
		if (atomic_read(&ha->loop_state) == LOOP_READY) {
			qla24xx_configure_vp(vha);
		} else {
			set_bit(VP_IDX_ACQUIRED, &vha->vp_flags);
			set_bit(VP_DPC_NEEDED, &ha->dpc_flags);
		}
		return 0;
	}

	if (test_and_clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags) &&
	    (!(test_and_set_bit(RESET_ACTIVE, &vha->dpc_flags)))) {
		clear_bit(RESET_ACTIVE, &vha->dpc_flags);
	}

	if (test_and_clear_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags))
		qla2x00_update_fcports(vha);

	if (atomic_read(&vha->vp_state) == VP_ACTIVE &&
	    test_and_clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)) {
		if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags))) {
			qla2x00_loop_resync(vha);
			clear_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags);
		}
	}

	return 0;
}

void
qla2x00_do_dpc_all_vps(scsi_qla_host_t *ha)
{
	scsi_qla_host_t *vha, *tvha, *localvha = NULL;
	unsigned long   flags;

	if (ha->parent)
		return;
	if (list_empty(&ha->vp_list))
		return;

	clear_bit(VP_DPC_NEEDED, &ha->dpc_flags);

	spin_lock_irqsave(&ha->vport_lock, flags);
	list_for_each_entry_safe(vha, tvha, &ha->vp_list, vp_list) { 

		/* Get vport reference. */
		qla2xxx_vha_get(vha);
		/* Skip if vport delete is in progress. */
		if (test_bit(VP_DELETE_ACTIVE, &vha->dpc_flags)) {
			/* Drop vport reference */
			qla2xxx_vha_put(vha);
			continue;
		}

		/* Get temp vport reference. */
		qla2xxx_vha_get(tvha);
		spin_unlock_irqrestore(&ha->vport_lock, flags);

		qla2x00_do_dpc_vp(vha);

		spin_lock_irqsave(&ha->vport_lock, flags);
		/* Drop vport reference */
		qla2xxx_vha_put(vha);

		localvha = tvha;
		/* Skip current vport entry as it is in the process of deletion. */
		if (test_bit(VP_DELETE_ACTIVE, &tvha->dpc_flags))
			tvha = list_entry(tvha->vp_list.next, typeof(*tvha), vp_list); 
		/* Drop temp vport reference */
		qla2xxx_vha_put(localvha);
	}
	spin_unlock_irqrestore(&ha->vport_lock, flags);
}

int
qla24xx_vport_create_req_sanity_check(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) fc_vport->shost->hostdata;
	scsi_qla_host_t *vha;
	uint8_t port_name[WWN_SIZE];

	if (fc_vport->roles != FC_PORT_ROLE_FCP_INITIATOR)
		return VPCERR_UNSUPPORTED;

	/* Check up the F/W and H/W support NPIV */
	if (!ha->flags.npiv_supported)
		return VPCERR_UNSUPPORTED;

	/* Check up whether npiv supported switch presented */
	if (!(ha->switch_cap & FLOGI_MID_SUPPORT))
		return VPCERR_NO_FABRIC_SUPP;

	/* Check up unique WWPN */
	u64_to_wwn(fc_vport->port_name, port_name);
	if (!memcmp(port_name, ha->port_name, WWN_SIZE))
		return VPCERR_BAD_WWN;
	vha = qla24xx_find_vhost_by_name(ha, port_name);
	if (vha)
		return VPCERR_BAD_WWN;

	/* Check up max-npiv-supports */
	if (ha->num_vhosts > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): num_vhosts %ud is bigger than "
		    "max_npv_vports %ud.\n", ha->host_no,
		    ha->num_vhosts, ha->max_npiv_vports));
		return VPCERR_UNSUPPORTED;
	}
	return 0;
}

scsi_qla_host_t *
qla24xx_create_vhost(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) fc_vport->shost->hostdata;
	scsi_qla_host_t *vha;
	struct Scsi_Host *host;

	host = scsi_host_alloc(&qla24xx_driver_template,
	    sizeof(scsi_qla_host_t));
	if (!host) {
		printk(KERN_WARNING
		    "qla2xxx: scsi_host_alloc() failed for vport\n");
		return(NULL);
	}

	vha = (scsi_qla_host_t *)host->hostdata;

	/* clone the parent hba */
	memcpy(vha, ha, sizeof (scsi_qla_host_t));

	fc_vport->dd_data = vha;
	vha->node_name = NULL;
	vha->port_name = NULL;

	vha->node_name = kmalloc(WWN_SIZE * sizeof(char), GFP_KERNEL);
	if (!vha->node_name)
		goto create_vhost_failed_1;

	vha->port_name = kmalloc(WWN_SIZE * sizeof(char), GFP_KERNEL);
	if (!vha->port_name)
		goto create_vhost_failed_2;

	/* New host info */
	u64_to_wwn(fc_vport->node_name, vha->node_name);
	u64_to_wwn(fc_vport->port_name, vha->port_name);

	vha->host = host;
	vha->host_no = host->host_no;
	vha->parent = ha;
	vha->fc_vport = fc_vport;
	vha->device_flags = 0;

	INIT_LIST_HEAD(&vha->list);
	INIT_LIST_HEAD(&vha->fcports);
	INIT_LIST_HEAD(&vha->vp_list);
	INIT_LIST_HEAD(&vha->vp_del_list);

	vha->vp_idx = qla24xx_allocate_vp_id(vha);
	if (vha->vp_idx > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): Couldn't allocate vp_id.\n",
			vha->host_no));
		goto create_vhost_failed_3;
	}
	vha->mgmt_svr_loop_id = 10 + vha->vp_idx;

	kref_init(&vha->kref);
	init_completion(&vha->mbx_cmd_comp);
	complete(&vha->mbx_cmd_comp);
	init_completion(&vha->mbx_intr_comp);

	vha->dpc_flags = 0L;
	set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);
	set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);

	/* Need to allocate memory for ioctls  */
	vha->ioctl = NULL;
        vha->ioctl_mem = NULL;
        vha->ioctl_mem_size = 0;
	if (qla2x00_alloc_ioctl_mem(vha)) {
	       	DEBUG15(printk("scsi(%ld): Couldn't allocate vp ioctl memory.\n",
                   vha->host_no));
		goto create_vhost_failed_3;
	}

	/*
	 * To fix the issue of processing a parent's RSCN for the vport before
	 * its SCR is complete.
	 */
	set_bit(VP_SCR_NEEDED, &vha->vp_flags);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);

	qla2x00_start_timer(vha, qla2x00_timer, WATCH_INTERVAL);

	host->can_queue = vha->request_q_length + 128;
	host->this_id = 255;
	host->cmd_per_lun = 3;
	host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = MAX_BUSES - 1;
	host->max_lun = MAX_LUNS;
	host->max_id = MAX_TARGETS;
	host->transportt = qla2xxx_transport_vport_template;

	/* 
	 * Insert new entry into the list of adapters.
	 * IOCTLs can start coming down after this operation.
	 */
	mutex_lock(&instance_lock);
	list_add_tail(&vha->list, &qla_hostlist);
	vha->instance = find_first_zero_bit(host_instance_map, MAX_HBAS);
	if (vha->instance == MAX_HBAS) {
		DEBUG9_10(printk("Host instance exhausted\n"));
	}
	set_bit(vha->instance, host_instance_map);
	num_hosts++;
	mutex_unlock(&instance_lock);
	host->unique_id = vha->instance;

	DEBUG15(printk("DEBUG: detect vport hba %ld at address = %p\n",
	    vha->host_no, vha));

	vha->flags.init_done = 1;

	return vha;

create_vhost_failed_3:
	kfree(vha->port_name);

create_vhost_failed_2:
	kfree(vha->node_name);

create_vhost_failed_1:
	return NULL;
}

static void
__qla2xxx_vha_release(struct kref *kref)
{
	struct scsi_qla_host *vha =
	    container_of(kref, struct scsi_qla_host, kref);

	if (!vha->parent)
		return;

	qla24xx_deallocate_vp_id(vha);
	set_bit(VPORT_CLEANUP_NEEDED, &vha->parent->dpc_flags);
	qla2xxx_wake_dpc(vha->parent);
}

/*
 * qla2xxx_vha_put(): Drop a reference from the vport.
 * If we are the last caller, free vha.
 * @vha: Vport context.
 *
 * Note: The caller of this function needs to ensure that
 * the vport_lock of the corresponding physical port is
 * held while making this call.
 */
void
qla2xxx_vha_put(struct scsi_qla_host *vha)
{
	if(vha->parent)
		kref_put(&vha->kref, __qla2xxx_vha_release);
}

void
qla2xxx_vha_get(struct scsi_qla_host *vha)
{
	if(vha->parent)
		kref_get(&vha->kref);
}

/*
 * qla24xx_getinfo_vport() -  Query information about virtual fabric port
 * @ha: HA context
 * @vp_info: pointer to buffer of information about virtual port.
 * instance : instance of virtual port from 0 to MAX_MULTI_ID_NPORTS.
 * For Sansurfer Use only.
 *             
 * Returns error code.
 */
uint32_t
qla24xx_getinfo_vport(scsi_qla_host_t *ha, fc_vport_info_t *vp_info , uint32_t  instance        )
{
       scsi_qla_host_t *vha;
       int     i = 0;

	   if (instance >= ha->max_npiv_vports) {
               DEBUG(printk(KERN_INFO "instance number out of range...\n"));
               return VP_RET_CODE_FATAL;
       }

       memset(vp_info, 0, sizeof (fc_vport_info_t));
       /* Always return these numbers */
	   vp_info->free_vpids = ha->max_npiv_vports - ha->num_vhosts - 1;
       vp_info->used_vpids =  ha->num_vhosts; 
       list_for_each_entry(vha, &ha->vp_list, vp_list) {
               if (i==instance) {
                               vp_info->vp_id = vha->vp_idx; 
                               vp_info->vp_state = atomic_read(&vha->vp_state);
                               memcpy(vp_info->node_name , vha->node_name, WWN_SIZE);
                               memcpy(vp_info->port_name, vha->port_name, WWN_SIZE);
                               return VP_RET_CODE_OK;
               }
               i++;
       }
       DEBUG(printk(KERN_INFO "instance number not found..\n"));
       return VP_RET_CODE_FATAL;
}
