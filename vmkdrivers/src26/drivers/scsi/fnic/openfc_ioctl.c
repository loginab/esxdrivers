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
 * $Id: openfc_ioctl.c 18557 2008-09-14 22:36:38Z jre $
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>

#include "sa_assert.h"
#include "fc_types.h"
#include "fc_encaps.h"
#include "fc_frame.h"
#include "fc_local_port.h"
#include "fcdev.h"
#include "openfc.h"
#include "openfc_ioctl.h"
#include "sa_log.h"
#include "fcs_state.h"
#include "fc_fs.h"
#include "fc_gs.h"
#include "fc_ns.h"
#include "fc_els.h"

#define OPENFC_CLASS_NAME    "openfc"	/* class name for udev */
#define OPENFC_DEV_NAME	     "openfc"	/* class name for udev */
#define OPENFC_DRIVER_NAME   "openfc"	/* driver name for ioctl */
#define OPENFC_MAX_PORT_CNT	1
int		openfc_major;
struct class   *openfc_class;
extern uint32_t max_host_no;
extern char	openfc_version_str[];
extern struct fc_host_statistics *fcoe_get_fc_host_stats(struct Scsi_Host *);
/*
 * using this ioctl interface user can create multiple
 * instance of fcoe hba (dcehba). each hba structure will
 * be mapped to a ethernet interface.  persistent binding
 * table information and fc configuration information
 * will be passed to opfc driver.
 */
int		openfc_reg_char_dev(void);
void		openfc_unreg_char_dev(void);
/*
 * Static functions and variables definations
 */
static int	openfc_ioctl(struct inode *, struct file *, u_int32_t, ulong);
static int	openfc_open(struct inode *, struct file *);
static int	openfc_close(struct inode *, struct file *);
static int	openfc_ioctl_send_cmd(void __user *);
static int	openfc_ioctl_event_read(void __user *, int);
#ifdef CONFIG_COMPAT
static long	openfc_compat_ioctl(struct file *, u_int32_t, ulong);
#endif

static struct file_operations openfc_file_ops = {
	.owner = THIS_MODULE,
	.open = openfc_open,
	.release = openfc_close,
	.ioctl = openfc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = openfc_compat_ioctl,
#endif
};

/*
 *  fcoe driver's open routine
 */
