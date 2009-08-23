/* **********************************************************
 * Copyright 2005 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Memory                                                         */ /**
 *
 * \defgroup Memory Memory Management
 * ESX Server has various means for manipulating both machine memory
 * and virtual address space. The interfaces here provide operations for
 * acquiring, manipulating, and managing both the machine memory and 
 * the virtual address space.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_MEMORY_H_
#define _VMKAPI_MEMORY_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_VMNIXMOD
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_heap.h"
#include "base/vmkapi_compiler.h"

/**
 * \brief Number of bits that all of the byte offsets in a page
 *        can occupy in an address
 */
#define VMK_PAGE_SHIFT 12

/**
 * \brief Number of bytes in a page
 */
#define VMK_PAGE_SIZE (1<<VMK_PAGE_SHIFT)

/**
 * \brief Bitmask that corresponds to the bits that all of the byte
 *        offsets in a page can occupy in an address.
 */
#define VMK_PAGE_MASK (VMK_PAGE_SIZE - 1)

/**
 * \brief Guaranteed invalid machine page
 */
#define VMK_INVALID_MACH_PAGE ((vmk_MachPage)(-1))

/**
 * \brief Description of a block of contiguous machine pages.
 */
typedef struct vmk_MachPageRange {
   /** First page in the page range. */
   vmk_MachPage startPage;
   
   /** Total number of pages in the page range */
   vmk_uint32 numPages;
} vmk_MachPageRange;

/*
 ***********************************************************************
 * vmk_CopyFromMachineMem--                                       */ /**
 *
 * \ingroup Memory
 * \brief Copy data from an MA data buffer to a VA data buffer
 *
 * \retval VMK_OK       The copy completed successfully.
 * \retval VMK_FAILURE  The copy failed. Some bytes may have been copied.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus vmk_CopyFromMachineMem(
   void *dataBuffer,
   vmk_MachAddr machineAddress,
   unsigned int dataLen);

/*
 ***********************************************************************
 * vmk_CopyToMachineMem--                                         */ /**
 *
 * \ingroup Memory
 * \brief Copy data from a VA data buffer to an MA data buffer.
 *
 * \retval VMK_OK       The copy completed successfully.
 * \retval VMK_FAILURE  The copy failed. Some bytes may have been copied.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus vmk_CopyToMachineMem(
   vmk_MachAddr machineAddress,
   void *dataBuffer,
   unsigned int dataLen);

/*
 ***********************************************************************
 * vmk_CopyFromUser --                                            */ /**
 *
 * \ingroup Memory
 * \brief Copy memory from a user space application to the VMKernel.
 *
 * \param[in] dest   Copy-to location.
 * \param[in] src    Copy-from location.
 * \param[in] len    Amount to copy.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CopyFromUser(
   vmk_VirtAddr dest,
   vmk_VirtAddr src,
   vmk_size_t len);

/*
 ***********************************************************************
 * vmk_CopyToUser --                                              */ /**
 *
 * \ingroup Memory
 * \brief Copy memory from the VMKernel to a user space application.
 *
 * \param[in] dest   Copy-to location.
 * \param[in] src    Copy-from location.
 * \param[in] len    Amount to copy.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CopyToUser(
   vmk_VirtAddr dest,
   vmk_VirtAddr src,
   vmk_size_t len);


/*
 ***********************************************************************
 * vmk_MapVAUncached --                                           */ /**
 *
 * \ingroup Memory
 * \brief Map the provided machine page ranges to virtual address space, 
 *        but do not cache the entries on the processor TLB.
 *
 * \param[in]  ranges      Array of machine page ranges.
 * \param[in]  numRanges   Number of ranges in the array.
 * \param[out] vaddr       The mapped virtual addr range.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MapVAUncached(
   vmk_MachPageRange *ranges, 
   vmk_uint32 numRanges, 
   vmk_VirtAddr *vaddr);

/*
 ***********************************************************************
 * vmk_MapVA --                                                   */ /**
 *
 * \ingroup Memory
 * \brief Map the provided machine page ranges to virtual address space.
 *
 * \param[in]  ranges      Array of machine page ranges.
 * \param[in]  numRanges   Number of ranges in the array.
 * \param[out] vaddr       Is set to the mapped virtual addr on success.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MapVA(
   vmk_MachPageRange *ranges, 
   vmk_uint32 numRanges, 
   vmk_VirtAddr *vaddr);

/*
 ***********************************************************************
 * vmk_UnmapVA --                                                 */ /**
 *
 * \ingroup Memory
 * \brief Unmap virtual address space mapped by vmk_MapVA* functions.
 * 
 * The  address that is passed in must be within the first page of the
 * mapped region.
 *
 * \param[in] vaddr  Virtual address to unmap.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_UnmapVA(
   vmk_VirtAddr vaddr);

/*
 ***********************************************************************
 * vmk_MachAddrToVirtAddr --                                      */ /**
 *
 * \ingroup Memory
 * \brief Get a virtual address mapping for the supplied machine address
 *
 * \param[in]  maddr Machine address to resolve
 * \param[out] vaddr A virtual address mapped to that machine address
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MachAddrToVirtAddr(
   vmk_MachAddr maddr,
   vmk_VirtAddr *vaddr);

/*
 ***********************************************************************
 * vmk_VirtAddrToMachAddr --                                      */ /**
 *
 * \ingroup Memory
 * \brief Get the machine address backing the supplied virtual address
 *
 * \param[in]  vaddr Virtual address to resolve
 * \param[out] maddr The backing machine address
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_VirtAddrToMachAddr(
   vmk_VirtAddr vaddr,
   vmk_MachAddr *maddr);

/*
 ***********************************************************************
 * vmk_AssertMemorySupportsIO --                                  */ /**
 *
 * \ingroup Memory
 * \brief Panic the system if a region of machine memory does not
 *        support IO.
 *
 * \note This only performs a check on debug builds.
 * 
 * \param[in] maddr Starting machine address of the range to check
 * \param[in] len   Length of the range to check in bytes
 *
 ***********************************************************************
 */
