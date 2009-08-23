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
 * $Id: fcs_event.c 18557 2008-09-14 22:36:38Z jre $
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include "sa_assert.h"
#include "sa_log.h"
#include "fc_types.h"
#include "fc_frame.h"
#include "fc_port.h"
#include "fc_virt_fab.h"
#include "fc_local_port.h"
#include "fc_remote_port.h"
#include "fc_print.h"
#include "fc_sess.h"
#include "fc_event.h"
#include "fcs_state.h"
#include "fcs_state_impl.h"
#include "fcdev.h"
#include "openfc_ioctl.h"
#include "openfc.h"

#define	FCS_EV_Q_LIMIT	128		/* max length of event queue */

/*
 * Events for HBA-API.
 */
struct fcs_event_queue {
	struct list_head fev_list;	/* list of events to be delivered */
	uint32_t	fev_count;	/* list element count */
	uint32_t	fev_limit;	/* list limit */
	uint32_t	fev_mask;	/* event mask */
	uint32_t	fev_seq;	/* sequence number */
	spinlock_t	fev_lock;	/* lock covering this structure */
	wait_queue_head_t fev_wait;
};

static struct fcs_event_queue fcs_event_queue = {
	.fev_list = LIST_HEAD_INIT(fcs_event_queue.fev_list),
	.fev_limit = FCS_EV_Q_LIMIT,
	.fev_mask = ~0,			/* interested in all events */
	.fev_lock = SPIN_LOCK_UNLOCKED,
	.fev_wait = __WAIT_QUEUE_HEAD_INITIALIZER(fcs_event_queue.fev_wait),
};

/*
 * Elements on the queue.
 */
struct fcs_ev_elem {
	struct list_head ev_list;
	struct ofc_io_event ev_ev;	/* variable-length must be last */
};

/*
 * Get pointer to first entry in event queue.
 */
static struct fcs_ev_elem *fcs_ev_peek(struct fcs_event_queue *qp)
{
	struct fcs_ev_elem *fp = NULL;

	ASSERT(spin_is_locked(&qp->fev_lock));
	if (!list_empty(&qp->fev_list))
		fp = list_entry(qp->fev_list.next, struct fcs_ev_elem, ev_list);
	return fp;
}

/*
 * Get an entry from the queue.
 */
static struct fcs_ev_elem *fcs_ev_dequeue(struct fcs_event_queue *qp)
{
	struct fcs_ev_elem *fp;

	fp = fcs_ev_peek(qp);
	if (fp) {
		list_del(&fp->ev_list);
		qp->fev_count--;
	}
	return fp;
}

void fcs_ev_destroy(void)
{
	struct fcs_event_queue *qp = &fcs_event_queue;
	struct fcs_ev_elem *fp;

	spin_lock_bh(&qp->fev_lock);
	while ((fp = fcs_ev_dequeue(qp)) != NULL)
		kfree(fp);
	spin_unlock_bh(&qp->fev_lock);
}

/*
 * Get queued event reports.
 * Returns the length gotten or a negative error number.
 */
int fcs_ev_get(uint32_t mask, void __user *buf, uint32_t len, int wait_flag)
{
	struct fcs_event_queue *qp = &fcs_event_queue;
	struct ofc_io_event *ep;
	struct fcs_ev_elem *fp;
	int	offset;
	int	rc;

	if (wait_flag && (rc = wait_event_interruptible(qp->fev_wait,
	    !list_empty(&qp->fev_list)) < 0))
		return rc;
	offset = 0;
	while (offset < len) {
		spin_lock_bh(&qp->fev_lock);
		for (;;) {
			fp = fcs_ev_peek(qp);
			if (!fp)
				break;
			if ((mask & (1U << fp->ev_ev.ev_type)) == 0) {
				list_del(&fp->ev_list);
				qp->fev_count--;
				continue;
			}
			if (offset + fp->ev_ev.ev_len > len) {
				fp = NULL;
				break;		/* quit without taking record */
			}
			list_del(&fp->ev_list);	/* delete record from list */
			qp->fev_count--;
			break;
		}
		spin_unlock_bh(&qp->fev_lock);
		if (!fp)
			break;
		ep = &fp->ev_ev;
		if (copy_to_user((char *)buf + offset, ep, ep->ev_len))
			return -EFAULT;
		offset += ep->ev_len;
		kfree(fp);
	}
	wake_up(&qp->fev_wait);
	return offset;
}

/*
 * fcs_ev_add - add event record to the list of pending HBA events.
 * The data pointer may be NULL.  If data is not NULL, len is data length.
 */
void fcs_ev_add(struct fcs_state *sp, u_int et, void *data, size_t len)
{
	struct fcs_event_queue *qp = &fcs_event_queue;
	struct fcs_ev_elem *fp;
	struct ofc_io_event *ep;
	struct openfc_softc *openfcp;

	if ((qp->fev_mask & (1U << et)) == 0)	/* ok without lock */
		return;
	fp = kzalloc(sizeof(*fp) + len, GFP_ATOMIC);
	if (!fp)
		return;
	ep = &fp->ev_ev;
	ep->ev_len = sizeof (*ep) + len;
	ep->ev_type = et;
	if (data)
		memcpy(ep + 1, data, len);
	openfcp = sp->fs_args.fca_cb_arg;
	ep->ev_hba = openfcp->host_no;
	ep->ev_wwpn = openfcp->fd.fd_wwpn;
	ep->ev_fid = fc_local_port_get_fid(sp->fs_local_port);
	ep->ev_seq = ++(qp->fev_seq);

	/*
	 * enqueue the event.
	 */
	spin_lock_bh(&qp->fev_lock);
	list_add_tail(&fp->ev_list, &qp->fev_list);
	if (qp->fev_count++ > qp->fev_limit)
		fcs_ev_dequeue(qp);
	spin_unlock_bh(&qp->fev_lock);
	wake_up(&qp->fev_wait);
}

/*
 * RSCN notification.  The second arg may be NULL if there was no specific
 * data associated with the RSCN.
 */
void fcs_ev_els(void *sp_arg, u_int els_cmd, void *data, size_t len)
{
	switch (els_cmd) {
	case ELS_RSCN:
		fcs_ev_add(sp_arg, OFC_EV_RSCN, data, len);
		fcs_ev_add(sp_arg, OFC_EV_PT_FABRIC, data, len);
		break;
	case ELS_RLIR:
		fcs_ev_add(sp_arg, OFC_EV_RLIR, data, len);
		break;
	}
}
