/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Constants                                                      */ /**
 * \defgroup Constants Constants
 *
 * Useful constants
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_CONST_H_
#define _VMKAPI_CONST_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"
/** \endcond never */

#if !defined(NULL)
# ifndef __cplusplus
#  define NULL (void *)0
# else
#  define NULL __null
# endif
#endif

#if defined(VMX86_DEBUG)
#  define vmkApiDebug 1
#else
#  define vmkApiDebug 0
#endif

/**
 *  \brief Wrapper for 64 bit signed and unsigned constants
 */
#if defined(__ia64__) || defined(__x86_64__)
#define VMK_CONST64(c)     c##L
#define VMK_CONST64U(c)    c##UL
#else
#define VMK_CONST64(c)     c##LL
#define VMK_CONST64U(c)    c##ULL
#endif

/** Max length of a device name such as 'scsi0' */
#define VMK_DEVICE_NAME_MAX_LENGTH	32

/** Printf format for a 64 bit wide value */
#if defined(__x86_64__)
#define VMK_FMT64 "l"
#else
#define VMK_FMT64 "L"
#endif

#endif /* _VMKAPI_CONST_H_ */
/** @} */
