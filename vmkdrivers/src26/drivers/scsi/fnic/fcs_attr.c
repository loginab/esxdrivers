/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fcs_attr.c 18557 2008-09-14 22:36:38Z jre $
 */

/*
 * Attribute handling for fc_exch sysfs variables.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_tcq.h>

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_event.h"
#include "net_types.h"
#include "fc_encaps.h"
#include "fc_types.h"
#include "fc_exch.h"
#include "fc_virt_fab.h"
#include "fc_local_port.h"
#include "fc_print.h"
#include "fcs_state.h"

#include "fcs_state_impl.h"
#include "fc_exch_impl.h"
#include "fc_virt_fab_impl.h"
#include "fc_local_port_impl.h"
#include "fcdev.h"
#include "openfc.h"

static struct fcs_state *fcs_class_device_state(struct class_device *cdev)
{
	struct openfc_softc *openfcp = cdev->class_data;

	return openfcp->fcs_state;
}

static struct fc_virt_fab *fcs_class_device_vf(struct class_device *cdev)
{
	return fcs_class_device_state(cdev)->fs_vf;
}

static struct fc_exch_mgr *fcs_class_device_exch_mgr(struct class_device *
							cdev)
{
	return fcs_class_device_vf(cdev)->vf_exch_mgr;
}

static struct fc_local_port *fcs_class_device_local_port(struct class_device *
							cdev)
{
	return TAILQ_FIRST(&fcs_class_device_vf(cdev)->vf_local_ports);
}

#if 0 /* XXX not used just now */
static ssize_t fcs_exch_show_int(struct class_device *cdev, char *buf,
					u_int offset)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);
	u_int *ip;

	ip = (u_int *) ((char *) mp + offset);
	return snprintf(buf, PAGE_SIZE, "0x%x\n", *ip);
}
#endif /* XXX */

static ssize_t fcs_exch_show_atomic(struct class_device *cdev, char *buf,
					u_int offset)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);
	atomic_t *ap;

	ap = (atomic_t *) ((char *) mp + offset);
	return snprintf(buf, PAGE_SIZE, "%u\n", atomic_read(ap));
}

/*
 * Macro to declare routines showing statistics.
 */
#define FCS_EM_ATTR_STAT(_name) \
static ssize_t fcs_exch_show_##_name(struct class_device *cdev, char *buf) \
{ \
	return fcs_exch_show_atomic(cdev, buf, \
			offsetof(struct fc_exch_mgr, em_stats.ems_##_name)); \
} \
static CLASS_DEVICE_ATTR(_name, S_IRUGO, fcs_exch_show_##_name, NULL);

#define FCS_EM_ATTR_INT(_name) \
static ssize_t fcs_exch_show_##_name(struct class_device *cdev, char *buf) \
{ \
	return fcs_exch_show_int(cdev, buf, \
			offsetof(struct fc_exch_mgr, em_##_name)); \
} \
static CLASS_DEVICE_ATTR(_name, S_IRUGO, fcs_exch_show_##_name, NULL);

FCS_EM_ATTR_STAT(error_no_free_exch)
FCS_EM_ATTR_STAT(error_xid_not_found)
FCS_EM_ATTR_STAT(error_xid_busy)
FCS_EM_ATTR_STAT(error_seq_not_found)
FCS_EM_ATTR_STAT(error_non_bls_resp)
FCS_EM_ATTR_STAT(ex_aborts)
FCS_EM_ATTR_STAT(seq_aborts)
FCS_EM_ATTR_STAT(error_abort_in_prog)
FCS_EM_ATTR_STAT(error_in_rec_qual)

static ssize_t fcs_exch_show_xid_min(struct class_device *cdev, char *buf)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", mp->em_min_xid);
}
static CLASS_DEVICE_ATTR(xid_min, S_IRUGO, fcs_exch_show_xid_min, NULL);

static ssize_t fcs_exch_show_xid_max(struct class_device *cdev, char *buf)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", mp->em_max_xid);
}
static CLASS_DEVICE_ATTR(xid_max, S_IRUGO, fcs_exch_show_xid_max, NULL);

static ssize_t fcs_exch_show_xid_inuse(struct class_device *cdev, char *buf)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);
	struct fc_exch_pool *pp;
	u_int	count = 0;

	for (pp = mp->em_pool; pp < &mp->em_pool[FC_EXCH_POOLS]; pp++)
		count += pp->emp_exch_in_use;
	return snprintf(buf, PAGE_SIZE, "%u\n", count);
}
static CLASS_DEVICE_ATTR(xid_inuse, S_IRUGO, fcs_exch_show_xid_inuse, NULL);

static ssize_t fcs_exch_show_xid_total(struct class_device *cdev, char *buf)
{
	struct fc_exch_mgr *mp = fcs_class_device_exch_mgr(cdev);
	struct fc_exch_pool *pp;
	u_int	count = 0;

	for (pp = mp->em_pool; pp < &mp->em_pool[FC_EXCH_POOLS]; pp++)
		count += pp->emp_exch_total;
	return snprintf(buf, PAGE_SIZE, "%u\n", count);
}
static CLASS_DEVICE_ATTR(xid_total, S_IRUGO, fcs_exch_show_xid_total, NULL);

static struct class_device_attribute *fcs_exch_attrs[] = {
	&class_device_attr_error_no_free_exch,
	&class_device_attr_error_xid_not_found,
	&class_device_attr_error_xid_busy,
	&class_device_attr_error_seq_not_found,
	&class_device_attr_error_non_bls_resp,
	&class_device_attr_ex_aborts,
	&class_device_attr_seq_aborts,
	&class_device_attr_error_abort_in_prog,
	&class_device_attr_error_in_rec_qual,
	&class_device_attr_xid_min,
	&class_device_attr_xid_max,
	&class_device_attr_xid_inuse,
	&class_device_attr_xid_total,
	NULL,
};

struct attribute_group fcs_exch_attr_group = {
	.name = "exch",
	.attrs = (struct attribute **) fcs_exch_attrs,
};

/*
 * Local port attributes.
 */

/*
 * Including WWNs here for now.  Also provided under /sys/class/fc_hosts.
 */
static ssize_t fcs_local_port_show_port_wwn(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "0x%8.8llx\n", lp->fl_port_wwn);
}

static ssize_t fcs_local_port_show_node_wwn(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "0x%8.8llx\n", lp->fl_node_wwn);
}

