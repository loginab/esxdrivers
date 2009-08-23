/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Logging                                                        */ /**
 * \defgroup Logging Kernel Logging
 *
 * The logging interfaces provide a means of writing informational
 * and error messages to the kernel's logs. 
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_LOGGING_H_
#define _VMKAPI_LOGGING_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_compiler.h"

#include <stdarg.h>

/** \brief Max length of log component name. */
#define VMK_MAX_LOG_COMPONENT_NAME (32)

/** \brief Opaque log component handle. */
typedef struct vmk_LogComponent *vmk_LogComponentHandle;

/** \brief Log handle guaranteed to be invalid. */
#define VMK_INVALID_LOG_HANDLE NULL

/** \brief Log urgency level. */
typedef enum {
   VMK_LOG_URGENCY_NORMAL,
   VMK_LOG_URGENCY_WARNING,
   VMK_LOG_URGENCY_ALERT
} vmk_LogUrgency ;

/** \brief Types of log throttling */
typedef enum {
   /** Log is not throttled. All messages will be logged. */
   VMK_LOG_THROTTLE_NONE=0,

   /**
    * An internal message count will be kept and messages will
    * only be logged as the count reaches certain wider-spaced values.
    */
   VMK_LOG_THROTTLE_COUNT=1,

   /**
    * Messages will be logged depending on the return value
    * of a custom log throttling function. 
    */
   VMK_LOG_THROTTLE_CUSTOM=2,
} vmk_LogThrottleType;

/*
 ***********************************************************************
 * vmk_LogThrottleFunc --                                         */ /**
 *
 * \ingroup Logging
 * \brief Custom throttling function for a log component.
 *
 * A log throttling function will be called each time an attempt to
 * log a message to a log component is made. If this function returns
 * VMK_TRUE, the message will be logged. Otherwise, the message will
 * not be logged.
 *
 * \param[in] arg    Private data argument
 *
 * \return Whether or not the logger should log the current log message.
 * \retval VMK_TRUE     Log the current message.
 * \retval VMK_FALSE    Do not log the current message.
 *
 ***********************************************************************
 */
typedef vmk_Bool (*vmk_LogThrottleFunc)(void *arg);

/**
 * \brief Properties that define the type of throttling for
 *        a particular log component.
 */
typedef struct vmk_LogThrottleProperties {
   /** Type of log throttling to use. */
   vmk_LogThrottleType type;

   /** Properties for the specified log throttling type. */
   union {
      /** Properties for a custom log throttler. */
      struct {
         /**
          * Throttling function to call on each message submitted to the
          * log component.
          */
         vmk_LogThrottleFunc throttler;
         
         /**
          * Private data argument to pass to the log throttling function
          * on each call.
          */
         void *arg;
      } custom;
   } info;
}
vmk_LogThrottleProperties;

/*
 ***********************************************************************
 * vmk_StatusToString --                                          */ /**
 *
 * \ingroup Logging
 * \brief Convert a status into a human readable text string
 *
 * \param[in] status    Return status code to convert to a
 *                      human-readable string.
 *
 * \return Human-readable string that describes the supplied status.
 *
 ***********************************************************************
 */
const char *vmk_StatusToString(
   VMK_ReturnStatus status);

