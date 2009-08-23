/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_version.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

/* link auto negotiation normally takes roughly 2s.   */
/* If we don't have link in 3 times that period quit. */
#define	QLA4XXX_LINK_UP_DELAY	6

/*
 * QLogic ISP4xxx Hardware Support Function Prototypes.
 */

static void ql4xxx_set_mac_number(struct scsi_qla_host *ha)
{
	uint32_t value;
	uint8_t func_number;
	unsigned long flags;

	/* Get the function number */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	value = readw(&ha->reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	func_number = (uint8_t) ((value >> 4) & 0x30);
	switch (value & ISP_CONTROL_FN_MASK) {
	case ISP_CONTROL_FN0_SCSI:
		ha->mac_index = 1;
		break;
	case ISP_CONTROL_FN1_SCSI:
		ha->mac_index = 3;
		break;
	default:
		DEBUG2(printk("scsi%ld: %s: Invalid function number, "
			      "ispControlStatus = 0x%x\n", ha->host_no,
			      __func__, value));
		break;
	}
	DEBUG2(printk("scsi%ld: %s: mac_index %d.\n", ha->host_no, __func__,
		      ha->mac_index));
}

/**
 * qla4xxx_free_ddb - deallocate ddb	
 * @ha: pointer to host adapter structure.
 * @ddb_entry: pointer to device database entry
 *
 * This routine deallocates and unlinks the specified ddb_entry from the
 * adapter's
 **/
void qla4xxx_free_ddb(struct scsi_qla_host *ha, struct ddb_entry *ddb_entry)
{
	/* Remove device entry from list */
	list_del_init(&ddb_entry->list);

	/* Remove device pointer from index mapping arrays */
#ifndef __VMWARE__
	ha->fw_ddb_index_map[ddb_entry->fw_ddb_index] = NULL;
	ha->tot_ddbs--;
#else
	/* We already removed the session in the dpc. */
	if ((atomic_read(&ddb_entry->state) != DDB_STATE_REMOVED) &&
		(ddb_entry->fw_ddb_index != INVALID_ENTRY) &&
		(ddb_entry == ha->fw_ddb_index_map[ddb_entry->fw_ddb_index])) {

		ha->fw_ddb_index_map[ddb_entry->fw_ddb_index] = NULL;
		ha->tot_ddbs--;

	}
#endif

	/* Free memory and scsi-ml struct for device entry */
	qla4xxx_destroy_sess(ddb_entry);
}

/**
 * qla4xxx_free_ddb_list - deallocate all ddbs
 * @ha: pointer to host adapter structure.
 *
 * This routine deallocates and removes all devices on the sppecified adapter.
 **/
void qla4xxx_free_ddb_list(struct scsi_qla_host *ha)
{
	struct list_head *ptr;
	struct ddb_entry *ddb_entry;

	while (!list_empty(&ha->ddb_list)) {
		ptr = ha->ddb_list.next;
		/* Free memory for device entry and remove */
		ddb_entry = list_entry(ptr, struct ddb_entry, list);
		qla4xxx_free_ddb(ha, ddb_entry);
	}
}

/**
 * qla4xxx_init_rings - initialize hw queues
 * @ha: pointer to host adapter structure.
 *
 * This routine initializes the internal queues for the specified adapter.
 * The QLA4010 requires us to restart the queues at index 0.
 * The QLA4000 doesn't care, so just default to QLA4010's requirement.
 **/
int qla4xxx_init_rings(struct scsi_qla_host *ha)
{
	uint16_t i;
	unsigned long flags = 0;

	/* Initialize request queue. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->request_out = 0;
	ha->request_in = 0;
	ha->request_ptr = &ha->request_ring[ha->request_in];
	ha->req_q_count = REQUEST_QUEUE_DEPTH;

	/* Initialize response queue. */
	ha->response_in = 0;
	ha->response_out = 0;
	ha->response_ptr = &ha->response_ring[ha->response_out];

	/*
	 * Initialize DMA Shadow registers.  The firmware is really supposed to
	 * take care of this, but on some uniprocessor systems, the shadow
	 * registers aren't cleared-- causing the interrupt_handler to think
	 * there are responses to be processed when there aren't.
	 */
	ha->shadow_regs->req_q_out = __constant_cpu_to_le32(0);
	ha->shadow_regs->rsp_q_in = __constant_cpu_to_le32(0);
	wmb();

	writel(0, &ha->reg->req_q_in);
	writel(0, &ha->reg->rsp_q_out);
	readl(&ha->reg->rsp_q_out);

	/* Initialize active array */
	for (i = 0; i < MAX_SRBS; i++)
		ha->active_srb_array[i] = NULL;

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;
}

/**
 * qla4xxx_validate_mac_address - validate adapter MAC address(es)
 * @ha: pointer to host adapter structure.
 *
 **/
static int qla4xxx_validate_mac_address(struct scsi_qla_host *ha)
{
	struct flash_sys_info *sys_info;
	dma_addr_t sys_info_dma;
	int status = QLA_ERROR;

	sys_info = dma_alloc_coherent(&ha->pdev->dev, sizeof(*sys_info),
				      &sys_info_dma, GFP_KERNEL);
	if (sys_info == NULL) {
		DEBUG2(printk("scsi%ld: %s: Unable to allocate dma buffer.\n",
			      ha->host_no, __func__));

		goto exit_validate_mac_no_free;
	}
	memset(sys_info, 0, sizeof(*sys_info));

	/* Get flash sys info */
	if (qla4xxx_get_flash(ha, sys_info_dma, FLASH_OFFSET_SYS_INFO,
			      sizeof(*sys_info)) != QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: get_flash FLASH_OFFSET_SYS_INFO "
			      "failed\n", ha->host_no, __func__));

		goto exit_validate_mac;
	}

	/* Save M.A.C. address & serial_number */
	memcpy(ha->my_mac, &sys_info->physAddr[0].address[0],
	       min(sizeof(ha->my_mac),
		   sizeof(sys_info->physAddr[0].address)));
	memcpy(ha->serial_number, &sys_info->acSerialNumber,
	       min(sizeof(ha->serial_number),
		   sizeof(sys_info->acSerialNumber)));

	status = QLA_SUCCESS;

 exit_validate_mac:
	dma_free_coherent(&ha->pdev->dev, sizeof(*sys_info), sys_info,
			  sys_info_dma);

 exit_validate_mac_no_free:
	return status;
}

/**
 * qla4xxx_init_local_data - initialize adapter specific local data
 * @ha: pointer to host adapter structure.
 *
 **/
static int qla4xxx_init_local_data(struct scsi_qla_host *ha)
{
	int i;

	/* Initialize passthru PDU list */
	for (i = 0; i < (MAX_PDU_ENTRIES - 1); i++)
		ha->pdu_queue[i].Next = &ha->pdu_queue[i + 1];
	ha->free_pdu_top = &ha->pdu_queue[0];
	ha->free_pdu_bottom = &ha->pdu_queue[MAX_PDU_ENTRIES - 1];
	ha->free_pdu_bottom->Next = NULL;
	ha->pdu_active = 0;

	/* Initilize aen queue */
	ha->aen_q_count = MAX_AEN_ENTRIES;

	return qla4xxx_get_firmware_status(ha);
}

