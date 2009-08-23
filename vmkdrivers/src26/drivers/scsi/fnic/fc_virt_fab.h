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
 * $Id: fc_virt_fab.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBFC_VIRT_FAB_H_
#define _LIBFC_VIRT_FAB_H_

#include "fc_encaps.h"

/*
 * Fibre Channel Virtual Fabric.
 * This facility coordinates remote ports and local ports to the same
 * virtual fabric.
 *
 * Struct fc_virt_fab is semi-opaque structure.
 */
struct fc_virt_fab;
struct fc_virt_fab *fc_virt_fab_alloc(u_int tag, enum fc_class,
				      fc_xid_t min_xid, fc_xid_t max_fid);
void fc_virt_fab_free(struct fc_virt_fab *);

/*
 * Default exchange ID limits for user applications.
 */
#define	FC_VF_MIN_XID	0x101
#define	FC_VF_MAX_XID	0x2ff

#endif /* _LIBFC_VIRT_FAB_H_ */
