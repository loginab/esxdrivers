/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006-2007 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: openfc_ioctl.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _OPENFC_IOCTL_H_
#define _OPENFC_IOCTL_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include "net_types.h"
#include "fc_els.h"

/*
 * IOCTL command codes for OpenFC.
 *
 * These ioctls use the file /dev/openfc.
 *
 * The user level command will first get the version of the 
 * ioctl interface via the OFC_GET_VERSION ioctl.  It should be greater
 * or equal to OFC_IOCTL_VER.  Hopefully this will be extended compatibly.
 *
 * General usage:
 *
 * OFC_GET_HBA_COUNT returns the number of HBA devices, OFC_GET_HBA_INFO
 * gets the information on a specific HBA, including the number of ports it
 * has.  For FCoE, it can either report itself as one HBA with many ports
 * (preferred), or as several HBAs, each with one port.  
 *
 * OFC_GET_PORT_INFO gets the port-specific information.
 *
 * OFC_SET_PORT_CONF can be used to change port settings, including
 * disabling it temporarily or changing its mode.
 */
#define OFC_DEV_NAME   "/dev/openfc"
#define	OFC_MAGIC	0xfc
#define	OFC_IOCTL_VER	1

#define	OFC_GET_VERSION		_IOR(OFC_MAGIC, 1, int)
#define	OFC_GET_HBA_COUNT	_IOR(OFC_MAGIC, 2, int)
#define	OFC_GET_HBA_INFO	_IOWR(OFC_MAGIC, 3, struct ofc_io_hba_info)
#define	OFC_GET_PORT_INFO	_IOWR(OFC_MAGIC, 4, struct ofc_io_port_info)
#define	OFC_SET_PORT_CONF	_IOWR(OFC_MAGIC, 5, struct ofc_io_port_conf)
#define OFC_GET_PORT_STATS	_IOWR(OFC_MAGIC, 6, struct ofc_io_port_stats)
#define OFC_GET_FC4_STATS	_IOWR(OFC_MAGIC, 7, struct ofc_io_fc4_stats)
#define OFC_SEND_CMD 		_IOWR(OFC_MAGIC, 8, struct ofc_io_cmd)
#define OFC_GET_RNID_DATA	_IOWR(OFC_MAGIC, 9, struct ofc_io_rnid)
#define OFC_SET_RNID_DATA	_IOW(OFC_MAGIC, 10, struct ofc_io_rnid)
#define OFC_EVENT_READ		_IOW(OFC_MAGIC, 11, struct ofc_io_event_read)
#define OFC_EVENT_WAIT		_IOW(OFC_MAGIC, 12, struct ofc_io_event_read)
#define OFC_RLIR_READ		_IOW(OFC_MAGIC, 13, struct ofc_io_event_read)

#define OFC_LNAME_LEN 	256
#define OFC_SNAME_LEN 	64

typedef	char	ofc_sname_t[OFC_SNAME_LEN];	/* name field for HBA model name, etc */
typedef	char	ofc_lname_t[OFC_LNAME_LEN];	/* name filed for HBA desc. etc. */

/*
 * OpenFC HBA stats structure
 *
 * Although some of these counts cannot occur except with real FC HBAs,
 * we include them anyway, for the day when such HBAs use OpenFC.
 */
struct ofc_port_stats {
	uint64_t	ps_sec_since_reset;
	uint64_t	ps_tx_frames;
	uint64_t	ps_tx_words;
	uint64_t	ps_rx_frames;
	uint64_t	ps_rx_words;
	uint64_t	ps_LIP_count;
	uint64_t	ps_NOS_count;
	uint64_t	ps_error_frames;
	uint64_t	ps_dumped_frames;
	uint64_t	ps_link_fails;
	uint64_t	ps_loss_of_sync;
	uint64_t	ps_loss_of_signal;
	uint64_t	ps_primitive_seq_proto_errs;
	uint64_t	ps_invalid_tx_words;
	uint64_t	ps_invalid_CRC_count;
} __attribute__((aligned (8)));

struct ofc_io_port_stats {
	u_int		ps_hba;
	u_int		ps_port;
	struct ofc_port_stats ps_stats;
} __attribute__((aligned (8)));

/*
 * FC4-protocol stats.
 */
struct ofc_fc4_stats {
	uint64_t	fs_sec_since_reset;	/* seconds since reset */
	uint64_t	fs_in_req;		/* input requests */
	uint64_t	fs_out_req;		/* output reqeuests */
	uint64_t	fs_ctl_req;		/* control requests */	
	uint64_t	fs_in_bytes;		/* input bytes */
	uint64_t	fs_out_bytes;		/* output bytes */
} __attribute__((aligned (8)));

