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
 * $Id: fc_virt_fab.c 18557 2008-09-14 22:36:38Z jre $
 */

/*
 * Virtual Fabric Support
 *
 * A virtual fabric has lookup tabless for the local_ports,
 * sessions, and remote_ports in the fabric.
 */

#include "sa_kernel.h"
#undef LIST_HEAD
#include "sa_assert.h"
#include "sa_event.h"
#include "net_types.h"

#include "fc_encaps.h"

#include "fc_types.h"
#include "fc_remote_port.h"
#include "fc_local_port.h"
#include "fc_exch.h"
#include "fc_sess.h"
#include "fc_virt_fab.h"
#include "fc_scsi_lun.h"

#include "fc_virt_fab_impl.h"

struct fc_virt_fab *fc_virt_fab_alloc(u_int tag, enum fc_class class,
					fc_xid_t min_xid, fc_xid_t max_xid)
{
	struct fc_virt_fab *vp;

	vp = sa_malloc(sizeof(*vp));
	if (!vp)
		return NULL;
	memset(vp, 0, sizeof(*vp));
	vp->vf_tag = tag;


	if (class != FC_CLASS_NONE) {

		vp->vf_exch_mgr = fc_exch_mgr_alloc(class, min_xid, max_xid);

		if (!vp->vf_exch_mgr)
			goto out_em;
	}
	spin_lock_init(&vp->vf_lock);
	if (fc_sess_table_create(vp))
		goto out_sp;
	if (fc_remote_port_table_create(vp))
		goto out_rp;
	if (fc_local_port_table_create(vp))
		goto out_lp;
	return vp;

out_lp:
	fc_remote_port_table_destroy(vp);
out_rp:
	fc_sess_table_destroy(vp);
out_sp:
	if (vp->vf_exch_mgr)
		fc_exch_mgr_free(vp->vf_exch_mgr);
out_em:
	sa_free(vp);
	return NULL;
}

void fc_virt_fab_free(struct fc_virt_fab *vp)
{

	fc_sess_table_destroy(vp);
	fc_remote_port_table_destroy(vp);
	fc_local_port_table_destroy(vp);
	if (vp->vf_exch_mgr)
		fc_exch_mgr_free(vp->vf_exch_mgr);
	sa_free(vp);
}
