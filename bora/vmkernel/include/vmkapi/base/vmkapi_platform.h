/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Platform                                                       */ /**
 * \defgroup Platform Platform
 *
 * Interfaces relating to the underlying platform.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_PLATFORM_H_
#define _VMKAPI_PLATFORM_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_assert.h"
#include "base/vmkapi_compiler.h"
#include "base/vmkapi_const.h"

/** \brief Size of an L1 cacheline */
#define VMK_L1_CACHELINE_SIZE       64

/** \brief Type that is large enough to store CPU flags */
typedef vmk_uintptr_t vmk_CPUFlags;

/** \brief A set of CPUs */
typedef struct vmk_AffinityMaskInt *vmk_AffinityMask;

/** \brief Affinity mask returned if a new mask cannot be created */
#define VMK_INVALID_AFFINITY_MASK   ((vmk_AffinityMask)NULL)

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_L1_ALIGNED --                                    */ /**
 * \ingroup Compiler
 *
 * \brief Indicate to the compiler that a data structure should be
 *        aligned on an L1 cacheline boundary.
 *
 ***********************************************************************
 */
#define VMK_ATTRIBUTE_L1_ALIGNED VMK_ATTRIBUTE_ALIGN(VMK_L1_CACHELINE_SIZE)

/*
 ***********************************************************************
 * vmk_NumPCPUs --                                                */ /**
 *
 * \ingroup Platform
 * \brief Return vmkernels numPCPUs global.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_NumPCPUs(void);

/*
 ***********************************************************************
 * vmk_GetPCPUNum --                                              */ /**
 *
 * \ingroup Platform
 * \brief Return the PCPU we're currently executing on.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_GetPCPUNum(void);

/*
 ***********************************************************************
 * vmk_AffinityMaskCreate --                                      */ /**
 *
 * \ingroup Platform
 * \brief Allocate zeroed out affinity bitmask object.
 *
 * \note Requires that the module heap be initialized.
 *
 * \param[in] module    The module whose heap will be used to allocate
 *                      the affinity mask.
 *
 * \return Allocated affinity mask.
 *
 ***********************************************************************
 */
vmk_AffinityMask vmk_AffinityMaskCreate(
   vmk_ModuleID module);

/*
 ***********************************************************************
 * vmk_AffinityMaskDestroy --                                     */ /**
 *
 * \ingroup Platform
 * \brief Free affinity bitmask object.
 *
 * \param[in] affinityMask    Affinity mask to be freed.
 *
 ***********************************************************************
 */
