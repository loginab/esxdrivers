/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006, 2007 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: fc_fcp.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _FC_FCP_H_
#define	_FC_FCP_H_

/*
 * Fibre Channel Protocol for SCSI.
 * From T10 FCP-3, T10 project 1560-D Rev 4, Sept. 13, 2005.
 */

/*
 * fc/fs.h defines FC_TYPE_FCP.
 */

/*
 * Service parameter page parameters (word 3 bits) for Process Login.
 */
#define	FCP_SPPF_TASK_RETRY_ID	0x0200	/* task retry ID requested */
#define	FCP_SPPF_RETRY		0x0100	/* retry supported */
#define	FCP_SPPF_CONF_COMPL	0x0080	/* confirmed completion allowed */
#define	FCP_SPPF_OVLY_ALLOW	0x0040	/* data overlay allowed */
#define	FCP_SPPF_INIT_FCN	0x0020	/* initiator function */
#define	FCP_SPPF_TARG_FCN	0x0010	/* target function */
#define	FCP_SPPF_RD_XRDY_DIS	0x0002	/* disable XFER_RDY for reads */
#define	FCP_SPPF_WR_XRDY_DIS	0x0001	/* disable XFER_RDY for writes */

/*
 * FCP_CMND IU Payload.
 */
struct fcp_cmnd {
	net8_t		fc_lun[8];	/* logical unit number */
	net8_t		fc_cmdref;	/* commmand reference number */
	net8_t		fc_pri_ta;	/* priority and task attribute */
	net8_t		fc_tm_flags;	/* task management flags */
	net8_t		fc_flags;	/* additional len & flags */
	net8_t		fc_cdb[16];	/* base CDB */
	net32_t		fc_dl;		/* data length (must follow fc_cdb) */
};

#define	FCP_CMND_LEN	32	/* expected length of structure */

struct fcp_cmnd32 {
	net8_t		fc_lun[8];	/* logical unit number */
	net8_t		fc_cmdref;	/* commmand reference number */
	net8_t		fc_pri_ta;	/* priority and task attribute */
	net8_t		fc_tm_flags;	/* task management flags */
	net8_t		fc_flags;	/* additional len & flags */
	net8_t		fc_cdb[32];	/* base CDB */
	net32_t		fc_dl;		/* data length (must follow fc_cdb) */
};

#define	FCP_CMND32_LEN	    48	/* expected length of structure */
#define	FCP_CMND32_ADD_LEN  (16 / 4)	/* Additional cdb length */

/*
 * fc_pri_ta.
 */
#define	FCP_PTA_SIMPLE	    0	/* simple task attribute */
#define	FCP_PTA_HEADQ	    1	/* head of queue task attribute */
#define	FCP_PTA_ORDERED     2	/* ordered task attribute */
#define	FCP_PTA_ACA	    4	/* auto. contigent allegiance */
#define	FCP_PRI_SHIFT	    3	/* priority field starts in bit 3 */
#define	FCP_PRI_RESVD_MASK  0x80	/* reserved bits in priority field */

/*
 * fc_tm_flags - task management flags field.
 */
#define	FCP_TMF_CLR_ACA		0x40	/* clear ACA condition */
#define	FCP_TMF_LUN_RESET	0x10	/* logical unit reset task management */
#define	FCP_TMF_CLR_TASK_SET	0x04	/* clear task set */
#define	FCP_TMF_ABT_TASK_SET	0x02	/* abort task set */

/*
 * fc_flags.
 *  Bits 7:2 are the additional FCP_CDB length / 4.
 */
#define	FCP_CFL_LEN_MASK	0xfc	/* mask for additional length */
#define	FCP_CFL_LEN_SHIFT	2	/* shift bits for additional length */
#define	FCP_CFL_RDDATA		0x02	/* read data */
#define	FCP_CFL_WRDATA		0x01	/* write data */

/*
 * FCP_TXRDY IU - transfer ready payload.
 */
