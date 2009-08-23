/* ****************************************************************
 * Portions Copyright 2008 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

#ifndef _LINUX_STRESS_H_
#define _LINUX_STRESS_H_

#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"

#include "vmkapi.h"
#include <vmklinux26/vmklinux26_stress.h>

#if defined(VMX86_DEBUG)
#define VMKLNX_STRESS_DEBUG_COUNTER(_opt) vmklnx_stress_counter(_opt)
#define VMKLNX_STRESS_DEBUG_OPTION(_opt)  vmklnx_stress_option(_opt)
#else
#define VMKLNX_STRESS_DEBUG_COUNTER(_opt) VMK_FALSE
#define VMKLNX_STRESS_DEBUG_OPTION(_opt)  VMK_FALSE
#endif

#endif /* _LINUX_STRESS_H_ */
