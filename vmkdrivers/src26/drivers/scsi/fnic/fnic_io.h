/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2008 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fnic_io.h 21988 2008-12-17 23:26:36Z jre $
 */

#ifndef _FNIC_IO_H_
#define _FNIC_IO_H_

#include "fnic_common.h"

/*
 * Place the sense buffer after the used SG list entries in the same mapping.
 */
#ifdef SCSI_SENSE_BUFFERSIZE
#define	FNIC_SENSE_SGES \
		DIV_ROUND_UP(SCSI_SENSE_BUFFERSIZE, sizeof(struct host_sg_desc))
#define FNIC_DFLT_SG_DESC_CNT	(32 - FNIC_SENSE_SGES)
#define FNIC_MAX_SG_DESC_CNT	(PALO_MAX_SG_DESC_CNT - FNIC_SENSE_SGES)

struct fnic_dflt_sgl_list {
	struct host_sg_desc sg_desc[FNIC_DFLT_SG_DESC_CNT];
	u8 sg_sense_pad[SCSI_SENSE_BUFFERSIZE];
};

struct fnic_sgl_list {
	struct host_sg_desc sg_desc[FNIC_MAX_SG_DESC_CNT];
	u8 sg_sense_pad[SCSI_SENSE_BUFFERSIZE];
};
#endif /* SCSI_SENSE_BUFFERSIZE */

enum fnic_sgl_list_type {
	FNIC_SGL_CACHE_DFLT = 0,  /* cache with default size sgl */ 
	FNIC_SGL_CACHE_MAX,       /* cache with max size sgl */
	FNIC_SGL_NUM_CACHES       /* number of sgl caches */
};

enum fnic_io_state {
	FNIC_IO_UNUSED = 0,
	FNIC_IO_CMD_PENDING,
	FNIC_IO_CMD_COMPLETE,
	FNIC_IO_ABTS_PENDING,
	FNIC_IO_ABTS_COMPLETE
};

#define IO_INDEX_INVALID -1
#define IO_INDEX_MASK (BIT(24) - 1)

/* Structure to keep track of IO with firmware */
struct fnic_io_info {
	/* list_head has to be first field */
	struct list_head free_io;               /* link in free io list */
	struct fnic_io_req *io_req;             /* link to io_req state */
	unsigned int indx;                      /* indx in array */
	enum fnic_io_state io_state;            /* tracks io state */
	spinlock_t   io_info_lock;              /* protects io info */
	u_int32_t fw_io_completed:1;            /* fw completed io */
	u_int8_t lr_status;                     /* lun reset status */
	u_int8_t abts_status;                   /* abts status */
};

/* Structure to keep track of an IO that comes down from OpenFC/SCSI*/
struct fnic_io_req {
	struct fc_scsi_pkt *fsp;	       /* pointer to OpenFC fsp */
	struct fnic_io_info *io_info;          /* link to fw io_info */
	struct host_sg_desc *sgl_list;	       /* sgl list */
#if defined(__VMKLNX__)
	void        *sgl_list_alloc;	/* allocator's address for sgl list */
#else
#define sgl_list_alloc sgl_list
#endif /* not __VMKLNX__ */
	enum fnic_sgl_list_type sgl_list_type; /* dflt sgl or max sgl */
	unsigned long sgl_cnt;                 /* number of entires in sgl*/
	dma_addr_t  sgl_list_pa;	       /* dma addr for sgl list */
	struct completion *abts_done;
	struct completion *dr_done;
};

#endif //_FNIC_IO_H_
