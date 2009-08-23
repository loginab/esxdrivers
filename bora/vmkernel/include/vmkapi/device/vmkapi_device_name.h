/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Devices                                                        */ /**
 * \addtogroup Device
 * @{
 * \defgroup DeviceName Device-Name Database Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_DEVICE_NAME_H_
#define _VMKAPI_DEVICE_NAME_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "device/vmkapi_device_name_types.h"

/*
 ***********************************************************************
 * vmk_DevNameLookup --                                           */ /**
 *
 * \ingroup DeviceName
 * \brief Determine if a device is in the device name list.
 *
 * This function looks for the device name in the device name list
 * and if the device name is found, it returns the device attributes and
 * the aliased device name.
 *
 * \param[in]  name        Name of the device to look for.
 * \param[out] attributes  Pointer to device attributes or can be set
 *                         to NULL if the caller isn't interested
 *                         in the device attributes.
 *                         See VmkDeviceAttributes
 *
 * \retval VMK_OK          The specified device name is found and it
 *                         returned its device attributes.
 * \retval VMK_NOT_FOUND   The device name is not found.
 * \retval VMK_BAD_PARAM   Null or bad length of the device name is
 *                         specified.
 ***********************************************************************
 */
VMK_ReturnStatus vmk_DevNameLookup(const char *name, vmk_uint32 *attributes);

#endif /* _VMKAPI_DEVICE_NAME_H_ */
/** @} */
/** @} */