static int qla4xxx_fw_ready(struct scsi_qla_host *ha)
{
	uint32_t timeout_count;
	int ready = 0;

	DEBUG2(dev_info(&ha->pdev->dev, "Waiting for Firmware Ready..\n"));
	for (timeout_count = ADAPTER_INIT_TOV; timeout_count > 0;
	     timeout_count--) {
		if (test_and_clear_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags))
			qla4xxx_get_dhcp_ip_address(ha);

		/* Get firmware state. */
		if (qla4xxx_get_firmware_state(ha) != QLA_SUCCESS) {
			DEBUG2(printk("scsi%ld: %s: unable to get firmware "
				      "state\n", ha->host_no, __func__));
			break;

		}

		if (ha->firmware_state & FW_STATE_ERROR) {
			DEBUG2(printk("scsi%ld: %s: an unrecoverable error has"
				      " occurred\n", ha->host_no, __func__));
			break;

		}
		if (ha->firmware_state & FW_STATE_CONFIG_WAIT) {
			/*
			 * The firmware has not yet been issued an Initialize
			 * Firmware command, so issue it now.
			 */
			if (qla4xxx_initialize_fw_cb(ha) == QLA_ERROR)
				break;

			/* Go back and test for ready state - no wait. */
			continue;
		}

		if (ha->firmware_state == FW_STATE_READY) {
			DEBUG2(dev_info(&ha->pdev->dev, "Firmware Ready..\n"));
			/* The firmware is ready to process SCSI commands. */
			DEBUG2(dev_info(&ha->pdev->dev,
					  "scsi%ld: %s: MEDIA TYPE - %s\n",
					  ha->host_no,
					  __func__, (ha->addl_fw_state &
						     FW_ADDSTATE_OPTICAL_MEDIA)
					  != 0 ? "OPTICAL" : "COPPER"));
			DEBUG2(dev_info(&ha->pdev->dev,
					  "scsi%ld: %s: DHCP STATE Enabled "
					  "%s\n",
					  ha->host_no, __func__,
					  (ha->addl_fw_state &
					   FW_ADDSTATE_DHCP_ENABLED) != 0 ?
					  "YES" : "NO"));
			DEBUG2(dev_info(&ha->pdev->dev,
					  "scsi%ld: %s: LINK %s\n",
					  ha->host_no, __func__,
					  (ha->addl_fw_state &
					   FW_ADDSTATE_LINK_UP) != 0 ?
					  "UP" : "DOWN"));
			DEBUG2(dev_info(&ha->pdev->dev,
					  "scsi%ld: %s: iSNS Service "
					  "Started %s\n",
					  ha->host_no, __func__,
					  (ha->addl_fw_state &
					   FW_ADDSTATE_ISNS_SVC_ENABLED) != 0 ?
					  "YES" : "NO"));

			ready = 1;
			break;
		}
		DEBUG2(printk("scsi%ld: %s: waiting on fw, state=%x:%x - "
			      "seconds expired= %d\n", ha->host_no, __func__,
			      ha->firmware_state, ha->addl_fw_state,
			      timeout_count));
		if (!(ha->addl_fw_state & FW_ADDSTATE_LINK_UP) &&
			(timeout_count < ADAPTER_INIT_TOV - 5)) {
			break;
		}
		msleep(1000);
	}			/* end of for */

	if (timeout_count == 0)
		DEBUG2(printk("scsi%ld: %s: FW Initialization timed out!\n",
			      ha->host_no, __func__));

	if ((ha->firmware_state & FW_STATE_DHCP_IN_PROGRESS)||
		!(ha->addl_fw_state & FW_ADDSTATE_LINK_UP))  {
		DEBUG2(printk("scsi%ld: %s: FW is reporting its waiting to"
			      " grab an IP address from DHCP server\n",
			      ha->host_no, __func__));
		ready = 1;
	}

	return ready;
}

/**
 * qla4xxx_init_firmware - initializes the firmware.
 * @ha: pointer to host adapter structure.
 *
 **/
static int qla4xxx_init_firmware(struct scsi_qla_host *ha)
{
	int status = QLA_ERROR;

	dev_info(&ha->pdev->dev, "Initializing firmware..\n");
	if (qla4xxx_initialize_fw_cb(ha) == QLA_ERROR) {
		DEBUG2(printk("scsi%ld: %s: Failed to initialize firmware "
			      "control block\n", ha->host_no, __func__));
		return status;
	}
	if (!qla4xxx_fw_ready(ha))
		return status;

	return qla4xxx_get_firmware_status(ha);
}

static void qla4xxx_fill_ddb(struct ddb_entry *ddb_entry,
		struct dev_db_entry *fw_ddb_entry)
{
	ddb_entry->target_session_id = le16_to_cpu(fw_ddb_entry->tsid);
	ddb_entry->task_mgmt_timeout =
		le16_to_cpu(fw_ddb_entry->def_timeout);
	ddb_entry->CmdSn = 0;
	ddb_entry->exe_throttle = le16_to_cpu(fw_ddb_entry->exec_throttle);

	ddb_entry->default_relogin_timeout =
		le16_to_cpu(fw_ddb_entry->def_timeout);
	ddb_entry->default_time2wait =
		le16_to_cpu(fw_ddb_entry->iscsi_def_time2wait);

	ddb_entry->port = le16_to_cpu(fw_ddb_entry->port);
	ddb_entry->tpgt = le32_to_cpu(fw_ddb_entry->tgt_portal_grp);
	memcpy(ddb_entry->isid, fw_ddb_entry->isid, sizeof(ddb_entry->isid));
	memcpy(&ddb_entry->iscsi_name[0], &fw_ddb_entry->iscsi_name[0],
		min(sizeof(ddb_entry->iscsi_name),
		sizeof(fw_ddb_entry->iscsi_name)));
	memcpy(&ddb_entry->ip_addr[0], &fw_ddb_entry->ip_addr[0],
		min(sizeof(ddb_entry->ip_addr), sizeof(fw_ddb_entry->ip_addr)));
	ddb_entry->ka_timeout = fw_ddb_entry->ka_timeout;
}

/**
 * qla4xxx_alloc_ddb - allocate device database entry
 * @ha: Pointer to host adapter structure.
 * @fw_ddb_index: Firmware's device database index
 *
 * This routine allocates a ddb_entry, ititializes some values, and
 * inserts it into the ddb list.
 **/
struct ddb_entry * qla4xxx_alloc_ddb(struct scsi_qla_host *ha,
				     uint32_t fw_ddb_index)
{
	struct ddb_entry *ddb_entry;

	DEBUG2(printk("scsi%ld: %s: fw_ddb_index [%d]\n", ha->host_no,
		      __func__, fw_ddb_index));

	ddb_entry = qla4xxx_alloc_sess(ha);
	if (ddb_entry == NULL) {
		DEBUG2(printk("scsi%ld: %s: Unable to allocate memory "
			      "to add fw_ddb_index [%d]\n",
			      ha->host_no, __func__, fw_ddb_index));
		return ddb_entry;
	}

	ddb_entry->fw_ddb_index = fw_ddb_index;
	atomic_set(&ddb_entry->port_down_timer, ha->port_down_retry_count);
	atomic_set(&ddb_entry->retry_relogin_timer, INVALID_ENTRY);
	atomic_set(&ddb_entry->relogin_timer, 0);
	atomic_set(&ddb_entry->relogin_retry_count, 0);
	atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
	dev_info(&ha->pdev->dev,
		"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE\n",
		ha->host_no, __func__, ddb_entry->fw_ddb_index,
		ddb_entry->os_target_id);
	list_add_tail(&ddb_entry->list, &ha->ddb_list);
	ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
	ha->tot_ddbs++;

	return ddb_entry;
}