static ssize_t fcs_local_port_show_e_d_tov(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_e_d_tov);
}

static ssize_t fcs_local_port_show_r_a_tov(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_r_a_tov);
}

static ssize_t fcs_local_port_show_max_payload(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_max_payload);
}

static ssize_t fcs_local_port_show_retry_limit(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_retry_limit);
}

static ssize_t fcs_local_port_show_state(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%s\n", fc_local_port_state(lp));
}

static ssize_t fcs_local_port_show_disc_ver(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_disc_ver);
}

static ssize_t fcs_local_port_show_disc_in_prog(struct class_device *cdev,
					 char *buf)
{
	struct fc_local_port *lp = fcs_class_device_local_port(cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", lp->fl_disc_in_prog);
}

static CLASS_DEVICE_ATTR(port_wwn, S_IRUGO,
				fcs_local_port_show_port_wwn, NULL);
static CLASS_DEVICE_ATTR(node_wwn, S_IRUGO,
				fcs_local_port_show_node_wwn, NULL);
static CLASS_DEVICE_ATTR(e_d_tov, S_IRUGO, fcs_local_port_show_e_d_tov, NULL);
static CLASS_DEVICE_ATTR(r_a_tov, S_IRUGO, fcs_local_port_show_r_a_tov, NULL);
static CLASS_DEVICE_ATTR(max_payload, S_IRUGO,
				fcs_local_port_show_max_payload, NULL);
static CLASS_DEVICE_ATTR(retry_limit, S_IRUGO,
				fcs_local_port_show_retry_limit, NULL);
static CLASS_DEVICE_ATTR(state, S_IRUGO, fcs_local_port_show_state, NULL);
static CLASS_DEVICE_ATTR(disc_ver, S_IRUGO,
				fcs_local_port_show_disc_ver, NULL);
static CLASS_DEVICE_ATTR(disc_in_prog, S_IRUGO,
				fcs_local_port_show_disc_in_prog, NULL);

static struct class_device_attribute *fcs_local_port_attrs[] = {
	&class_device_attr_port_wwn,
	&class_device_attr_node_wwn,
	&class_device_attr_e_d_tov,
	&class_device_attr_r_a_tov,
	&class_device_attr_max_payload,
	&class_device_attr_retry_limit,
	&class_device_attr_state,
	&class_device_attr_disc_ver,
	&class_device_attr_disc_in_prog,
	NULL,
};

struct attribute_group fcs_local_port_attr_group = {
	.name = "local_port",
	.attrs = (struct attribute **) fcs_local_port_attrs,
};
