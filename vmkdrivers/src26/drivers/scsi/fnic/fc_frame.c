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
 * $Id: fc_frame.c 18557 2008-09-14 22:36:38Z jre $
 */

/*
 * Frame allocation.
 */

#include "sa_kernel.h"
#include "net_types.h"
#include "sa_assert.h"
#include "sa_log.h"
#undef LIST_HEAD		/* name space conflict with queue.h */
#include "queue.h"
#include "fc_fs.h"
#include "fc_types.h"
#include "fc_frame.h"
#include "fc_print.h"
#include "crc32_le.h"


#define FC_FRAME_ACTIVE 0xd00d1234UL	/* clean pattern to catch double free */
#define FC_FRAME_FREE   0xdeadbeefUL	/* dirty pattern to catch double free */


/*
 * Check the CRC in a frame.
 */
u_int32_t fc_frame_crc_check(struct fc_frame *fp)
{
	u_int32_t crc;
	u_int32_t error;
	const u_int8_t *bp;
	u_int len;

	ASSERT_NOTIMPL(fc_frame_is_linear(fp));
	fp->fr_flags &= ~FCPHF_CRC_UNCHECKED;
	len = (fp->fr_len + 3) & ~3;	/* round up length to include fill */
	bp = (const u_int8_t *) fp->fr_hdr;
	crc = ~crc32_sb8_64_bit(~0, bp, len);
	error = crc ^ * (u_int32_t *) (bp + len);
	return (error);
}

struct fc_frame *fc_frame_alloc_fill(struct fc_port *port, size_t payload_len)
{
	struct fc_frame *fp;
	u_int16_t fill;

	fill = payload_len % 4;
	if (fill != 0)
		fill = 4 - fill;
	fp = fc_port_frame_alloc(port, payload_len + fill);
	if (fp) {
		fp->fr_len -= fill;
		memset((char *) fp->fr_hdr + fp->fr_len, 0, fill);
	}
	return fp;
}


#if defined(__VMKLNX__)
static void vmk_frame_free( struct fc_frame *fp )
{
	sa_free(fp);
}
#endif


/*
 * Allocate frame header and buffer.
 * These are currently allocated in a single allocation.
 * The length argument does not include room for the fc_frame_header.
 */

struct fc_frame *fc_frame_alloc_int(size_t len)
{
	struct fc_frame *fp;

	len += sizeof(struct fc_frame_header);
	fp = sa_malloc(len + sizeof(*fp));
	if (fp) {
		memset(fp, 0, sizeof (fp));
		fp->fr_hdr = (struct fc_frame_header *) (fp + 1);
		fp->fr_len = (u_int16_t)len;
#if defined(__VMKLNX__)
		fp->fr_free = vmk_frame_free;
#else /* not __VMKLNX__ */
		fp->fr_free = (void (*)(struct fc_frame *)) kfree;
#endif /* not __VMKLNX__ */
	}
	return fp;
}

/*
 * Callback for freeing a frame allocated staticly.
 * This only marks the frame for debugging and makes it unusable.
 */
void fc_frame_free_static(struct fc_frame *fp)
{
	fp->fr_flags |= FCPHF_FREED;
	fp->fr_hdr = NULL;
	fp->fr_len = 0;
}

EXPORT_SYMBOL(fc_frame_alloc_int);
EXPORT_SYMBOL(fc_frame_free_static);
EXPORT_SYMBOL(fc_frame_crc_check);