/**
 * qla4xxx_configure_ddbs - builds driver ddb list
 * @ha: Pointer to host adapter structure.
 *
 * This routine searches for all valid firmware ddb entries and builds
 * an internal ddb list. Ddbs that are considered valid are those with 
 * a device state of SESSION_ACTIVE.
 **/
static int qla4xxx_build_ddb_list(struct scsi_qla_host *ha)
{
	int status = QLA_SUCCESS;
	uint32_t fw_ddb_index = 0;
	uint32_t next_fw_ddb_index = 0;
	uint32_t ddb_state;
	uint32_t conn_err, err_code;
	struct ddb_entry *ddb_entry;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint16_t src_port, conn_id;
	uint32_t ipv6_device;

	dev_info(&ha->pdev->dev, "Initializing DDBs ...\n");

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
			&fw_ddb_entry_dma, GFP_KERNEL);

	if (fw_ddb_entry == NULL) {
		DEBUG2(dev_info(&ha->pdev->dev, "%s: DMA alloc failed\n",
			__func__));
		return QLA_ERROR;
	}

	for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES;
	     fw_ddb_index = next_fw_ddb_index) {
		/* First, let's see if a device exists here */
		if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, fw_ddb_entry,
			fw_ddb_entry_dma, NULL, &next_fw_ddb_index,
			&ddb_state, &conn_err, &src_port,
			&conn_id) == QLA_ERROR) {
			DEBUG2(dev_info(&ha->pdev->dev, "%s: get_ddb_entry,"
				" fw_ddb_index %d failed", __func__,
				fw_ddb_index));
			goto exit_ddb_list;
		}

		DEBUG2(dev_info(&ha->pdev->dev, "%s: DDB[%d] ddbstate=0x%x, "
			      "next_fw_ddb_index=%d.\n", __func__,
			      fw_ddb_index, ddb_state, next_fw_ddb_index));

		/* Issue relogin, if necessary. */
		if (ddb_state == DDB_DS_SESSION_FAILED ||
		    ddb_state == DDB_DS_NO_CONNECTION_ACTIVE) {
			ipv6_device = le16_to_cpu(fw_ddb_entry->options) &
					DDB_OPT_IPV6_DEVICE;
			/* Try and login to device */
			DEBUG2(dev_info(&ha->pdev->dev, "%s: Login DDB[%d]\n",
				__func__, fw_ddb_index));
			err_code = ((conn_err & 0x00ff0000) >> 16);
			if (err_code == 0x1c || err_code == 0x06) {
				DEBUG2(dev_info(&ha->pdev->dev,
					": %s send target completed or access"
					" denied failure\n", __func__));
			} else if ((!ipv6_device &&
					*((uint32_t *)fw_ddb_entry->ip_addr)) ||
					ipv6_device) {
				qla4xxx_set_ddb_entry(ha, fw_ddb_index, 0);
				if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index,
					fw_ddb_entry, fw_ddb_entry_dma, NULL,
					&next_fw_ddb_index, &ddb_state,
					&conn_err, &src_port,
					&conn_id) == QLA_ERROR) {
					DEBUG2(dev_info(&ha->pdev->dev,
						"%s: get_fwddb %d failed\n",
						__func__, fw_ddb_index));
					goto exit_ddb_list;
				}
			}
		}

		if (!(le16_to_cpu(fw_ddb_entry->options) & DDB_OPT_DISC_SESSION) &&
			(ddb_state != DDB_DS_UNASSIGNED) &&
			(strlen(fw_ddb_entry->iscsi_name) != 0)){
			ddb_entry = qla4xxx_alloc_ddb(ha, fw_ddb_index);
			if (ddb_entry == NULL) {
				DEBUG2(dev_info(&ha->pdev->dev,"%s alloc_ddb %d "
					"failed\n", __func__, fw_ddb_index));
				goto exit_ddb_list;
			}
			ddb_entry->fw_ddb_index = fw_ddb_index;
			ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
			ddb_entry->tcp_source_port_num = src_port;
			ddb_entry->connection_id = conn_id;
			qla4xxx_fill_ddb(ddb_entry, fw_ddb_entry);
			ddb_entry->fw_ddb_device_state = ddb_state;
			
#ifndef __VMWARE__
			if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
				atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
				dev_info(&ha->pdev->dev,
					"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE\n",
					ha->host_no, __func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
			} else {
				atomic_set(&ddb_entry->state, DDB_STATE_MISSING);
				dev_info(&ha->pdev->dev,
					"scsi%ld: %s: ddb[%d] os[%d] marked MISSING\n",
					ha->host_no, __func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
			}
#else
			/*
			 * The above code is incomplete / incorrect.  First
			 * qla4xxx_alloc_ddb() just set DDB_STATE_ONLINE so we
			 * can ignore that case.  The MISSING case is incorrect.
			 * It makes no attempt to notify [vmk]linux the device is
			 * missing or start a relogin request.  The existing code
			 * to initiate those phases is unsafe at this point because
			 * while we did allocate a ddb and session we don't have
			 * the ddb->conn and and other states in the driver are 
			 * not ready yet.
			 *
			 * A low risk workaround for this is to continue to claim
			 * the device is ONLINE instead of MISSING.  Then the normal
			 * failure code paths (IO/AEN) will discover the DDB is really
			 * MISSING and complete the required processing in a safe
			 * state.
			 */
#endif

			DEBUG6(dev_info(&ha->pdev->dev, "%s: DDB[%d] osIdx = %d State %04x"
				" ConnErr %08x %d.%d.%d.%d:%04d \"%s\"\n", __func__,
				fw_ddb_index, ddb_entry->os_target_id, ddb_state, conn_err,
				fw_ddb_entry->ip_addr[0], fw_ddb_entry->ip_addr[1],
				fw_ddb_entry->ip_addr[2], fw_ddb_entry->ip_addr[3],
				le16_to_cpu(fw_ddb_entry->port),
				fw_ddb_entry->iscsi_name));
		}

		/* We know we've reached the last device when
		 * next_fw_ddb_index is 0 */
		if (next_fw_ddb_index == 0)
			break;
	}

exit_ddb_list:
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry), fw_ddb_entry,
		fw_ddb_entry_dma);
	dev_info(&ha->pdev->dev, "DDB list done..\n");

	return status;
}

struct qla4_relog_scan {
	int halt_wait;
	uint32_t conn_err;
	uint32_t err_code;
	uint32_t fw_ddb_index;
	uint32_t next_fw_ddb_index;
	uint32_t fw_ddb_device_state;
};

static int qla4_test_rdy(struct scsi_qla_host *ha, struct qla4_relog_scan *rs)
{
	struct ddb_entry *ddb_entry;

	/*
	 * Don't want to do a relogin if connection
	 * error is 0x1c.
	 */
	rs->err_code = ((rs->conn_err & 0x00ff0000) >> 16);
	if (rs->err_code == 0x1c || rs->err_code == 0x06) {
		DEBUG2(printk(
			       "scsi%ld: %s send target"
			       " completed or "
			       "access denied failure\n",
			       ha->host_no, __func__));
	} else {
		/* We either have a device that is in
		 * the process of relogging in or a
		 * device that is waiting to be
		 * relogged in */
		rs->halt_wait = 0;

		ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha,
							   rs->fw_ddb_index);
		if (ddb_entry == NULL)
			return QLA_ERROR;

		if (ddb_entry->dev_scan_wait_to_start_relogin != 0
		    && time_after_eq(jiffies,
				     ddb_entry->
				     dev_scan_wait_to_start_relogin))
		{
			ddb_entry->dev_scan_wait_to_start_relogin = 0;
			qla4xxx_set_ddb_entry(ha, rs->fw_ddb_index, 0);
		}
	}
	return QLA_SUCCESS;
}

