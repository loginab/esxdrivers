#ifndef _ASM_GENERIC_PAGE_H
#define _ASM_GENERIC_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/compiler.h>

/* Pure 2^n version of get_order */
/**                                          
 *  get_order - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: get_order */
static __inline__ __attribute_const__ int get_order(unsigned long size)
{
	int order;

	size = (size - 1) >> (PAGE_SHIFT - 1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#endif	/* __ASSEMBLY__ */
#endif	/* __KERNEL__ */

#endif	/* _ASM_GENERIC_PAGE_H */
