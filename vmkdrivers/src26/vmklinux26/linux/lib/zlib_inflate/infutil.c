/* inflate_util.c -- data and routines common to blocks and codes
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
#include <linux/zutil.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/**
 *  zlib_inflate_blob - initialize zlib, unpack binary blob, clean up zlib 
 *  @gunzip_buf: a pointer to specify next_out in a z_stream where next_out defines next output byte should be 
 *  @sz: remaining free space at next_out  
 *  @buf: a pointer to define next_in in a z_stream where next_in specifies next input byte should be 
 *  @len: the number of bytes available at next_in 
 *
 *  This is a utility function to initialize, unpack, and clean up zlib.  
 *
 *  RETURN VALUE:
 *  len or negative error code 
 *
 */
/* _VMKLNX_CODECHECK_: zlib_inflate_blob */
int zlib_inflate_blob(void *gunzip_buf, unsigned int sz,
                      const void *buf, unsigned int len)
{
        const u8 *zbuf = buf;
        struct z_stream_s *strm;
        int rc;
        
        rc = -ENOMEM;
        strm = kmalloc(sizeof(*strm), GFP_KERNEL);
        if (strm == NULL)
                goto gunzip_nomem1;
        strm->workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
        if (strm->workspace == NULL)
                goto gunzip_nomem2;
        
        /* gzip header (1f,8b,08... 10 bytes total + possible asciz filename)
         * expected to be stripped from input
         */
        strm->next_in = zbuf;
        strm->avail_in = len;
        strm->next_out = gunzip_buf;
        strm->avail_out = sz;
        
        rc = zlib_inflateInit2(strm, -MAX_WBITS);
        if (rc == Z_OK) {
                rc = zlib_inflate(strm, Z_FINISH);
                /* after Z_FINISH, only Z_STREAM_END is "we unpacked it all" */
                if (rc == Z_STREAM_END)
                        rc = sz - strm->avail_out;
                else
                        rc = -EINVAL;
                zlib_inflateEnd(strm);
        } else
                rc = -EINVAL;
        
        kfree(strm->workspace);
 gunzip_nomem2:
        kfree(strm);
 gunzip_nomem1:
        return rc; /* returns Z_OK (0) if successful */
}
#else /* !defined(__COMPAT_LAYER_2_6_18_PLUS__) */
#include <linux/zutil.h>
#include "infblock.h"
#include "inftrees.h"
#include "infcodes.h"
#include "infutil.h"

struct inflate_codes_state;

/* And'ing with mask[n] masks the lower n bits */
uInt zlib_inflate_mask[17] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};


/* copy as much as possible from the sliding window to the output area */
int zlib_inflate_flush(
	inflate_blocks_statef *s,
	z_streamp z,
	int r
)
{
  uInt n;
  Byte *p;
  Byte *q;

  /* local copies of source and destination pointers */
  p = z->next_out;
  q = s->read;

  /* compute number of bytes to copy as far as end of window */
  n = (uInt)((q <= s->write ? s->write : s->end) - q);
  if (n > z->avail_out) n = z->avail_out;
  if (n && r == Z_BUF_ERROR) r = Z_OK;

  /* update counters */
  z->avail_out -= n;
  z->total_out += n;

  /* update check information */
  if (s->checkfn != NULL)
    z->adler = s->check = (*s->checkfn)(s->check, q, n);

  /* copy as far as end of window */
  memcpy(p, q, n);
  p += n;
  q += n;

  /* see if more to copy at beginning of window */
  if (q == s->end)
  {
    /* wrap pointers */
    q = s->window;
    if (s->write == s->end)
      s->write = s->window;

    /* compute bytes to copy */
    n = (uInt)(s->write - q);
    if (n > z->avail_out) n = z->avail_out;
    if (n && r == Z_BUF_ERROR) r = Z_OK;

    /* update counters */
    z->avail_out -= n;
    z->total_out += n;

    /* update check information */
    if (s->checkfn != NULL)
      z->adler = s->check = (*s->checkfn)(s->check, q, n);

    /* copy */
    memcpy(p, q, n);
    p += n;
    q += n;
  }

  /* update pointers */
  z->next_out = p;
  s->read = q;

  /* done */
  return r;
}
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */
