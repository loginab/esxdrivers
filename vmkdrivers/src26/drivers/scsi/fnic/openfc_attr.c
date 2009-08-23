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
 * $Id: openfc_attr.c 18829 2008-09-22 18:43:36Z ajoglekar $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include "sa_kernel.h"
#include "fc_types.h"
#include "sa_assert.h"
#include "fc_port.h"
#include "fc_remote_port.h"
#include "fcdev.h"
#include "fcoeioctl.h"
#include "fc_frame.h"
#include "sa_log.h"
#include "openfc.h"
#include "openfc_scsi_pkt.h"
#include "openfc_ioctl.h"
#include "fcs_state.h"

extern char	openfc_version_str[];
extern ulong	openfc_boot_time;
/*
 * Scsi_Host class Attributes
 */
static ssize_t openfc_drv_version_show(struct class_device *cdev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", openfc_version_str);
}

static ssize_t openfc_name_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);

	return snprintf(buf, PAGE_SIZE, "FCOEHBA%d\n", host->host_no);
}
static ssize_t openfc_info_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	return snprintf(buf, PAGE_SIZE, "%s", openfc_info(host));
}
static ssize_t openfc_state_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct openfc_softc *openfcp = (struct openfc_softc *) host->hostdata;
	ssize_t		len;

	switch (openfcp->state) {
	case OPENFC_INITIALIZATION:
		len = snprintf(buf, PAGE_SIZE, "FCOE INITIALIZATION");
		break;
	case OPENFC_FCS_INITIALIZATION:
		len = snprintf(buf, PAGE_SIZE, "FCS INITIALIZATION");
		break;
	case OPENFC_DISCOVERY_DONE:
		len = snprintf(buf, PAGE_SIZE, "DISCOVERY COMPLETED");
		break;
	case OPENFC_RUNNING:
		len = snprintf(buf, PAGE_SIZE, "RUNNING");
		break;
	case OPENFC_GOING_DOWN:
		len = snprintf(buf, PAGE_SIZE, "GOING DOWN");
		break;
	default:
		len = snprintf(buf, PAGE_SIZE, "UNKNOWN");
		break;
	}
	if (openfcp->status & OPENFC_LINK_UP)
		len += snprintf(buf + len, PAGE_SIZE, " Link up\n");
	else
		len += snprintf(buf + len, PAGE_SIZE, " Link down\n");

	return len;
}

static ssize_t openfc_stop(struct class_device *cdev,
					const char *buffer, size_t size)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct openfc_softc *openfcp = (struct openfc_softc *) host->hostdata;

	if (buffer == NULL) {
		return size;
	}

	switch (buffer[0]) {
	case '1':
		/*
		 * stop openfc
		 */
		openfc_fcs_stop(openfcp);
		break;
	case '0':
	default:
		break;
	}

	return size;
}

static ssize_t openfc_start(struct class_device *cdev,
					const char *buffer, size_t size)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct openfc_softc *openfcp = (struct openfc_softc *) host->hostdata;

	if (buffer == NULL) {
		return size;
	}

	switch (buffer[0]) {
	case '1':
		/*
		 * start openfc
		 */
		openfc_fcs_start(openfcp);
		break;
	case '0':
	default:
		break;
	}

	return size;
}
static ssize_t openfc_reset(struct class_device *cdev,
					const char *buffer, size_t size)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct openfc_softc *openfcp = (struct openfc_softc *) host->hostdata;

	if (buffer == NULL) {
		return size;
	}

	switch (buffer[0]) {
	case '1':
		/*
		 * reset openfc
		 */
		openfc_reset_if(openfcp);
		break;
	case '0':
	default:
		break;
	}

	return size;
}

char	*openfc_info(struct Scsi_Host *host)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) host->hostdata;
	static char	buf[256];
	int		len = 0;

	memset(buf, 0, 256);
	if (openfcp) {
		len = snprintf(buf, 256, "Openfc / %s driver attached to %s\n",
			       openfcp->dev->drv_info.model_desc,
			       openfcp->dev->ifname);
		if (len < 256)
			return buf;
	}
	return NULL;
}

static		CLASS_DEVICE_ATTR(driver_version, S_IRUGO,
				  openfc_drv_version_show, NULL);
