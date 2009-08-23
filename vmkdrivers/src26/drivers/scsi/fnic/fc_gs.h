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
 * $Id: fc_gs.h 18557 2008-09-14 22:36:38Z jre $
 *
 */

#ifndef _FC_GS_H_
#define	_FC_GS_H_

/*
 * Fibre Channel Services - Common Transport.
 * From T11.org FC-GS-2 Rev 5.3 November 1998.
 */

struct fc_ct_hdr {
	net8_t		ct_rev;		/* revision */
	net24_t		ct_in_id;	/* N_Port ID of original requestor */
	net8_t		ct_fs_type;	/* type of fibre channel service */
	net8_t		ct_fs_subtype;	/* subtype */
	net8_t		ct_options;
	net8_t		_ct_resvd1;
	net16_t		ct_cmd;		/* command / response code */
	net16_t		ct_mr_size;	/* maximum / residual size */
	net8_t		_ct_resvd2;
	net8_t		ct_reason;	/* reject reason */
	net8_t		ct_explan;	/* reason code explanation */
	net8_t		ct_vendor;	/* vendor unique data */
};

#define	FC_CT_HDR_LEN	16	/* expected sizeof (struct fc_ct_hdr) */

enum fc_ct_rev {
	FC_CT_REV = 1		/* common transport revision */
};

/*
 * ct_fs_type values.
 */
enum fc_ct_fs_type {
	FC_FST_ALIAS =	0xf8,	/* alias service */
	FC_FST_MGMT =	0xfa,	/* management service */
	FC_FST_TIME =	0xfb,	/* time service */
	FC_FST_DIR =	0xfc,	/* directory service */
};

/*
 * ct_cmd: Command / response codes
 */
enum fc_ct_cmd {
	FC_FS_RJT =	0x8001,	/* reject */
	FC_FS_ACC =	0x8002,	/* accept */
};

/*
 * FS_RJT reason codes.
 */
enum fc_ct_reason {
	FC_FS_RJT_CMD =		0x01,	/* invalid command code */
	FC_FS_RJT_VER =		0x02,	/* invalid version level */
	FC_FS_RJT_LOG =		0x03,	/* logical error */
	FC_FS_RJT_IUSIZ =	0x04,	/* invalid IU size */
	FC_FS_RJT_BSY =		0x05,	/* logical busy */
	FC_FS_RJT_PROTO =	0x07,	/* protocol error */
	FC_FS_RJT_UNABL =	0x09,	/* unable to perform command request */
	FC_FS_RJT_UNSUP =	0x0b,	/* command not supported */
};

/*
 * FS_RJT reason code explanations.
 */
enum fc_ct_explan {
	FC_FS_EXP_NONE =	0x00,	/* no additional explanation */
	FC_FS_EXP_PID =		0x01,	/* port ID not registered */
	FC_FS_EXP_PNAM =	0x02,	/* port name not registered */
	FC_FS_EXP_NNAM =	0x03,	/* node name not registered */
	FC_FS_EXP_COS =		0x04,	/* class of service not registered */
	/* definitions not complete */
};

#ifdef DEBUG_ASSERTS
/*
 * Static checks for packet structure sizes.
 * These catch some obvious errors in structure definitions.
 * These should generate no code since they can be tested at compile time.
 */
static inline void fc_gs_size_checks(void)
{
	ASSERT_NOTIMPL(sizeof(struct fc_ct_hdr) == FC_CT_HDR_LEN);
}
#endif /* DEBUG_ASSERTS */

#endif /* _FC_GS_H_ */
