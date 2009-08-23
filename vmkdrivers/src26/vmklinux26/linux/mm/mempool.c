/*
 *  linux/mm/mempool.c
 *
 *  memory buffer pool support. Such pools are mostly used
 *  for guaranteed, deadlock-free memory allocations during
 *  extreme VM load.
 *
 *  started by Ingo Molnar, Copyright (C) 2001
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#if !defined(__VMKLNX__)
#include <linux/writeback.h>
#endif /* defined(__VMKLNX__) */

static void add_element(mempool_t *pool, void *element)
{
	BUG_ON(pool->curr_nr >= pool->min_nr);
	pool->elements[pool->curr_nr++] = element;
}

static void *remove_element(mempool_t *pool)
{
	BUG_ON(pool->curr_nr <= 0);
	return pool->elements[--pool->curr_nr];
}

static void free_pool(mempool_t *pool)
{
	while (pool->curr_nr) {
		void *element = remove_element(pool);
#if defined(__VMKLNX__)
                VMKAPI_MODULE_CALL_VOID(pool->module_id, pool->free, element, 
                                        pool->pool_data);
#else /* !defined(__VMKLNX__) */
		pool->free(element, pool->pool_data);
#endif /* defined(__VMKLNX__) */
	}
	kfree(pool->elements);
	kfree(pool);
}

/**
 * mempool_create - create a memory pool
 * @min_nr: the minimum number of elements guaranteed to be
 *          allocated for this pool.
 * @alloc_fn: user-defined element-allocation function.
 * @free_fn: user-defined element-freeing function.
 * @pool_data: optional private data available to the user-defined functions.
 *
 * Create and allocate a guaranteed size, preallocated
 * memory pool. The pool can be used from the mempool_alloc and mempool_free
 * functions. mempool_create() might sleep. Both the alloc_fn() and the free_fn()
 * functions might sleep - as long as the mempool_alloc function is not called
 * from IRQ contexts.
 *
 * RETURN VALUE:
 * a pointer to a mempool descriptor on success; otherwise a NULL.
 *
 * SEE ALSO:
 * mempool_create_kmalloc_pool() and mempool_create_slab_pool()
 */
/* _VMKLNX_CODECHECK_: mempool_create */
mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
				mempool_free_t *free_fn, void *pool_data)
{
	return  mempool_create_node(min_nr,alloc_fn,free_fn, pool_data,-1);
}
EXPORT_SYMBOL(mempool_create);

mempool_t *mempool_create_node(int min_nr, mempool_alloc_t *alloc_fn,
			mempool_free_t *free_fn, void *pool_data, int node_id)
{
	mempool_t *pool;
	pool = kmalloc_node(sizeof(*pool), GFP_KERNEL, node_id);
	if (!pool)
		return NULL;
	memset(pool, 0, sizeof(*pool));
	pool->elements = kmalloc_node(min_nr * sizeof(void *),
					GFP_KERNEL, node_id);
	if (!pool->elements) {
		kfree(pool);
		return NULL;
	}
	spin_lock_init(&pool->lock);
	pool->min_nr = min_nr;
	pool->pool_data = pool_data;
	init_waitqueue_head(&pool->wait);
	pool->alloc = alloc_fn;
	pool->free = free_fn;
        
#if defined(__VMKLNX__)
        pool->module_id = vmk_ModuleStackTop();
#endif /* defined(__VMKLNX__) */

	/*
	 * First pre-allocate the guaranteed number of buffers.
	 */
	while (pool->curr_nr < pool->min_nr) {
		void *element;

#if defined(__VMKLNX__)
                VMKAPI_MODULE_CALL(pool->module_id, element, pool->alloc, 
                                   GFP_KERNEL, pool->pool_data);
#else /* !defined(__VMKLNX__) */
		element = pool->alloc(GFP_KERNEL, pool->pool_data);
#endif /* defined(__VMKLNX__) */
		if (unlikely(!element)) {
			free_pool(pool);
			return NULL;
		}
		add_element(pool, element);
	}
	return pool;
}
EXPORT_SYMBOL(mempool_create_node);

/**
 * mempool_resize - resize an existing memory pool
 * @pool:       pointer to the memory pool which was allocated via
 *              mempool_create().
 * @new_min_nr: the new minimum number of elements guaranteed to be
 *              allocated for this pool.
 * @gfp_mask:   the usual allocation bitmask.
 *
 * This function shrinks/grows the pool. In the case of growing,
 * it cannot be guaranteed that the pool will be grown to the new
 * size immediately, but new mempool_free() calls will refill it.
 *
 * Note, the caller must guarantee that no mempool_destroy is called
 * while this function is running. mempool_alloc() & mempool_free()
 * might be called (eg. from IRQ contexts) while this function executes.
 */
