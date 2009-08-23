/* **********************************************************
 * Copyright 1998 VMware, Inc.  All rights reserved. -- VMware Confidential
 * **********************************************************/

/*
 * vmklinux_version_dist.h --
 *
 *      Version of the vmkernel / vmklinux interface
 */

#ifndef _VMKLINUX_VERSION_DIST_H_
#define _VMKLINUX_VERSION_DIST_H_

#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE

#include "buildNumber.h"

/*
 * Versioning between vmkernel and drivers.
 */

#define VMKLNX_VERSION_OK                   0
#define VMKLNX_VERSION_NOT_SUPPORTED        1
#define VMKLNX_VERSION_MINOR_NOT_MATCHED    2

#define VMKLNX_MAKE_VERSION(major,minor)    (((major) << 16) | (minor))

/*
 * The version here should match up with ESX version
 * For patch release, a patch ID can be appended.
 */
#define VMKLNX_MODULE_VERSION                "4.0"

/*
 * When VMKLNX_API_VERSION needs to be bumped up, please make sure also
 * change the version in DriverAPI-x.y defined in
 * bora/scons/modules/vmkDrivers.sc
 */
#define VMKLNX_API_VERSION_MAJOR_NUM	   9
#define VMKLNX_API_VERSION_MINOR_NUM	   0
#define VMKLNX_API_VERSION                 VMKLNX_MAKE_VERSION(VMKLNX_API_VERSION_MAJOR_NUM, \
                                                               VMKLNX_API_VERSION_MINOR_NUM)

#define VMKDRIVER_VERSION                  VMKLNX_API_VERSION

#define VMKLNX_API_VERSION_MAJOR(version)  ((version) >> 16)
#define VMKLNX_API_VERSION_MINOR(version)  ((version) & 0xffff)

#if defined(VMKLINUX)
 /* Compiling vmklinux */
#undef LINUX_MODULE_VERSION
#define LINUX_MODULE_VERSION                 VMKLNX_MODULE_VERSION
#endif /* defined(VMKLINUX) */

extern int vmklnx_CheckModuleVersion(unsigned int drvAPIVersion, char *modName);

#endif // _VMKLINUX_VERSION_DIST_H_
