/* **********************************************************
 * Copyright 2007-2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

#include <linux/pci.h>
#include <asm/dma-mapping.h>
#if defined(__x86_64__)
#include <asm/proto.h>
#endif /* defined(__x86_64__) */
#include <linux/dmapool.h>

#include "vmkapi.h"
#include "linux_pci.h"
#include "linux_stubs.h"
#include "vmklinux26_log.h"

struct dma_pool {
   char	 name [32];
   struct device *dev;
   size_t size;
   size_t align;
   vmk_HeapID heapId;
#ifdef VMX86_DEBUG
   u64 coherent_dma_mask;
#endif
};

dma_addr_t bad_dma_address;

void vmklnx_dma_free_coherent_by_ma(struct device *dev, size_t size, dma_addr_t handle);
void vmklnx_dma_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr);
void vmklnx_pci_free_consistent_by_ma(struct pci_dev *hwdev, size_t size, dma_addr_t dma_handle);
void vmklnx_pci_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr);

/**                                          
 *  dma_pool_create - Create a DMA memory pool for use with the given device
 *  @name: descriptive name for the pool
 *  @dev: hardware device to be used with this pool
 *  @size: size of memory blocks for this pool, in bytes
 *  @align: alignment specification for blocks in this pool.  Must 
 *          be a power of 2.
 *  @boundary: boundary constraints for blocks in this pool.  Blocks 
 *             in this pool will not cross this boundary.  Must be a 
 *             power of 2.
 *
 *  Creates an allocation pool of coherent DMA memory.  dma_pool_alloc
 *  and dma_pool_free should be used to allocate and free blocks from this pool,
 *  respectively.  Memory allocated from the pool will have DMA mappings, will 
 *  be accessible by the given device, and will be guaranteed to satisfy the 
 *  given alignment and boundary conditions.  (A boundary parameter of '0' means
 *  that there are no boundary conditions).
 *
 *  RETURN VALUE:  
 *  A pointer to the DMA pool, or NULL on failure.
 *
 *  SEE ALSO:
 *  dma_pool_destroy
 *  
 */                                          
/* _VMKLNX_CODECHECK_: dma_pool_create */
struct dma_pool *
dma_pool_create(const char *name, struct device *dev, size_t size,
   size_t align, size_t boundary)
{
   struct dma_pool *pool;
   vmk_HeapID heapID = VMK_INVALID_HEAP_ID;

   if (align == 0) {
      align = SMP_CACHE_BYTES;
   } else if ((align & (align - 1)) != 0) {
      return NULL;
   }

   if (boundary != 0 && (boundary < size || (boundary & (boundary - 1)) != 0)) {
      return NULL;
   }

   if (size == 0) {
      return NULL;
   } else if (align < size) {
      // guarantee power of 2
      align = size;
      if ((align & (align - 1)) != 0) {
         align = 1 << fls(align);
      }
      VMK_ASSERT(align >= size);
      VMK_ASSERT(align < size * 2);
      VMK_ASSERT((align & (align - 1)) == 0);
   }

   if (!(pool = kmalloc(sizeof *pool, GFP_KERNEL)))
      return pool;

   strlcpy (pool->name, name, sizeof pool->name);
   pool->dev = dev;
   pool->size = size;
   pool->align = align;
   if (dev != NULL) {
      heapID = (vmk_HeapID) dev->dma_mem;
   }
   pool->heapId = (heapID == VMK_INVALID_HEAP_ID) ? Linux_GetModuleHeapID() : heapID;
#ifdef VMX86_DEBUG
   pool->coherent_dma_mask = (dev == NULL) ? DMA_32BIT_MASK : dev->coherent_dma_mask;
#endif

   return pool;
}

/**                                          
 *  dma_pool_destroy - Destroy a DMA pool       
 *  @pool: pool to be destroyed    
 *                                           
 *  Destroys the DMA pool.  Use this only after all memory has been given
 *  back to the pool using dma_pool_free.  The caller must guarantee that
 *  the memory will not be used again.
 * 
 *  ESX Deviation Notes:                                
 *  dma_pool_destroy will not free memory that has been allocated by
 *  dma_pool_alloc.  It is the caller's responsibility to make sure that the
 *  memory has been freed before calling dma_pool_destroy.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *
 *  SEE ALSO:
 *  dma_pool_create, dma_pool_free
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: dma_pool_destroy */
void 
dma_pool_destroy(struct dma_pool *pool)
{
   kfree(pool);
}