static		CLASS_DEVICE_ATTR(info, S_IRUGO, openfc_info_show, NULL);
static		CLASS_DEVICE_ATTR(fcoe_name, S_IRUGO, openfc_name_show, NULL);
static		CLASS_DEVICE_ATTR(state, S_IRUGO, openfc_state_show, NULL);
static		CLASS_DEVICE_ATTR(stop, S_IWUSR,
				  NULL,
				  openfc_stop);
static		CLASS_DEVICE_ATTR(start, S_IWUSR,
				  NULL,
				  openfc_start);
static		CLASS_DEVICE_ATTR(reset, S_IWUSR,
				  NULL,
				  openfc_reset);



struct class_device_attribute *openfc_host_attrs[] = {
	&class_device_attr_driver_version,
	&class_device_attr_info,
	&class_device_attr_fcoe_name,
	&class_device_attr_state,
	&class_device_attr_stop,
	&class_device_attr_start,
	&class_device_attr_reset,
	NULL,
};

/*
 *  Host attributes.
 */
static void	openfc_get_host_fabric_name(struct Scsi_Host *shost);
static void	openfc_get_host_port_id(struct Scsi_Host *shost);
static void	openfc_get_starget_port_name(struct scsi_target *starget);
static void	openfc_get_starget_node_name(struct scsi_target *starget);
static void	openfc_get_host_port_type(struct Scsi_Host *shost);
static void	openfc_get_host_port_state(struct Scsi_Host *);
static void	openfc_get_host_speed(struct Scsi_Host *shost);
static void	openfc_get_starget_port_id(struct scsi_target *starget);
static void	openfc_set_rport_loss_tmo(struct fc_rport *rport,
					  uint32_t timeout);
struct fc_host_statistics *fcoe_get_fc_host_stats(struct Scsi_Host *);

struct fc_function_template openfc_transport_function = {

	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_active_fc4s = 1,

	.get_host_port_id = openfc_get_host_port_id,
	.show_host_port_id = 1,
	.get_host_speed = openfc_get_host_speed,
	.show_host_speed = 1,
	.get_host_port_type = openfc_get_host_port_type,
	.show_host_port_type = 1,
	.get_host_port_state = openfc_get_host_port_state,
	.show_host_port_state = 1,

	.dd_fcrport_size = sizeof(struct os_tgt),
	.show_rport_supported_classes = 1,

	.get_host_fabric_name = openfc_get_host_fabric_name,
	.show_host_fabric_name = 1,
	.get_starget_node_name = openfc_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = openfc_get_starget_port_name,
	.show_starget_port_name = 1,
	.get_starget_port_id = openfc_get_starget_port_id,
	.show_starget_port_id = 1,
	.set_rport_dev_loss_tmo = openfc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,
	.get_fc_host_stats = fcoe_get_fc_host_stats,

};


static void openfc_get_host_port_id(struct Scsi_Host *shost)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) shost->hostdata;

	fc_host_port_id(shost) = fcs_get_fid(openfcp->fcs_state);
}

static void openfc_get_host_speed(struct Scsi_Host *shost)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) shost->hostdata;
	u32	speed;

	switch (openfcp->fd.fd_speed) {
	case OFC_SPEED_1GBIT:
		speed = FC_PORTSPEED_1GBIT;
		break;
	case OFC_SPEED_2GBIT:
		speed = FC_PORTSPEED_2GBIT;
		break;
	case OFC_SPEED_4GBIT:
		speed = FC_PORTSPEED_4GBIT;
		break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22))
	case OFC_SPEED_8GBIT:
		speed = FC_PORTSPEED_8GBIT;
		break;
#endif
	case OFC_SPEED_10GBIT:
		speed = FC_PORTSPEED_10GBIT;
		break;
	case OFC_SPEED_NOT_NEG:
		speed = FC_PORTSPEED_NOT_NEGOTIATED;
		break;
	default:
		speed = FC_PORTSPEED_UNKNOWN;
		break;
	}
	fc_host_speed(shost) = speed;
}

static void openfc_get_host_port_type(struct Scsi_Host *shost)
{
	fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
}

static void openfc_get_host_port_state(struct Scsi_Host *shost)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) shost->hostdata;
	enum fc_port_state state;

	if (!fc_port_ready(openfcp->fcs_port))
		state = FC_PORTSTATE_LINKDOWN;
	else if (atomic_read(&openfcp->fcs_status) == OPENFC_FCS_ONLINE)
		state = FC_PORTSTATE_ONLINE;
	else
		state = FC_PORTSTATE_OFFLINE;
	fc_host_port_state(shost) = state;
}

