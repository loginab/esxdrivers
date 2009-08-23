/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Devices                                                        */ /**
 * \addtogroup DeviceName
 * @{
 *
 * Device-name API types.
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_DEVICE_NAME_TYPES_H_
#define _VMKAPI_DEVICE_NAME_TYPES_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"
/** \endcond never */

/** \brief Device attributes. */
typedef enum {
   /** \brief The device is of char device class. */
   VmkDevAttrCharClass         =  1 << 0,

   /** \brief The device is of network device class. */
   VmkDevAttrNetClass          =  1 << 1,

   /** \brief The device is of storage device class. */
   VmkDevAttrStorageClass      =  1 << 2,

   /**
    * \brief The device name is associated with the PCI bus slot func.
    *
    * This attribute is set to the device which is in the PCI device list.
    */
   VmkDevAttrPCIBUSModel       =  1 << 8
} VmkDeviceAttributes;

#endif /* _VMKAPI_DEVICE_NAME_TYPES_H_ */
/** @} */