static int qla4_scan_for_relogin(struct scsi_qla_host *ha,
				 struct qla4_relog_scan *rs)
{
	int error;

	/* scan for relogins
	 * ----------------- */
	for (rs->fw_ddb_index = 0; rs->fw_ddb_index < MAX_DDB_ENTRIES;
	     rs->fw_ddb_index = rs->next_fw_ddb_index) {
		if (qla4xxx_get_fwddb_entry(ha, rs->fw_ddb_index, NULL, 0,
					    NULL, &rs->next_fw_ddb_index,
					    &rs->fw_ddb_device_state,
					    &rs->conn_err, NULL, NULL)
		    == QLA_ERROR)
			return QLA_ERROR;

		if (rs->fw_ddb_device_state == DDB_DS_LOGIN_IN_PROCESS)
			rs->halt_wait = 0;

		if (rs->fw_ddb_device_state == DDB_DS_SESSION_FAILED ||
		    rs->fw_ddb_device_state == DDB_DS_NO_CONNECTION_ACTIVE) {
			error = qla4_test_rdy(ha, rs);
			if (error)
				return error;
		}

		/* We know we've reached the last device when
		 * next_fw_ddb_index is 0 */
		if (rs->next_fw_ddb_index == 0)
			break;
	}
	return QLA_SUCCESS;
}

/**
 * qla4xxx_devices_ready - wait for target devices to be logged in
 * @ha: pointer to adapter structure
 *
 * This routine waits up to ql4xdiscoverywait seconds
 * F/W database during driver load time.
 **/
static int qla4xxx_devices_ready(struct scsi_qla_host *ha)
{
	int error;
	unsigned long discovery_wtime = 0;
	struct qla4_relog_scan rs;

	DEBUG(printk("Waiting (%d) for devices ...\n", ql4xdiscoverywait));
	do {
		/* poll for AEN. */
		qla4xxx_get_firmware_state(ha);

		if(!(ha->addl_fw_state & FW_ADDSTATE_LINK_UP) &&
			(discovery_wtime > QLA4XXX_LINK_UP_DELAY))
			break;

		if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags)) {
			/* Set time-between-relogin timer */
			qla4xxx_process_aen(ha, RELOGIN_DDB_CHANGED_AENS);
		}

		/* if no relogins active or needed, halt discvery wait */
		rs.halt_wait = 1;

		error = qla4_scan_for_relogin(ha, &rs);

		if (rs.halt_wait) {
			DEBUG2(printk("scsi%ld: %s: Delay halted.  Devices "
				      "Ready.\n", ha->host_no, __func__));
			return QLA_SUCCESS;
		}

		msleep(2000);
		discovery_wtime += 2;
	} while (discovery_wtime < ql4xdiscoverywait);

	DEBUG3(qla4xxx_get_conn_event_log(ha));

	return QLA_SUCCESS;
}

static void qla4xxx_flush_AENS(struct scsi_qla_host *ha)
{
	unsigned long wtime;

	/* Flush the 0x8014 AEN from the firmware as a result of
	 * Auto connect. We are basically doing get_firmware_ddb()
	 * to determine whether we need to log back in or not.
	 *  Trying to do a set ddb before we have processed 0x8014
	 *  will result in another set_ddb() for the same ddb. In other
	 *  words there will be stale entries in the aen_q.
	 */
	wtime = jiffies + (2 * HZ);
	do {
		if (qla4xxx_get_firmware_state(ha) == QLA_SUCCESS)
			if (ha->firmware_state & (BIT_2 | BIT_0))
				return;

		if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
			qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

		msleep(1000);
	} while (!time_after_eq(jiffies, wtime));

}

static int qla4xxx_initialize_ddb_list(struct scsi_qla_host *ha)
{
	uint16_t fw_ddb_index;
	int status = QLA_SUCCESS;

	/* free the ddb list if is not empty */
	if (!list_empty(&ha->ddb_list))
		qla4xxx_free_ddb_list(ha);

	for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES; fw_ddb_index++)
		ha->fw_ddb_index_map[fw_ddb_index] = NULL;

	ha->tot_ddbs = 0;

	qla4xxx_flush_AENS(ha);

	/* Wait for an AEN */
	qla4xxx_devices_ready(ha);

	/*
	 * First perform device discovery for active
	 * fw ddb indexes and build
	 * ddb list.
	 */
	if ((status = qla4xxx_build_ddb_list(ha)) == QLA_ERROR)
		return status;

	/*
	 * Targets can come online after the inital discovery, so processing
	 * the aens here will catch them.
	 */
	if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
		qla4xxx_process_aen(ha, PROCESS_FOR_PROBE);

	return status;
}

/**
 * qla4xxx_update_ddb_list - update the driver ddb list
 * @ha: pointer to host adapter structure.
 *
 * This routine obtains device information from the F/W database after
 * firmware or adapter resets.  The device table is preserved.
 **/
int qla4xxx_reinitialize_ddb_list(struct scsi_qla_host *ha)
{
	int status = QLA_SUCCESS;
#ifdef __VMWARE__
	uint32_t fw_ddb_index = 0;	
	uint32_t next_fw_ddb_index = 0;
	uint32_t ddb_state, found = 0;
	uint32_t conn_err;
	uint16_t src_port, conn_id;
#endif /* __VMWARE__ */
	struct ddb_entry *ddb_entry, *detemp;
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev,
				sizeof(*fw_ddb_entry), &fw_ddb_entry_dma,
				GFP_KERNEL);
	if (fw_ddb_entry == NULL)
		return QLA_ERROR;

#ifndef __VMWARE__
	/* Update the device information for all devices. */
	list_for_each_entry_safe(ddb_entry, detemp, &ha->ddb_list, list) {
		if (qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index,
			fw_ddb_entry, fw_ddb_entry_dma, NULL, NULL,
			&ddb_entry->fw_ddb_device_state, NULL,
			&ddb_entry->tcp_source_port_num,
			&ddb_entry->connection_id) == QLA_SUCCESS) {

			qla4xxx_fill_ddb(ddb_entry, fw_ddb_entry);

			if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
				atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
				dev_info(&ha->pdev->dev,
					"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE\n",
					ha->host_no, __func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
			} else if (atomic_read(&ddb_entry->state) == DDB_STATE_ONLINE)
				qla4xxx_mark_device_missing(ha, ddb_entry);
		}
	}