static int openfc_open(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *  fcoe driver's close routine
 */
static int openfc_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * IOCTL interface for HBA API
 */
static int openfc_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int		rc = 0;
	void __user    *_arg = (void __user *) arg;
	int		tmp;
	struct openfc_softc *openfcp;
	struct ofc_io_hba_info local_hba, *hba = &local_hba;
	struct ofc_io_port_info local_port, *port = &local_port;
	struct ofc_io_port_stats lps, *ps = &lps;
	struct ofc_io_fc4_stats fcs, *fcps = &fcs;
	struct fc_drv_info *drvi;
	struct ofc_port_stats *stats;
	struct ofc_fc4_stats *fcp_stats;
	struct fc_host_statistics *fcoe_stats;
	struct ofc_io_rnid rnid;
	struct fc_els_rnid_gen *rnidp;
	struct fc_local_port *lp;
	const struct fc_ns_fts *fm;

	if (!capable(CAP_SYS_ADMIN)) {
		return -EACCES;
	}
	switch (cmd) {
	case OFC_GET_VERSION:
		tmp = OFC_IOCTL_VER;
		if (copy_to_user(_arg, &tmp, sizeof(int)))
			rc = -EFAULT;
		break;
	case OFC_GET_HBA_COUNT:
		tmp = max_host_no + 1;
		if (copy_to_user(_arg, &tmp, sizeof(int)))
			rc = -EFAULT;
		break;
	case OFC_GET_HBA_INFO:
		memset(hba, 0, sizeof(struct ofc_io_port_info));
		if (copy_from_user(hba, _arg, sizeof(struct ofc_io_hba_info))) {
			rc = -EFAULT;
			break;
		}
		openfcp = openfc_find_hba(hba->hi_hba);
		if (openfcp == NULL) {
			rc = -ENXIO;
		} else {
			hba->hi_wwnn = openfcp->dev->fd_wwnn;
			drvi = &openfcp->dev->drv_info;
			hba->hi_port_count = OPENFC_MAX_PORT_CNT;
			snprintf(hba->hi_model, OFC_SNAME_LEN, "%s",
				 drvi->model);
			snprintf(hba->hi_model_desc, OFC_LNAME_LEN, "%s",
				 openfc_info(openfcp->host));
			snprintf(hba->hi_driver_vers, OFC_LNAME_LEN, "%s / %s",
				 openfc_version_str, drvi->drv_version);
			snprintf(hba->hi_driver_name, OFC_LNAME_LEN, "%s / %s",
				 OPENFC_DRIVER_NAME, drvi->drv_name);
			rc = copy_to_user(_arg, hba,
					  sizeof(struct ofc_io_hba_info));
		}
		break;
	case OFC_GET_PORT_INFO:
		memset(port, 0, sizeof(struct ofc_io_port_info));
		if (copy_from_user(port, _arg,
					sizeof(struct ofc_io_port_info))) {
			rc = -EFAULT;
			break;
		}
		if (port->pi_port != (OPENFC_MAX_PORT_CNT -1)) {
			printk(KERN_ERR "port %d not a valid port\n",
				port->pi_port);
			rc = -ENXIO;
			break;
		}
		openfcp = openfc_find_hba(port->pi_hba);
		if (openfcp == NULL) {
			rc = -ENXIO;
		} else {
			port->pi_wwnn = openfcp->dev->fd_wwnn;
			port->pi_wwpn = openfcp->dev->fd_wwpn;
			port->pi_fcid = fcs_get_fid(openfcp->fcs_state);;
			port->pi_port_type = OFC_PTYPE_N;
			port->pi_speed = openfcp->fd.fd_speed;
			port->pi_speed_support = openfcp->fd.fd_speed_support;
			port->pi_port_mode = OFC_MODE_INIT;
			port->pi_max_frame_size = (openfcp->dev->framesize -
			    sizeof(struct fc_frame_header)) & ~3;
			if (port->pi_max_frame_size > FC_MAX_PAYLOAD) {
			    port->pi_max_frame_size = FC_MAX_PAYLOAD;
			}
			port->pi_class = FC_COS_CLASS3;
			if (fc_port_ready(openfcp->fcs_port) == 0) {
				port->pi_port_state = OFC_PSTATE_NOLINK;
			} else if (atomic_read(&openfcp->fcs_status) ==
			    OPENFC_FCS_ONLINE) {
				port->pi_port_state = OFC_PSTATE_ONLINE;
			} else {
				port->pi_port_state = OFC_PSTATE_OFFLINE;
			}
			lp = fcs_get_local_port(openfcp->fcs_state);
			fm = fc_local_port_get_fc4_map(lp);
			memcpy(port->pi_fc4_support, fm, sizeof(*fm));
			memcpy(port->pi_fc4_active, fm, sizeof(*fm));
			port->pi_fc4_support[2] |= 1; 	/* FC byte order FCP */
			port->pi_fc4_support[7] |= 1; 	/* FC byte order CT */
			port->pi_fc4_active[2] |= 1; 	/* FC byte order FCP */
			port->pi_fc4_active[7] |= 1; 	/* FC byte order CT */
			port->pi_disc_ports =
					atomic_read(&openfcp->discover_devs);
			snprintf(port->pi_os_dev_name, OFC_LNAME_LEN, "%s",
				 openfcp->dev->ifname);
			rc = copy_to_user(_arg, port,
					  sizeof(struct ofc_io_port_info));
		}
		break;
	case OFC_SET_PORT_CONF:
		rc = -ENXIO;
		break;
	case OFC_GET_PORT_STATS:
		memset(ps, 0, sizeof(struct ofc_io_port_stats));
		if (copy_from_user(ps, _arg, sizeof(struct ofc_io_port_stats))) {
			rc = -EFAULT;
			break;
		}
		if (ps->ps_port != (OPENFC_MAX_PORT_CNT -1)) {
			printk(KERN_ERR "port %d not a valid port\n", ps->ps_port);
			rc = -ENXIO;
			break;
		}
		openfcp = openfc_find_hba(ps->ps_hba);
		if (openfcp == NULL) {
			rc = -ENXIO;
		} else {
			fcoe_stats = fcoe_get_fc_host_stats(openfcp->host);
			stats = &ps->ps_stats;
			stats->ps_sec_since_reset =
			    fcoe_stats->seconds_since_last_reset;
			stats->ps_tx_frames = fcoe_stats->tx_frames;
			stats->ps_rx_frames = fcoe_stats->rx_frames;
			stats->ps_tx_words = fcoe_stats->tx_words;
			stats->ps_rx_words = fcoe_stats->rx_words;
			stats->ps_LIP_count = fcoe_stats->lip_count;
			stats->ps_NOS_count = fcoe_stats->nos_count;
			stats->ps_error_frames = fcoe_stats->error_frames;
			stats->ps_dumped_frames = fcoe_stats->dumped_frames;
			stats->ps_link_fails = fcoe_stats->link_failure_count;
			stats->ps_loss_of_sync = fcoe_stats->loss_of_sync_count;
			stats->ps_loss_of_signal =
			    fcoe_stats->loss_of_signal_count;
			stats->ps_primitive_seq_proto_errs =
			    fcoe_stats->prim_seq_protocol_err_count;
			stats->ps_invalid_tx_words =
			    fcoe_stats->invalid_tx_word_count;
			stats->ps_invalid_CRC_count =
			    fcoe_stats->invalid_crc_count;
			rc = copy_to_user(_arg, ps,
					  sizeof(struct ofc_io_port_stats));
			break;
		}
		break;
	case OFC_GET_FC4_STATS:
		memset(fcps, 0, sizeof(struct ofc_io_fc4_stats));
		if (copy_from_user(fcps, _arg, sizeof(struct ofc_io_fc4_stats))) {
			rc = -EFAULT;
			break;
		}
		if (fcps->fs_port != (OPENFC_MAX_PORT_CNT - 1)) {
			printk(KERN_ERR "port %d not a valid port\n", fcps->fs_port);
			rc = -ENXIO;
			break;
		}
		if (fcps->fs_fc4_type != FC_TYPE_FCP) {
			printk(KERN_ERR "invalid type %d\n", fcps->fs_fc4_type);
			rc = -ENXIO;
			break;
		}
		openfcp = openfc_find_hba(fcps->fs_hba);
		if (openfcp == NULL) {
			rc = -ENXIO;
		} else {
			fcoe_stats = fcoe_get_fc_host_stats(openfcp->host);
			fcp_stats = &fcps->fs_stats;
			fcp_stats->fs_sec_since_reset =
			    fcoe_stats->seconds_since_last_reset;
			fcp_stats->fs_in_req = fcoe_stats->fcp_input_requests;
			fcp_stats->fs_out_req = fcoe_stats->fcp_output_requests;
			fcp_stats->fs_ctl_req =
			    fcoe_stats->fcp_control_requests;
			fcp_stats->fs_in_bytes =
			    fcoe_stats->fcp_input_megabytes;
			fcp_stats->fs_out_bytes =
			    fcoe_stats->fcp_output_megabytes;
			if (copy_to_user(_arg, fcps,
					  sizeof(struct ofc_io_fc4_stats)))
				rc = -EFAULT;
		}
		break;
	case OFC_SEND_CMD:
		rc = openfc_ioctl_send_cmd(_arg);
		break;
	case OFC_GET_RNID_DATA:
	case OFC_SET_RNID_DATA:
		if (copy_from_user(&rnid, _arg, sizeof (rnid)))
			return -EFAULT;
		if (rnid.ic_port >= OPENFC_MAX_PORT_CNT)
			return -ENXIO;
		openfcp = openfc_find_hba(rnid.ic_hba);
		if (openfcp == NULL)
			return -ENXIO;
		lp = fcs_get_local_port(openfcp->fcs_state);
		rnidp = fc_local_port_get_rnidp(lp);
		if (rnidp == NULL)
			return -EOPNOTSUPP;
		if (cmd == OFC_SET_RNID_DATA) {
			*rnidp = rnid.ic_rnid;
		} else {
			rnid.ic_rnid = *rnidp;
			if (copy_to_user(_arg, &rnid, sizeof (rnid)))
				return -EFAULT;
		}
		break;
	case OFC_EVENT_READ:
		rc = openfc_ioctl_event_read(_arg, 0);
		break;
	case OFC_EVENT_WAIT:
		rc = openfc_ioctl_event_read(_arg, 1);
		break;
	default:
		rc = -ENXIO;
		break;
	}

	return rc;
}

