#ifndef _X8664_SCATTERLIST_H
#define _X8664_SCATTERLIST_H

#if defined(__VMKLNX__)
#include "base/vmkapi_memory.h"
enum sg_type { SG_LINUX = 0, SG_VMK = 1, };

struct scatterlist {
    union {
        /* linux sg type */
        struct {
            struct page		*page;
            unsigned int	offset;
            unsigned int	length;
            dma_addr_t		dma_address;
            unsigned int	dma_length;
        };
        /* vmkapi sg type */
        struct {
            vmk_SgElem		*vmksgel; /* always points to the first vmk sgel */
            vmk_SgElem		*cursgel; /* points to the currently used vmk sgel */
        };
    };
    enum sg_type sg_type;	/* tracks type of sg */
};

static inline int valid_sg_type(enum sg_type sg_type)
{
    return ((sg_type == SG_LINUX) || (sg_type == SG_VMK));
}

#else /* !defined(__VMKLNX__) */
struct scatterlist {
    struct page		*page;
    unsigned int	offset;
    unsigned int	length;
    dma_addr_t		dma_address;
    unsigned int        dma_length;
};
#endif /* defined(__VMKLNX__) */

#define ISA_DMA_THRESHOLD (0x00ffffff)

/* These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns.
 */
#if defined(__VMKLNX__)

/**
 *  sg_next - <1 Line Description>
 *  @<arg1>: <first argument description>
 *  @<arg2>: <second argument description>
 *
 *  <full description>
 *
 *  ESX Deviation Notes:
 *  <Describes how ESX implementation is different from standard Linux.>
 *
 */
/* _VMKLNX_CODECHECK_: sg_next */
static inline struct scatterlist * 
sg_next(struct scatterlist *sg)
{
    if(sg->sg_type == SG_LINUX) {
        sg++;
    } else {
        vmk_SgElem *__vmksgel = sg->cursgel;
        __vmksgel++;
        sg->cursgel = __vmksgel;
    }
    return sg;
}

/**
 *  nth_sg - get address of the n'th element in sg array 
 *  @sg: scatterlist
 *  @n: the index of sg element to look for
 *
 *  Get the address of specified sg element from the scatterlist 
 *  provided as input
 *
 *  RETURN VALUE:
 *  Pointer to the specified sg entry in the list
 */
/* _VMKLNX_CODECHECK_: nth_sg */
static inline struct scatterlist *
nth_sg(struct scatterlist *sg, unsigned int n)
{
    if(sg->sg_type == SG_LINUX) {
        sg += n;
    } else {
        vmk_SgElem *__vmksgel = sg->cursgel;
        sg->cursgel = __vmksgel + n;
    }
    return sg;
}

/**
 *  sg_reset - <1 Line Description>
 *  @<arg1>: <first argument description>
 *  @<arg2>: <second argument description>
 *
 *  <full description>
 *
 *  ESX Deviation Notes:
 *  <Describes how ESX implementation is different from standard Linux.>
 *
 */
/* _VMKLNX_CODECHECK_: sg_reset */
static inline void
sg_reset(struct scatterlist *sg)
{
    if(sg->sg_type == SG_VMK) {
        sg->cursgel = sg->vmksgel; 
    }
}

/**
 *  sg_set_dma - <1 Line Description>
 *  @<arg1>: <first argument description>
 *  @<arg2>: <second argument description>
 *
 *  <full description>
 *
 *  ESX Deviation Notes:
 *  <Describes how ESX implementation is different from standard Linux.>
 *
 */
/* _VMKLNX_CODECHECK_: sg_set_dma */
static inline void
sg_set_dma(struct scatterlist *sg, dma_addr_t dma_address, unsigned int dma_length)
{
    BUG_ON(!valid_sg_type(sg->sg_type));

    if(sg->sg_type == SG_LINUX) {
        sg->dma_address = dma_address;
        sg->dma_length = dma_length;
    } else {
        /* vmklinux or drivers are just consumers of vmksg list and usually
         * doesn't need to modify vmkSgElem. But if a case arises we can do
         * something here...
         */
    }
}

/*
 * Instead of deciding the type of sg at run-time as above, here we emit macros
 * at compile-time based on directive VMKLNX_VMKSGARRAY_SUPPORTED.
 * This way we can avoid modifications to drivers where these macros
 * are used as lvalues.
 */
#if defined(VMKLNX_VMKSGARRAY_SUPPORTED)
#define sg_dma_address(sg)	((sg)->cursgel->addr)
#define sg_dma_len(sg)		((sg)->cursgel->length)
#else /* !defined(VMKLNX_VMKSGARRAY_SUPPORTED) */

/**
 *  sg_dma_address - Return the dma_address element of the scatterlist
 *  @sg: the scatterlist
 *
 *  Return the dma_address element of the scatterlist.
 *
 *  SYNOPSIS:
 *   # define sg_dma_address(sg)
 *
 *  RETURN VALUE:
 *  dma_address of the scatterlist
 *
 */
 /* _VMKLNX_CODECHECK_: sg_dma_address */

#define sg_dma_address(sg)	((sg)->dma_address)

/**
 *  sg_dma_len - Return the dma_len element of the scatterlist
 *  @sg: the scatterlist
 *
 *  Return the dma_len element of the scatterlist.
 *
 *  SYNOPSIS:
 *   # define sg_dma_len(sg)
 *
 *  RETURN VALUE:
 *  dma_length of the scatterlist
 *
 */
 /* _VMKLNX_CODECHECK_: sg_dma_len */

#define sg_dma_len(sg)		((sg)->dma_length)
#endif /* defined(VMKLNX_VMKSGARRAY_SUPPORTED) */

#else /* !defined(__VMKLNX__) */
#define sg_dma_address(sg)     ((sg)->dma_address)
#define sg_dma_len(sg)         ((sg)->dma_length)
#endif /* defined(__VMKLNX__) */

#endif 
