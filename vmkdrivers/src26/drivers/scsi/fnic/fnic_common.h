/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * [Insert appropriate license here when releasing outside of Cisco]
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
 */
#ident "$Id: fnic_common.h 18383 2008-09-11 03:53:39Z jre $"

#ifndef _FNIC_COMMON_H_
#define _FNIC_COMMON_H_

#include "net_types.h"
#include "palo_desc.h"
#include "fcpio.h"

#include "vnic_wq_copy.h"
#include "vnic_cq_copy.h"

#include "queue.h"

#define FNIC_WQ_MAX 1
#define FNIC_RQ_MAX 1
#define FNIC_CQ_MAX (FNIC_WQ_MAX + VNIC_WQ_COPY_MAX + FNIC_RQ_MAX)

#define FNIC_LEGACY_INTR 0
#define FNIC_WQ 0
#define FNIC_RQ 0

#define FNIC_MSIX_MAX (FNIC_CQ_MAX + 1)

enum fnic_cmd_state {
	FNIC_CMD_UNUSED,
	FNIC_CMD_BUILT,
	FNIC_CMD_IO_SENT,
	FNIC_CMD_ITMF_SENT,
	FNIC_CMD_COMPLETE,
};

struct fnic_lunmap_info {
	unsigned int lunmap_initialized:1;
	unsigned int resvd:31;
	struct fcpio_lunmap_tbl *active;
	struct fcpio_lunmap_tbl *stby;
	dma_addr_t stby_pa;
	dma_addr_t active_pa;
	unsigned int num_updates;
};

#endif /* _FNIC_COMMON_H_ */
