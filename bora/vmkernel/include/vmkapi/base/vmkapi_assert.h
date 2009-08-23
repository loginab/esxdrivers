/* **********************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Assertions                                                     */ /**
 * \defgroup Assert Assertions
 *
 * Assertions and related interfaces.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_ASSERT_H_
#define _VMKAPI_ASSERT_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_VMNIXMOD
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_compiler.h"
#include "base/vmkapi_types.h"

#include <stdarg.h>

/*
 ***********************************************************************
 * vmk_vPanic --                                                  */ /**
 *
 * \ingroup Assert
 * \brief Panics the system.
 *
 * Used in unrecoverable error conditions.\n
 * A system dump is generated if a dump device has been configured.
 *
 ***********************************************************************
 */
void vmk_vPanic(
   const char *fmt,
   va_list ap);

/*
 ***********************************************************************
 * vmk_Panic --                                                   */ /**
 *
 * \ingroup Assert
 * \brief Panics the system.
 *
 * Used in unrecoverable error conditions.\n
 * A system dump is generated if a dump device has been configured.
 *
 ***********************************************************************
 */
void
vmk_Panic(
   const char *fmt,
   ...)
VMK_ATTRIBUTE_PRINTF(1,2);

/*
 ***********************************************************************
 * VMK_ASSERT_BUG --                                              */ /**
 *
 * \ingroup Assert
 * \brief Panics the system if a runtime expression evalutes to false
 *        regardless of debug build status.
 *
 ***********************************************************************
 */
#define VMK_ASSERT_BUG(condition)                           \
   do {                                                     \
      if (VMK_UNLIKELY(!(condition))) {                     \
         vmk_Panic("Failed at %s:%d -- VMK_ASSERT(%s)\n",   \
                   __FILE__, __LINE__, #condition);         \
      }                                                     \
   } while(0)

/*
 ***********************************************************************
 * VMK_ASSERT --                                                  */ /**
 *
 * \ingroup Assert
 * \brief Panics the system if a runtime expression evalutes to false
 *        only in debug builds.
 *
 ***********************************************************************
 */
#if defined(VMX86_DEBUG)
#define VMK_ASSERT(condition) VMK_ASSERT_BUG(condition)
#else
#define VMK_ASSERT(condition)
#endif


/*
 ***********************************************************************
 * VMK_ASSERT_ON_COMPILE --                                       */ /**
 *
 * \ingroup Assert
 * \brief Fail compilation if a condition does not hold true
 *
 * \note This macro must be used inside the context of a function.
 *
 ***********************************************************************
 */
#define VMK_ASSERT_ON_COMPILE(condition)                 \
   do {                                                  \
      switch(0) {                                        \
         case 0:                                         \
         case (condition):                               \
            ;                                            \
      }                                                  \
   } while(0)                                            \

/*
 ***********************************************************************
 * VMK_ASSERT_LIST --                                             */ /**
 *
 * \ingroup Assert
 * \brief  To put a VMK_ASSERT_ON_COMPILE() outside a function, wrap it
 *         in VMK_ASSERT_LIST(). The first parameter must be unique in
 *         each .c file where it appears
 *
 * \par Example usage with VMK_ASSERT_ON_COMPILE:
 *
 * \code
 * VMK_ASSERT_LIST(FS3_INT,
 *    VMK_ASSERT_ON_COMPILE(sizeof(vmk_FS3_DiskLock) == 128);
 *    VMK_ASSERT_ON_COMPILE(sizeof(vmk_FS3_LockRes) == DISK_BLK_SIZE);
 * )
 * \endcode
 *
 *
 ***********************************************************************
 */
#define VMK_ASSERT_LIST(name, assertions)  \
   static inline void name(void) {         \
      assertions                           \
   }

/*
 ***********************************************************************
 * VMK_DEBUG_ONLY --                                              */ /**
 *
 * \ingroup Assert
 * \brief Compile code only for debug builds.
 *
 * \par Example usage:
 *
 * \code
 * VMK_DEBUG_ONLY(
 *    myFunc();
 *    x = 1;
 *    y = 3;
 * )
 * \endcode
 *
 ***********************************************************************
 */
#if defined(VMX86_DEBUG)
#define VMK_DEBUG_ONLY(x) x
#else
#define VMK_DEBUG_ONLY(x)
#endif

/*
 ***********************************************************************
 * VMK_NOT_REACHED --                                             */ /**
 *
 * \ingroup Assert
 * \brief Panic if code reaches this call
 *
 ***********************************************************************
 */
#define VMK_NOT_REACHED() \
   vmk_Panic("Failed at %s:%d -- NOT REACHED\n", __FILE__, __LINE__)

#endif /* _VMKAPI_ASSERT_H_ */
/** @} */
