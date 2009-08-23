/* **********************************************************
 * Copyright 1998 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Module                                                         */ /**
 * \defgroup Module Kernel Module Management
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_MODULE_H_
#define _VMKAPI_MODULE_H_

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

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_heap.h"
#include "base/vmkapi_util.h"
#include "base/vmkapi_module_int.h"
#include "base/vmkapi_compiler.h"

/**
 * \brief Opaque handle for a vmkernel module.
 *
 * \note A handle should never be printed directly. Instead, use
 *       vmk_ModuleGetDebugID to get a printable value.
 */
typedef int vmk_ModuleID;

/**
 * \brief Module stack element. 
 */
typedef struct vmk_ModInfoStack {
   /** \brief Module ID. */
   vmk_ModuleID modID;

   /** \brief Module function called. */ 
   void     *mod_fn;

   /** \brief Return address of caller. */
   void     *pushRA;
   
   /** \brief Next module stack element. */
   struct vmk_ModInfoStack *oldStack;
} vmk_ModInfoStack;

/**
 * \brief Guaranteed invalid module ID.
 */
#define VMK_INVALID_MODULE_ID    ((vmk_uint32)-1)

/**
 * \brief Module ID for vmkernel itself.
 */
#define VMK_VMKERNEL_MODULE_ID   0

/**
 * \brief The maximum length of a module name including the terminating nul.
 */
#define VMK_MODULE_NAME_MAX 32

/*
 ***********************************************************************
 * VMK_MODPARAM_NAMED --                                          */ /**
 *
 * \ingroup Module
 *
 * \brief Define a parameter set by the user during module load.
 *
 * \param[in] name   Name of the parameter.
 * \param[in] var    Name of variable to store parameter value.
 * \param[in] type   Type of the variable.
 * \param[in] desc   String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM_NAMED(name, var, type, desc)	\
   __VMK_MODPARAM_NAMED(name, var, type);		\
   __VMK_MODPARAM_DESC(name, desc)

/*
 ***********************************************************************
 * VMK_MODPARAM --                                                */ /**
 *
 * \ingroup Module
 * \brief Define a parameter set by the user during module load.
 *
 * \note This macro relies on having a variable with the same name
 *       as the parameter.  If your variable has a different name
 *       than the parameter name, use VMK_MODPARAM_NAMED.
 *
 * \param[in] name   Name of the parameter and variable.
 * \param[in] type   Type of the variable.
 * \param[in] desc   String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM(name, type, desc) \
   VMK_MODPARAM_NAMED(name, name, type, desc)

/*
 ***********************************************************************
 * VMK_MODPARAM_ARRAY_NAMED --                                    */ /**
 *
 * \ingroup Module
 *
 * \brief Define an array parameter that can be set by the user during
 *        module load.
 *
 * \param[in] name   Name of parameter.
 * \param[in] var    Name of array variable.
 * \param[in] type   Type of array elements.
 * \param[in] nump   Variable to store count of set elements.
 * \param[in] desc   String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM_ARRAY_NAMED(name, var, type, nump, desc)	\
  __VMK_MODPARAM_ARRAY_NAMED(name, var, type, nump);	        \
  __VMK_MODPARAM_DESC(name, desc)

/*
 ***********************************************************************
 * VMK_MODPARAM_ARRAY --                                          */ /**
 *
 * \ingroup Module
 *
 * \brief Define an array parameter that can be set by the user during
 *        module load.
 *
 * \note This macro relies on having a variable with the same name
 *       as the parameter. If your variable has a different name
 *       than the parameter name, use VMK_MODPARAM_NAMED.
 *
 * \param[in] name   Name of parameter and variable.
 * \param[in] type   Type of array elements.
 * \param[in] nump   Variable to store count of set elements.
 * \param[in] desc   String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM_ARRAY(name, type, nump, desc) \
     VMK_MODPARAM_ARRAY_NAMED(name, name, type, nump, desc)

/*
 ***********************************************************************
 * VMK_MODPARAM_STRING_NAMED --                                   */ /**
 *
 * \ingroup Module
 * \brief Define an string parameter that can be set by the user
 *        during module load.
 *
 * \note This creates a copy of the string; your variable must be an
 *       array of sufficient size to hold the copy.  If you do not
 *       need to modify the string consider using a charp type.
 *
 * \param[in] name      Name of parameter.
 * \param[in] string    Variable name for the string copy.
 * \param[in] len       Maximum length of string.
 * \param[in] desc      String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM_STRING_NAMED(name, string, len, desc)	\
   __VMK_MODPARAM_STRING_NAMED(name, string, len);		\
   __VMK_MODPARAM_DESC(name, desc)

/*
 ***********************************************************************
 * VMK_MODPARAM_STRING --                                         */ /**
 *
 * \ingroup Module
 * \brief Define an string parameter that can be set by the user
 *        during module load.
 *
 * \note This creates a copy of the string; your variable must be an
 *       array of sufficient size to hold the copy.  If you do not
 *       need to modify the string consider using a charp type.
 *
 * \param[in] name   Name of parameter and char array variable.
 * \param[in] len    Maximum length of string.
 * \param[in] desc   String describing the variable.
 *
 ***********************************************************************
 */
