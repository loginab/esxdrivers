/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: sa_hash.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_HASH_H_
#define _LIBSA_HASH_H_

#include <linux/list.h>

/*
 * Hash table facility.
 */
struct sa_hash;

/*
 * Hash key value.
 */
typedef void *		sa_hash_key_t;		/* pointer hash key */
typedef u_int32_t	sa_hash_key32_t;	/* fixed-size 32-bit hash key */

struct sa_hash_type {
	u_int16_t	st_link_offset;	/* offset of linkage in the element */
	int		(*st_match)(const sa_hash_key_t, void *elem);
	u_int32_t	(*st_hash)(const sa_hash_key_t);
};

/*
 * Element linkage on the hash.
 * The collision list is circular.
 */
#define sa_hash_link    hlist_node

struct sa_hash *sa_hash_create(const struct sa_hash_type *, u_int32_t size);

void sa_hash_destroy(struct sa_hash *);

void *sa_hash_lookup(struct sa_hash *, const sa_hash_key_t);

void sa_hash_insert(struct sa_hash *, const sa_hash_key_t, void *elem);

void sa_hash_insert_next(struct sa_hash *, sa_hash_key32_t *,
			 sa_hash_key32_t min_key, sa_hash_key32_t max_key,
			 void *elem);

void *sa_hash_lookup_delete(struct sa_hash *, const sa_hash_key_t);

void sa_hash_iterate(struct sa_hash *,
			void (*callback)(void *entry, void *arg), void *arg);

#endif /* _LIBSA_HASH_H_ */