static void openfc_get_starget_node_name(struct scsi_target *starget)
{
	struct fc_rport  *rp = starget_to_rport(starget);
	struct os_tgt  *tgtp;

	tgtp = rp->dd_data;
	fc_starget_port_name(starget) = tgtp->port_name;
}

static void openfc_get_starget_port_name(struct scsi_target *starget)
{
	struct fc_rport  *rp = starget_to_rport(starget);
	struct os_tgt  *tgtp;

	tgtp = rp->dd_data;
	fc_starget_node_name(starget) = tgtp->node_name;
}

static void openfc_get_starget_port_id(struct scsi_target *starget)
{
	struct fc_rport  *rp = starget_to_rport(starget);
	struct os_tgt  *tgtp;

	tgtp = rp->dd_data;
	fc_starget_port_id(starget) = tgtp->fcs_rport->rp_fid;
}
static void openfc_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct openfc_softc *openfcp = (struct openfc_softc *) shost->hostdata;

	fc_host_fabric_name(shost) = openfcp->dev->fd_wwnn;
}

void openfc_fc_attr_init(struct openfc_softc *openfcp)
{
	fc_host_node_name(openfcp->host) = openfcp->dev->fd_wwnn;
	fc_host_port_name(openfcp->host) = openfcp->dev->fd_wwpn;
	fc_host_supported_classes(openfcp->host) = FC_COS_CLASS3;
	memset(fc_host_supported_fc4s(openfcp->host), 0,
	       sizeof(fc_host_supported_fc4s(openfcp->host)));
	fc_host_supported_fc4s(openfcp->host)[2] = 1;
	fc_host_supported_fc4s(openfcp->host)[7] = 1;
	/* This value is also unchanging */
	memset(fc_host_active_fc4s(openfcp->host), 0,
	       sizeof(fc_host_active_fc4s(openfcp->host)));
	fc_host_active_fc4s(openfcp->host)[2] = 1;
	fc_host_active_fc4s(openfcp->host)[7] = 1;
	fc_host_maxframe_size(openfcp->host) =
	    openfcp->dev->framesize - sizeof(struct fc_frame_header);

}

static void openfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout + 5;
	else
		rport->dev_loss_tmo = OPENFC_DFLT_DEVLOSS_TMO;

}

struct fc_host_statistics *fcoe_get_fc_host_stats(struct Scsi_Host *shp)
{
	int		i;
	struct fc_host_statistics *fcoe_stats;
	struct openfc_softc *openfcp = (struct openfc_softc *) shp->hostdata;
	struct timespec v0, v1;

	fcoe_stats = &openfcp->openfc_host_stat;
	memset(fcoe_stats, 0, sizeof(struct fc_host_statistics));

	jiffies_to_timespec(jiffies, &v0);
	jiffies_to_timespec(openfc_boot_time, &v1);
	fcoe_stats->seconds_since_last_reset = (v0.tv_sec - v1.tv_sec);

	/* Get stats from the transport driver*/
	if (openfcp->dev->port_ops.get_stats)
		openfcp->dev->port_ops.get_stats(openfcp->dev);
	
	for_each_online_cpu(i) {
		struct fcoe_dev_stats *stats = openfcp->dev->dev_stats[i];
		if (stats != NULL) {
			fcoe_stats->tx_frames += stats->TxFrames;
			fcoe_stats->tx_words += stats->TxWords;
			fcoe_stats->rx_frames += stats->RxFrames;
			fcoe_stats->rx_words += stats->RxWords;
			fcoe_stats->error_frames += stats->ErrorFrames;
			fcoe_stats->invalid_crc_count += stats->InvalidCRCCount;
			fcoe_stats->fcp_input_requests += stats->InputRequests;
			fcoe_stats->fcp_output_requests +=
			    stats->OutputRequests;
			fcoe_stats->fcp_control_requests +=
			    stats->ControlRequests;
			fcoe_stats->fcp_input_megabytes +=
			    stats->InputMegabytes;
			fcoe_stats->fcp_output_megabytes +=
			    stats->OutputMegabytes;
			fcoe_stats->link_failure_count +=
			    stats->LinkFailureCount;
		}
	}
	fcoe_stats->lip_count = fcoe_stats->nos_count =
	    fcoe_stats->loss_of_sync_count = fcoe_stats->loss_of_signal_count =
	    fcoe_stats->prim_seq_protocol_err_count =
	    fcoe_stats->dumped_frames = -1;
	return fcoe_stats;
}
