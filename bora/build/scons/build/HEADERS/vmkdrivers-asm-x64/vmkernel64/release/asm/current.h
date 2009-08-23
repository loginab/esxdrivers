#ifndef _X86_64_CURRENT_H
#define _X86_64_CURRENT_H

#if !defined(__ASSEMBLY__) 
struct task_struct;

#include <asm/pda.h>

/**                                          
 *  get_current - Gets current task pointer for the current world. 
 *
 *  Gets current task pointer for the current world. 
 *
 *  RETURN VALUE:
 *  Pointer to the task struct of the running process.
 */                                          
/* _VMKLNX_CODECHECK_: get_current */
static inline struct task_struct *get_current(void) 
{ 
#if defined(__VMKLNX__)
	extern struct task_struct *vmklnx_GetCurrent(void);
	return vmklnx_GetCurrent();
#else /* !defined(__VMKLNX__) */
	struct task_struct *t = read_pda(pcurrent); 
	return t;
#endif /* defined(__VMKLNX__) */
} 

/**                                          
 *  current - Get current task pointer of current task       
 *                                           
 *  Returns a pointer to the task struct of the running task
 *                                           
 *  SYNOPSIS:
 *     #define current
 *                                           
 *  RETURN VALUE:                     
 *  Pointer to current task of type task_struct  
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: current */
#define current get_current()

#else

#ifndef ASM_OFFSET_H
#include <asm/asm-offsets.h> 
#endif

#define GET_CURRENT(reg) movq %gs:(pda_pcurrent),reg

#endif

#endif /* !(_X86_64_CURRENT_H) */