/**                                          
 *  dma_pool_alloc - Allocate a block of memory from the dma pool
 *  @pool: pool object from which to allocate
 *  @mem_flags: flags to be used in allocation (see deviation notes)
 *  @handle: output bus address for the allocated memory
 *                               
 *  Allocates a physically and virtually contiguous region of memory having the 
 *  size and alignment characteristics specified for the pool.  The memory is 
 *  set up for DMA with the pool's hardware device.  The memory's virtual 
 *  address is returned, and the bus address is passed back through handle.  If
 *  allocation fails, NULL is returned.
 *                                          
 *  ESX Deviation Notes:                     
 *  mem_flags is ignored on ESX.  The semantics used are those
 *  of mem_flags = GFP_KERNEL.
 *
 *  RETURN VALUE:
 *  Virtual-address of the allocated memory, NULL on allocation failure
 *
 *  SEE ALSO:
 *  dma_pool_free
 *
 */                                          
/* _VMKLNX_CODECHECK_: dma_pool_alloc */
void *
dma_pool_alloc(struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{
   void *va;

   VMK_ASSERT((pool->align & (pool->align - 1)) == 0);
   VMK_ASSERT(pool->align >= pool->size);

   va = vmklnx_kmalloc_align(pool->heapId, pool->size, pool->align);

   if (va) {
      VMK_ASSERT(virt_to_bus(va) < pool->coherent_dma_mask);
      if (handle) {
         *handle = virt_to_bus(va);
      }
   }

   return va;
}

/**                                          
 *  vmklnx_dma_pool_free_by_ma - Give back memory to the DMA pool      
 *  @pool: DMA pool structure    
 *  @addr: machine address of the memory being given back
 *                                           
 *  Frees memory that was allocated by the 
 *  DMA pool, given that memory's machine address.  The memory MUST have
 *  been allocated from a contiguous heap.  In general, this function should
 *  not be used, and instead drivers should track the original virtual address
 *  that was given by dma_pool_alloc, and then use that in dma_pool_free
 *                                           
 *  ESX Deviation Notes:                     
 *  This function does not appear in Linux and is provided for drivers that
 *  expect phys_to_virt() to provide an identity mapping back to an original
 *  memory-allocation address in order to later free it.  phys_to_virt does not
 *  work that way on ESX.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: vmklnx_dma_pool_free_by_ma */
void vmklnx_dma_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr)
{
   vmk_HeapFreeByMachAddr(pool->heapId, addr);
}

/**                                          
 *  vmklnx_pci_pool_free_by_ma - Give back memory to the DMA pool      
 *  @pool: DMA pool structure    
 *  @addr: machine address of the memory being given back
 *                                           
 *  Frees memory that was allocated by the 
 *  DMA pool, given that memory's machine address.  The memory MUST have
 *  been allocated from a contiguous heap.  In general, this function should
 *  not be used, and instead drivers should track the original virtual address
 *  that was given by dma_pool_alloc, and then use that in dma_pool_free
 *                                           
 *  ESX Deviation Notes:                     
 *  This function does not appear in Linux and is provided for drivers that
 *  expect phys_to_virt() to provide an identity mapping back to an original
 *  memory-allocation address in order to later free it.  phys_to_virt does not
 *  work that way on ESX.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: vmklnx_pci_pool_free_by_ma */
void
vmklnx_pci_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr)
{
   vmklnx_dma_pool_free_by_ma(pool, addr);
}

/**                                          
 *  dma_pool_free - Give back memory to the DMA pool       
 *  @pool: DMA pool structure    
 *  @vaddr: virtual address of the memory being returned
 *  @addr: machine address of the memory being returned
 *                                           
 *  Frees memory that was allocated by the DMA pool.  The
 *  virtual address given must match that which was returned originally
 *  by dma_pool_alloc.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: dma_pool_free */
void 
dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr)
{
   VMK_ASSERT(virt_to_phys(vaddr) == addr);
   vmklnx_kfree(pool->heapId, vaddr);
}

