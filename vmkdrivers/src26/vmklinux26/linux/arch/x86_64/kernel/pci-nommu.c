/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#if defined(__VMKLNX__)
#include <asm/processor.h>
#else /* !defined(__VMKLNX__) */
#include <asm/proto.h>
#include <asm/processor.h>
#include <asm/dma.h>
#endif /* defined(__VMKLNX__) */

static int
check_addr(char *name, struct device *hwdev, dma_addr_t bus, size_t size)
{
        if (hwdev && bus + size > *hwdev->dma_mask) {
		if (*hwdev->dma_mask >= DMA_32BIT_MASK)
			printk(KERN_ERR
			    "nommu_%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static dma_addr_t
nommu_map_single(struct device *hwdev, void *ptr, size_t size,
	       int direction)
{
	dma_addr_t bus = virt_to_bus(ptr);
#if defined(__VMKLNX__)
	BUG_ON(!check_addr("map_single", hwdev, bus, size));
#else /* !defined(__VMKLNX__) */
	if (!check_addr("map_single", hwdev, bus, size))
				return bad_dma_address;
#endif /* defined(__VMKLNX__) */
	return bus;
}

void nommu_unmap_single(struct device *dev, dma_addr_t addr,size_t size,
			int direction)
{
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int nommu_map_sg(struct device *hwdev, struct scatterlist *sg,
	       int nents, int direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);
#if defined(__VMKLNX__)
	/* we come here only for SG_LINUX type */
	BUG_ON(sg->sg_type != SG_LINUX);
	/*
	 * The linux SG entries coming from upper layers are already initialized in vmklinux
	 * to map to dma addresses - so dma_map_sg do not have to do any mapping here.
	 * But, mapping is needed in case a driver built its own SG list. In such
	 * cases just checking dma_address/dma_length of first sg element tells us
	 * if mapping is needed.
	 */
	if(!sg_dma_address(sg) || !sg_dma_len(sg))
#endif /* defined(__VMKLNX__) */
 	for (i = 0; i < nents; i++ ) {
		struct scatterlist *s = &sg[i];
		BUG_ON(!s->page);
		s->dma_address = virt_to_bus(page_address(s->page) +s->offset);
		if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
			return 0;
		s->dma_length = s->length;
	}
	return nents;
}

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void nommu_unmap_sg(struct device *dev, struct scatterlist *sg,
		  int nents, int dir)
{
}

struct dma_mapping_ops nommu_dma_ops = {
	.map_single = nommu_map_single,
	.unmap_single = nommu_unmap_single,
	.map_sg = nommu_map_sg,
	.unmap_sg = nommu_unmap_sg,
	.is_phys = 1,
};

void __init no_iommu_init(void)
{
	if (dma_ops)
		return;

#if !defined(__VMKLNX__)
	force_iommu = 0; /* no HW IOMMU */
#endif /* !defined(__VMKLNX__) */
	dma_ops = &nommu_dma_ops;
}
