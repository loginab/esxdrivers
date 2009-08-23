/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Configuration                                                  */ /**
 * \defgroup Configuration Configuration Options
 * 
 * The configuration option interface allows access to kernel
 * configuration options set by the user.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_CONFIG_H_
#define _VMKAPI_CONFIG_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"

/*
 * Useful configuration group and parameter names
 */

/** \brief Misc parameter group name */
#define CONFIG_GROUP_MISC "Misc"

/** \brief Disk parameter group name */
#define CONFIG_GROUP_DISK "Disk"

/** \brief Scsi parameter group name */
#define CONFIG_GROUP_SCSI "Scsi"

/** \brief Scsi parameter group name */
#define CONFIG_GROUP_NET "Net"


/** \brief Host Name parameter name */
#define CONFIG_PARAM_HOSTNAME "HostName"

/** \brief Host IP Address parameter name */
#define CONFIG_PARAM_MGMT_IPADDR "ManagementAddr"

/**
 * \brief Opaque handle to a configuration parameter.
 */
typedef vmk_uint64 vmk_ConfigParamHandle;

/*
 ***********************************************************************
 * vmk_ConfigParamOpen --                                         */ /**
 *
 * \ingroup Configuration
 * \brief Open a handle to a configuration parameter.
 *
 * \param[in]  groupName       A functional group name associated with 
 *                             configuration parameter.
 * \param[in]  paramName       A parameter name.
 * \param[out] handle          Handle to the configuration parameter.
 *
 * \retval     VMK_BAD_PARAM   The group name or configuration parameter 
 *                             name was invalid.
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_ConfigParamOpen(
   const char *groupName,
   const char *paramName, 
   vmk_ConfigParamHandle *handle);

/*
 ***********************************************************************
 * vmk_ConfigParamClose --                                        */ /**
 *
 * \ingroup Configuration
 * \brief Close a handle to a configuration parameter.
 *
 * \param[in]  handle          Handle to the configuration parameter.
 *
 * \retval     VMK_BAD_PARAM   The configuration handle was invalid
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_ConfigParamClose(
   vmk_ConfigParamHandle handle);

/*
 ***********************************************************************
 * vmk_ConfigParamGetUint --                                      */ /**
 *
 * \ingroup Configuration
 * \brief Get an UInt value associated with a configuration parameter
 *        handle.
 *
 * \param[in]  handle          Handle to the configuration parameter.
 * \param[out] value           unsigned integer value associated with 
 *                             configuration parameter handle.
 *
 * \retval     VMK_BAD_PARAM   Invalid handle or there is no unsigned.
 *                             integer value associated with the handle.
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_ConfigParamGetUint(
   vmk_ConfigParamHandle handle,
   unsigned *value);
/*
 ***********************************************************************
 * vmk_ConfigParamGetStringSize --                                */ /**
 *
 * \ingroup Configuration
 * \brief Get size of a buffer required to hold string value associated 
 *        with a configuration parameter handle.
 *
 * \param[in]  handle          Handle to the configuration parameter.
 * \param[out] size            Size of the buffer required to hold
 *                             string value (including terminating nul)
 *                             associated with configuration parameter
 *                             handle.
 *
 * \retval     VMK_BAD_PARAM   Invalid handle or there is no string value 
 *                             associated with the handle
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_ConfigParamGetStringSize(
   vmk_ConfigParamHandle handle,
   vmk_size_t *size);

/*
 ***********************************************************************
 * vmk_ConfigParamGetString --                                    */ /**
 *
 * \ingroup Configuration
 * \brief Get a string value associated with a configuration parameter
 *        handle.
 *
 * \param[in]  handle          Handle to the configuration parameter
 * \param[out] value           Location for a value associated with
 *                             configuration parameter handle to be
 *                             copied to.
 * \param[in]  size            Size of the buffer in bytes. The buffer
 *                             should have enough room for a terminating
 *                             nul.
 *
 * \retval     VMK_BAD_PARAM           Invalid handle or there is no
 *                                     string value associated with the
 *                                     handle.
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_ConfigParamGetString(
   vmk_ConfigParamHandle handle,
   char *value, 
   vmk_size_t size);

#endif
/** @} */