#else
	/* first null out the fw_ddb_index_map */
	ha->tot_ddbs = 0;
        for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES; fw_ddb_index++) {
		ha->fw_ddb_index_map[fw_ddb_index] = NULL;
	}
	
	ddb_entry = NULL;
	list_for_each_entry(ddb_entry, &ha->ddb_list, list) {
		ddb_entry->fw_ddb_index = INVALID_ENTRY;
		ddb_entry->fw_ddb_device_state = DDB_DS_UNASSIGNED;
	}

        for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES;
             fw_ddb_index = next_fw_ddb_index) {
                /* First, let's see if a device exists here */
                if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, fw_ddb_entry,
                        fw_ddb_entry_dma, NULL, &next_fw_ddb_index,
                        &ddb_state, &conn_err, &src_port,
                        &conn_id) == QLA_ERROR) {
                        DEBUG2(dev_info(&ha->pdev->dev, "%s: get_ddb_entry,"
                                " fw_ddb_index %d failed", __func__,
                                fw_ddb_index));
			dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				fw_ddb_entry, fw_ddb_entry_dma);
			return QLA_ERROR;
                }

		found = 0;
		ddb_entry = NULL;
		list_for_each_entry(ddb_entry, &ha->ddb_list, list) {
			if ((strncmp(ddb_entry->iscsi_name, fw_ddb_entry->iscsi_name,
				ISCSI_NAME_SIZE) == 0) && 
				(memcmp(ddb_entry->isid, fw_ddb_entry->isid,
					sizeof(ddb_entry->isid)) == 0) &&
				(atomic_read(&ddb_entry->state) != DDB_STATE_REMOVED)) {
				found = 1;
				break;
			}
		}
		if (found) {
			ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
			ha->tot_ddbs++;
			ddb_entry->fw_ddb_index = fw_ddb_index;
			ddb_entry->fw_ddb_device_state = ddb_state;
			ddb_entry->tcp_source_port_num = src_port;
			ddb_entry->connection_id = conn_id;

			qla4xxx_fill_ddb(ddb_entry, fw_ddb_entry);

			if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
				atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
				dev_info(&ha->pdev->dev,
					"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE\n",
					ha->host_no, __func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
			} else if (atomic_read(&ddb_entry->state) == DDB_STATE_ONLINE)
				qla4xxx_mark_device_missing(ha, ddb_entry);
		}
		if (next_fw_ddb_index == 0)
			break;
	}
	/* 
	 * Check if driver DDB is no longer found in firmware
	 */
	ddb_entry = NULL;
	list_for_each_entry(ddb_entry, &ha->ddb_list, list) {
		if ((atomic_read(&ddb_entry->state) != DDB_STATE_REMOVED) &&
			(ddb_entry->fw_ddb_index == INVALID_ENTRY)) {
			/*
			 * SPECIAL CASE REMOVAL: The firmware no longer
			 * knows about the target on reinit.  If we 
			 * leave the target record the driver and 
			 * firmware will be out of sync.  To avoid this
			 * we need to REMOVE the target.
			 *
			 * This needs to match the same removal flow as
			 * ql4_logout().  Since the firmware doesn't 
			 * have a DDB we don't have to close connection
			 * and free DDB.  Next we would flag the DPC
			 * to do the DF_REMOVE action.  We are already
			 * in the dpc so we can just do that work here.
			 * In the dpc we remove from the ddb map and
			 * reduce the tot_ddbs count.  Thats already
			 * done so the remaining work is to remove the
			 * session via iscsi_linux and then mark the
			 * state removed.  All remaining cleanup work is
			 * handled by the target_destroy() call back. 
			 *
			 * Call offline session for sole purpose of 
			 * forcing async notification to upper layers.
			 * This async event does not occur on remove.
			 */
			iscsi_offline_session(ddb_entry->sess);
			iscsi_remove_session(ddb_entry->sess);

			atomic_set(&ddb_entry->state, DDB_STATE_REMOVED);
			dev_info(&ha->pdev->dev,
				"scsi%ld: %s: ddb[%d] os[%d] marked REMOVED - reinit\n",
				ha->host_no, __func__, ddb_entry->fw_ddb_index,
				ddb_entry->os_target_id);
		}
	}
#endif
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
		fw_ddb_entry, fw_ddb_entry_dma);
	return status;
}

/**
 * qla4xxx_relogin_device - re-establish session
 * @ha: Pointer to host adapter structure.
 * @ddb_entry: Pointer to device database entry
 *
 * This routine does a session relogin with the specified device.
 * The ddb entry must be assigned prior to making this call.
 **/
int qla4xxx_relogin_device(struct scsi_qla_host *ha,
			   struct ddb_entry * ddb_entry)
{
	uint16_t relogin_timer;

	relogin_timer = max(ddb_entry->default_relogin_timeout,
			    (uint16_t)RELOGIN_TOV);
	atomic_set(&ddb_entry->relogin_timer, relogin_timer);

	DEBUG2(printk("scsi%ld: Relogin index [%d]. TOV=%d\n", ha->host_no,
		      ddb_entry->fw_ddb_index, relogin_timer));

	qla4xxx_set_ddb_entry(ha, ddb_entry->fw_ddb_index, 0);

	return QLA_SUCCESS;
}

static int qla4xxx_config_nvram(struct scsi_qla_host *ha)
{
	unsigned long flags;
	union external_hw_config_reg extHwConfig;

	DEBUG2(printk("scsi%ld: %s: Get EEProm parameters \n", ha->host_no,
		      __func__));
	if (ql4xxx_lock_flash(ha) != QLA_SUCCESS)
		return (QLA_ERROR);
	if (ql4xxx_lock_nvram(ha) != QLA_SUCCESS) {
		ql4xxx_unlock_flash(ha);
		return (QLA_ERROR);
	}

	/* Get EEPRom Parameters from NVRAM and validate */
	dev_info(&ha->pdev->dev, "Configuring NVRAM ...\n");
	if (qla4xxx_is_nvram_configuration_valid(ha) == QLA_SUCCESS) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		extHwConfig.Asuint32_t =
			rd_nvram_word(ha, eeprom_ext_hw_conf_offset(ha));
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	} else {
		/*
		 * QLogic adapters should always have a valid NVRAM.
		 * If not valid, do not load.
		 */
		dev_warn(&ha->pdev->dev,
			   "scsi%ld: %s: EEProm checksum invalid.  "
			   "Please update your EEPROM\n", ha->host_no,
			   __func__);

		/* set defaults */
		if (is_qla4010(ha))
			extHwConfig.Asuint32_t = 0x1912;
		else if (is_qla4022(ha) | is_qla4032(ha))
			extHwConfig.Asuint32_t = 0x0023;
	}
	DEBUG(printk("scsi%ld: %s: Setting extHwConfig to 0xFFFF%04x\n",
		     ha->host_no, __func__, extHwConfig.Asuint32_t));

	spin_lock_irqsave(&ha->hardware_lock, flags);
	writel((0xFFFF << 16) | extHwConfig.Asuint32_t, isp_ext_hw_conf(ha));
	readl(isp_ext_hw_conf(ha));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	ql4xxx_unlock_nvram(ha);
	ql4xxx_unlock_flash(ha);

	return (QLA_SUCCESS);
}

static void qla4x00_pci_config(struct scsi_qla_host *ha)
{
	uint16_t w;

	dev_info(&ha->pdev->dev, "Configuring PCI space...\n");

	pci_set_master(ha->pdev);
	pci_set_mwi(ha->pdev);
	/*
	 * We want to respect framework's setting of PCI configuration space
	 * command register and also want to make sure that all bits of
	 * interest to us are properly set in command register.
	 */
	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	w &= ~PCI_COMMAND_INTX_DISABLE;
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);
}

