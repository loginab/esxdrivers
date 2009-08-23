/* **********************************************************
 * Copyright 1998 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Types                                                          */ /**
 * \defgroup Types Basic Types
 * 
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_TYPES_H_
#define _VMKAPI_TYPES_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMCORE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"
/** \endcond never */

/** \brief vmk_Bool FALSE value */
#define VMK_FALSE 0

/** \brief vmk_Bool TRUE value */
#define VMK_TRUE  1

/** \brief Boolean value */
typedef char vmk_Bool;

typedef signed char        vmk_int8;
typedef unsigned char      vmk_uint8;
typedef short              vmk_int16;
typedef unsigned short     vmk_uint16;
typedef int                vmk_int32;
typedef unsigned int       vmk_uint32;

#if defined(__ia64__) || defined(__x86_64__)
typedef long               vmk_int64;
typedef unsigned long      vmk_uint64;
typedef vmk_uint64         vmk_VirtAddr;
typedef vmk_uint64         vmk_uintptr_t;
#else
typedef long long          vmk_int64;
typedef unsigned long long vmk_uint64;
typedef vmk_uint32         vmk_VirtAddr;
typedef vmk_uint32         vmk_uintptr_t;
#endif

typedef vmk_uint32	   vmk_MachPage;
typedef vmk_uint64         vmk_MachAddr;
typedef unsigned long      vmk_size_t;
typedef long               vmk_ssize_t;
typedef vmk_uint32         vmk_small_size_t;
typedef vmk_int32          vmk_small_ssize_t;
typedef long long          vmk_loff_t;

/**
 * \brief Abstract address
 */
typedef union {
   vmk_VirtAddr addr;
   void *ptr;
} vmk_AddrCookie __attribute__ ((__transparent_union__));

/**
 * \brief Structure containing information about a generic string
 */
typedef struct {
   vmk_uint32 bufferSize;
   vmk_uint32 stringLength;
   vmk_uint8  *buffer;
} vmk_String;

#define VMK_STRING_CHECK_CONSISTENCY(string) VMK_ASSERT((string) && ((string)->bufferSize > (string)->stringLength))

#define VMK_STRING_SET(str,ptr,size,len) { \
         (str)->buffer = (ptr); \
         (str)->bufferSize = (size); \
         (str)->stringLength = (len); \
         }

/**
 * \brief Address space size of ioctl caller.
 */ 
typedef enum {
   /** \brief Caller has 64-bit address space. */
   VMK_IOCTL_CALLER_64 = 0,
   
   /** \brief Caller has 32-bit address space. */
   VMK_IOCTL_CALLER_32 = 1
} vmk_IoctlCallerSize;

#endif /* _VMKAPI_TYPES_H_ */
/** @} */