struct ofc_io_fc4_stats {
	u_int		fs_hba;
	u_int		fs_port;
	u_int		fs_fc4_type;
	struct ofc_fc4_stats fs_stats;
} __attribute__((aligned (8)));     /* for same size on 64 and 32-bit arch */

/*
 * Structure giving information about an HBA for ioctl OFC_GET_HBA_INFO.
 */
struct ofc_io_hba_info {
	uint32_t	hi_hba;			/* HBA index */
	uint32_t	hi_port_count;		/* number of ports */
	fc_wwn_t	hi_wwnn;		/* WW node name */
	ofc_lname_t	hi_node_sym_name;
	ofc_sname_t	hi_model;
	ofc_sname_t	hi_vendor;
	ofc_sname_t	hi_sn;
	ofc_lname_t	hi_model_desc;
	ofc_lname_t	hi_node;
	ofc_lname_t	hi_hw_vers;
	ofc_lname_t	hi_fw_vers;
	ofc_lname_t	hi_driver_name;
	ofc_lname_t	hi_driver_vers;
	ofc_lname_t	hi_opt_rom_vers;
} __attribute__((aligned (8)));

/*
 * Note: These numbers must match the equivalent HBA-API definitions.
 */
enum ofc_io_port_type {
	OFC_PTYPE_NO_CHANGE = 0,		/* when setting, don't change */
	OFC_PTYPE_UNK = 1,			/* unknown */
	OFC_PTYPE_OTHER = 2,			/* none-of-the-below */
	OFC_PTYPE_NOT_PRES = 3,			/* not present */
	OFC_PTYPE_N = 5,
	OFC_PTYPE_NL = 6,
	OFC_PTYPE_FL = 7,
	OFC_PTYPE_F = 8,
	OFC_PTYPE_E = 9,
	OFC_PTYPE_G = 10,
	OFC_PTYPE_L = 20,
	OFC_PTYPE_PTP = 21,
};

enum ofc_io_port_state {
	OFC_PSTATE_NO_CHANGE = 0,		/* when setting, don't change */
	OFC_PSTATE_UNK = 1,			/* unknown */
	OFC_PSTATE_ONLINE = 2,			/* online */
	OFC_PSTATE_OFFLINE = 3,			/* offline */
	OFC_PSTATE_BYPASS = 4,			/* bypassed */
	OFC_PSTATE_DIAG = 5,			/* diagnostics mode */
	OFC_PSTATE_NOLINK = 6,			/* no link */
	OFC_PSTATE_ERROR = 7,			/* error state */
	OFC_PSTATE_LOOPB = 8,			/* loopback enabled */
};

enum ofc_io_speed {
	OFC_SPEED_UNK = 0,
	OFC_SPEED_1GBIT = 1 << 0,
	OFC_SPEED_2GBIT = 1 << 1,
	OFC_SPEED_10GBIT = 1 << 2,
	OFC_SPEED_4GBIT = 1 << 3,
	OFC_SPEED_8GBIT = 1 << 4,		/* XXX unofficial */
	OFC_SPEED_NOT_NEG = 1 << 15,		/* speed not established */
	OFC_SPEED_AUTO = 1 << 15,		/* auto negotiation */
};

enum ofc_io_port_mode {
	OFC_MODE_INIT = 0,			/* initiator */
	OFC_MODE_TARGET = 1,			/* target */
};

/*
 * Structure giving port attributes and common settings.
 */
struct ofc_io_port_info {
	uint32_t	pi_hba;			/* HBA index */
	uint32_t	pi_port;		/* port index */
	fc_wwn_t	pi_wwnn;		/* WW node name */
	fc_wwn_t	pi_wwpn;		/* WW port name */
	fc_wwn_t	pi_fab_name;		/* fabric name if known */
	fc_fid_t	pi_fcid;		/* FC ID if loogged in */
	enum ofc_io_port_type	pi_port_type;	/* port type in use */
	enum ofc_io_port_state	pi_port_state;
	enum ofc_io_speed pi_speed;		/* mask of current speed */
	enum ofc_io_speed pi_speed_support;	/* mask of supported speeds */
	enum ofc_io_port_mode	pi_port_mode;
	uint32_t	pi_max_frame_size;	/* mfs in bytes */
	uint8_t		pi_class;		/* class of service */
	uint8_t		pi_fc4_support[32];	/* mask of supported types */
	uint8_t		pi_fc4_active[32];	/* mask of active types */
	uint32_t	pi_disc_ports;		/* number of discovered ports */
	ofc_lname_t	pi_os_dev_name;		/* for FCoE, the ethernet i/f */
	ofc_lname_t	pi_port_sym_name;
} __attribute__((aligned (8)));     /* for same size on 64 and 32-bit arch */

