/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
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
#ident "$Id: palo_desc.h 18557 2008-09-14 22:36:38Z jre $"

#ifndef _PALO_DESC_H_
#define _PALO_DESC_H_

/*
 * Host SG descriptor
 */
#define PALO_MAX_SG_DESC_CNT        1024     // Maximum descriptors per sgl
#define PALO_SG_DESC_ALIGN          16      // Descriptor address alignment
struct host_sg_desc {
    u_int64_t addr;
    u_int32_t len;
    u_int32_t _resvd;
};

static __inline__ void host_sg_desc_enc(struct host_sg_desc *desc,
                                        u_int64_t addr,
                                        u_int32_t len)
{
    desc->addr = addr;
    desc->len = len;
}

static __inline__ void host_sg_desc_dec(struct host_sg_desc *desc,
                                        u_int64_t *addr,
                                        u_int32_t *len)
{
    *addr = desc->addr;
    *len = desc->len;
}

#endif // _PALO_DESC_H_
