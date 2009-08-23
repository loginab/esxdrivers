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
 * $Id: fc_disc_targ.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_DISC_TARG_H_
#define _LIBFC_DISC_TARG_H_

/*
 * Fibre Channel Target discovery.
 *
 * Returns non-zero if discovery cannot be started.
 *
 * Callback is called for each target remote port found in discovery.
 * When discovery is complete, the callback is called with a NULL remote port.
 */
int fc_disc_targ_start(struct fc_local_port *, u_int fc4_type,
			void (*callback)(void *arg,
				struct fc_remote_port *, enum fc_event),
			void *arg);

/*
 * Registers a callback with discovery 
 */
int fc_disc_targ_register_callback(struct fc_local_port *, u_int fc4_type,
			void (*callback)(void *arg,
				struct fc_remote_port *, enum fc_event),
			void *arg);

int fc_disc_targ_restart(struct fc_local_port *);

void fc_disc_targ_single(struct fc_local_port *, fc_fid_t);
void fc_disc_targ_single_wwpn(struct fc_local_port *, fc_wwn_t, fc_wwn_t);

#endif /* _LIBFC_DISC_TARG_H_ */