static int qla4xxx_start_firmware_from_flash(struct scsi_qla_host *ha)
{
	int status = QLA_ERROR;
	uint32_t max_wait_time;
	unsigned long flags;
	uint32_t mbox_status;

	dev_info(&ha->pdev->dev, "Starting firmware ...\n");

	/*
	 * Start firmware from flash ROM
	 *
	 * WORKAROUND: Stuff a non-constant value that the firmware can
	 * use as a seed for a random number generator in MB7 prior to
	 * setting BOOT_ENABLE.	 Fixes problem where the TCP
	 * connections use the same TCP ports after each reboot,
	 * causing some connections to not get re-established.
	 */
	DEBUG(printk("scsi%d: %s: Start firmware from flash ROM\n",
		     ha->host_no, __func__));

	spin_lock_irqsave(&ha->hardware_lock, flags);
	writel(jiffies, &ha->reg->mailbox[7]);
	if (is_qla4022(ha) | is_qla4032(ha))
		writel(set_rmask(NVR_WRITE_ENABLE),
		       &ha->reg->u1.isp4022.nvram);

        writel(2, &ha->reg->mailbox[6]);
        readl(&ha->reg->mailbox[6]);

	writel(set_rmask(CSR_BOOT_ENABLE), &ha->reg->ctrl_status);
	readl(&ha->reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait for firmware to come UP. */
	max_wait_time = FIRMWARE_UP_TOV * 4;
	do {
		uint32_t ctrl_status;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = readw(&ha->reg->ctrl_status);
		mbox_status = readw(&ha->reg->mailbox[0]);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if (ctrl_status & set_rmask(CSR_SCSI_PROCESSOR_INTR))
			break;
		if (mbox_status == MBOX_STS_COMMAND_COMPLETE)
			break;

		DEBUG2(printk("scsi%ld: %s: Waiting for boot firmware to "
			      "complete... ctrl_sts=0x%x, remaining=%d\n",
			      ha->host_no, __func__, ctrl_status,
			      max_wait_time));

		msleep(250);
	} while ((max_wait_time--));

	if (mbox_status == MBOX_STS_COMMAND_COMPLETE) {
		DEBUG(printk("scsi%ld: %s: Firmware has started\n",
			     ha->host_no, __func__));

		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel(set_rmask(CSR_SCSI_PROCESSOR_INTR),
		       &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		status = QLA_SUCCESS;
	} else {
		printk(KERN_INFO "scsi%ld: %s: Boot firmware failed "
		       "-  mbox status 0x%x\n", ha->host_no, __func__,
		       mbox_status);
		status = QLA_ERROR;
	}
	return status;
}

int ql4xxx_lock_drvr_wait(struct scsi_qla_host *a)
{
#define QL4_LOCK_DRVR_WAIT	60
#define QL4_LOCK_DRVR_SLEEP	1

	int drvr_wait = QL4_LOCK_DRVR_WAIT;
	while (drvr_wait) {
		if (ql4xxx_lock_drvr(a) == 0) {
			ssleep(QL4_LOCK_DRVR_SLEEP);
			if (drvr_wait) {
				DEBUG2(printk("scsi%ld: %s: Waiting for "
					      "Global Init Semaphore(%d)...\n",
					      a->host_no,
					      __func__, drvr_wait));
			}
			drvr_wait -= QL4_LOCK_DRVR_SLEEP;
		} else {
			DEBUG2(printk("scsi%ld: %s: Global Init Semaphore "
				      "acquired\n", a->host_no, __func__));
			return QLA_SUCCESS;
		}
	}
	return QLA_ERROR;
}

/**
 * qla4xxx_start_firmware - starts qla4xxx firmware
 * @ha: Pointer to host adapter structure.
 *
 * This routine performs the neccessary steps to start the firmware for
 * the QLA4010 adapter.
 **/
static int qla4xxx_start_firmware(struct scsi_qla_host *ha)
{
	unsigned long flags = 0;
	uint32_t mbox_status;
	int status = QLA_ERROR;
	int soft_reset = 1;
	int config_chip = 0;

	if (is_qla4022(ha) | is_qla4032(ha))
		ql4xxx_set_mac_number(ha);

	if (ql4xxx_lock_drvr_wait(ha) != QLA_SUCCESS)
		return QLA_ERROR;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	DEBUG2(printk("scsi%ld: %s: port_ctrl	= 0x%08X\n", ha->host_no,
		      __func__, readw(isp_port_ctrl(ha))));
	DEBUG(printk("scsi%ld: %s: port_status = 0x%08X\n", ha->host_no,
		     __func__, readw(isp_port_status(ha))));

	/* Is Hardware already initialized? */
	if ((readw(isp_port_ctrl(ha)) & 0x8000) != 0) {
		DEBUG(printk("scsi%ld: %s: Hardware has already been "
			     "initialized\n", ha->host_no, __func__));

		/* Receive firmware boot acknowledgement */
		mbox_status = readw(&ha->reg->mailbox[0]);

		DEBUG2(printk("scsi%ld: %s: H/W Config complete - mbox[0]= "
			      "0x%x\n", ha->host_no, __func__, mbox_status));

		/* Is firmware already booted? */
		if (mbox_status == 0) {
			/* F/W not running, must be config by net driver */
			config_chip = 1;
			soft_reset = 0;
		} else {
			writel(set_rmask(CSR_SCSI_PROCESSOR_INTR),
			       &ha->reg->ctrl_status);
			readl(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if (qla4xxx_get_firmware_state(ha) == QLA_SUCCESS) {
				DEBUG2(printk("scsi%ld: %s: Get firmware "
					      "state -- state = 0x%x\n",
					      ha->host_no,
					      __func__, ha->firmware_state));
				/* F/W is running */
				if (ha->firmware_state &
				    FW_STATE_CONFIG_WAIT) {
					DEBUG2(printk("scsi%ld: %s: Firmware "
						      "in known state -- "
						      "config and "
						      "boot, state = 0x%x\n",
						      ha->host_no, __func__,
						      ha->firmware_state));
					config_chip = 1;
					soft_reset = 0;
				}
			} else {
				DEBUG2(printk("scsi%ld: %s: Firmware in "
					      "unknown state -- resetting,"
					      " state = "
					      "0x%x\n", ha->host_no, __func__,
					      ha->firmware_state));
			}
			spin_lock_irqsave(&ha->hardware_lock, flags);
		}
	} else {
		DEBUG(printk("scsi%ld: %s: H/W initialization hasn't been "
			     "started - resetting\n", ha->host_no, __func__));
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG(printk("scsi%ld: %s: Flags soft_rest=%d, config= %d\n ",
		     ha->host_no, __func__, soft_reset, config_chip));
	if (soft_reset) {
		DEBUG(printk("scsi%ld: %s: Issue Soft Reset\n", ha->host_no,
			     __func__));
		status = qla4xxx_soft_reset(ha);
		if (status == QLA_ERROR) {
			DEBUG(printk("scsi%d: %s: Soft Reset failed!\n",
				     ha->host_no, __func__));
			ql4xxx_unlock_drvr(ha);
			return QLA_ERROR;
		}
		config_chip = 1;

		/* Reset clears the semaphore, so aquire again */
		if (ql4xxx_lock_drvr_wait(ha) != QLA_SUCCESS)
			return QLA_ERROR;
	}

	if (config_chip) {
		if ((status = qla4xxx_config_nvram(ha)) == QLA_SUCCESS)
			status = qla4xxx_start_firmware_from_flash(ha);
	}

	ql4xxx_unlock_drvr(ha);
	if (status == QLA_SUCCESS) {
		qla4xxx_get_fw_version(ha);
		if (test_and_clear_bit(AF_GET_CRASH_RECORD, &ha->flags))
			qla4xxx_get_crash_record(ha);
	} else {
		DEBUG(printk("scsi%ld: %s: Firmware has NOT started\n",
			     ha->host_no, __func__));
	}
	return status;
}


/**
 * qla4xxx_initialize_adapter - initiailizes hba
 * @ha: Pointer to host adapter structure.
 * @renew_ddb_list: Indicates what to do with the adapter's ddb list
 *	after adapter recovery has completed.
 *	0=preserve ddb list, 1=destroy and rebuild ddb list
 * 
 * This routine parforms all of the steps necessary to initialize the adapter.
 *
 **/
int qla4xxx_initialize_adapter(struct scsi_qla_host *ha,
			       uint8_t renew_ddb_list)
{
	int status = QLA_ERROR;
	int8_t ip_address[IP_ADDR_LEN] = {0} ;

	ha->eeprom_cmd_data = 0;

	qla4x00_pci_config(ha);

	qla4xxx_disable_intrs(ha);

	/* Initialize the Host adapter request/response queues and firmware */
	if (qla4xxx_start_firmware(ha) == QLA_ERROR)
		goto exit_init_hba;

	if (qla4xxx_validate_mac_address(ha) == QLA_ERROR)
		goto exit_init_hba;

	if (qla4xxx_init_local_data(ha) == QLA_ERROR)
		goto exit_init_hba;

	status = qla4xxx_init_firmware(ha);
	if (status == QLA_ERROR)
		goto exit_init_hba;

	/*
	 * FW is waiting to get an IP address from DHCP server: Skip building
	 * the ddb_list and wait for DHCP lease acquired aen to come in
	 * followed by 0x8014 aen" to trigger the tgt discovery process.
	 */
	if (ha->firmware_state & FW_STATE_DHCP_IN_PROGRESS)
		goto exit_init_hba0;

	/* Skip device discovery if ip and subnet is zero */
	if (memcmp(ha->ip_address, ip_address, IP_ADDR_LEN) == 0 ||
	    memcmp(ha->subnet_mask, ip_address, IP_ADDR_LEN) == 0)
		goto exit_init_hba0;

	if (renew_ddb_list == PRESERVE_DDB_LIST) {
		/*
		 * We want to preserve lun states (i.e. suspended, etc.)
		 * for recovery initiated by the driver.  So just update
		 * the device states for the existing ddb_list.
		 */
		qla4xxx_reinitialize_ddb_list(ha);
	} else if (renew_ddb_list == REBUILD_DDB_LIST) {
		/*
		 * We want to build the ddb_list from scratch during
		 * driver initialization and recovery initiated by the
		 * INT_HBA_RESET IOCTL.
		 */
		status = qla4xxx_initialize_ddb_list(ha);
		if (status == QLA_ERROR) {
			DEBUG2(printk("%s(%ld) Error occurred during build"
				      "ddb list\n", __func__, ha->host_no));
			goto exit_init_hba;
		}

	}
	if (!ha->tot_ddbs) {
		DEBUG2(printk("scsi%ld: Failed to initialize devices or none "
			      "present in Firmware device database\n",
			      ha->host_no));
	}

exit_init_hba0:
	set_bit(AF_ONLINE, &ha->flags);
#ifdef __VMWARE__
	/* Reset af_offline so next time the check doesn't fire early */
	ha->af_offline = 0;

        dev_info(&ha->pdev->dev,
           "scsi%ld: %s: adapter ONLINE\n",
           ha->host_no, __func__);
#endif

exit_init_hba:
	return status;
}

/**
 * qla4xxx_add_device_dynamically - ddb addition due to an AEN
 * @ha:  Pointer to host adapter structure.
 * @fw_ddb_index:  Firmware's device database index
 *
 * This routine processes adds a device as a result of an 8014h AEN.
 **/
static void qla4xxx_add_device_dynamically(struct scsi_qla_host *ha,
				   uint32_t fw_ddb_index, uint32_t probe)
{
	struct dev_db_entry *fw_ddb_entry = NULL;
	dma_addr_t fw_ddb_entry_dma;
	uint16_t src_port, conn_id;
	struct ddb_entry *ddb_entry = NULL;
	uint32_t ddb_state, found = 0;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
				&fw_ddb_entry_dma, GFP_KERNEL);

	if (fw_ddb_entry == NULL) {
		DEBUG2(dev_info(&ha->pdev->dev, "%s dmaalloc failed\n", __func__));
		return;
	}

	if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, fw_ddb_entry,
		fw_ddb_entry_dma, NULL, NULL, &ddb_state, NULL, &src_port,
		&conn_id) == QLA_ERROR) {
		DEBUG2(dev_info(&ha->pdev->dev, "%s getddb %d failed\n",
			__func__, fw_ddb_index));
		return;
	}

	list_for_each_entry(ddb_entry, &ha->ddb_list, list) {
#ifndef __VMWARE__
		if (strncmp(ddb_entry->iscsi_name, fw_ddb_entry->iscsi_name,
			ISCSI_NAME_SIZE) == 0) {
#else
		/*
		 * On dynamic addition make sure an existing driver ddb 
		 * doesn't already exist.  Exclude those in REMOVED state
		 * since those are marked STARGET_DEL in vmklinux, these
		 * are in the process of cleanup.
		 */
		if ((strncmp(ddb_entry->iscsi_name, fw_ddb_entry->iscsi_name,
			ISCSI_NAME_SIZE) == 0) &&
			(memcmp(ddb_entry->isid, fw_ddb_entry->isid,
				sizeof(ddb_entry->isid)) == 0) &&
			(atomic_read(&ddb_entry->state) != DDB_STATE_REMOVED)) {
#endif
			found = 1;

			DEBUG6(dev_info(&ha->pdev->dev, "%s found target ddb = 0x%p"
				" sess 0x%p conn 0x%p state 0x%x nidx 0x%x oidx 0x%x\n",
				__func__, ddb_entry, ddb_entry->sess, ddb_entry->conn,
				ddb_entry->state, fw_ddb_index, ddb_entry->fw_ddb_index));
			break;
		}
	}

	if (!found)
		ddb_entry = qla4xxx_alloc_ddb(ha, fw_ddb_index);
	else if (ddb_entry->fw_ddb_index != fw_ddb_index) {
		/* Target has been bound to a new fw_ddb_index */
#ifndef __VMWARE__
		qla4xxx_free_ddb(ha, ddb_entry);
		ddb_entry = qla4xxx_alloc_ddb(ha, fw_ddb_index);
#else
		if ((ddb_entry->fw_ddb_index != INVALID_ENTRY) &&
			(ddb_entry == ha->fw_ddb_index_map[ddb_entry->fw_ddb_index]))
			ha->fw_ddb_index_map[ddb_entry->fw_ddb_index] = NULL;

		ddb_entry->fw_ddb_index = fw_ddb_index;
		ddb_entry->fw_ddb_device_state = ddb_state;
		ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
                atomic_set(&ddb_entry->port_down_timer,
                           ha->port_down_retry_count);
                atomic_set(&ddb_entry->relogin_retry_count, 0);
                atomic_set(&ddb_entry->relogin_timer, 0);
                clear_bit(DF_RELOGIN, &ddb_entry->flags);
                clear_bit(DF_NO_RELOGIN, &ddb_entry->flags);
                atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
		dev_info(&ha->pdev->dev,
			"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE sess:%p conn:%p\n",
			ha->host_no, __func__, ddb_entry->fw_ddb_index,
			ddb_entry->os_target_id, ddb_entry->sess, ddb_entry->conn);

		if (!probe)
                	qla4xxx_conn_start(ddb_entry->conn);
		DEBUG6(dev_info(&ha->pdev->dev, "%s calling conn_start ddb 0x%p sess 0x%p"
			" conn 0x%p state 0x%x\n", __func__, ddb_entry, ddb_entry->sess,
			ddb_entry->conn, ddb_entry->state));
		goto exit_dyn_add;
#endif
	}

	if (ddb_entry == NULL) {
		DEBUG2(dev_info(&ha->pdev->dev, "%s NULL DDB %d\n",
			__func__, fw_ddb_index));
		goto exit_dyn_add;
	}

	ddb_entry->fw_ddb_index = fw_ddb_index;
	ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
	ddb_entry->tcp_source_port_num = src_port;
	ddb_entry->connection_id = conn_id;
	qla4xxx_fill_ddb(ddb_entry, fw_ddb_entry);
	ddb_entry->fw_ddb_device_state = ddb_state;

	if (!probe) {
		if (qla4xxx_add_sess(ddb_entry, 1)) {
			DEBUG2(printk(KERN_WARNING
			      "scsi%ld: failed to add new device at index "
			      "[%d]\n Unable to add connection and session\n",
			      ha->host_no, fw_ddb_index));
			qla4xxx_free_ddb(ha, ddb_entry);
		}
	}

	DEBUG6(dev_info(&ha->pdev->dev, "%s added ddb 0x%p sess 0x%p conn 0x%p"
		" state 0x%x\n", __func__, ddb_entry, ddb_entry->sess,
		ddb_entry->conn, ddb_entry->state));
exit_dyn_add:
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry), fw_ddb_entry,
		fw_ddb_entry_dma);
	return;
}