/*
 ***********************************************************************
 * vmk_LogRegister --                                             */ /**
 *
 * \ingroup Logging
 * \brief Register a log component
 *
 * Should be used to create a new log component with a particular
 * string prepended to each message, that can have its log levels
 * controlled independently from other log components.
 *
 * \pre The supplied module should have a heap associated with it.
 * 
 * \param[in]  name           Log component name.
 * \param[in]  module         Module the log component belongs to.
 * \param[in]  defaultLevel   Default log level.
 * \param[in]  throttleProps  User-defined throttling properties or
 *                            NULL to indicate that no throttling is
 *                            desired.
 * \param[out] handle         Handle to the newly created log component.
 *
 * \retval VMK_BAD_PARAM         Bad LogThrottleType specified in
 *                               the throttle properties.
 * \retval VMK_NO_MEMORY         Not enough memory on the module or
 *                               vmkernel heap to create the new
 *                               log component.
 * \retval VMK_INVALID_MODULE    Supplied module ID is invalid.
 * \retval VMK_EXISTS            A log component with the same name has
 *                               already been registered.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_LogRegister(
   const char *name,
   vmk_ModuleID module,
   vmk_int32 defaultLevel,
   const vmk_LogThrottleProperties *throttleProps,
   vmk_LogComponentHandle *handle);

/*
 ***********************************************************************
 * vmk_LogUnregister --                                           */ /**
 *
 * \ingroup Logging
 * \brief Unregister a log component
 *
 * Should be used to unregister an existing logging component
 *
 * \note Should be called before the module heap is destroyed.
 *
 * \param[in] handle       Pointer to handle for log component to be
 *                         unregistered.
 *
 * \retval VMK_NOT_FOUND   Supplied log component is invalid/unregisterd.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_LogUnregister(
   vmk_LogComponentHandle *handle);

/*
 ***********************************************************************
 * vmk_LogGetName --                                              */ /**
 *
 * \ingroup Logging
 * \brief Get log component name
 *
 * \param[in] handle    Log component handle.
 *
 * \return The name associated with the supplied log component handle.
 *
 ***********************************************************************
 */
const char * vmk_LogGetName(
   vmk_LogComponentHandle handle);

/*
 ***********************************************************************
 * vmk_LogGetCurrentLogLevel --                                   */ /**
 *
 * \ingroup Logging
 * \brief Get current log level of the given component
 *
 * \param[in] handle Log component handle.
 *
 * \return Current log level of the given component returned.
 *
 ***********************************************************************
 */
vmk_int32 vmk_LogGetCurrentLogLevel(
   vmk_LogComponentHandle handle);

/*
 ***********************************************************************
 * vmk_LogDebug --                                                */ /**
 *
 * \ingroup Logging
 * \brief Log a message to a logging component on debug builds only.
 *
 * Should be used to log information messages and non-error conditions.
 *
 * Messages are logged only if the component's log level is greater
 * than or equal to the minimum log level specified.
 * 
 * \param[in] handle    Log component handle,
 * \param[in] min       Minimum log level required to print the message,
 * \param[in] fmt       Format string,
 * \param[in] args      List of message arguments,
 *
 ***********************************************************************
 */
