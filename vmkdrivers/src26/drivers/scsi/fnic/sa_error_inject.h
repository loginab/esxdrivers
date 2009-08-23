/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: sa_error_inject.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_SA_ERROR_INJECT_H_
#define _LIBSA_SA_ERROR_INJECT_H_

/*
 * Support for artificially causing error cases in code for testing.
 *
 * This is enabled by defining SA_ERROR_INJECT_ENABLE
 *
 * Usage:  Each site uses the macro SA_ERROR_INJECT to decide whether
 * to take the error case path.  This injection macro drops out and returns 0
 * if injection is disabled.
 *
 * Injection is controlled by the environment variable "SA_INJECT_SITES"
 * or in the kernel by some string TBD set via TBD.
 *
 * If that environment variable is set to "list", the sites are merely listed.
 * Otherwise, it is expected to contain a comma-separated list of sites to
 * be enabled, along with the parameters for each.
 *
 * Each site element will be of the form
 *	<func>[:<line>=[<rate>][-<duration>]
 * <func> is the function containing the injection site.
 * <line> is the line number.  If not specified, the site element applies to
 * 		all sites in that function.
 * <rate> is the percentage (from 1 to 100) of the time that the error will
 *		be injected.  If not specified, the injection will happen just
 *		once the first time the site is entered.
 * <duration> is the number of consecutive failures that'll occur.
 *
 * Example:
 *	SA_INJECT_SITES=main=5,foo:440=-20
 *
 *	This causes any injection site in main() to fail 5% of the time and
 *	the injection site at foo() line 440 to occur the first 20 times.
 */

#define	SA_ERROR_INJECT_ENVVAR	"SA_INJECT_SITES"

#ifndef SA_ERROR_INJECT_ENABLE

#define	SA_ERROR_INJECT	0

static inline void sa_inject_init(void)
{
}

static inline void sa_inject_stats(void)
{
}

#else /* SA_ERROR_INJECT_ENABLE */

void	sa_inject_init(void);
void 	sa_inject_stats(void);

/*
 * Injection site data.
 */
struct sa_inject_site {
	struct sa_inject_site *sd_next;
	struct sa_inject_spec *sd_spec;		/* spec */
	const char	*sd_func;		/* function name */
	unsigned short	sd_line;		/* line number */
	unsigned char	sd_inject;		/* non-zero to inject error */
	unsigned int	sd_hit_count;		/* times site entered */
	unsigned int	sd_trigger;		/* update state at this count */
	unsigned int	sd_inject_count;	/* statistic on injection */
} __attribute__((aligned (64)));		/* for padding by linker */

/*
 * Linker features:  The section sa_inject_data contains the injection sites.
 * Symbols for the beginning and end of the section are provided by the linker.
 */
#define	SA_INJECT_DATA __attribute__((__section__("sa_inject_data")))

extern struct sa_inject_site __start_sa_inject_data[];
extern struct sa_inject_site __stop_sa_inject_data[];

void	sa_inject_update(struct sa_inject_site *);

#define	SA_ERROR_INJECT ({ 						\
		static struct sa_inject_site __site SA_INJECT_DATA = {	\
			.sd_func = __FUNCTION__,			\
			.sd_line = __LINE__,				\
		};							\
									\
		if (++__site.sd_hit_count == __site.sd_trigger)		\
			sa_inject_update(&__site); 			\
		if (__site.sd_inject) 					\
			__site.sd_inject_count++;			\
		__site.sd_inject;					\
	})

#endif /* SA_ERROR_INJECT_ENABLE */

#endif /* _LIBSA_SA_ERROR_INJECT_H_ */