int mempool_resize(mempool_t *pool, int new_min_nr, gfp_t gfp_mask)
{
	void *element;
	void **new_elements;
	unsigned long flags;

	BUG_ON(new_min_nr <= 0);

	spin_lock_irqsave(&pool->lock, flags);
	if (new_min_nr <= pool->min_nr) {
		while (new_min_nr < pool->curr_nr) {
			element = remove_element(pool);
			spin_unlock_irqrestore(&pool->lock, flags);
#if defined(__VMKLNX__)
                        VMKAPI_MODULE_CALL_VOID(pool->module_id, pool->free, 
                                                element, pool->pool_data);
#else /* !defined(__VMKLNX__) */
			pool->free(element, pool->pool_data);
#endif /* defined(__VMKLNX__) */
			spin_lock_irqsave(&pool->lock, flags);
		}
		pool->min_nr = new_min_nr;
		goto out_unlock;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* Grow the pool */
	new_elements = kmalloc(new_min_nr * sizeof(*new_elements), gfp_mask);
	if (!new_elements)
		return -ENOMEM;

	spin_lock_irqsave(&pool->lock, flags);
	if (unlikely(new_min_nr <= pool->min_nr)) {
		/* Raced, other resize will do our work */
		spin_unlock_irqrestore(&pool->lock, flags);
		kfree(new_elements);
		goto out;
	}
	memcpy(new_elements, pool->elements,
			pool->curr_nr * sizeof(*new_elements));
	kfree(pool->elements);
	pool->elements = new_elements;
	pool->min_nr = new_min_nr;

	while (pool->curr_nr < pool->min_nr) {
		spin_unlock_irqrestore(&pool->lock, flags);
#if defined(__VMKLNX__)
                VMKAPI_MODULE_CALL(pool->module_id, element, pool->alloc,
                                   gfp_mask, pool->pool_data);
#else /* !defined(__VMKLNX__) */
		element = pool->alloc(gfp_mask, pool->pool_data);
#endif /* defined(__VMKLNX__) */
		if (!element)
			goto out;
		spin_lock_irqsave(&pool->lock, flags);
		if (pool->curr_nr < pool->min_nr) {
			add_element(pool, element);
		} else {
			spin_unlock_irqrestore(&pool->lock, flags);
#if defined(__VMKLNX__)
                        VMKAPI_MODULE_CALL_VOID(pool->module_id, pool->free,
                                                element, pool->pool_data);
#else /* !defined(__VMKLNX__) */
			pool->free(element, pool->pool_data);	/* Raced */
#endif /* defined(__VMKLNX__) */
			goto out;
		}
	}
out_unlock:
	spin_unlock_irqrestore(&pool->lock, flags);
out:
	return 0;
}
EXPORT_SYMBOL(mempool_resize);

/**
 * mempool_destroy - deallocate a memory pool
 * @pool: pointer to the memory pool that was allocated by
 *        mempool_create().
 *
 * Delete the memory pool identified by the argument @pool.
 * This function only sleeps if the free_fn() function sleeps. The caller
 * has to guarantee that all elements have been returned to the pool (ie:
 * freed) prior to calling mempool_destroy().
 */
/* _VMKLNX_CODECHECK_: mempool_destroy */
void mempool_destroy(mempool_t *pool)
{
	/* Check for outstanding elements */
	BUG_ON(pool->curr_nr != pool->min_nr);
	free_pool(pool);
}
EXPORT_SYMBOL(mempool_destroy);

/**
 * mempool_alloc - allocate an element from a specific memory pool
 * @pool: pointer to the memory pool that was allocated by mempool_create().
 * @gfp_mask: allocation bitmask. Please refer to kmalloc for
 *             a list of values.
 *
 * Allocate memory for an element from a memory pool reserved
 * by a previous call to mempool_create().
 * This function only sleeps if the alloc_fn function sleeps or
 * returns NULL. Note that due to preallocation, this function
 * *never* fails when called from process contexts. (It might
 * fail if called from an IRQ context.)
 *
 * RETURN VALUE:
 * a pointer to the element allocated on success; otherwise a NULL.
 */
/* _VMKLNX_CODECHECK_: mempool_alloc */
void * mempool_alloc(mempool_t *pool, gfp_t gfp_mask)
{
	void *element;
	unsigned long flags;
#if !defined(__VMKLNX__)
	wait_queue_t wait;
#endif /* defined(__VMKLNX__) */
	gfp_t gfp_temp;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	gfp_mask |= __GFP_NOMEMALLOC;	/* don't allocate emergency reserves */
	gfp_mask |= __GFP_NORETRY;	/* don't loop in __alloc_pages */
	gfp_mask |= __GFP_NOWARN;	/* failures are OK */

	gfp_temp = gfp_mask & ~(__GFP_WAIT|__GFP_IO);

#if !defined(__VMKLNX__)
repeat_alloc:
#endif /* defined(__VMKLNX__) */

#if defined(__VMKLNX__)
        VMKAPI_MODULE_CALL(pool->module_id, element, pool->alloc,
                           gfp_temp, pool->pool_data);
#else /* !defined(__VMKLNX__) */
	element = pool->alloc(gfp_temp, pool->pool_data);
#endif /* defined(__VMKLNX__) */
	if (likely(element != NULL))
		return element;

	spin_lock_irqsave(&pool->lock, flags);
	if (likely(pool->curr_nr)) {
		element = remove_element(pool);
		spin_unlock_irqrestore(&pool->lock, flags);
		return element;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

#if defined(__VMKLNX__)
	return NULL;
#else /* !defined(__VMKLNX__) */
	/* We must not sleep in the GFP_ATOMIC case */
	if (!(gfp_mask & __GFP_WAIT))
		return NULL;

	/* Now start performing page reclaim */
	gfp_temp = gfp_mask;
	init_wait(&wait);
	prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE);
	smp_mb();
	if (!pool->curr_nr) {
		/*
		 * FIXME: this should be io_schedule().  The timeout is there
		 * as a workaround for some DM problems in 2.6.18.
		 */
		io_schedule_timeout(5*HZ);
	}
	finish_wait(&pool->wait, &wait);

	goto repeat_alloc;
#endif /* defined(__VMKLNX__) */
}
EXPORT_SYMBOL(mempool_alloc);

/**
 * mempool_free - return an element to the pool.
 * @element: pool element pointer.
 * @pool: pointer to the memory pool which was allocated via
 *        mempool_create().
 *
 * Release the element allocated by mempool_alloc() back to the
 * memory pool. This function only sleeps if the free_fn() function 
 * sleeps.
 */
/* _VMKLNX_CODECHECK_: mempool_free */
void mempool_free(void *element, mempool_t *pool)
{
	unsigned long flags;

	smp_mb();
	if (pool->curr_nr < pool->min_nr) {
		spin_lock_irqsave(&pool->lock, flags);
		if (pool->curr_nr < pool->min_nr) {
			add_element(pool, element);
			spin_unlock_irqrestore(&pool->lock, flags);
#if !defined(__VMKLNX__)
			wake_up(&pool->wait);
#endif /* defined(__VMKLNX__) */
			return;
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}
#if defined(__VMKLNX__)
        VMKAPI_MODULE_CALL_VOID(pool->module_id, pool->free,
                                element, pool->pool_data);
#else /* !defined(__VMKLNX__) */
        pool->free(element, pool->pool_data);   
#endif /* defined(__VMKLNX__) */
}
EXPORT_SYMBOL(mempool_free);

/*
 * A commonly used alloc and free fn.
 */
void *mempool_alloc_slab(gfp_t gfp_mask, void *pool_data)
{
	struct kmem_cache *mem = pool_data;
	return kmem_cache_alloc(mem, gfp_mask);
}
EXPORT_SYMBOL(mempool_alloc_slab);

void mempool_free_slab(void *element, void *pool_data)
{
	struct kmem_cache *mem = pool_data;
	kmem_cache_free(mem, element);
}
EXPORT_SYMBOL(mempool_free_slab);

/*
 * A commonly used alloc and free fn that kmalloc/kfrees the amount of memory
 * specfied by pool_data
 */
void *mempool_kmalloc(gfp_t gfp_mask, void *pool_data)
{
	size_t size = (size_t)(long)pool_data;
	return kmalloc(size, gfp_mask);
}
EXPORT_SYMBOL(mempool_kmalloc);

void *mempool_kzalloc(gfp_t gfp_mask, void *pool_data)
{
	size_t size = (size_t) pool_data;
	return kzalloc(size, gfp_mask);
}
EXPORT_SYMBOL(mempool_kzalloc);

void mempool_kfree(void *element, void *pool_data)
{
	kfree(element);
}
EXPORT_SYMBOL(mempool_kfree);

#if !defined(__VMKLNX__)
/*
 * A simple mempool-backed page allocator that allocates pages
 * of the order specified by pool_data.
 */
void *mempool_alloc_pages(gfp_t gfp_mask, void *pool_data)
{
	int order = (int)(long)pool_data;
	return alloc_pages(gfp_mask, order);
}
EXPORT_SYMBOL(mempool_alloc_pages);

void mempool_free_pages(void *element, void *pool_data)
{
	int order = (int)(long)pool_data;
	__free_pages(element, order);
}
EXPORT_SYMBOL(mempool_free_pages);
#endif /* defined(__VMKLNX__) */