#ifndef VMX86_LOG
#define vmk_LogDebug(handle, min, fmt, args...)
#else 
#define vmk_LogDebug(handle, min, fmt, args...)  \
   vmk_LogLevel(VMK_LOG_URGENCY_NORMAL, handle, min, \
                "%s: %s: " fmt "\n", vmk_LogGetName(handle), \
                __FUNCTION__, ##args)
#endif

/*
 ***********************************************************************
 * vmk_Log --                                                     */ /**
 *
 * \ingroup Logging
 * \brief Log message to a logging component at its current log level.
 *
 * Should be used to log information messages and non-error conditions.
 * 
 * \param[in] handle    Log component handle.
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments.
 *
 ***********************************************************************
 */
#define vmk_Log(handle, fmt, args...)  \
   vmk_LogLevel(VMK_LOG_URGENCY_NORMAL, \
                handle, vmk_LogGetCurrentLogLevel(handle),   \
                "%s: %s: " fmt "\n", vmk_LogGetName(handle), \
                __FUNCTION__, ##args)

/*
 ***********************************************************************
 * vmk_Warning --                                                 */ /**
 *
 * \ingroup Logging
 * \brief Log a warning message to a logging component at its current
 *        log level.
 *
 * Should be used to log abnormal conditions.
 * 
 * \param[in] handle    Log component handle.
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments.
 *
 ***********************************************************************
 */
#define vmk_Warning(handle, fmt, args...) \
    vmk_LogLevel(VMK_LOG_URGENCY_WARNING, \
                 handle, vmk_LogGetCurrentLogLevel(handle),   \
                 "%s: %s: " fmt "\n", vmk_LogGetName(handle), \
                 __FUNCTION__, ##args)

/*
 ***********************************************************************
 * vmk_Alert --                                                   */ /**
 *
 * \ingroup Logging
 * \brief Log an alert message to a logging component at its current
 *        log level.
 *
 * Should be used to notify users of system alerts.
 * 
 * \param[in] handle    Log component handle.
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments.
 *
 ***********************************************************************
 */
#define vmk_Alert(handle, fmt, args...) \
    vmk_LogLevel(VMK_LOG_URGENCY_ALERT, \
                 handle, vmk_LogGetCurrentLogLevel(handle),   \
                 "%s: %s: " fmt "\n", vmk_LogGetName(handle), \
                 __FUNCTION__, ##args)

/*
 ***********************************************************************
 * vmk_LogDebugMessage --                                         */ /**
 *
 * \ingroup Logging
 * \brief Log an information message to the vmkernel log unconditionally
 *        on debug builds only.
 *
 * Should be used to log information messages and non-error conditions
 * when no log component is available.
 *
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments. 
 *
 ***********************************************************************
 */
#ifndef VMX86_LOG
#define vmk_LogDebugMessage(fmt, args...)
#else 
#define vmk_LogDebugMessage(fmt, args...)  \
   vmk_LogNoLevel(VMK_LOG_URGENCY_NORMAL, fmt, ##args)
#endif

/*
 ***********************************************************************
 * vmk_LogMessage --                                              */ /**
 *
 * \ingroup Logging
 * \brief Log an information message to the vmkernel log unconditionally.
 *
 * Should be used to log information messages and non-error conditions
 * when no log component is available.
 *
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments. 
 *
 ***********************************************************************
 */
#define vmk_LogMessage(fmt, args...) \
    vmk_LogNoLevel(VMK_LOG_URGENCY_NORMAL, fmt, ##args)

/*
 ***********************************************************************
 * vmk_WarningMessage --                                          */ /**
 *
 * \ingroup Logging
 * \brief Log a warning or error message to the vmkernel log
 *        unconditionally.
 *
 * Should be used to log abnormal conditions when no log component is
 * available.
 *
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments.
 *
 ***********************************************************************
 */
#define vmk_WarningMessage(fmt, args...) \
    vmk_LogNoLevel(VMK_LOG_URGENCY_WARNING, fmt, ##args)
   

/*
 ***********************************************************************
 * vmk_AlertMessage --                                            */ /**
 *
 * \ingroup Logging
 * \brief Log a system alert to the vmkernel log and the console
 *        unconditionally.
 *
 * Should be used to log severe problems when no log component is
 * available.
 *
 * \param[in] fmt       Format string.
 * \param[in] args      List of message arguments.
 *
 ***********************************************************************
 */
#define vmk_AlertMessage(fmt, args...) \
    vmk_LogNoLevel(VMK_LOG_URGENCY_ALERT, fmt, ##args)

/*
 ***********************************************************************
 * vmk_LogSetCurrentLogLevel --                                   */ /**
 *
 * \ingroup Logging
 * \brief Set current log level of a given log component
 *
 * \param[in] handle    Log component handle to modify.
 * \param[in] level     Log level to set component to.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_LogSetCurrentLogLevel(
   vmk_LogComponentHandle handle,
   vmk_int32 level);

/*
 ***********************************************************************
 * vmk_vLogLevel --                                               */ /**
 *
 * \ingroup Logging
 * \brief Log a message using a log component
 *
 * Output a log message to the vmkernel log if the current log level
 * on the given log component is equal to or greater than the given
 * log level. 
 *
 * \param[in] urgency   How urgent the message is.
 * \param[in] handle    Log component handle.
 * \param[in] level     Minimum log level the component must be set to
 *                      in order to print the message.
 * \param[in] fmt       Format string.
 * \param[in] ap        List of message arguments.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_vLogLevel(
   vmk_LogUrgency urgency,
   vmk_LogComponentHandle handle,
   vmk_int32 level,
   const char *fmt,
   va_list ap);

/*
 ***********************************************************************
 * vmk_LogLevel --                                                */ /**
 *
 * \ingroup Logging
 * \brief Log a message using a log component
 *
 * Output a log message to the vmkernel log if the current log level
 * on the given log component is equal to or greater than the given
 * log level. 
 *
 * \param[in] urgency   How urgent the message is.
 * \param[in] handle    Log component handle.
 * \param[in] level     Minimum log level the component must be set to
 *                      in order to print the message.
 * \param[in] fmt       Format string.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_LogLevel(
   vmk_LogUrgency urgency,
   vmk_LogComponentHandle handle,
   vmk_int32 level,
   const char *fmt,
   ...)
VMK_ATTRIBUTE_PRINTF(4,5);

/*
 ***********************************************************************
 * vmk_vLogNoLevel --                                             */ /**
 *
 * \ingroup Logging
 * \brief Log an information message to the vmkernel log
 *        unconditionally with a va_list.
 *
 * Should be used to log information messages and non-error conditions
 * when no log component is available.
 *
 * \param[in] urgency   How urgent the message is.
 * \param[in] fmt       Format string.
 * \param[in] ap        List of message arguments.
 *
 ***********************************************************************
 */
VMK_ReturnStatus  vmk_vLogNoLevel(
   vmk_LogUrgency urgency,
   const char *fmt,
   va_list ap);

/*
 ***********************************************************************
 * vmk_LogNoLevel --                                              */ /**
 *
 * \ingroup Logging
 * \brief Log an information message to the vmkernel log
 *        unconditionally with variable arguments.
 *
 * Should be used to log information messages and non-error conditions
 * when no log component is available.
 *
 * \param[in] urgency   How urgent the message is.
 * \param[in] fmt       Format string.
 *
 ***********************************************************************
 */
VMK_ReturnStatus  vmk_LogNoLevel(
   vmk_LogUrgency urgency,
   const char *fmt,
   ...)
VMK_ATTRIBUTE_PRINTF(2,3);


/*
 ***********************************************************************
 * vmk_LogFindLogComponentByName --                               */ /**
 *
 * \ingroup Logging
 * \brief Get a log component by the given log component name and
 *        the module ID.
 *
 * \param[in]  id       Module ID that registered the log component
 * \param[in]  name     Log component name
 * \param[out] handle   Returns log component handle of the specified
 *                      name and the module ID.
 *
 * \retval VMK_NOT_FOUND  The given log component name does not exist.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_LogFindLogComponentByName(
   vmk_ModuleID id,
   const char *name,
   vmk_LogComponentHandle *handle);

/*
 ***********************************************************************
 * vmk_LogBacktrace --                                            */ /**
  *
  * \ingroup Logging
  * \brief Write the current stack backtrace to a log component.
  *
  * This routine logs at the component's current logging level and
  * at NORMAL urgency.
  *
  * \param[in] handle   Log component to write the backtrace to.
  *
  ***********************************************************************
  */
VMK_ReturnStatus vmk_LogBacktrace(
   vmk_LogComponentHandle handle);

/*
 ***********************************************************************
 * vmk_LogBacktraceMessage --                                     */ /**
  *
  * \ingroup Logging
  * \brief Write the current stack backtrace to the vmkernel log.
  *
  * Should be used to log the backtrace when no logging component 
  * is available.
  *
  ***********************************************************************
  */
VMK_ReturnStatus vmk_LogBacktraceMessage(void);

#endif /* _VMKAPI_LOGGING_H_ */
/** @} */