/*
 * Read queued event reports.
 */
static int openfc_ioctl_event_read(void __user *arg, int wait_flag)
{
	struct ofc_io_event_read cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd)))
		return -EFAULT;
	return fcs_ev_get(cmd.ev_mask,
	  (void __user *) (long) cmd.ev_buf, cmd.ev_buf_len, wait_flag);
}

/*
 * Send request on fibre channel and wait for reply.
 */
static int openfc_ioctl_send_cmd(void __user *_arg)
{
	struct openfc_softc *openfcp;
        struct ofc_io_cmd io_cmd;
	struct ofc_io_cmd *cp = &io_cmd;
	struct fc_frame *fp = NULL;
	struct fc_frame *rfp = NULL;
	struct fc_frame_header *fh;
	struct fc_ct_hdr *ct;
	uint32_t	len;
	int		rc;
	u_int		login = 0;

	if (copy_from_user(cp, _arg, sizeof(*cp)))
		return -EFAULT;
	openfcp = openfc_find_hba(cp->ic_hba);
	if (!openfcp)
		return -ENXIO;
	if (cp->ic_port >= OPENFC_MAX_PORT_CNT)
		return -ENXIO;
	len = cp->ic_send_len;
	if (len > FC_SP_MAX_MAX_PAYLOAD)	/* len == 0 is allowed */ 
		return -EINVAL;
	if (cp->ic_resp_len > 0xffff)		/* arbitrary defensive max */
		return -EINVAL;
	rc = -ENOMEM;
	fp = fc_frame_alloc(openfcp->fcs_port, len);
	if (!fp)
		goto out;
	rfp = fc_frame_alloc(openfcp->fcs_port, cp->ic_resp_len);
	if (!rfp)
		goto out;
	rc = -EFAULT;
	if (copy_from_user(fc_frame_payload_get(fp, len),
				(void __user *) (long) cp->ic_send_buf, len))
		goto out;
	rc = -EINVAL;
	fh = fc_frame_header_get(fp);
	memset(fh, 0, sizeof (*fh));
	switch (cp->ic_type) {
	case FC_TYPE_CT:
		if (cp->ic_did != 0)		/* may accept this someday */
			goto out;
		ct = fc_frame_payload_get(fp, sizeof (*ct));
		if (!ct)
			goto out;
		switch (ct->ct_fs_type) {
		case FC_FST_ALIAS:
			cp->ic_did = FC_FID_ALIASES;
			break;
		case FC_FST_MGMT:
			cp->ic_did = FC_FID_MGMT_SERV;
			break;
		case FC_FST_TIME:
			cp->ic_did = FC_FID_TIME_SERV;
			break;
		case FC_FST_DIR:
			cp->ic_did = FC_FID_DIR_SERV;
			break;
		default:
			goto out;
		}
		fh->fh_r_ctl = FC_RCTL_DD_UNSOL_CTL;
		login = 1;
		break;

	case FC_TYPE_ELS:
		/*
		 * Only certain ELS requests are allowed.
		 */
		switch (fc_frame_payload_op(fp)) {
		case ELS_ECHO:
		case ELS_LKA:
		case ELS_RNID:
			break;
		case ELS_LIRR:
		case ELS_RLIR:
		case ELS_RLS:
		case ELS_RPL:
		case ELS_RPS:
		case ELS_SRL:
			login = (cp->ic_did < FC_FID_DOM_MGR);
			break;
		default:
			goto out;
		}
		fh->fh_r_ctl = FC_RCTL_ELS_REQ;
		break;

	default:
		goto out;
	}
	fh->fh_type = cp->ic_type;
	net24_put(&fh->fh_d_id, cp->ic_did);
	
	rc = fcs_cmd_send(openfcp->fcs_state, fp, rfp, cp->ic_time_ms, login);
	fp = NULL;
	len = rfp->fr_len;
	if (rc) {
		len = 0;
	} else if (len > 0) {
		if (len > cp->ic_resp_len) {
			rc = -EMSGSIZE;		/* can't happen */
			len = cp->ic_resp_len;
		}
		if (copy_to_user((void __user *) (long) cp->ic_resp_buf, 
					rfp->fr_hdr, len)) {
			rc = -EFAULT;
		}
	}
	cp->ic_resp_len = len;		/* return desired or delivered length */
	if (copy_to_user(_arg, cp, sizeof(*cp)))
		rc = -EFAULT;
out:
	if (rfp)
		fc_frame_free(rfp);
	if (fp)
		fc_frame_free(fp);
	return rc;
}

