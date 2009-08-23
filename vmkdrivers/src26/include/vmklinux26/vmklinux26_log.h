
/* **********************************************************
 * Copyright 2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * vmklinux26_log.h --
 *
 *      Definitions of logging macros for vmklinux26.
 *
 *      To use the logging macros here, you need to first specify the 
 *	log handle that you want to use by either creating a new one or
 *      by re-using an existing one that is created in a another .c file
 *      of the same module.
 *
 *      To create a new log handle for your module, include the following
 *      lines in the header portion of the .c file that provides the 
 *      initialization code for the module.
 *
 *         #define VMKLNX_LOG_HANDLE <your handle name>
 *         #include "vmklinux26_log.h"
 *
 *      In the initialization section, add the following macro call
 *
 *         VMKLNX_CREATE_LOG();
 *
 *      In the cleanup section, add the following macro call
 *
 *         VMKLNX_DESTROY_LOG();
 *
 *      To use a handle that is created in another file, which is part of
 *      the same module of your .c file, include the following lines in
 *      your .c file,
 *
 *         #define VMKLNX_REUSE_LOG_HANDLE <name of the handle you want>
 *         #include "vmklinux26_log.h"
 *
 *      There is no need for calling VMKLNX_CREATE_LOG() if you are
 *      re-using an existing log handle.
 *
 *      NOTE: You can use VMKLNX_REUSE_LOG_HANDLE to specify a log handle
 *      that is exported by a totally separated module, but you need
 *      to synchronize with the module that owns the handle to ensure 
 *      that the handle is created before being used in your code.
 *
 *	Logging macros available in here are:
 *
 *         VMKLNX_DEBUG          - Log a message for debug build only
 *         VMKLNX_INFO           - Log an info message
 *         VMKLNX_THROTTLED_INFO - Log an info message with throttling control
 *         VMKLNX_WARN           - Log a warning message
 *         VMKLNX_THROTTLED_WARN - Log a warning message with throttling control
 *         VMKLNX_ALERT          - Log an alert message
 *         VMKLNX_PANIC          - Log a message and panic the system
 *         VMKLNX_LOG_ONLY       - Specify a line of code just for debug build
 *
 *      See the macros below for their arugment lists, they are 
 *      self-explanatory.
 */

#ifndef _VMKLINUX26_LOG_H_
#define _VMKLINUX26_LOG_H_

#define INCLUDE_ALLOW_DISTRIBUTE

#include "vmkapi.h"

extern vmk_LogComponentHandle  vmklinux26Log;

#define __XHANDLE__(name)        name ## vmklinux26Log
#define __XLOGHANDLE__(name)     __XHANDLE__(name)
#define __XNAME_STR__(name)      # name
#define __XLOGNAME_STR__(name)   __XNAME_STR__(name)

#if defined(VMKLNX_LOG_HANDLE)
vmk_LogComponentHandle __XLOGHANDLE__(VMKLNX_LOG_HANDLE);
#elif defined(VMKLNX_REUSE_LOG_HANDLE)
#define VMKLNX_LOG_HANDLE VMKLNX_REUSE_LOG_HANDLE
extern vmk_LogComponentHandle __XLOGHANDLE__(VMKLNX_LOG_HANDLE);
#else /* !defined(VMKLNX_LOG_HANDLE) && !defined(VMKLNX_REUSE_LOG) */
/*
 * A null value means use the default vmklinux log.
 */
#define VMKLNX_LOG_HANDLE
#endif /* defined(VMKLNX_LOG_HANDLE) */

#define VMKLNX_CREATE_LOG()                                               \
   if (vmk_LogRegister(__XLOGNAME_STR__(VMKLNX_LOG_HANDLE),               \
                       vmk_ModuleStackTop(),                              \
                       0,                                                 \
                       NULL,                                              \
                       &(__XLOGHANDLE__(VMKLNX_LOG_HANDLE))) != VMK_OK) { \
      VMKLNX_WARN("Can't register log name %s",                           \
                  __XLOGNAME_STR__(VMKLNX_LOG_HANDLE));                   \
      __XLOGHANDLE__(VMKLNX_LOG_HANDLE) = vmklinux26Log;                  \
   }

#define VMKLNX_DESTROY_LOG()                                              \
   vmk_LogUnregister(&(__XLOGHANDLE__(VMKLNX_LOG_HANDLE)))

#ifdef VMX86_LOG
#define VMKLNX_LOG_ONLY(x)   x
#else
#define VMKLNX_LOG_ONLY(x)
#endif

static inline int                                                         \
__xCountCheckx__(vmk_uint32 count) {                                      \
   return count <     100                            ||                   \
         (count <   12800 && (count &    0x7F) == 0) ||                   \
         (count < 1024000 && (count &   0x3FF) == 0) ||                   \
                             (count & 0xEFFFF) == 0;                      \
}

/*
 * The argument "level" associates a debug level to the log message.
 *
 * To see the debug message printed in the system log, you would need
 * to change the debug level of the log handle to the level either 
 * the same level or above the level that the message is at.
 *
 * The debug level can be adjusted via a VSI node, the path of the node
 * would be:
 *    /system/modules/vmklinux/loglevels/<VMKLNX_LOG_HANDLE>
 */
#define VMKLNX_DEBUG(level, fmt, args...)                                 \
   vmk_LogDebug(__XLOGHANDLE__(VMKLNX_LOG_HANDLE), level,                 \
                fmt, ##args)

#define VMKLNX_INFO(fmt, args...)                                         \
   vmk_Log(__XLOGHANDLE__(VMKLNX_LOG_HANDLE), fmt, ##args)

#define VMKLNX_THROTTLED_INFO(count, fmt, args...)                        \
   if (__xCountCheckx__(++count)) {                                       \
      vmk_Log(__XLOGHANDLE__(VMKLNX_LOG_HANDLE),                          \
              "This message has repeated %d times: " fmt,                 \
              count, ##args);                                             \
   }

#define VMKLNX_WARN(fmt, args...)                                         \
   vmk_Warning(__XLOGHANDLE__(VMKLNX_LOG_HANDLE), fmt "\n", ##args)

#define VMKLNX_THROTTLED_WARN(count, fmt, args...)                        \
   if (__xCountCheckx__(++count)) {                                       \
      vmk_Warning(__XLOGHANDLE__(VMKLNX_LOG_HANDLE),                      \
                  "This message has repeated %d times: " fmt,             \
                  count, ##args);                                         \
   }

#define VMKLNX_ALERT(fmt, args...)                                        \
   vmk_Alert(__XLOGHANDLE__(VMKLNX_LOG_HANDLE), fmt, ##args)

#define VMKLNX_PANIC(fmt, args...)                                        \
   vmk_Panic("vmklinux: " fmt "\n", ##args)

#endif // _VMKLINUX26_LOG_H_


