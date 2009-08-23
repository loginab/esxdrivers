/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 * Module internal macros, defines, etc.
 *
 * Do not directly include or use the interfaces
 * provided in this header; only use those provided by
 * vmkapi_module.h.
 */
/** \cond nodoc */

#ifndef _VMKAPI_MODULE_PARAM_H_
#define _VMKAPI_MODULE_PARAM_H_

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
 * Internal macro-machinery for parameter handling
 */
#define __vmk_ModuleParamType_int          1
#define __vmk_ModuleParamType_int_array    2
#define __vmk_ModuleParamType_uint         3
#define __vmk_ModuleParamType_uint_array   4
#define __vmk_ModuleParamType_long         5
#define __vmk_ModuleParamType_long_array   6
#define __vmk_ModuleParamType_ulong        7
#define __vmk_ModuleParamType_ulong_array  8
#define __vmk_ModuleParamType_short        9
#define __vmk_ModuleParamType_short_array  10
#define __vmk_ModuleParamType_ushort       11
#define __vmk_ModuleParamType_ushort_array 12
#define __vmk_ModuleParamType_string       13
#define __vmk_ModuleParamType_charp        14
#define __vmk_ModuleParamType_bool         15
#define __vmk_ModuleParamType_byte         16

#define VMK_PARAM(type) VMK_PARAM_##type = __vmk_ModuleParamType_##type

typedef enum {
   VMK_PARAM(int),
   VMK_PARAM(int_array),
   VMK_PARAM(uint),
   VMK_PARAM(uint_array),
   VMK_PARAM(long),
   VMK_PARAM(long_array),
   VMK_PARAM(ulong),
   VMK_PARAM(ulong_array),
   VMK_PARAM(short),
   VMK_PARAM(short_array),
   VMK_PARAM(ushort),
   VMK_PARAM(ushort_array),
   VMK_PARAM(string),
   VMK_PARAM(charp),
   VMK_PARAM(bool),
   VMK_PARAM(byte)
} vmk_ModuleParamType;

struct vmk_ModuleParam {
   const char *name;
   vmk_ModuleParamType type;
   union {
      void *arg;
      struct {
	 char *arg;
	 int maxlen;
      } string;
      struct {
	 void *arg;
	 int maxlen;
	 int *nump;
      } array;
   } param;
};

#define VMK_PARAM_SEC ".vmkmodparam"
#define VMK_MODINFO_SEC ".vmkmodinfo"

#define __VMK_MODPARAM_NAME(name, type)      	\
    const char __module_param_##name[]		\
    __attribute__((section(VMK_MODINFO_SEC))) =	\
    "param_" #name ":" type

#define __VMK_MODPARAM_DESC(name, desc)		\
    const char __module_desc_##name[]		\
    __attribute__((section(VMK_MODINFO_SEC))) =	\
    "param_desc_" #name "=" desc

/* Required attributes for variables in VMK_PARAM_SEC */
#define __VMK_MODPARAM_ATTRS				\
   __attribute__ (( __section__(VMK_PARAM_SEC) ))	\
   __attribute__ (( __used__ ))				\
   __attribute__ (( aligned(sizeof(void*)) ))

/* Basic type */
#define __VMK_MODPARAM_NAMED(__name, __var, __type)	\
   static char __vmk_param_str_##__name[] = #__name;	\
   static struct vmk_ModuleParam const __param_##__name	\
     __VMK_MODPARAM_ATTRS				\
      = {						\
         .name = __vmk_param_str_##__name,		\
         .type = __vmk_ModuleParamType_##__type,	\
         .param.arg = &__var,				\
        };						\
   __VMK_MODPARAM_NAME(__name, #__type)

/* Array */
#define __VMK_MODPARAM_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define __VMK_MODPARAM_ARRAY_NAMED(__name, __array, __type, __nump)	\
   static char __vmk_param_str_##__name[] = #__name;			\
   static struct vmk_ModuleParam const __param_##__name			\
     __VMK_MODPARAM_ATTRS						\
      = {								\
          .name = __vmk_param_str_##__name,				\
          .type = __vmk_ModuleParamType_##__type##_array,		\
          .param.array.arg = __array,					\
          .param.array.maxlen = __VMK_MODPARAM_ARRAY_SIZE(__array),	\
          .param.array.nump = __nump,					\
        };								\
   __VMK_MODPARAM_NAME(__name, "array of " #__type)

/* String */
#define __VMK_MODPARAM_STRING_NAMED(__name, __string, __max)	\
   static char __vmk_param_str_##__name[] = #__name;		\
   static struct vmk_ModuleParam const __param_##__name		\
     __VMK_MODPARAM_ATTRS					\
      = {							\
         .name = __vmk_param_str_##__name,			\
         .type = __vmk_ModuleParamType_string,			\
         .param.string.arg = __string,				\
         .param.string.maxlen = __max,				\
        };							\
   __VMK_MODPARAM_NAME(__name, "string")


/*
 * Version Information
 */

#define VMK_VERSION_INFO_SYM              __vmk_versionInfo_str
#define VMK_VERSION_INFO_TAG              "version="
#define VMK_VERSION_INFO_TAG_LEN          (sizeof(VMK_VERSION_INFO_TAG)-1)

#define __VMK_VERSION_INFO(__string)                                    \
   const static char VMK_VERSION_INFO_SYM[]                             \
   __attribute__((used))                                                \
   __attribute__((section(VMK_MODINFO_SEC))) = VMK_VERSION_INFO_TAG __string

/*
 * Symbol Exports
 */

#define VMK_EXPORT_SYMBOL_SEC ".vmksymbolexports"

struct vmk_ExportSymbol {
   char *name;
   enum {
      VMK_EXPORT_SYMBOL_DEFAULT,
   } exportType;
};

#define __VMK_EXPORT_SYMBOL_ATTRS                               \
   __attribute__ (( __section__(VMK_EXPORT_SYMBOL_SEC) ))	\
   __attribute__ (( __used__ ))                                 \
   __attribute__ (( aligned(sizeof(void*)) ))

#define __VMK_MODULE_EXPORT_SYMBOL(__symname)                           \
   static char __vmk_symbol_str_##__symname[] = #__symname;             \
   static struct vmk_ExportSymbol const __vmk_symbol_##__symname        \
   __VMK_EXPORT_SYMBOL_ATTRS                                            \
   = {                                                                  \
      .name = __vmk_symbol_str_##__symname,                             \
      .exportType = VMK_EXPORT_SYMBOL_DEFAULT,                          \
   };

#endif /* _VMKAPI_MODULE_PARAM_H_ */
/** \endcond nodoc */
