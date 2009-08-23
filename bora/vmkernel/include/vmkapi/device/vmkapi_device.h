/***************************************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Devices                                                        */ /**
 * \defgroup Device Device and Bus Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_DEVICE_H_
#define _VMKAPI_DEVICE_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_memory.h"

/**
 * \brief Opaque device handle
 */
typedef struct vmk_DeviceInt *vmk_Device;

/** \brief A null device handle. */
#define VMK_DEVICE_NONE ((vmk_Device)0)

/**
 * \brief Bus types.
 */
typedef enum {
   /*
    * Registered bus types
    */
   VMK_BUS_TYPE_NONE=0,
   VMK_BUS_TYPE_LOCALBUS=1,
   VMK_BUS_TYPE_ISA=2,
   VMK_BUS_TYPE_PCI=3,
   VMK_BUS_TYPE_PSEUDO=4,
   VMK_BUS_TYPE_RESERVED=5
} vmk_DeviceBusType;

/**
 * \brief Generic device information.
 *
 * This structure is created by the bus-specific device driver
 * and is filled in when a device is discovered and then registered
 * to get a device handle.
 */
typedef struct vmk_DeviceInfo {

   /** Bus type the device is on */
   vmk_DeviceBusType bus;
   
   /** Bus-specific information about the device */
   void *specific;
   
   /** Device-private Info */
   void *private;
} vmk_DeviceInfo;

/*
 ***********************************************************************
 * vmk_DeviceRegister --                                          */ /**
 *
 * \ingroup Device
 * \brief Register the device info with the device database and
 *        get a device handle back.
 *
 * \param[in]  deviceInfo  Device information about device to register.
 * \param[out] device      New device handle.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_DeviceRegister(vmk_DeviceInfo *deviceInfo,
                                    vmk_Device *device);

/*
 ***********************************************************************
 * vmk_DeviceRegister --                                          */ /**
 *
 * \ingroup Device
 * \brief Retrieve deviceInfo given device handle.
 *
 * \param[in] device       Device handle.
 * \param[in] deviceInfo   Device information.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_DeviceGetInfo(vmk_Device device,
                                   vmk_DeviceInfo *deviceInfo);

/*
 ***********************************************************************
 * vmk_DeviceGetBusType --                                        */ /**
 *
 * \ingroup Device
 * \brief Returns the bus type of a device.
 *
 * \param[in] device     Device handle.
 *
 * \retval VMK_BUS_TYPE_NONE  Specified device is invalid.
 *
 ***********************************************************************
 */
vmk_DeviceBusType vmk_DeviceGetBusType(vmk_Device device);

/*
 ***********************************************************************
 * vmk_DeviceGetBusPrivateInfo --                                 */ /**
 *
 * \ingroup Device
 * \brief Returns the bus private info
 *
 * \param[in] device     device handle
 *
 * \retval Bus private info if handle is valid or NULL on error.
 *
 ***********************************************************************
 */
void *vmk_DeviceGetBusPrivateInfo(vmk_Device device);

/*
 ***********************************************************************
 * vmk_DeviceUnregister --                                          */ /**
 *
 * \ingroup Device
 * \brief Unregister the device handle and remove the info from
 *        the device database.
 *
 * \param[in] device     Device handle.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_DeviceUnregister(vmk_Device device);

#endif /* _VMKAPI_DEVICE_H_ */
/** @} */