struct fcp_txrdy {
	net32_t		ft_data_ro;	/* data relative offset */
	net32_t		ft_burst_len;	/* burst length */
	net8_t		_ft_resvd[4];	/* reserved */
};

#define	FCP_TXRDY_LEN	12	/* expected length of structure */

/*
 * FCP_RESP IU - response payload.
 * 
 * The response payload comes in three parts: the flags/status, the 
 * sense/response lengths and the sense data/response info section.
 *
 * From FCP3r04, note 6 of section 9.5.13: 
 *
 * Some early implementations presented the FCP_RSP IU without the FCP_RESID,
 * FCP_SNS_LEN, and FCP_RSP_LEN fields if the FCP_RESID_UNDER, FCP_RESID_OVER,
 * FCP_SNS_LEN_VALID, and FCP_RSP_LEN_VALID bits were all set to zero. This
 * non-standard behavior should be tolerated.
 *
 * All response frames will always contain the fcp_resp template.  Some
 * will also include the fcp_resp_len template.
 */
struct fcp_resp {
	net8_t		_fr_resvd[8];	/* reserved */
	net16_t		fr_retry_delay;	/* retry delay timer */
	net8_t		fr_flags;	/* flags */
	net8_t		fr_status;	/* SCSI status code */
};

#define	FCP_RESP_LEN	12	/* expected length of structure */

struct fcp_resp_ext {
	net32_t		fr_resid;	/* Residual value */
	net32_t		fr_sns_len;	/* SCSI Sense length */
	net32_t		fr_rsp_len;	/* Response Info length */

	/*
	 * Optionally followed by RSP info and/or SNS info and/or
	 * bidirectional read residual length, if any.
	 */
};

#define FCP_RESP_EXT_LEN    12  /* expected length of the structure */

struct fcp_resp_rsp_info {
    net8_t      _fr_resvd[3];       /* reserved */
    net8_t      rsp_code;           /* Response Info Code */
    net8_t      _fr_resvd2[4];      /* reserved */
};

struct fcp_resp_with_ext {
	struct fcp_resp resp;
	struct fcp_resp_ext ext;
};

#define	FCP_RESP_WITH_EXT   (FCP_RESP_LEN + FCP_RESP_EXT_LEN)

/*
 * fr_flags.
 */
#define	FCP_BIDI_RSP	    0x80	/* bidirectional read response */
#define	FCP_BIDI_READ_UNDER 0x40	/* bidir. read less than requested */
#define	FCP_BIDI_READ_OVER  0x20	/* DL insufficient for full transfer */
#define	FCP_CONF_REQ	    0x10	/* confirmation requested */
#define	FCP_RESID_UNDER     0x08	/* transfer shorter than expected */
#define	FCP_RESID_OVER	    0x04	/* DL insufficient for full transfer */
#define	FCP_SNS_LEN_VAL     0x02	/* SNS_LEN field is valid */
#define	FCP_RSP_LEN_VAL     0x01	/* RSP_LEN field is valid */

/*
 * rsp_codes
 */
enum fcp_resp_rsp_codes {
	FCP_TMF_CMPL = 0,
	FCP_DATA_LEN_INVALID = 1,
	FCP_CMND_FIELDS_INVALID = 2,
	FCP_DATA_PARAM_MISMATCH = 3,
	FCP_TMF_REJECTED = 4,
	FCP_TMF_FAILED = 5,
	FCP_TMF_INVALID_LUN = 9,
};

/*
 * FCP SRR Link Service request - Sequence Retransmission Request.
 */
struct fcp_srr {
	net8_t		srr_op;		/* opcode ELS_SRR */
	net8_t		srr_resvd[3];	/* opcode / reserved - must be zero */
	net16_t		srr_ox_id;	/* OX_ID of failed command */
	net16_t		srr_rx_id;	/* RX_ID of failed command */
	net32_t		srr_rel_off;	/* relative offset */
	net8_t		srr_r_ctl;	/* r_ctl for the information unit */
	net8_t		srr_resvd2[3];	/* reserved */
};

#endif /* _FC_FCP_H_ */
