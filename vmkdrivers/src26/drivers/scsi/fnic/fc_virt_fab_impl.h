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
 * $Id: fc_virt_fab_impl.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_FC_VIRT_FAB_IMPL_H_
#define _LIBFC_FC_VIRT_FAB_IMPL_H_

#include "queue.h"

struct fc_virt_fab {
	uint		vf_tag;		/* virtual fabric tag (or zero) */
	TAILQ_HEAD(, fc_remote_port) vf_remote_ports; /* remote ports */
	struct sa_hash	*vf_rport_by_fid;	/* remote ports by FCID */
	struct sa_hash	*vf_rport_by_wwpn;	/* remote ports by WWPN */
	struct sa_hash	*vf_lport_by_fid;	/* local ports by FCID */
	struct sa_hash	*vf_sess_by_fids;	/* sessions by FCID pairs */
	TAILQ_HEAD(, fc_local_port) vf_local_ports; /* list of local ports */
	struct fc_exch_mgr *vf_exch_mgr;	/* exchange mgr for fabric */
	spinlock_t	vf_lock;	/* lock for all tables and lists */
};

/*
 * Locking code.
 */
static inline void fc_virt_fab_lock(struct fc_virt_fab *vp)
{
	spin_lock_bh(&vp->vf_lock);
}

static inline void fc_virt_fab_unlock(struct fc_virt_fab *vp)
{
	spin_unlock_bh(&vp->vf_lock);
}

#endif /* _LIBFC_FC_VIRT_FAB_IMPL_H_ */