#define VMK_MODPARAM_STRING(name, len, desc) \
   VMK_MODPARAM_STRING_NAMED(name, name, len, desc)

/*
 ***********************************************************************
 * VMK_VERSION_INFO --                                            */ /**
 *
 * \ingroup Module
 * \brief String parameter describing version, build, etc. information.
 *
 * \param[in] string    A string to be embedded as version information.
 *
 ***********************************************************************
 */
#define VMK_VERSION_INFO(string) \
   __VMK_VERSION_INFO(string)

/*
 ***********************************************************************
 * vmk_ModuleRegister --                                          */ /**
 *
 * \ingroup Module
 * \brief Register a module with the VMKernel
 *
 * \pre The module shall not call any VMKernel function before this
 *      function has been invoked and has returned.
 *
 * \note A module should make a successful call to this function only
 *       once inside its initalization function, else undefined
 *       behavior may occur.
 *
 * \param[out] id                The address of a variable to store
 *                               the module's module ID handle.
 * \param[in] vmkApiModRevision  The module version for compatability
 *                               checks.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModuleRegister(
   vmk_ModuleID *id,
   vmk_uint32 vmkApiModRevision);

/*
 ***********************************************************************
 * vmk_ModuleUnregister --                                        */ /**
 *
 * \ingroup Module
 * \brief Unregister a module with the VMKernel
 *
 * \pre The module shall not have any VMKernel call in progress at
 *      the time this function is invoked, nor initiate any VMKernel
 *      call after it has been invoked.
 *
 * \note The module ID handle will be invalid after the success of
 *       this call and should not be used again.
 *
 * \param[in] id  The module ID handle to unregister.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModuleUnregister(
   vmk_ModuleID id);

/*
 ***********************************************************************
 * vmk_ModuleSetHeapID --                                         */ /**
 *
 * \ingroup Module
 * \brief Set a module's default heap
 *
 * Any vmkapi call that does not take an explicity heap that also has
 * a side effect of allocating storage will use the heap passed to this
 * function.
 *
 * \pre The default heap shall not be changed until all objects
 *      on the existing allocated module heap have been freed.
 *
 ***********************************************************************
 */
void vmk_ModuleSetHeapID(
   vmk_ModuleID module,
   vmk_HeapID heap);

/*
 ***********************************************************************
 * vmk_ModuleGetHeapID --                                         */ /**
 *
 * \ingroup Module
 * \brief Query a module's default heap
 *
 * \return The calling module's current default heap.
 * \retval VMK_INVALID_HEAP_ID The module has no default heap.
 *
 ***********************************************************************
 */
vmk_HeapID vmk_ModuleGetHeapID(
   vmk_ModuleID module);

/*
 ***********************************************************************
 * vmk_ModuleGetID --                                             */ /**
 *
 * \ingroup Module
 * \brief Get the identifier of the VMKernel module
 *
 * \param[in] moduleName   Name of the module to find.
 *
 * \return The module ID of the module with the specified name.
 * \retval VMK_INVALID_MODULE_ID    No module with the specified name
 *                                  was found.
 *
 ***********************************************************************
 */
vmk_ModuleID vmk_ModuleGetID(
   const char *moduleName);

/*
 ***********************************************************************
 * vmk_ModuleGetName --                                           */ /**
 *
 * \ingroup Module
 * \brief Get the name associated with a module.
 *
 * \note This call will return an error when called to retrieve the
 *       name of a module that has not yet returned from the module
 *       init function.
 *
 * \param[in]  module      The module ID to query.
 * \param[out] moduleName  A character buffer large enough to hold the
 *                         module name including the terminating nul.
 * \param[in]  len         The length of the character buffer in bytes.
 *
 * \retval VMK_NOT_FOUND   The module ID was not found.
 * \retval VMK_BAD_PARAM   The buffer isn't large enough to hold
 *                         the module's string name. 
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModuleGetName(
   vmk_ModuleID module,
   char *moduleName,
   vmk_uint32 len);

/*
 ***********************************************************************
 * vmk_ModuleGetDebugID --                                        */ /**
 *
 * \ingroup Module
 * \brief Convert a vmk_ModuleID to a 64-bit integer representation.
 *        This should not be used be used for anything other than a
 *        short-hand in debugging output.
 *
 * \param[in] module    The module id.
 *
 ***********************************************************************
 */
vmk_uint64 vmk_ModuleGetDebugID(vmk_ModuleID module);

