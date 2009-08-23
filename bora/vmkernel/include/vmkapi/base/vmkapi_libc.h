/* **********************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * LibC                                                           */ /**
 * \defgroup LibC C-Library-Style Interfaces
 *
 * @{
 ***********************************************************************
 */ 

#ifndef _VMKAPI_LIBC_H_
#define _VMKAPI_LIBC_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_compiler.h"

#include <stdarg.h>

/*
 ***********************************************************************
 * vmk_Strnlen --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Determine the length of a string up to a maximum number
 *        of bytes.
 *
 * \param[in] src    String whose length is to be determined.
 * \param[in] maxLen Maximum number of bytes to check.
 *
 * \return Length of the string in bytes.
 *
 ***********************************************************************
 */
vmk_size_t vmk_Strnlen (
   const char *src,
   vmk_size_t maxLen);

/*
 ***********************************************************************
 * vmk_Strcpy --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Copy a string up to a maximum number of bytes.
 *
 * \param[out] dst   String to copy to.
 * \param[in]  src   String to copy from.
 *
 * \return Pointer to src.
 *
 ***********************************************************************
 */
char *vmk_Strcpy(
   char *dst,
   const char *src);

/*
 ***********************************************************************
 * vmk_Strncpy --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Copy a string up to a maximum number of bytes.
 *
 * \param[out] dst      String to copy to.
 * \param[in]  src      String to copy from.
 * \param[in]  maxLen   Maximum number of bytes to copy into dst.
 *
 * \warning A copied string is not automatically null terminated.
 *          Users should take care to ensure that the destination
 *          is large enough to hold the string and the nul terminator.
 *
 * \return Pointer src.
 *
 ***********************************************************************
 */
char *vmk_Strncpy(
   char *dst,
   const char *src,
   vmk_size_t maxLen);

/*
 ***********************************************************************
 * vmk_Strlcpy --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Copy a string and ensure nul termination.
 *
 * \param[out] dst   String to copy to.
 * \param[in]  src   String to copy from.
 * \param[in]  size  Maximum number of bytes to store a string in dst.
 *
 * \note At most size-1 characters will be copied. All copies will
 *       be terminated with a nul unless size is set to zero.
 *
 * \return The length of src. If the return value is greater than
 *         size then truncation has occured during the copy.
 *
 ***********************************************************************
 */
vmk_size_t vmk_Strlcpy(
   char *dst,
   const char *src,
   vmk_size_t size);

/*
 ***********************************************************************
 * vmk_Strcmp --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Compare two strings.
 *
 * \param[in] src1      First string to compare.
 * \param[in] src2      Second string to compare.
 *
 * \return A value greater than, less than or equal to 0 depending on
 *         the lexicographical difference between the strings.
 *
 ***********************************************************************
 */
int vmk_Strcmp(
   const char *src1,
   const char *src2);

/*
 ***********************************************************************
 * vmk_Strncmp --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Compare two strings up to a maximum number of bytes.
 *
 * \param[in] src1      First string to compare.
 * \param[in] src2      Second string to compare.
 * \param[in] maxLen    Maximum number of bytes to compare.
 *
 * \return A value greater than, less than or equal to 0 depending on
 *         the lexicographical difference between the strings.
 *
 ***********************************************************************
 */
int vmk_Strncmp(
   const char *src1,
   const char *src2,
   vmk_size_t maxLen);

/*
 ***********************************************************************
 * vmk_Strncasecmp --                                             */ /**
 *
 * \ingroup LibC
 * \brief Compare two strings while disregarding case.
 *
 * \param[in] src1      First string to compare.
 * \param[in] src2      Second string to compare.
 * \param[in] maxLen    Maximum number of bytes to compare.
 *
 * \return A value greater than, less than or equal to 0 depending on
 *         the lexicographical difference between the strings as if
 *         all characters in the string are lower-case.
 *
 ***********************************************************************
 */
int vmk_Strncasecmp(
   const char *src1,
   const char *src2,
   vmk_size_t maxLen);

