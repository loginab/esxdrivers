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
 * $Id: fc_local_port.h 20630 2008-11-11 23:33:40Z jre $
 */

#ifndef _LIBFC_LOCAL_PORT_H_
#define _LIBFC_LOCAL_PORT_H_

/*
 * Fibre Channel Logical Interfaces.
 *
 * This data type encapsulates the WWPN / WWNN of a port.
 */
#include "sa_event.h"
#include "fc_fs.h"

struct fc_local_port;		/* semi-opaque.  See fc_local_port_impl.h */
struct fc_remote_port;
struct fc_virt_fab;
struct fc_port;
struct fc_exch_mgr;
struct fc_els_flogi;
struct fc_frame;
struct fc_ns_fts;

struct fc_local_port *fc_local_port_create(struct fc_virt_fab *,
					   struct fc_port *,
					   fc_wwn_t wwpn, fc_wwn_t wwnn,
					   u_int timeout_msec,
					   u_int retry_limit);
void fc_local_port_hold(struct fc_local_port *);
void fc_local_port_release(struct fc_local_port *);
void fc_local_port_reset(struct fc_local_port *);
void fc_local_port_quiesce(struct fc_local_port *);
void fc_local_port_destroy(struct fc_local_port *);

/*
 * Set FID for LOCAL_PORT so it needs no fabric login.
 * This might be used only for well-known services.
 */
void fc_local_port_set_fid(struct fc_local_port *, fc_fid_t);
fc_fid_t fc_local_port_get_fid(const struct fc_local_port *);
void fc_local_port_add_fc4_type(struct fc_local_port *, enum fc_fh_type);
void fc_local_port_set_fc4_map(struct fc_local_port *, u_int32_t * map);
const struct fc_ns_fts *fc_local_port_get_fc4_map(struct fc_local_port *);
struct fc_els_rnid_gen *fc_local_port_get_rnidp(struct fc_local_port *);

void fc_local_port_logon(struct fc_local_port *,
			 sa_event_handler_t * callback, void *arg);
void fc_local_port_logoff(struct fc_local_port *);
void fc_local_port_restart(struct fc_local_port *);

void fc_local_port_set_prli_cb(struct fc_local_port *,
			       int (*prli_accept_cb)(struct fc_local_port *,
						     struct fc_remote_port *,
						     void *),
			       void *);

void fc_local_port_set_els_cb(struct fc_local_port *,
			void (*)(void *, u_int, void *, size_t), void *);

/*
 * Add or remove event handlers for the local port.
 */
struct sa_event *fc_local_port_event_enq(struct fc_local_port *,
			     sa_event_handler_t *, void *);
void fc_local_port_event_deq(struct fc_local_port *,
			     sa_event_handler_t *, void *);

/*
 * Receive a frame for a local port.
 */
void fc_local_port_recv(struct fc_local_port *, struct fc_frame *);

/*
 * Internal functions for use only by fc_sess code.
 */
void fc_local_port_flogi_fill(struct fc_local_port *,
			      struct fc_els_flogi *, u_int op);
u_int fc_local_port_get_payload_size(struct fc_els_flogi *, u_int maxval);

/*
 * Return non-zero if the LOCAL_PORT is ready for use (logged in).
 */
int fc_local_port_test_ready(struct fc_local_port *);

/*
 * Return non-zero if the LOCAL_PORT is in the init state without any
 * active sessions opened
 */
int fc_local_port_test_init(struct fc_local_port *);

/*
 * Initialize local port lookups in virtual fabric.
 */
int fc_local_port_table_create(struct fc_virt_fab *);
void fc_local_port_table_destroy(struct fc_virt_fab *);

/*
 * For debugging and /sys only - return state name.
 */
const char *fc_local_port_state(struct fc_local_port *lp);

#endif /* _LIBFC_LOCAL_PORT_H_ */