#ifdef CONFIG_COMPAT

static int do_ioctl(unsigned cmd, unsigned long arg)
{
	int		rc;
	lock_kernel();
	rc = openfc_ioctl(NULL, NULL, cmd, arg);
	unlock_kernel();
	return rc;
}

static long openfc_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long		rc = 0;

	switch (cmd) {
	case OFC_GET_VERSION:
	case OFC_GET_HBA_COUNT:
	case OFC_GET_HBA_INFO:
	case OFC_GET_PORT_INFO:
	case OFC_SET_PORT_CONF:
	case OFC_GET_PORT_STATS:
	case OFC_GET_FC4_STATS:
	case OFC_SEND_CMD:
	case OFC_GET_RNID_DATA:
	case OFC_SET_RNID_DATA:
		rc = do_ioctl(cmd, arg);
		break;
	default:
		rc = -ENOIOCTLCMD;
	}

	return rc;
}
#endif

int openfc_reg_char_dev()
{
	/*
	 * This routine will create the char dev entry and a major number
	 * returns 0 if major number for char dev is greater then -1
	 */
	return register_chrdev(0, OPENFC_DEV_NAME, &openfc_file_ops);
}

void openfc_unreg_char_dev()
{
	/*
	 * This routine will create the char dev entry and a major number
	 * returns 0 if major number for char dev is greater then -1
	 */
	unregister_chrdev(openfc_major, OPENFC_DEV_NAME);
}


int openfc_ioctl_init()
{
	int		rc = 0;

	/*
	 * This routine will create the char dev entry and a major number
	 * returns 0 if major number for char dev is greater then -1
	 */
	openfc_major = openfc_reg_char_dev();
	if (openfc_major < 0) {
		SA_LOG("failed to register the control device\n");
		rc = -ENODEV;
		goto out;
	}
	SA_LOG("Control device /dev/ofc major number %d\n", openfc_major);

	openfc_class = class_create(THIS_MODULE, OPENFC_CLASS_NAME);
	if (IS_ERR(openfc_class)) {
		rc = -ENODEV;
		goto out_chrdev;
	}
	class_device_create(openfc_class, NULL,
			    MKDEV(openfc_major, 0), NULL, OPENFC_DEV_NAME);

	return rc;
      out_chrdev:
	openfc_unreg_char_dev();
      out:
	return rc;
}

int openfc_ioctl_exit()
{
	class_device_destroy(openfc_class, MKDEV(openfc_major, 0));
	class_destroy(openfc_class);
	openfc_unreg_char_dev();
	return 0;
}