/*
 ***********************************************************************
 * vmk_Strchr --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Forward search through a string for a character.
 *
 * \param[in] src  String to search forward.
 * \param[in] c    Character to search for.
 *
 * \return Pointer to the offset in src where c was found or NULL
 *         if c was not found in src.
 *
 ***********************************************************************
 */
char *vmk_Strchr(
   const void *src,
   int c);

/*
 ***********************************************************************
 * vmk_Strrchr --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Reverse search through a string for a character.
 *
 * \param[in] src  String to search backward.
 * \param[in] c    Character to search for.
 *
 * \return Pointer to the offset in src where c was found or NULL
 *         if c was not found in src.
 *
 ***********************************************************************
 */
char *vmk_Strrchr(
   const void *src,
   int c);

/*
 ***********************************************************************
 * vmk_Strstr --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Search for a substring in a string.
 *
 * \param[in] s1  String to search.
 * \param[in] s2  String to search for.
 *
 * \return Pointer to the offset in s1 where s2 was found or NULL
 *         if s2 was not found in s1.
 *
 ***********************************************************************
 */
char *vmk_Strstr(
   const void *s1,
   const void *s2);

/*
 ***********************************************************************
 * vmk_Strtol --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Convert a string to a signed long integer.
 *
 * \param[in]  str   String to convert
 * \param[out] end   If non-NULL, the address of the first invalid
 *                   character or the value of str if there are no
 *                   digits in the string.
 * \param[in]  base  Base of the number being converted which may be
 *                   between 2 and 36, or 0 may be supplied such
 *                   that strtoul will attempt to detect the base
 *                   of the number in the string.
 *
 * \return Numeric value of the string.
 *
 ***********************************************************************
 */
long vmk_Strtol(
   const char *str,
   char **end,
   int base);

/*
 ***********************************************************************
 * vmk_Strtoul --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Convert a string to an unsigned long integer.
 *
 * \param[in]  str   String to convert
 * \param[out] end   If non-NULL, the address of the first invalid
 *                   character or the value of str if there are no
 *                   digits in the string.
 * \param[in]  base  Base of the number being converted which may be
 *                   between 2 and 36, or 0 may be supplied such
 *                   that strtoul will attempt to detect the base
 *                   of the number in the string.
 *
 * \return Numeric value of the string.
 *
 ***********************************************************************
 */
unsigned long vmk_Strtoul(
   const char *str,
   char **end,
   int base);

/*
 ***********************************************************************
 * vmk_Sprintf --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Formatted print to a string.
 *
 * \param[in] str       Buffer in which to place output.
 * \param[in] format    Printf-style format string.
 *
 * \return Number of characters output.
 *
 ***********************************************************************
 */
int vmk_Sprintf(
   char *str,
   const char *format,
   ...)
VMK_ATTRIBUTE_PRINTF(2,3);

/*
 ***********************************************************************
 * vmk_Snprintf --                                                */ /**
 *
 * \ingroup LibC
 * \brief Formatted print to a string with a maximum bound.
 *
 * \param[in] str       Buffer in which to place output.
 * \param[in] size      Maximum number of bytes to output.
 * \param[in] format    Printf-style format string.
 *
 * \return Number of characters output.
 *
 ***********************************************************************
 */
int vmk_Snprintf(
   char *str,
   vmk_size_t size,
   const char *format,
   ...)
VMK_ATTRIBUTE_PRINTF(3,4);


/*
 ***********************************************************************
 * vmk_Vsprintf --                                                */ /**
 *
 * \ingroup LibC
 * \brief Formatted print to a string using varargs.
 *
 * \param[in] str       Buffer in which to place output.
 * \param[in] format    Printf-style format string.
 * \param[in] ap        Varargs for format.
 *
 * \return Number of characters output.
 *
 ***********************************************************************
 */
int
vmk_Vsprintf(
   char *str,
   const char *format,
   va_list ap);

/*
 ***********************************************************************
 * vmk_Vsnprintf --                                                */ /**
 *
 * \ingroup LibC
 * \brief Formatted print to a string with a maixmum bound using varargs.
 *
 * \param[in] str       Buffer in which to place output.
 * \param[in] size      Maximum number of bytes to output.
 * \param[in] format    Printf-style format string.
 * \param[in] ap        Varargs for format.
 *
 * \return Number of characters output.
 *
 ***********************************************************************
 */
