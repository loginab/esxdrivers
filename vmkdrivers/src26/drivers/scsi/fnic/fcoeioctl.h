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
 * $Id: fcoeioctl.h 18557 2008-09-14 22:36:38Z jre $
 */
#ifndef _FCOEIOCTL_H_
#define _FCOEIOCTL_H_

#include <linux/ioctl.h>
#include <linux/types.h>


/*
 * ioctl part of the driver structure
 */

/* fcoe_type / mode flags */
#define INITIATOR_MODE		0x01
#define TARGET_MODE 		0x02
#define FCOE_CFG_TYPE_T11 	0x04	/* fcoe_type for T11 protocol */

#ifndef ETH_ALEN
#define ETH_ALEN   6
#endif /* ETH_ALEN */

#ifndef IFNAMSIZ
#define IFNAMSIZ   16
#endif /* IFNAMSIZ */


struct fcoe_cfg {
	fc_wwn_t	wwnn;
	fc_wwn_t	wwpn;
	char		local_ifname[IFNAMSIZ];
	u_int8_t	interface_type;
	u_int8_t	fcoe_type;
	u_int8_t	intr_delay_timer_enable;
	u_int8_t	intr_delay_timer_def_value;
	u_int8_t	login_timeout;
	u_int8_t	login_retry_count;
	u_int8_t	login_down_time;
	u_int8_t	resv0;
	u_int32_t	fcoe_mtu;
	u_int8_t	dest_addr[ETH_ALEN];
	u_int8_t	src_addr[ETH_ALEN];
	u_int8_t	vlan_tag;
	u_int8_t 	rsv[3];
} __attribute__((aligned (8)));

/*
 * Set/get flags information.
 * To get flags, supply zeros for the clr and set values.
 */
struct fcoe_flags {
	u_int32_t	fcf_clr_flags;		/* mask of flags to clear */
	u_int32_t	fcf_set_flags;		/* mask of flags to set */
	char		fcf_ifname[IFNAMSIZ];	/* instance interface */
} __attribute__((aligned (8)));

/*
 * Flags controlled by ioctl.
 */
#define	FCOE_CFL_PFC	(1 << 0)	/* indicates PFC is provided by NIC */
#define FCOE_CFL_RESV	(1 << 31)	/* reserved for error values */

#define FCOE_IOCTL_MAGIC  0xfc
#define FCOE_CREATE_INTERFACE	_IOW(FCOE_IOCTL_MAGIC, 1, struct fcoe_cfg)
#define FCOE_DESTROY_INTERFACE  _IOW(FCOE_IOCTL_MAGIC, 2, struct fcoe_cfg)
#define FCOE_SET_GET_FLAGS	_IOW(FCOE_IOCTL_MAGIC, 3, struct fcoe_flags)
#define FCOE_IOCTL_VERSION	1
#define FCOE_MAXNR  2


#endif /* _FCOEIOCTL_H_ */