/*
 * Structure for setting port configuration.
 */
struct ofc_io_port_conf {
	uint32_t	pc_hba;			/* HBA index */
	uint32_t	pc_port;		/* port index */

	/*
	 * Any of the following fields left set to zero will not cause a change.
	 */
	fc_wwn_t	pi_wwnn;		/* WW node name */
	fc_wwn_t	pi_wwpn;		/* WW port name */
	enum ofc_io_port_type	pc_port_type;	/* new port type */
	enum ofc_io_port_state	pc_port_state;	/* new port state */
	ofc_sname_t	pc_ifname;		/* network interface name */
} __attribute__((aligned (8)));

/*
 * Structure for OFC_SEND_CMD.
 *
 * This ioctl sends a request frame and waits for a response.
 * When the response is received, the structure is copied back with an updated
 * ic_resp_len field.
 * The command and response buffers do not include the FC frame headers.
 * The structure is written back with the actual response length.
 */
struct ofc_io_cmd {
	uint32_t	ic_hba;			/* HBA index */
	uint32_t	ic_port;		/* port index */
	uint32_t	ic_time_ms;		/* time limit in milliseconds */
	uint32_t	_rsrvd;			/* filler */

	uint32_t	ic_type;		/* type (e.g., FC_TYPE_CT) */
	uint32_t	ic_did;			/* destination addr */
	uint32_t	ic_send_len;		/* sending payload length */
	uint32_t	ic_resp_len;		/* response payload length */

	uint64_t	ic_send_buf;		/* sending buffer address */
	uint64_t	ic_resp_buf;		/* response buffer address */
} __attribute__((aligned (8)));

/*
 * Structure for OFC_GET_RNID_DATA and OFC_SET_RNID_DATA (see fc_els.h).
 */
struct ofc_io_rnid {
	uint32_t	ic_hba;			/* HBA index */
	uint32_t	ic_port;		/* port index */
	struct fc_els_rnid_gen ic_rnid;		/* RNID general info */
} __attribute__((aligned (8)));

/*
 * HBA event types.
 * These are used as mask bit numbers and must be in the range 0 - 31.
 */
enum ofc_io_event_type {
	OFC_EV_NONE = 		0,
	OFC_EV_LINK_UP = 	1,
	OFC_EV_LINK_DOWN = 	2,
	OFC_EV_RSCN =		3,
	OFC_EV_HBA_ADD = 	4,
	OFC_EV_HBA_DEL = 	5,
	OFC_EV_HBA_CHANGE = 	6,	/* HBA change - XXX usage unknown */
	OFC_EV_PT_OFFLINE = 	7,
	OFC_EV_PT_ONLINE = 	8,
	OFC_EV_PT_NEW_TARG = 	9,	/* port found new targets */
	OFC_EV_PT_FABRIC = 	10,
	OFC_EV_STAT_THRESH = 	11,	/* stats reached threshold */
	OFC_EV_STAT_GROWTH = 	12,	/* stats growth rate */
	OFC_EV_TARG_OFFLINE =	13,
	OFC_EV_TARG_ONLINE = 	14,
	OFC_EV_TARG_REMOVED = 	15,
	OFC_EV_RLIR =		16,	/* registered link incident report */
};

/*
 * HBA event structure
 */
struct ofc_io_event {
	uint16_t	ev_len;			/* length of entire struct */
	uint16_t	_ev_resvd;		/* reserved */
	enum ofc_io_event_type ev_type;		/* event type */
	uint32_t	ev_hba;			/* HBA index */
	uint32_t	ev_port;		/* port index */
	fc_wwn_t	ev_wwpn;		/* local port WWN */
	fc_fid_t	ev_fid;			/* port FC_ID */
	uint32_t	ev_seq;			/* event sequence number */

	/*
	 * Followed by a variable length piece.
	 */
} __attribute__((aligned (8)));

/*
 * Structure for ioctl OFC_EVENT_WAIT, OFC_EVENT_READ, and OFC_RLIR_READ.
 * Returns the length used or a negative error number.
 */
struct ofc_io_event_read {
	uint32_t	ev_buf_len;	/* length of buffer in / out */
	uint32_t	ev_mask;	/* mask of events to read / present */
	uint64_t	ev_buf;		/* event buffer pointer */
} __attribute__((aligned (8)));

#endif /* _OPENFC_IOCTL_H_ */
