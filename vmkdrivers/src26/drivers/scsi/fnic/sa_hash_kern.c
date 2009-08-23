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
 * $Id: sa_hash_kern.c 18557 2008-09-14 22:36:38Z jre $
 *
 * Version of sa_hash.c for the Linux kernel.  Uses RCU lists.
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_hash.h"

struct sa_hash {
	struct sa_hash_type sh_type;
	u_int32_t	sh_mask;	/* mask for the size of the table */
	u_int32_t	sh_entries;	/* number of entries now in the table */
	struct hlist_head sh_table[0];	/* table (will be allocated bigger) */
};

struct sa_hash_elem {		/* stand-in for the real client element */
	struct hlist_node elem_node;
};

static inline struct hlist_head *sa_hash_bucket(struct sa_hash *hp,
						sa_hash_key_t key)
{
	return &hp->sh_table[(*hp->sh_type.st_hash) (key) & hp->sh_mask];
}

struct sa_hash *sa_hash_create(const struct sa_hash_type *tp, uint32_t req_size)
{
	struct sa_hash *hp;
	u_int32_t size;
	size_t len;

	/*
	 * Pick power of 2 at least as big as size.
	 */
	for (size = 4; size < (1UL << 31); size <<= 1)
		if (size >= req_size)
			break;
	ASSERT(size >= req_size);
	ASSERT(size < (1UL << 31));
	ASSERT((size & (size - 1)) == 0);	/* test for a power of 2 */

	len = sizeof(*hp) + size * sizeof(struct hlist_head);
	hp = sa_malloc(len);
	if (hp) {
		memset(hp, 0, len);
		hp->sh_type = *tp;
		hp->sh_mask = size - 1;
	}
	return hp;
}

void sa_hash_destroy(struct sa_hash *hp)
{
	ASSERT(hp->sh_entries == 0);
	sa_free(hp);
}

void *sa_hash_lookup(struct sa_hash *hp, const sa_hash_key_t key)
{
	struct sa_hash_elem *ep;
	struct hlist_node *np;
	struct hlist_head *hhp;
	void *rp = NULL;

	hhp = sa_hash_bucket(hp, key);
	hlist_for_each_entry_rcu(ep, np, hhp, elem_node) {
		rp = (void *) ((char *) ep - hp->sh_type.st_link_offset);
		if ((*hp->sh_type.st_match)(key, rp))
			break;
		rp = NULL;
	}
	return rp;
}

void *sa_hash_lookup_delete(struct sa_hash *hp, const sa_hash_key_t key)
{
	struct sa_hash_elem *ep;
	struct hlist_node *np;
	struct hlist_head *hhp;
	void *rp = NULL;

	hhp = sa_hash_bucket(hp, key);
	hlist_for_each_entry_rcu(ep, np, hhp, elem_node) {
		rp = (void *) ((char *) ep - hp->sh_type.st_link_offset);
		if ((*hp->sh_type.st_match)(key, rp)) {
			hlist_del_rcu(np);
			hp->sh_entries--;
			break;
		}
		rp = NULL;
	}
	return (rp);
}

void sa_hash_insert(struct sa_hash *hp, const sa_hash_key_t key, void *ep)
{
	struct hlist_head *hhp;
	struct hlist_node *lp;	/* new link pointer */

	lp = (struct hlist_node *) ((char *) ep + hp->sh_type.st_link_offset);
	hhp = sa_hash_bucket(hp, key);
	hlist_add_head_rcu(lp, hhp);
	hp->sh_entries++;
	ASSERT(hp->sh_entries > 0);	/* check for overflow */
}

/*
 * Iterate through all hash entries.
 * For debugging.  This can be slow.
 */
void
sa_hash_iterate(struct sa_hash *hp,
		void (*callback) (void *ep, void *arg), void *arg)
{
	struct hlist_head *hhp;
	struct hlist_node *np;
	struct sa_hash_elem *ep;
	void *entry;
	int count = 0;

	for (hhp = hp->sh_table; hhp < &hp->sh_table[hp->sh_mask + 1]; hhp++) {
		hlist_for_each_entry_rcu(ep, np, hhp, elem_node) {
			entry = (void *) ((char *) ep -
					  hp->sh_type.st_link_offset);
			(*callback)(entry, arg);
			count++;
		}
	}
	if (count != hp->sh_entries)
		SA_LOG("sh_entries %d != count %d", hp->sh_entries, count);
	ASSERT(count == hp->sh_entries);
}