/**                                          
 *  dma_alloc_coherent - Allocate memory for DMA use with the given device       
 *  @dev: device to be used in the DMA - may be NULL    
 *  @size: size of the requested memory, in bytes
 *  @handle: Output parameter to hold the bus address for the allocated memory
 *  @gfp: Flags for the memory allocation
*                                           
 *  Allocates a physically contiguous region of memory
 *  and sets up the physical bus connectivity to enable DMA between the
 *  given device and the allocated memory.  The bus address for the allocated
 *  memory is returned in "handle".  This bus address is usable by
 *  the device in DMA operations.  If no device is given (ie, dev = NULL),
 *  dma_alloc_coherent allocates physically contiguous memory that may be
 *  anywhere in the address map, and no specific bus accomodations are made.
 *  Generally this only is usable on systems that do not have an IOMMU.
 *                                           
 *  ESX Deviation Notes:                     
 *  "gfp" is ignored.  dma_alloc_coherent behaves as though "gfp" is always
 *  GFP_KERNEL.  Zone specifiers for gfp are ignored, but the 
 *  dev->coherent_dma_mask is obeyed (see pci_set_consistent_dma_mask).  The 
 *  memory associated with the device (dev->dma_mem) must be suitable for the
 *  requested dma_alloc_coherent allocation.
 *
 *  RETURN VALUE:
 *  Returns a pointer to the allocated memory on success, NULL on failure
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: dma_alloc_coherent */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
   void *ret;
   vmk_HeapID heapID = VMK_INVALID_HEAP_ID;

   /*
    * Preferentially use the coherent dma heap in dev->dma_mem.
    * If its not set, use the module heap.
    *
    * For most drivers, these two are the same heap.  Only drivers
    * with special requirements end up with a distict coherent dma heap.
    */
   if (dev != NULL) {
      heapID = (vmk_HeapID) dev->dma_mem;
   }
   if (heapID == VMK_INVALID_HEAP_ID) {
      heapID = Linux_GetModuleHeapID();
   }

   ret = vmk_HeapAlign(heapID, VMK_PAGE_SIZE << get_order(size), PAGE_SIZE);
   if (ret == NULL) {
      VMKLNX_WARN("Out of memory");
      return 0;
   } else {
      memset(ret, 0, size);
      *handle = virt_to_phys(ret);
      if(dev != NULL) {
         VMK_ASSERT(*handle < dev->coherent_dma_mask);
      }         
   }

   return ret;
}

/**                                          
 *  dma_free_coherent - Free memory that was set up for DMA for the given device       
 *  @dev: device that was used in the DMA   
 *  @size: size of the memory being freed, in bytes   
 *  @vaddr: virtual address of the memory being freed
 *  @handle: bus address of the memory being freed
 *                                           
 *  Frees the given memory and tears down any special bus
 *  connectivity that was needed to make this memory reachable by the given
 *  device.  If the given device is NULL, the memory is just returned to the
 *  module's memory pool.
 *                                           
 *  ESX Deviation Notes:                     
 *  It is valid on some Linux configurations to call dma_free_coherent on 
 *  a subset region of memory that was part of a larger dma_alloc_coherent
 *  allocation.  This is not valid on ESX.  You must free the entire size
 *  (with the base vaddr and handle) that was obtained with dma_alloc_coherent.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: dma_free_coherent */
void dma_free_coherent(struct device *dev, size_t size, void *vaddr, dma_addr_t handle)
{
   vmk_HeapID heapID = VMK_INVALID_HEAP_ID;
   
   VMK_ASSERT(virt_to_phys(vaddr) == handle);
   
   if(dev != NULL) {
      heapID = (vmk_HeapID) dev->dma_mem;
   }

   if (heapID == VMK_INVALID_HEAP_ID) {
        heapID = Linux_GetModuleHeapID();
   }

   vmk_HeapFree(heapID, vaddr);
}

/**                                          
 *  vmklnx_dma_free_coherent_by_ma - Free memory that was set up for DMA for the given device       
 *  @dev: device that was used in the DMA   
 *  @size: size of the memory being freed, in bytes   
 *  @handle: bus address of the memory being freed
 *                                           
 *  Frees the given memory and tears down any special bus
 *  connectivity that was needed to make this memory reachable by the given
 *  device.  If the given device is NULL, the memory is just returned to the
 *  module's memory pool.
 *                                           
 *  ESX Deviation Notes:                     
 *  It is valid on some Linux configurations to call dma_free_coherent on 
 *  a subset region of memory that was part of a larger dma_alloc_coherent
 *  allocation.  This is not valid on ESX.  You must free the entire size
 *  that was obtained with dma_alloc_coherent.
 *  This function does not appear in Linux and is provided for drivers that
 *  expect phys_to_virt() to provide an identity mapping back to an original
 *  memory-allocation address in order to later free it.  phys_to_virt does not 
 *  work that way on ESX.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *
 */                                          
/* _VMKLNX_CODECHECK_: vmklnx_dma_free_coherent_by_ma */
void
vmklnx_dma_free_coherent_by_ma(struct device *dev, size_t size, dma_addr_t handle)
{
   vmk_HeapID heapID = VMK_INVALID_HEAP_ID;
   
   if(dev != NULL) {
      heapID = (vmk_HeapID) dev->dma_mem;
   }
   
   if (heapID == VMK_INVALID_HEAP_ID) {
      heapID = Linux_GetModuleHeapID();
   }
   
   vmk_HeapFreeByMachAddr(heapID, handle);
}