/*
 ***********************************************************************
 * vmk_ModuleIncUseCount --                                       */ /**
 *
 * \ingroup Module
 * \brief Increment a module's reference count
 *
 * Any attempt to remove the module with \c vmkload_mod -u will fail
 * while the module's reference count is non nul.
 *
 * \param[in] module    Module to increment the reference count for.
 *
 * \retval VMK_OK                   The reference count was successfully
 *                                  incremented
 * \retval VMK_NOT_FOUND            The module doesn't exist
 * \retval VMK_MODULE_NOT_LOADED    The module is being unloaded
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModuleIncUseCount(
   vmk_ModuleID module);

/*
 ***********************************************************************
 * vmk_ModuleDecUseCount --                                       */ /**
 *
 * \ingroup Module
 * \brief Decrement a module's reference count.
 *
 * \param[in] module    Module to decrement the reference count for.
 *
 * \retval VMK_OK                   The reference count was successfully
 *                                  decremented.
 * \retval VMK_NOT_FOUND            The module doesn't exist.
 * \retval VMK_MODULE_NOT_LOADED    The module is being unloaded.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModuleDecUseCount(
   vmk_ModuleID module);

/*
 ***********************************************************************
 * vmk_ModulePushId --                                            */ /**
 *
 * \ingroup Module
 * \brief Push moduleID onto module tracking stack before an
 *        inter-module call.
 *
 * \deprecated This call should no longer be called directly as it is
 *             likely to go away in a future release.
 *
 * \param[in] moduleID     Module ID from which the inter-module call
 *                         is to be made.
 * \param[in] function     Address of the inter-module function call
 * \param[in] modStack     Pointer to a vmk_ModInfoStack struct, 
 *                         preferrably on the stack.
 *
 * \retval VMK_OK                   The moduleID was sucessfully pushed
 *                                  onto the module stack
 * \retval VMK_MODULE_NOT_LOADED    Module was not found
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ModulePushId(
    vmk_ModuleID moduleID,
    void *function,
    vmk_ModInfoStack *modStack);

/*
 ***********************************************************************
 * vmk_ModulePopId --                                             */ /**
 *
 * \ingroup Module
 * \brief Pop moduleID off of module tracking stack after an
 *        inter-module call.
 *
 * \deprecated This call should no longer be called directly as it is
 *             likely to go away in a future release.
 * 
 ***********************************************************************
 */
void vmk_ModulePopId(void);

/*
 ***********************************************************************
 * vmk_ModuleStackTop --                                          */ /**
 *
 * \ingroup Module
 * \brief Get the latest moduleID pushed onto the module tracking stack.
 *
 * \retval The moduleID at the top of the module tracking stack.
 * 
 ***********************************************************************
 */
vmk_ModuleID vmk_ModuleStackTop(void);

/*
 ***********************************************************************
 * VMKAPI_MODULE_CALL --                                          */ /**
 *
 * \ingroup Module
 * \brief Macro wrapper for inter-module calls that return a value.
 *
 * This wrapper should always be used when calling into another module
 * so that vmkernel can properly track resources associated with
 * a call.
 *
 * \param[in]     moduleID       moduleID of the calling module.
 * \param[out]    returnValue    Variable to hold the return value from
 *                               the called function.
 * \param[in]     function       Inter-module function call to be
 *                               invoked.
 * \param[in,out] args           Arguments to pass to the inter-module
 *                               function call.
 * 
 ***********************************************************************
 */
#define VMKAPI_MODULE_CALL(moduleID, returnValue, function, args...)    \
do {                                                                    \
    vmk_ModInfoStack modStack;						\
    vmk_ModulePushId(moduleID, function, &modStack) ;                   \
    returnValue = (function)(args);                                     \
    vmk_ModulePopId();                                                  \
} while(0)

/*
 ***********************************************************************
 * VMKAPI_MODULE_CALL_VOID --                                     */ /**
 *
 * \ingroup Module
 * \brief Macro wrapper for inter-module calls that do not return
 *        a value.
 *
 * This wrapper should always be used when calling into another module
 * so that vmkernel can properly track resources associated with
 * a call.
 *
 * \param[in]     moduleID    moduleID of the calling module
 * \param[in]     function    Inter-module function call to be invoked
 * \param[in,out] args        Arguments to pass to the inter-module
 *                            function call
 * 
 ***********************************************************************
 */
#define VMKAPI_MODULE_CALL_VOID(moduleID, function, args...)    \
do {                                                            \
    vmk_ModInfoStack modStack;					\
    vmk_ModulePushId(moduleID, function, &modStack);            \
    (function)(args);                                           \
    vmk_ModulePopId();                                          \
} while(0)

/*
 ***********************************************************************
 * VMK_MODULE_EXPORT_SYMBOL --                                    */ /**
 *
 * \ingroup Module
 * \brief Mark a symbol as exported
 *
 * Mark the given symbol as exported, and hence available for other
 * modules to find/call.
 *
 * \param[in] __symname    The symbol to export.
 *
 ***********************************************************************
 */
#define VMK_MODULE_EXPORT_SYMBOL(__symname)     \
   __VMK_MODULE_EXPORT_SYMBOL(__symname)

#endif /* _VMKAPI_MODULE_H_ */
/** @} */
