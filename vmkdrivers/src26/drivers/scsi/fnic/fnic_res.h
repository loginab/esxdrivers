/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * [Insert appropriate license here when releasing outside of Cisco]*
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
#ident "$Id: fnic_res.h 20768 2008-11-14 23:26:29Z kanna $"

#ifndef _FNIC_RES_H_
#define _FNIC_RES_H_

#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "fnic_common.h"

#define vnic_fc_config  vnic_scsi_config

static inline void fnic_queue_wq_desc(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	unsigned int fc_eof,
	int vlan_tag_insert, unsigned int vlan_tag,
	int cq_entry, int sop, int eop)
{
	struct wq_enet_desc *desc = vnic_wq_next_desc(wq);

	wq_enet_desc_enc(desc,
		(u64)dma_addr | VNIC_PADDR_TARGET,
		(u16)len,
		0, /* mss_or_csum_offset */
		(u16)fc_eof, 
		0, /* offload_mode */
		(u8)eop, (u8)cq_entry,
		1, /* fcoe_encap */
		(u8)vlan_tag_insert,
		(u16)vlan_tag,
		0 /* loopback */);

	vnic_wq_post(wq, os_buf, dma_addr, len, sop, eop);
}

static __inline void fnic_queue_wq_copy_desc_icmnd_16(struct vnic_wq_copy *wq, 
	u_int32_t req_id,
	u_int32_t lunmap_id, u_int8_t spl_flags,
	u_int32_t sgl_cnt, u_int32_t sense_len,
	u_int64_t sgl_addr, u_int64_t sns_addr, u_int8_t crn, 
	u_int8_t pri_ta, u_int8_t flags, u_int8_t *scsi_cdb, 
	u_int32_t data_len, u_int8_t *lun, u_int32_t d_id, u_int16_t mss,
	u_int32_t ratov, u_int32_t edtov)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_ICMND_16; /* enum fcpio_type */
	desc->hdr.status = 0;            /* header status entry */
	desc->hdr._resvd = 0;            /* reserved */
	desc->hdr.tag.u.req_id = req_id; /* id for this request */

	desc->u.icmnd_16.lunmap_id = lunmap_id; /* index into lunmap table */
	desc->u.icmnd_16.special_req_flags = spl_flags; /* exch req flags */
	desc->u.icmnd_16._resvd0[0] = 0;        /* reserved */
	desc->u.icmnd_16._resvd0[1] = 0;        /* reserved */
	desc->u.icmnd_16._resvd0[2] = 0;        /* reserved */
	desc->u.icmnd_16.sgl_cnt = sgl_cnt;     /* scatter-gather list count */
	desc->u.icmnd_16.sense_len = sense_len; /* sense buffer length */
	desc->u.icmnd_16.sgl_addr = sgl_addr;   /* scatter-gather list addr */
	desc->u.icmnd_16.sense_addr = sns_addr; /* sense buffer address */
	desc->u.icmnd_16.crn = crn;             /* SCSI Command Reference No.*/
	desc->u.icmnd_16.pri_ta = pri_ta; 	/* SCSI Pri & Task attribute */
	desc->u.icmnd_16._resvd1 = 0;           /* reserved: should be 0 */
	desc->u.icmnd_16.flags = flags;         /* command flags */
	memcpy(desc->u.icmnd_16.scsi_cdb, scsi_cdb, CDB_16);    /* SCSI CDB */
	desc->u.icmnd_16.data_len = data_len;   /* length of data expected */
	memcpy(desc->u.icmnd_16.lun, lun, LUN_ADDRESS);  /* LUN address */
	desc->u.icmnd_16._resvd2 = 0;          	/* reserved */
	net24_put(&desc->u.icmnd_16.d_id, d_id);/* FC vNIC only: Target D_ID */
	desc->u.icmnd_16.mss = mss;            	/* FC vNIC only: max burst */
	desc->u.icmnd_16.r_a_tov = ratov; /*FC vNIC only: Res. Alloc Timeout */
	desc->u.icmnd_16.e_d_tov = edtov; /*FC vNIC only: Err Detect Timeout */

	vnic_wq_copy_post(wq);
}