/**                                          
 *  vmklnx_pci_free_consistent_by_ma - Free memory that was set up for DMA for the given device       
 *  @hwdev: PCI device that was used in the DMA   
 *  @size: size of the memory being freed, in bytes   
 *  @handle: bus address of the memory being freed
 *                                           
 *  Frees the given memory and tears down any special bus
 *  connectivity that was needed to make this memory reachable by the given
 *  device.  If the given device is NULL, the memory is just returned to the
 *  module's memory pool.
 *                                           
 *  ESX Deviation Notes:                     
 *  It is valid on some Linux configurations to call dma_free_coherent on 
 *  a subset region of memory that was part of a larger dma_alloc_coherent
 *  allocation.  This is not valid on ESX.  You must free the entire size
 *  that was obtained with dma_alloc_coherent.
 *  This function does not appear in Linux and is provided for drivers that
 *  expect phys_to_virt() to provide an identity mapping back to an original
 *  memory-allocation address in order to later free it.  phys_to_virt does not 
 *  work that way on ESX.
 * 
 *  RETURN VALUE:
 *  Does not return any value
 *
 */                                          
/* _VMKLNX_CODECHECK_: vmklnx_pci_free_consistent_by_ma */
void
vmklnx_pci_free_consistent_by_ma(struct pci_dev *hwdev, size_t size, dma_addr_t handle)
{
   struct device *dev = NULL;
   if (hwdev != NULL) {
      dev = &hwdev->dev;
   }
   vmklnx_dma_free_coherent_by_ma(dev, size, handle);   
}

#if defined(__x86_64__)
struct dma_mapping_ops* dma_ops;

int 
vmklnx_dma_supported(struct vmklnx_codma *codma, struct device *dev, u64 mask)
{
   vmk_HeapID heapID;
   VMK_ReturnStatus status;
   vmk_HeapMemType memType = VMK_HEAP_4GB_MEM;

   if (dma_ops->dma_supported)
      return dma_ops->dma_supported(dev, mask);

   /*
    * Default policy for x86 follows (no IOMMU case).
    *
    * In this case, we really need to allocate low memory if driver/hw
    * says it needs it.  So the only way to answer the "supported" question
    * is to try to createe the heap now.
    */
   VMK_ASSERT(((mask + 1) & mask) == 0); // require (power of 2) - 1

   if (mask < DMA_32BIT_MASK) {
      switch (mask) {
      case DMA_30BIT_MASK:
         memType = VMK_HEAP_1GB_MEM;
         break;
         ;;
      case DMA_31BIT_MASK:
         memType = VMK_HEAP_2GB_MEM;
         break
         ;;
      default:
         VMKLNX_WARN("Unsupported dma requirement for heap %s", codma->heapName);
         return 0;
      }
   }

   down(codma->mutex);

   if (mask < DMA_32BIT_MASK) {
      // driver needs a special low memory heap
      if (codma->mask == DMA_32BIT_MASK) {
         // create the heap
         status = vmk_HeapCreate(codma->heapName,
                                 codma->heapSize,
                                 codma->heapSize,
                                 VMK_HEAP_PHYS_CONTIGUOUS,
                                 memType,
                                 &heapID);
         if (status != VMK_OK) {
            VMKLNX_WARN("No suitable memory available for heap %s", codma->heapName);
            up(codma->mutex);
            return 0;
         }
         codma->heapID = heapID;
         codma->mask = mask;
      } else if (codma->mask > mask) {
         VMKLNX_WARN("Conflicting dma requirements for heap %s", codma->heapName);
         up(codma->mutex);
         return 0;
      }
   }
   dev->dma_mem = (struct dma_coherent_mem *) codma->heapID;

   up(codma->mutex);

   return 1;
}

/**                                          
 *  dma_set_mask - Set a device's allowable DMA mask   
 *  @dev: device whose mask is being set
 *  @mask: address bitmask     
 *                                           
 *  Sets the range in which the device can perform DMA operations.
 *  The mask, when bitwise-ANDed with an arbitrary machine address, expresses
 *  the DMA addressability of the device.  This mask is used by dma_mapping
 *  functions (ie, dma_alloc_coherent, dma_pool_alloc) to guarantee that the
 *  memory allocated is usable by the device.
 *                                           
 *  RETURN VALUE:
 *  0 on success
 *  -EIO if the given mask cannot be used for DMA on the system, or if the
 *  dma_mask has not been previously initialized.
 *  
 */                                          
/* _VMKLNX_CODECHECK_: dma_set_mask */
int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}

void
LinuxDMA_Init()
{
   /*
    * When there are more IOMMUs to support, we need to provide
    * logic here to select which IOMMU implemnetation to initialize. 
    * For now, we only have to deal with the no iommu case.
    */
   no_iommu_init();
}

void
LinuxDMA_Cleanup()
{
}
#endif /* defined(__x86_64__) */
