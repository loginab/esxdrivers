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
 * $Id: openfc_pkt.c 18557 2008-09-14 22:36:38Z jre $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/atomic.h>
#include <linux/mempool.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

/*
 * non linux dot h files
 */
#include "sa_kernel.h"
#include "fc_types.h"
#include "sa_assert.h"
#include "fc_event.h"
#include "fc_port.h"
#include "fc_remote_port.h"
#include "fcdev.h"
#include "fc_fc2.h"
#include "fcoeioctl.h"
#include "fc_frame.h"
#include "fc_exch.h"
#include "fc_sess.h"
#include "fc_event.h"
#include "sa_log.h"
#ifdef FCOE_TARGET_MODE
#include "openfc_target.h"
#define openfc_softc openfchba_softc
#include "openfc_scst_pkt.h"
#else
#include "openfc.h"
#include "openfc_scsi_pkt.h"
#endif
/*
 * function prototypes
 * FC scsi I/O related functions
 */
#define FCOE_PKT_SIZE sizeof (struct fc_scsi_pkt)


/*
 * allocation routine for scsi_pkt packet
 * this is used by upper layer scsi driver
 *
 * Return Value : scsi_pkt structure
 * Context	: call from process context. no locking required
 *
 */
struct fc_scsi_pkt *openfc_alloc_scsi_pkt(struct openfc_softc *openfcp)
{
	struct fc_scsi_pkt *sp;

	ASSERT(openfcp != NULL);

	sp = kmem_cache_alloc(openfcp->openfc_scsi_pkt_cachep, openfcp->alloc_flags);
	if (sp) {
		memset(sp, 0, sizeof(struct fc_scsi_pkt));
		sp->openfcp = (void *) openfcp;
		atomic_set(&sp->ref_cnt, 1);
		if (openfcp->ext_fsp_size)
			sp->private = (void *) (sp + 1);
		init_timer(&sp->timer);
	}
	return sp;
}

/*
 * free routine for scsi_pkt packet
 * this is used by upper layer scsi driver
 *
 * Return Value : zero
 * Context	: call from process  and interrupt context.
 *		  no locking required
 *
 */
int openfc_free_scsi_pkt(struct fc_scsi_pkt *sp)
{
	struct openfc_softc *openfcp = sp->openfcp;

	if (atomic_dec_and_test(&sp->ref_cnt)) {
		if (sp->state == OPENFC_SRB_FREE) {
			kmem_cache_free(openfcp->openfc_scsi_pkt_cachep, sp);
		} else {
			SA_LOG("freeing scsi_pkt not marked free\n");
		}
	}

	return 0;
}
EXPORT_SYMBOL(openfc_free_scsi_pkt);

void openfc_scsi_pkt_hold(struct fc_scsi_pkt *sp)
{
	atomic_inc(&sp->ref_cnt);
	ASSERT(atomic_read(&sp->ref_cnt) != 0);
}

void openfc_scsi_pkt_release(struct fc_scsi_pkt *sp)
{
	openfc_free_scsi_pkt(sp);
}



int openfc_destroy_scsi_slab(struct openfc_softc *openfcp)
{
	int rc = -1;

	if (openfcp->openfc_scsi_pkt_cachep != NULL) {
		kmem_cache_destroy(openfcp->openfc_scsi_pkt_cachep);
		openfcp->openfc_scsi_pkt_cachep = NULL;
		rc = 0;
	}
	return rc;
}
int openfc_create_scsi_slab(struct openfc_softc *openfcp)
{
	ulong slab_flags = SLAB_HWCACHE_ALIGN;

	sprintf(openfcp->scsi_pkt_cachep_name, "nuova_openfc%d", 
		openfcp->host_no);

	if (openfcp->alloc_flags & GFP_DMA)
		slab_flags |= SLAB_CACHE_DMA;

	openfcp->openfc_scsi_pkt_cachep =
	    kmem_cache_create(openfcp->scsi_pkt_cachep_name,
			      sizeof(struct fc_scsi_pkt) + openfcp->ext_fsp_size, 0, slab_flags,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
			      NULL);
#else
			      NULL, NULL);
#endif
	if (openfcp->openfc_scsi_pkt_cachep == NULL) {
		SA_LOG("Unable to allocate SRB cache...module load failed!");
		return -ENOMEM;
	}
	return 0;
}
