/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Compiler Utilities                                             */ /**
 * \defgroup Compiler Compiler Utilities
 *
 * These interfaces allow easy access to special compiler-specific
 * information and tags.
 *
 * @{
 ***********************************************************************
 */
 
#ifndef _VMKAPI_COMPILER_H_
#define _VMKAPI_COMPILER_H_ 

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"
/** \endcond never */

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_NORETURN --                                      */ /**
 * \ingroup Compiler
 *
 * \brief Indicate to the compiler that the function never returns
 *
 ***********************************************************************
 */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 9)
#define VMK_ATTRIBUTE_NORETURN \
   __attribute__((__noreturn__))
#else
#define VMK_ATTRIBUTE_NORETURN
#endif

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_ALWAYS_INLINE --                                 */ /**
 * \ingroup Compiler
 *
 * \brief Indicate to the compiler that an inlined function should
 *        always be inlined.
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_ATTRIBUTE_ALWAYS_INLINE \
   __attribute__((always_inline))
#else
#define VMK_ATTRIBUTE_ALWAYS_INLINE
#endif

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_PRINTF --                                        */ /**
 * \ingroup Compiler
 *
 * \brief Compiler format checking for printf-like functions
 *
 * \param[in]  fmt      Argument number of the format string
 * \param[in]  vararg   Argument number of the first vararg
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_ATTRIBUTE_PRINTF(fmt, vararg) \
   __attribute__((format(__printf__, fmt, vararg)))
#else
#define VMK_ATTRIBUTE_PRINTF(fmt, vararg)
#endif

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_SCANF --                                         */ /**
 * \ingroup Compiler
 *
 * \brief Compiler format checking for scanf-like functions
 *
 * \param[in]  fmt      Argument number of the format string
 * \param[in]  vararg   Argument number of the first vararg
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_ATTRIBUTE_SCANF(fmt, vararg) \
   __attribute__((format(__scanf__, fmt, vararg)))
#else
#define VMK_ATTRIBUTE_SCANF(fmt, vararg)
#endif

/*
 ***********************************************************************
 * VMK_ALWAYS_INLINE --                                           */ /**
 * \ingroup Compiler
 *
 * \brief Indicate to the compiler that a function should be inlined
 *        and that it should always be inlined.
 *
 ***********************************************************************
 */
#define VMK_ALWAYS_INLINE \
   inline VMK_ATTRIBUTE_ALWAYS_INLINE

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_ALIGN --                                         */ /**
 * \ingroup Compiler
 *
 * \brief Align the data structure on "n" bytes.
 *
 * \param[in] n   Number of bytes to align the data structure on.
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_ATTRIBUTE_ALIGN(n) \
   /** \cond never */ __attribute__((__aligned__(n))) /** \endcond */
#else
#define VMK_ATTRIBUTE_ALIGN(n)
#endif

/*
 ***********************************************************************
 * VMK_ATTRIBUTE_PACKED --                                        */ /**
 * \ingroup Compiler
 *
 * \brief Pack a data structure into the minumum number of bytes.
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_ATTRIBUTE_PACKED \
   /** \cond never */ __attribute__((__packed__)) /** \endcond */
#else
#define VMK_ATTRIBUTE_PACKED
#endif

/*
 ***********************************************************************
 * VMK_LIKELY --                                                 */ /**
 *
 * \ingroup Compiler
 * \brief Branch prediction hint to the compiler that the supplied
 *        expression will likely evaluate to true.
 *
 * \param[in] _exp   Expression that will likely evaluate to true.
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_LIKELY(_exp)     __builtin_expect(!!(_exp), 1)
#else
#define VMK_LIKELY(_exp)      (_exp)
#endif

/*
 ***********************************************************************
 * VMK_UNLIKELY --                                                */ /**
 *
 * \ingroup Compiler
 * \brief Branch prediction hint to the compiler that the supplied
 *        expression will likely evaluate to false.
 *
 * \param[in] _exp   Expression that will likely evaluate to false.
 *
 ***********************************************************************
 */
#if (__GNUC__ >= 3)
#define VMK_UNLIKELY(_exp)   __builtin_expect((_exp), 0)
#else
#define VMK_UNLIKELY(_exp)   (_exp)
#endif

/*
 ***********************************************************************
 * VMK_PADDED_STRUCT --                                           */ /**
 *
 * \ingroup Compiler
 * \brief Macro used for padding a struct
 *
 * \param _align_sz_ Align the struct to this size
 * \param _fields_ fields of the struct
 *
 ***********************************************************************
 */
#define VMK_PADDED_STRUCT(_align_sz_, _fields_)                   \
   union {                                                        \
      struct _fields_;                                            \
      char pad[((((sizeof(struct _fields_)) + (_align_sz_) - 1) / \
                (_align_sz_)) * (_align_sz_))]; \
   };

/*
 ***********************************************************************
 * vmk_offsetof --                                               */ /**
 * \ingroup Compiler
 *
 * \brief Get the offset of a member of in a type
 *
 * \param[in]  TYPE     Type the member is a part of.
 * \param[in]  MEMBER   Member to get the offset of.
 *
 * \returns The offset in bytes of MEMBER in TYPE.
 *
 ***********************************************************************
 */
#define vmk_offsetof(TYPE, MEMBER) ((vmk_size_t) &((TYPE *)0)->MEMBER)

#endif /* _VMKAPI_COMPILER_H_ */
/** @} */