void vmk_AffinityMaskDestroy(
   vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_AffinityMaskAdd --                                         */ /**
 *
 * \ingroup Platform
 * \brief Add a CPU to an affinity bitmask.
 *
 * \param[in]  cpuNum         Index of the CPU, starting at 0.
 * \param[out] affinityMask   The affinity mask to be modified.
 *
 ***********************************************************************
 */
void vmk_AffinityMaskAdd(
   vmk_uint32 cpuNum,
   vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_AffinityMaskDel --                                         */ /**
 *
 * \ingroup Platform
 * \brief Delete a CPU from an affinity bitmask.
 *
 * \param[in]  cpuNum        Index of the CPU, starting at 0.
 * \param[out] affinityMask  The affinity mask to be modified.
 *
 ***********************************************************************
 */
void vmk_AffinityMaskDel(
   vmk_uint32 cpuNum,
   vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_AffinityMaskClear --                                       */ /**
 *
 * \ingroup Platform
 * \brief Clear an affinity bitmask of all CPUs.
 *
 * \param[out] affinityMask  The affinity mask to be modified.
 *
 ***********************************************************************
 */
void vmk_AffinityMaskClear(
   vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_AffinityMaskFill --                                        */ /**
 *
 * \ingroup Platform
 * \brief Set an affinity to include all CPUs.
 *
 * \param[out] affinityMask   The affinity mask to be modified.
 *
 ***********************************************************************
 */
void vmk_AffinityMaskFill(
   vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_AffinityMaskHasPCPU --                                     */ /**
 *
 * \ingroup Platform
 * \brief Test if a given affinity mask includes a particular PCPU number.
 *
 * \param[in] affinityMask    The affinity mask to be tested for
 *                            inclusion.
 * \param[in] cpuNum          The CPU number.
 *
 * \retval VMK_TRUE  cpuNum is represented in the bitmask.
 * \retval VMK_FALSE cpuNum is not represented in the bitmask.
 *
 ***********************************************************************
 */
vmk_Bool vmk_AffinityMaskHasPCPU(
   vmk_AffinityMask affinityMask,
   vmk_uint32 cpuNum);


/*
 ***********************************************************************
 * vmk_GetAPICCPUID --                                            */ /**
 *
 * \ingroup Platform
 * \brief Return the APIC ID of a CPU based on its PCPUNum
 *
 ***********************************************************************
 */
vmk_uint32 vmk_GetAPICCPUID(
   vmk_uint32 pcpuNum);

/*
 ***********************************************************************
 * vmk_CPUDisableInterrupts --                                    */ /**
 *
 * \ingroup Platform
 * \brief Disable interrupts on the current CPU.
 *
 ***********************************************************************
 */
void vmk_CPUDisableInterrupts(void);
   
/*
 ***********************************************************************
 * vmk_CPUEnableInterrupts --                                    */ /**
 *
 * \ingroup Platform
 * \brief Enable interrupts on the current CPU.
 *
 ***********************************************************************
 */
void vmk_CPUEnableInterrupts(void);

/*
 ***********************************************************************
 * vmk_CPUHasIntsEnabled --                                       */ /**
 *
 * \ingroup Platform
 * \brief Check whether interrupts are enabled on the current CPU.
 *
 * \retval VMK_TRUE  Interrupts are enabled on the current CPU.
 * \retval VMK_FALSE Interrupts are disabled on the current CPU.
 *
 ***********************************************************************
 */
vmk_Bool vmk_CPUHasIntsEnabled(void);

/*
 ***********************************************************************
 * VMK_ASSERT_CPU_HAS_INTS_ENABLED --                             */ /**
 *
 * \ingroup Platform
 * \brief Assert that interrupts are enabled on the current CPU.
 *
 ***********************************************************************
 */
#define VMK_ASSERT_CPU_HAS_INTS_ENABLED() \
      VMK_ASSERT(vmk_CPUHasIntsEnabled())

/*
 ***********************************************************************
 * VMK_ASSERT_CPU_HAS_INTS_DISABLED --                            */ /**
 *
 * \ingroup Platform
 * \brief Assert that interrupts are disabled on the current CPU.
 *
 ***********************************************************************
 */
#define VMK_ASSERT_CPU_HAS_INTS_DISABLED() \
      VMK_ASSERT(!vmk_CPUHasIntsEnabled())

/*
 ***********************************************************************
 * vmk_CPUGetFlags --                                             */ /**
 *
 * \ingroup Platform
 * \brief Get the current CPU's interrupt flags.
 *
 * \return The current CPU's interrupt flags.
 *
 ***********************************************************************
 */
vmk_CPUFlags vmk_CPUGetFlags(void);

/*
 ***********************************************************************
 * vmk_CPUSetFlags --                                             */ /**
 *
 * \ingroup Platform
 * \brief Restore the current CPU's interrupt flags
 *
 ***********************************************************************
 */
void vmk_CPUSetFlags(
   vmk_CPUFlags flags);


/*
 ***********************************************************************
 * vmk_CPUEnsureClearDF --                                        */ /**
 *
 * \ingroup Platform
 * \brief Ensures that the DF bit is clear.
 *
 * This is useful for instructions like outs, ins, scas, movs, stos,
 * cmps, lods which look at DF.
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE void vmk_CPUEnsureClearDF(void)
{
   __asm__ __volatile__ ("cld\n\t");
}

/*
 ***********************************************************************
 * vmk_CPUMemFenceRead --                                         */ /**
 *
 * \ingroup Platform
 * \brief Ensure that all loads have completed.
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE void vmk_CPUMemFenceRead(void)
{
   asm volatile ("lfence" ::: "memory");
}

/*
 ***********************************************************************
 * vmk_CPUMemFenceWrite --                                        */ /**
 *
 * \ingroup Platform
 * \brief Ensure that all stores are completed and globally visible.
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE void vmk_CPUMemFenceWrite(void)
{
   asm volatile ("sfence" ::: "memory");
}

/*
 ***********************************************************************
 * vmk_CPUMemFenceReadWrite --                                    */ /**
 *
 * \ingroup Platform
 * \brief Ensure that all loads and stores are completed and globally
 *        visible.
 *
 ***********************************************************************
 */
static VMK_ALWAYS_INLINE void vmk_CPUMemFenceReadWrite(void)
{
   asm volatile ("mfence" ::: "memory");
}

#endif /* _VMKAPI_PLATFORM_H_ */
/* @} */
