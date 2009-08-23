/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Utilities                                                      */ /**
 *
 * \defgroup Util Utilities
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_UTIL_H_
#define _VMKAPI_UTIL_H_

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
 * VMK_STRINGIFY --                                               */ /**
 *
 * \ingroup Util
 * \brief Turn a preprocessor variable into a string
 *
 * \param[in] v      A preprocessor variable to be converted to a
 *                   string.
 *
 ***********************************************************************
 */
/** \cond never */
#define __VMK_STRINGIFY(v) #v
/** \endcond never */
#define VMK_STRINGIFY(v) __VMK_STRINGIFY(v)

/*
 ***********************************************************************
 * VMK_UTIL_ROUNDUP --                                            */ /**
 *
 * \ingroup Util
 * \brief Round up a value X to the next multiple of Y.
 *
 * \param[in] x    Value to round up.
 * \param[in] y    Value to round up to the next multiple of.
 *
 * \returns Rounded up value.
 *
 ***********************************************************************
 */
#define VMK_UTIL_ROUNDUP(x, y)   ((((x)+(y)-1) / (y)) * (y))

#endif /* _VMKAPI_UTIL_H_ */
/** @} */