/**
 * qla4xxx_process_ddb_changed - process ddb state change
 * @ha - Pointer to host adapter structure.
 * @fw_ddb_index - Firmware's device database index
 * @state - Device state
 *
 * This routine processes a Decive Database Changed AEN Event.
 **/
int qla4xxx_process_ddb_changed(struct scsi_qla_host *ha,
		uint32_t fw_ddb_index, uint32_t state, uint32_t probe)
{
	struct ddb_entry * ddb_entry;
	uint32_t old_fw_ddb_device_state;

	DEBUG6(dev_info(&ha->pdev->dev, "%s idx %d nstate 0x%x\n",
		__func__, fw_ddb_index, state));
	/* check for out of range index */
	if (fw_ddb_index >= MAX_DDB_ENTRIES)
		return QLA_ERROR;

	/* Get the corresponging ddb entry */
	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);
	/* Device does not currently exist in our database. */
	if (ddb_entry == NULL) {
		if (state == DDB_DS_SESSION_ACTIVE)
			qla4xxx_add_device_dynamically(ha, fw_ddb_index, probe);
		return QLA_SUCCESS;
	}
	DEBUG6(dev_info(&ha->pdev->dev, "%s ddb_entry 0x%p ostate 0x%x"
		" sess 0x%p conn 0x%p\n", __func__, ddb_entry,
		ddb_entry->state, ddb_entry->sess, ddb_entry->conn));

	/* Device already exists in our database. */
	old_fw_ddb_device_state = ddb_entry->fw_ddb_device_state;
	DEBUG2(printk("scsi%ld: %s DDB - old state= 0x%x, new state=0x%x for "
		      "index [%d]\n", ha->host_no, __func__,
		      ddb_entry->fw_ddb_device_state, state, fw_ddb_index));
	if (old_fw_ddb_device_state == state &&
	    state == DDB_DS_SESSION_ACTIVE) {
		/* Do nothing, state not changed. */
		return QLA_SUCCESS;
	}

	ddb_entry->fw_ddb_device_state = state;

	if (probe)
		return QLA_SUCCESS;

	/* Device is back online. */
	if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
		atomic_set(&ddb_entry->port_down_timer,
			   ha->port_down_retry_count);
		atomic_set(&ddb_entry->state, DDB_STATE_ONLINE);
		dev_info(&ha->pdev->dev,
			"scsi%ld: %s: ddb[%d] os[%d] marked ONLINE\n",
			ha->host_no, __func__, ddb_entry->fw_ddb_index,
			ddb_entry->os_target_id);
		atomic_set(&ddb_entry->relogin_retry_count, 0);
		atomic_set(&ddb_entry->relogin_timer, 0);
		clear_bit(DF_RELOGIN, &ddb_entry->flags);
		clear_bit(DF_NO_RELOGIN, &ddb_entry->flags);
		clear_bit(DF_STOP_RELOGIN, &ddb_entry->flags);

		DEBUG6(dev_info(&ha->pdev->dev, "%s conn startddb_entry 0x%p"
			" sess 0x%p conn 0x%p\n",
			__func__, ddb_entry, ddb_entry->sess, ddb_entry->conn)); 

		qla4xxx_conn_start(ddb_entry->conn);

		DEBUG6(dev_info(&ha->pdev->dev, "%s conn start done "
			"ddb_entry 0x%p sess 0x%p conn 0x%p\n",
			__func__, ddb_entry, ddb_entry->sess, ddb_entry->conn));

		if (!test_bit(DF_SCAN_ISSUED, &ddb_entry->flags)) {
#ifndef __VMWARE__
               		scsi_scan_target(&ddb_entry->sess->dev, 0,
				ddb_entry->sess->target_id,
				SCAN_WILD_CARD, 0);
#else
			scsi_scan_target(ddb_entry->sess->device, 0,
				ddb_entry->sess->targetID,
				SCAN_WILD_CARD, 0);
#endif
			set_bit(DF_SCAN_ISSUED, &ddb_entry->flags);
		}
	} else {
		/* Device went away, try to relogin. */
		/* Mark device missing */
		DEBUG6(dev_info(&ha->pdev->dev, "%s mark missing ddb_entry 0x%p"
			" sess 0x%p conn 0x%p\n", __func__, ddb_entry,
			ddb_entry->sess, ddb_entry->conn));

		if (atomic_read(&ddb_entry->state) == DDB_STATE_ONLINE)
			qla4xxx_mark_device_missing(ha, ddb_entry);
		/*
		 * Relogin if device state changed to a not active state.
		 * However, do not relogin if this aen is a result of an IOCTL
		 * logout (DF_NO_RELOGIN) or if this is a discovered device.
		 */
		if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_FAILED &&
		    !test_bit(DF_RELOGIN, &ddb_entry->flags) &&
		    !test_bit(DF_NO_RELOGIN, &ddb_entry->flags)) {
			/*
			 * This triggers a relogin.  After the relogin_timer
			 * expires, the relogin gets scheduled.	 We must wait a
			 * minimum amount of time since receiving an 0x8014 AEN
			 * with failed device_state or a logout response before
			 * we can issue another relogin.
			 */
			/* Firmware padds this timeout: (time2wait +1).
			 * Driver retry to login should be longer than F/W.
			 * Otherwise F/W will fail
			 * set_ddb() mbx cmd with 0x4005 since it still
			 * counting down its time2wait.
			 */
			atomic_set(&ddb_entry->relogin_timer, 0);
			atomic_set(&ddb_entry->retry_relogin_timer,
				   ddb_entry->default_time2wait + 4);
		}
	}

	return QLA_SUCCESS;
}

