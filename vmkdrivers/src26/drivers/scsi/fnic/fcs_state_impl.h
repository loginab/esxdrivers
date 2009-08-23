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
 * $Id: fcs_state_impl.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _OPENFC_FCS_STATE_IMPL_H_
#define _OPENFC_FCS_STATE_IMPL_H_

#include "sa_timer.h"

/*
 * Private state structure.
 */
struct fcs_state {
	struct fcs_create_args	fs_args;
	struct fc_virt_fab *fs_vf;		/* virtual fabric (domain) */
	struct fc_local_port *fs_local_port;	/* local port */
	struct fc_port	*fs_inner_port;		/* port used by local port */
	uint8_t		fs_disc_done;		/* discovery complete */
};

void fcs_ev_destroy(void);

struct fc_els_rscn_page;

void fcs_ev_add(struct fcs_state *, u_int, void *, size_t);
void fcs_ev_els(void *, u_int, void *, size_t);

#endif /* _OPENFC_FCS_STATE_IMPL_H_ */
