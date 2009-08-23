/*
 * Copyright (C) 2003 - 2007 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    licensing@netxen.com
 * NetXen, Inc.
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */
/* Header file for lro definitions */

#ifndef UNM_NIC_LRO_H
#define UNM_NIC_LRO_H


/* Sub-types of the AGGR message
 * These apply to the real MSGTYPE passed between
 *   the aggr/lro and the nic input queue(s) 
 */
#define NX_RX_STAGE1_AGGR_PKT_MSG   1
#define NX_RX_STAGE1_AGGR_RQ_MSG    2
#define NX_RX_STAGE1_AGGR_RSP_MSG   3
#define NX_RX_STAGE1_LRO_PKT_MSG    4
#define NX_RX_STAGE1_LRO_RQ_MSG     5
#define NX_RX_STAGE1_LRO_RSP_MSG    6

#define NX_RX_LRO_RQ_NEW_FLOW   0
#define NX_RX_LRO_RQ_FREE_FLOW  1
#define NX_RX_LRO_RQ_CLEANUP_TIMER  2

/* These are message types meaningful only on
 *   the NIC's stage1 Rx queue 
 */
#define NX_RX_STAGE1_MSGTYPE_FBQ    0x1  /* L2/L4 Miss */
#define NX_RX_STAGE1_MSGTYPE_L2IFQ  0x2  /* L2 Hit, L4 Miss */
#define NX_RX_STAGE1_MSGTYPE_IFQ    0x3  /* L4 Hit */
#define NX_RX_STAGE1_MSGTYPE_CMD    0x4  /* Config */

#define NX_RX_STAGE1_MAX_FLOWS    32

#define NX_NUM_LRO_ENTRIES NX_RX_STAGE1_MAX_FLOWS
#define NX_LRO_CHECK_INTVL 0x7ff

/* TODO : Remove this hardcoding by sending values thru status desc */
#define NX_L2_HDR_BUFFER	32

#define ETH_HDR_SIZE 14

#define IPV4_HDR_SIZE		20
#define IPV6_HDR_SIZE		40
#define TCP_HDR_SIZE		20
#define TCP_TS_OPTION_SIZE	12
#define TCP_IPV4_HDR_SIZE	(TCP_HDR_SIZE + IPV4_HDR_SIZE)
#define TCP_IPV6_HDR_SIZE	(TCP_HDR_SIZE + IPV6_HDR_SIZE)
#define TCP_TS_HDR_SIZE		(TCP_HDR_SIZE + TCP_TS_OPTION_SIZE)
#define MAX_TCP_HDR             64

#define OFFSET_TYPE_TO_SIZE(type) (TCP_IP_HDR_SIZE+(type ? TCP_TS_OPTION_SIZE:0))
#define OFFSET_TYPE_TO_TCP_HDR_SIZE(type) (TCP_HDR_SIZE+(type ? TCP_TS_OPTION_SIZE:0))

#define NX_LRO_ACTUAL_LEN(len) (len&0x8000)?(len&0x7fff):(len+hdr_size+NX_L2_HDR_BUFFER)

typedef struct {
	unm_msg_hdr_t	hdr;
        union {
		struct {
			__uint8_t	command;
			__uint8_t	family;		/* IPv4 or IPv6 */
			__uint16_t	ctx_id;
			__uint16_t	dport;
			__uint16_t	sport;
			ip_addr_t	daddr;
			ip_addr_t	saddr;
			__uint32_t	rss_hash;
			__uint32_t	timestamp;	/* 0 : No TS
							   !0 : TS is expected
							        and the start
								value */
			__uint32_t	qid;	/* Filled in by the FW */
			__uint32_t	rsvd_1;
			__uint64_t	rsvd_2[3];
		} fields;
		__uint64_t	values[7];
	} body;
} nx_rx_lro_rq_msg_t;

typedef struct nx_rx_lro_resp_msg_s {
        union {
		host_peg_msg_hdr_t   rsp_hdr;
		struct {
			__uint16_t	rsvd0_status_desc;
			__uint16_t	sport;
			__uint16_t	dport;
			__uint16_t	rsvd1_status_desc;
		};
	};
	__uint32_t	saddr;
	__uint32_t	daddr;
} nx_rx_lro_resp_msg_t;
#endif