#ifdef VMX86_DEBUG
void vmk_AssertMemorySupportsIO(
   vmk_MachAddr maddr,
   vmk_size_t len);
#else
static inline void vmk_AssertMemorySupportsIO(
   vmk_MachAddr maddr,
   vmk_size_t len) {}
#endif

/*
 ***********************************************************************
 * vmk_GetLastValidMachPage --                                    */ /**
 *
 * \ingroup Memory
 * \brief Get the last valid machine page in the system.
 * 
 * \return Value of the last valid machine page in the system.
 *
 ***********************************************************************
 */
vmk_MachPage vmk_GetLastValidMachPage(void);

/*
 ***********************************************************************
 * vmk_IsLowMachAddr --                                           */ /**
 *
 * \ingroup Memory
 * \brief Check whether a machine address is in low memory.
 * 
 * \param[in] maddr  Machine address to check
 *
 * \retval VMK_TRUE  Supplied maddr falls within low memory range.
 * \retval VMK_FALSE Supplied maddr does not fall within the low memory
 *                   range.
 *
 ***********************************************************************
 */
vmk_Bool vmk_IsLowMachAddr(
   vmk_MachAddr maddr);

/*
 ***********************************************************************
 * vmk_MachAddrToMachPage --                                      */ /**
 *
 * \ingroup Memory
 * \brief Get the machine page associated with specified machine addr.
 * 
 * \param[in] maddr  Machine address to look up.
 *
 * \return Machine page associated with maddr.
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE
vmk_MachPage vmk_MachAddrToMachPage(vmk_MachAddr maddr)
{
    return (vmk_MachPage)(maddr >> VMK_PAGE_SHIFT);
}

/*
 ***********************************************************************
 * vmk_MachPageToMachAddr --                                      */ /**
 *
 * \ingroup Memory
 * \brief Get the machine addr for the specified machine page.
 *
 * \param[in] mpage  Machine page to look up
 *
 * \return Machine address for the specified mpage
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE vmk_MachAddr
vmk_MachPageToMachAddr(vmk_MachPage mpage)
{
    return ((vmk_MachAddr)mpage << VMK_PAGE_SHIFT);
}

#endif
/** @} */
