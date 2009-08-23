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
 * $Id: fc_fcoe.h 18557 2008-09-14 22:36:38Z jre $
 * 
 */

#ifndef _FC_FCOE_H_
#define	_FC_FCOE_H_

/*
 * FCoE - Fibre Channel over Ethernet.
 */

/*
 * The FCoE ethertype eventually goes in net/if_ether.h.
 */
#ifndef ETH_P_FCOE
#define	ETH_P_FCOE	0x8906		/* FCOE ether type */
#endif

/*
 * This is the T11-approved default FC-MAP OUI.
 * The FCoE standard is not final as of this writing.
 * Note that this is in locally-administered space.
 *
 * FC_FCOE_OUI
 */
#ifndef FC_FCOE_OUI

#define	FC_FCOE_OUI     0x0efc00	/* upper 24 bits of FCOE dest MAC */

#endif /* FC_FCOE_OUI */


/*
 * The destination MAC address for the fabric login.
 */
#ifndef FC_FCOE_FLOGI_MAC

#define	FC_FCOE_FLOGI_MAC 0x0efc00fffffeULL	/* gateway MAC */

#endif /* FC_FCOE_FLOGI_MAC */

#define	FC_FCOE_VER	0			/* version */

/*
 * Ethernet Addresses based on FC S_ID and D_ID.
 * Generated by FC_FCOE_OUI | S_ID/D_ID
 */
#define	FC_FCOE_ENCAPS_ID(n)	(((u_int64_t) FC_FCOE_OUI << 24) | (n))
#define	FC_FCOE_DECAPS_ID(n)	((n) >> 24)

#define	FC_FCOE_DECAPS_SOF(n) \
		(((n) & 0x8) ? (((n) & 0xf) + 0x20) : (((n) & 0xf) + 0x30))

#ifndef FCOE_T11_AUG07				/* old version */

/*
 * Start of frame values.
 * For FCOE the SOF value is encoded in 4 bits by simply trimming the
 * standard RFC 3643 encapsulation values.  See fc/encaps.h.
 *
 * The following macros work for class 3 and class F traffic.
 * It is still required to use net access functions to do the byte swapping.
 *
 * SOF code	Normal	 FCOE
 *  SOFf	0x28	    8
 *  SOFi3	0x2e	    e
 *  SOFn3	0x36	    6
 */
#define	FC_FCOE_ENCAPS_LEN_SOF(len, sof) \
		((FC_FCOE_VER << 14) | (((len) & 0x3ff) << 4) | ((sof) & 0xf))
#define	FC_FCOE_DECAPS_VER(n)	((n) >> 14)
#define	FC_FCOE_DECAPS_LEN(n)	(((n) >> 4) & 0x3ff)

/*
 * FCoE frame header
 * 
 * NB: This is the old version, defined by Cisco/Nuova before August 2007.
 *
 * This follows the VLAN header, which includes the ethertype.
 * The version is the MS 2 bits, followed by the 10-bit length (in 32b words),
 * followed by the 4-bit encoded SOF as the LSBs.
 */
struct fcoe_hdr {
	net16_t		fcoe_plen;	/* fc frame len and SOF */
};

/*
 * FCoE CRC & EOF
 * NB: This is the old version, defined by Cisco/Nuova before August 2007.
 */
struct fcoe_crc_eof {
	u_int32_t	fcoe_crc32;	/* CRC for FC packet */
	net8_t		fcoe_eof;	/* EOF from RFC 3643 */
} __attribute__((packed));

#else /* FCOE_T11_AUG07 */

/*
 * FCoE frame header - 14 bytes
 *
 * This is the August 2007 version of the FCoE header as defined by T11.
 * This follows the VLAN header, which includes the ethertype.
 */
struct fcoe_hdr {
	net8_t		fcoe_ver;	/* version field - upper 4 bits */
	net8_t		fcoe_resvd[12];	/* reserved - send zero and ignore */
	net8_t		fcoe_sof;	/* start of frame per RFC 3643 */
};

#define FC_FCOE_DECAPS_VER(hp)	    ((hp)->fcoe_ver >> 4)
#define FC_FCOE_ENCAPS_VER(hp, ver) ((hp)->fcoe_ver = (ver) << 4)

/*
 * FCoE CRC & EOF - 8 bytes.
 */
struct fcoe_crc_eof {
	u_int32_t	fcoe_crc32;	/* CRC for FC packet */
	net8_t		fcoe_eof;	/* EOF from RFC 3643 */
	net8_t		fcoe_resvd[3];	/* reserved - send zero and ignore */
} __attribute__((packed));

#endif /* FCOE_T11_AUG07 */

#define	FCOE_CRC_LEN	4	/* byte length of the FC CRC */

/*
 * Store OUI + DID into MAC address field.
 */
static inline void fc_fcoe_set_mac(net8_t * mac, net24_t * did)
{
	mac[0] = (net8_t) (FC_FCOE_OUI >> 16);
	mac[1] = (net8_t) (FC_FCOE_OUI >> 8);
	mac[2] = (net8_t) FC_FCOE_OUI;
	*(net24_t *) & mac[3] = *did;
}

/*
 * VLAN header.  This is also defined in linux/if_vlan.h, but for kernels only.
 */
struct fcoe_vlan_hdr {
	net16_t		vlan_tag;	/* VLAN tag including priority */
	net16_t		vlan_ethertype;	/* encapsulated ethertype ETH_P_FCOE */
};

#ifndef ETH_P_8021Q
#define	ETH_P_8021Q	0x8100
#endif

#endif /* _FC_FCOE_H_ */