int
vmk_Vsnprintf(
   char *str,
   vmk_size_t size,
   const char *format,
   va_list ap);

/*
 ***********************************************************************
 * vmk_Vsscanf  --                                                */ /**
 *
 * \ingroup LibC
 * \brief Formatted scan of a string using varargs.
 *
 * \param[in] inp    Buffer to scan.
 * \param[in] fmt0   Scanf-style format string.
 * \param[in] ap     Varargs that hold input values for format.
 *
 * \return Number of input values assigned.
 *
 ***********************************************************************
 */
int
vmk_Vsscanf(
    const char *inp, 
    char const *fmt0, 
    va_list ap);

/*
 ***********************************************************************
 * vmk_Sscanf   --                                                */ /**
 *
 * \ingroup LibC
 * \brief Formatted scan of a string.
 *
 * \param[in] ibuf   Buffer to scan.
 * \param[in] fmt    Scanf-style format string.
 *
 * \return Number of input values assigned.
 *
 ***********************************************************************
 */
int 
vmk_Sscanf(
    const char *ibuf, 
    const char *fmt,
    ...)
VMK_ATTRIBUTE_SCANF(2, 3);

/*
 ***********************************************************************
 * vmk_Memset --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Set bytes in a buffer to a particular value.
 *
 * \param[in] dst    Destination buffer to set.
 * \param[in] byte   Value to set each byte to.
 * \param[in] len    Number of bytes to set.
 *
 * \return Original value of dst.
 *
 ***********************************************************************
 */
void *vmk_Memset(
   void *dst,
   int byte,
   vmk_size_t len);

/*
 ***********************************************************************
 * vmk_Memcpy --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Copy bytes from one buffer to another.
 *
 * \param[in] dst    Destination buffer to copy to.
 * \param[in] src    Source buffer to copy from.
 * \param[in] len    Number of bytes to copy.
 *
 * \return Original value of dst.
 *
 ***********************************************************************
 */
void *vmk_Memcpy(
   void *dst,
   const void *src,
   vmk_size_t len);

/*
 ***********************************************************************
 * vmk_Memcmp --                                                  */ /**
 *
 * \ingroup LibC
 * \brief Compare bytes between two buffers.
 *
 * \param[in] src1   Buffer to compare.
 * \param[in] src2   Other buffer to compare.
 * \param[in] len    Number of bytes to compare.
 *
 * \return Difference between the first two differing bytes or
 *         zero if the buffers are identical over the specified length.
 *
 ***********************************************************************
 */
int vmk_Memcmp(
   const void *src1,
   const void *src2,
   vmk_size_t len);

/*
 ***********************************************************************
 * vmk_Rand --                                                    */ /**
 *
 * \ingroup LibC
 * \brief Generate a pseudo random number
 *
 * \note This function requires that a new seed value be passed in for
 *       each call. This can be done by incrementing the seed value or
 *       by using vmk_GetRandSeed each time.
 *
 * \warning This function's random numbers do not have high enough
 *          quality to be useful for encryption purposes.
 *
 * \param[in] seed   Seed number to random number algorithm.
 *
 * \return A pseudo random number.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_Rand(
   vmk_uint32 seed);

/*
 ***********************************************************************
 * vmk_GetRandSeed --                                             */ /**
 *
 * \ingroup LibC
 * \brief Generate a seed for vmk_Rand()
 *
 * \return A seed value suitable for vmk_Rand()
 *
 ***********************************************************************
 */
vmk_uint32
vmk_GetRandSeed(
   void);

/*
 ***********************************************************************
 * vmk_IsPrint --                                                 */ /**
 *
 * \ingroup LibC
 * \brief Determine if a character is printable.
 *
 * \param[in] c  Character to check.
 *
 * \retval VMK_TRUE  Supplied character is printable.
 * \retval VMK_FALSE Supplied character is not printable.
 *
 ***********************************************************************
 */
vmk_Bool
vmk_IsPrint(
   int c);

#endif /* _VMKAPI_LIBC_H_ */
/** @} */
