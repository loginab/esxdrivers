#ifndef _LINUX_SCATTERLIST_H
#define _LINUX_SCATTERLIST_H

/******************************************************************
 * scatterlist.h
 *
 * From linux-2.6.27.10/lib/scatterlist.c:
 *
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 ******************************************************************/

#include <asm/scatterlist.h>
#include <linux/mm.h>
#include <linux/string.h>

static inline void sg_set_buf(struct scatterlist *sg, void *buf,
			      unsigned int buflen)
{
	sg->page = virt_to_page(buf);
	sg->offset = offset_in_page(buf);
	sg->length = buflen;
}

/**                                          
 *  sg_init_one - Initialize a single entry sg list
 *  @sg: SG entry
 *  @buf: virtual address for IO 
 *  @buflen: IO length
 *                                           
 *  Initializes a single entry sg list 
 *                                           
 *  RETURN VALUE:
 *  NONE 
 * 
 */                                          
/* _VMKLNX_CODECHECK_: sg_init_one */
static inline void sg_init_one(struct scatterlist *sg, void *buf,
			       unsigned int buflen)
{
	memset(sg, 0, sizeof(*sg));
	sg_set_buf(sg, buf, buflen);
#if defined(__VMKLNX__)
	sg->sg_type = SG_LINUX;
#endif /* defined(__VMKLNX__) */
}

/**                                          
 *  sg_set_page - Set a scatterlist entry to a given page       
 *  @sg: The scatterlist entry to be set  
 *  @page: The page to be set in the scatterlist entry
 *  @len:  The length of the data for the scatterlist entry
 *  @offset:  The offset into the given page where the data begins
 *                                           
 *  sg_set_page takes the start page structure intended to be used in a
 *  scatterlist entry and marks the scatterlist entry to use that page, starting
 *  at the given offset in the page and having an overall length of 'len'.
 *                                           
 *  ESX Deviation Notes:                     
 *  Linux overloads the low order bits of the page pointer with sg information.
 *  ESX does not.  This interface is provided for compatibility with Linux.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sg_set_page */
static inline void sg_set_page(struct scatterlist *sg, struct page *page,
                               unsigned int len, unsigned int offset)
{
   sg->page   = page;
   sg->offset = offset;
   sg->length = len;
#if defined(__VMKLNX__)
   sg->sg_type = SG_LINUX;
#endif /* defined(__VMKLNX__) */
}

#if defined(__VMKLNX__)
/**
 *  sg_copy_buffer - Copy data between a linear buffer and an SG list
 *  @sgl: SG list
 *  @nents: number of SG entries
 *  @buf: buffer for copying
 *  @buflen: number of bytes to copy
 *  @to_buffer: transfer direction
 *
 *  Copies data between a linear buffer @buf and an SG list @sgl. 
 *  @to_buffer is used to decide source and destination for copying.
 *
 *  ESX Deviation Notes:
 *  Handles both SG_LINUX and SG_VMK types of SG list.
 *  For SG_LINUX type, caller should make sure that SG list dma mapping
 *  is done if needed.
 *
 *  RETURN VALUE:
 *  Returns the number of copied bytes
 *
 */
/* _VMKLNX_CODECHECK_: sg_copy_buffer */
static inline size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents,
				    void *buf, size_t buflen, int to_buffer)
{
	struct scatterlist *sg = sgl;
	unsigned int len, offset;
	void *addr;

	/* do reset to start from first sg element - for SG_VMK type */
	sg_reset(sg);

	for(offset = 0; nents && (offset < buflen); nents--, offset += len) {
		addr = __va(sg_dma_address(sg));
		len = min_t(size_t, sg_dma_len(sg), buflen - offset);

		if (to_buffer) {
			memcpy(buf + offset, addr, len);
		} else {
			memcpy(addr, buf + offset, len);
		}
		sg = sg_next(sg);
	}

	/* do reset again - again for SG_VMK type */
	sg_reset(sg);
	return offset;
}

/**
 *  sg_copy_from_buffer - Copy data from a linear buffer to an SG list
 *  @sgl: SG list
 *  @nents: number of SG entries
 *  @buf: buffer to copy from
 *  @buflen: number of bytes to copy
 *
 *  Copies data from a linear buffer @buf to an SG list @sgl.
 *
 *  ESX Deviation Notes:
 *  Handles both SG_LINUX and SG_VMK types of SG list.
 *  For SG_LINUX type, caller should make sure that SG list dma mapping
 *  is done if needed.
 *
 *  RETURN VALUE:
 *  Returns the number of copied bytes.
 *
 */
/* _VMKLNX_CODECHECK_: sg_copy_from_buffer */
static inline size_t sg_copy_from_buffer(struct scatterlist *sgl, unsigned int nents,
					 void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, 0);
}

/**
 *  sg_copy_to_buffer - Copy data from an SG list to a linear buffer
 *  @sgl: SG list
 *  @nents: number of SG entries
 *  @buf: buffer to copy from
 *  @buflen: number of bytes to copy
 *
 *  Copies data from an SG list @sgl to a linear buffer @buf.
 *
 *  ESX Deviation Notes:
 *  Handles both SG_LINUX and SG_VMK types of SG list.
 *  For SG_LINUX type, caller should make sure that SG list dma mapping
 *  is done if needed.
 *
 *  RETURN VALUE:
 *  Returns the number of copied bytes.
 *
 */
/* _VMKLNX_CODECHECK_: sg_copy_to_buffer */
static inline size_t sg_copy_to_buffer(struct scatterlist *sgl, unsigned int nents,
				       void *buf, size_t buflen)
{
	return sg_copy_buffer(sgl, nents, buf, buflen, 1);
}
#endif /* defined(__VMKLNX__) */

#endif /* _LINUX_SCATTERLIST_H */
