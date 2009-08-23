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
 * $Id: fc_types.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_TYPES_H_
#define _LIBFC_TYPES_H_

#include "net_types.h"

/*
 * Host-order type definitions for Fibre Channel.
 */

/*
 * Note, in order for fc_wwn_t to be acceptable for %qx format strings,
 * it cannot be declared as uint64_t.
 */
typedef unsigned long long fc_wwn_t;	/* world-wide name */
typedef uint32_t	fc_fid_t;	/* fabric address */
typedef uint16_t	fc_xid_t;	/* exchange ID */

/*
 * Encapsulation / port option flags.
 */
#define	FC_OPT_DEBUG_RX     0x01	/* log debug messages */
#define	FC_OPT_DEBUG_TX     0x02	/* log debug messages */
#define	FC_OPT_DEBUG        (FC_OPT_DEBUG_RX | FC_OPT_DEBUG_TX)
#define	FC_OPT_NO_TX_CRC    0x04	/* don't generate sending CRC */
#define	FC_OPT_NO_RX_CRC    0x08	/* don't check received CRC */
#define	FC_OPT_FCIP_NO_SFS  0x10	/* No special frame (FCIP only) */
#define	FC_OPT_PASSIVE      0x20	/* Responding to connect */
#define	FC_OPT_SET_MAC      0x40	/* use non-standard MAC addr (FCOE) */
#define	FC_OPT_FCOE_OLD     0x80	/* use old prototype FCoE encaps */

/*
 * Convert 48-bit IEEE MAC address to 64-bit FC WWN.
 */
fc_wwn_t fc_wwn_from_mac(u_int64_t, u_int32_t scheme, u_int32_t port);
fc_wwn_t fc_wwn_from_wwn(fc_wwn_t, u_int32_t scheme, u_int32_t port);

#endif /* _LIBFC_TYPES_H_ */
