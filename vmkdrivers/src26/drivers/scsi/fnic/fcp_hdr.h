/*
 * Copyright 2008 Cisco Systems, Inc. All Rights Reserved
 * Copyright 2006 Nuova Systems, Inc. All Rights Reserved
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
#ident "$Id:expand$"

#ifndef _FCP_HDR_H_
#define _FCP_HDR_H_

#include "if_ether.h"
#include "fc_fcoe.h"
#include "fc_fs.h"
#include "fc_encaps.h"

/*
 * Ethernet, FCOE and FC frame header for FCP frames
 */
struct fcp_hdr {
    struct ether_header         eth_hdr;    /* Ethernet header */
    struct fcoe_vlan_hdr        vlan_hdr;   /* FCoE vlan header */
    struct fcoe_hdr             fcoe_hdr;   /* FCoE header */
    struct fc_frame_header      fc_hdr;     /* FC header */
};

#ifndef FCOE_T11_AUG07
#define FCP_HDR_EXP_LEN     44              /* expected length of structure */
#else
#define FCP_HDR_EXP_LEN     56              /* expected length of structure */
#endif /* FCOE_T11_AUG07 */

static inline void
parse_fcoe_hdr(void *packet,
               u_int32_t packet_len,
               struct ether_header **eth_hdr,
               struct fcoe_vlan_hdr **vlan_hdr,
               struct fcoe_hdr **fcoe_hdr,
               struct fc_frame_header **fc_hdr,
               u_int16_t *ether_type,
               void **payload,
               u_int32_t *payload_len,
               struct fcoe_crc_eof **fcoe_crc_eof,
               u_int16_t *ex_id,
               u_int8_t *sof,
               u_int8_t *eof)
{
    u_int16_t len;
    u_int32_t f_ctl;

    *eth_hdr = packet;
    *ether_type = ntohs((*eth_hdr)->ether_type);
    if (*ether_type == ETH_P_8021Q) {
        *vlan_hdr = (struct fcoe_vlan_hdr *)(((unsigned char *) *eth_hdr) +
                    sizeof (**eth_hdr));

        *fcoe_hdr = (struct fcoe_hdr *)(((unsigned char *) *vlan_hdr) +
                    sizeof (**vlan_hdr));
        *ether_type = net16_get(&(*vlan_hdr)->vlan_ethertype);
    } else {
        *vlan_hdr = NULL;
        *fcoe_hdr = (struct fcoe_hdr *)(((unsigned char *) *eth_hdr) +
                    sizeof (**eth_hdr));
    }
    *fc_hdr = (struct fc_frame_header *)(((unsigned char *) *fcoe_hdr) +
                sizeof (**fcoe_hdr));

    *payload = ((unsigned char *) *fc_hdr) + sizeof (**fc_hdr);

    // Determine if the OX_ID (initiator command) or the RX_ID (target command)
    // is the exchange id.
    f_ctl = net24_get(&(*fc_hdr)->fh_f_ctl);
    if (f_ctl & FC_FC_EX_CTX) {
        *ex_id = net16_get(&(*fc_hdr)->fh_ox_id);
    } else {
        *ex_id = net16_get(&(*fc_hdr)->fh_rx_id);
    }

#ifndef FCOE_T11_AUG07
    len = FC_FCOE_DECAPS_LEN(net16_get(&(*fcoe_hdr)->fcoe_plen)) * 4;
#else
    // len is the FC portion from sof to eof, including the crc
    len = (u_int16_t) (packet_len - ((char *) *fc_hdr - (char *) packet) -
          sizeof (struct fcoe_crc_eof) + FCOE_CRC_LEN);
#endif /* FCOE_T11_AUG07 */

    *fcoe_crc_eof = (struct fcoe_crc_eof *)(((unsigned char *) *fc_hdr) +
                    len - FCOE_CRC_LEN);
    *eof = net8_get(&(*fcoe_crc_eof)->fcoe_eof);

    *payload_len = len - FCOE_CRC_LEN - sizeof (**fc_hdr);

#ifndef FCOE_T11_AUG07
    *sof = FC_FCOE_DECAPS_SOF(net16_get(&(*fcoe_hdr)->fcoe_plen));
#else
    *sof = net8_get(&(*fcoe_hdr)->fcoe_sof);
#endif /* FCOE_T11_AUG07 */
}

#endif /* _FCP_HDR_H_ */
