/***************************************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * ISA                                                            */ /**
 * \addtogroup Device
 * @{
 * \defgroup ISA ISA-Bus Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_ISA_H_
#define _VMKAPI_ISA_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"

/*
 ***********************************************************************
 * vmk_ISAMapIRQToVector --                                       */ /**
 *
 * \ingroup ISA
 * \brief Retrieves the vector the kernel assigned for an isa IRQ
 *
 * \param[in]  isaIRQ    ISA IRQ value.
 * \param[out] vector    Corresponding vector for specified IRQ.
 *
 * \retval VMK_BAD_PARAM   isaIRQ is out of range.
 * \retval VMK_BAD_PARAM   Vector was NULL.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ISAMapIRQToVector(vmk_uint32 isaIRQ,
                                       vmk_uint32 *vector);
#endif /* _VMKAPI_ISA_H_ */
/** @} */
/** @} */
