/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved. -- VMware Confidential
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 * vmkapi_npiv_wwn.h --
 *
 *      WWN Defines exported to everyone
 */

#ifndef _VMKAPI_NPIV_WWN_H_
#define _VMKAPI_NPIV_WWN_H_

/** \cond never */
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

/**
 * \brief VPORT API World Wide Name length
 */
#define VMK_VPORT_WWN_LEN  8 /* 8 bytes */

/**
 * \brief VPORT API Vport World Wide Name
 */
typedef unsigned char vmk_VportWwn[VMK_VPORT_WWN_LEN];

#endif // _VMKAPI_NPIV_WWN_H_