static __inline void fnic_queue_wq_copy_desc_itmf(struct vnic_wq_copy *wq, 
	u_int32_t req_id, u_int32_t lunmap_id, u_int32_t tm_req, 
	u_int32_t tm_id, u_int8_t *lun, u_int32_t d_id, u_int32_t r_a_tov,
	u_int32_t e_d_tov)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_ITMF;     /* enum fcpio_type */
	desc->hdr.status = 0;            /* header status entry */
	desc->hdr._resvd = 0;            /* reserved */
	desc->hdr.tag.u.req_id = req_id; /* id for this request */

	desc->u.itmf.lunmap_id = lunmap_id; /* index into lunmap table */
	desc->u.itmf.tm_req = tm_req;       /* SCSI Task Management request */
	desc->u.itmf.t_tag = tm_id;         /* tag of fcpio to be aborted */
	desc->u.itmf._resvd = 0;            
	memcpy(desc->u.itmf.lun, lun, LUN_ADDRESS);  /* LUN address */
	desc->u.itmf._resvd1 = 0;            
	net24_put(&desc->u.itmf.d_id, d_id);/* FC vNIC only: Target D_ID */
	desc->u.itmf.r_a_tov = r_a_tov;     /* FC vNIC only: R_A_TOV in msec */
	desc->u.itmf.e_d_tov = e_d_tov;     /* FC vNIC only: E_D_TOV in msec */

	vnic_wq_copy_post(wq);
}

static __inline void fnic_queue_wq_copy_desc_flogi_reg(struct vnic_wq_copy *wq, 
	u_int32_t req_id, u_int8_t format, u_int32_t s_id, u_int64_t gw_mac)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_FLOGI_REG;     /* enum fcpio_type */
	desc->hdr.status = 0;                 /* header status entry */
	desc->hdr._resvd = 0;                 /* reserved */
	desc->hdr.tag.u.req_id = req_id;      /* id for this request */

	desc->u.flogi_reg.format = format;    /* gateway / default */
	net24_put(&desc->u.flogi_reg.s_id, s_id); /* FC vNIC only: S_ID */
	net48_put(&desc->u.flogi_reg.gateway_mac, gw_mac); /* DA */

	vnic_wq_copy_post(wq);
}

static __inline void fnic_queue_wq_copy_desc_fw_reset(struct vnic_wq_copy *wq, 
	u_int32_t req_id)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_RESET;     /* enum fcpio_type */
	desc->hdr.status = 0;             /* header status entry */
	desc->hdr._resvd = 0;             /* reserved */
	desc->hdr.tag.u.req_id = req_id;  /* id for this request */

	vnic_wq_copy_post(wq);
}

static __inline void fnic_queue_wq_copy_desc_fw_echo(struct vnic_wq_copy *wq, 
	u_int32_t req_id)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_ECHO;      /* enum fcpio_type */
	desc->hdr.status = 0;             /* header status entry */
	desc->hdr._resvd = 0;             /* reserved */
	desc->hdr.tag.u.req_id = req_id;  /* id for this request */

	vnic_wq_copy_post(wq);
}

static __inline void fnic_queue_wq_copy_desc_lunmap(struct vnic_wq_copy *wq, 
	u_int32_t req_id, u_int64_t lunmap_addr, u_int32_t lunmap_len)
{
	struct fcpio_host_req *desc = vnic_wq_copy_next_desc(wq);

	desc->hdr.type = FCPIO_LUNMAP_REQ;	/* enum fcpio_type */
	desc->hdr.status = 0;			/* header status entry */
	desc->hdr._resvd = 0;			/* reserved */
	desc->hdr.tag.u.req_id = req_id;	/* id for this request */

	desc->u.lunmap_req.addr = lunmap_addr;	/* address of the buffer */
	desc->u.lunmap_req.len = lunmap_len;	/* len of the buffer */

	vnic_wq_copy_post(wq);
}

static inline void fnic_queue_rq_desc(struct vnic_rq *rq,
	void *os_buf, dma_addr_t dma_addr, u16 len)
{
	struct rq_enet_desc *desc = vnic_rq_next_desc(rq);

	rq_enet_desc_enc(desc,
		(u64)dma_addr | VNIC_PADDR_TARGET,
		RQ_ENET_TYPE_ONLY_SOP,
		(u16)len);

	vnic_rq_post(rq, os_buf, 0, dma_addr, len);
}
	

struct fnic;

int fnic_get_vnic_config(struct fnic *);
int fnic_alloc_vnic_resources(struct fnic *);
void fnic_free_vnic_resources(struct fnic *);
int fnic_get_vnic_resources_size(struct fnic *);
void fnic_get_res_counts(struct fnic *);
int fnic_set_nic_cfg(struct fnic *fnic, u8 rss_default_cpu, u8 rss_hash_type,
	u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable, u8 tso_ipid_split_en,
	u8 ig_vlan_strip_en);

#endif /* _FNIC_RES_H_ */
