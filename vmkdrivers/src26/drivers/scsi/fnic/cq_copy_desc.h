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
#ident "$Id: cq_copy_desc.h 18893 2008-09-25 02:03:16Z gsapozhnikov $"

#ifndef _CQ_COPY_DESC_H_
#define _CQ_COPY_DESC_H_

#include "cq_desc.h"

/* Copy completion queue descriptor: 16B */
struct cq_copy_wq_desc {
    u16 completed_index;
    u16 q_number;
    u8 reserved[11];
    u8 type_color;
};

static __inline__ void cq_copy_wq_desc_dec(struct cq_copy_wq_desc *desc_ptr,
                                           u_int8_t *type,
                                           u_int8_t *color,
                                           u_int16_t *q_number,
                                           u_int16_t *completed_index)
{
    cq_desc_dec((struct cq_desc *)desc_ptr, type,
      color, q_number, completed_index);
}

#endif // _CQ_COPY_DESC_H_
