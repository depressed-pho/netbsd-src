/* $NetBSD$ */

/*-
 * Copyright (c) 2023 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/cdefs.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/param.h>
#include <sys/rbtree.h>
#include <sys/stdbool.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/vmem.h>

#include <linux/dmapool.h>

struct dma_pool_segment {
	rb_node_t		node;
	size_t			size;
	bus_dmamap_t		dmam;
	bus_dma_segment_t	dseg;
	void			*virt_addr;
};

struct dma_pool {
	/*
	 * This vmem contains physical addresses as spans. Segments are
	 * allocated, wired, mapped into kernel virtual address space, and
	 * also loaded. They are never freed until the entire pool is
	 * destroyed.
	 *
	 * Why loaded? Because alignment and boundary only makes sense in
	 * the context of physical address, and loading mappings is the way
	 * how we obtain them.
	 */
	vmem_t		*vm;

	/* These fields are constants and not protected by any locks. */

	bus_dma_tag_t	dmat;
	size_t		block_size;
	size_t		align;
	size_t		boundary;

	/* These fields are protected by dma_pool::mtx. */

	kmutex_t	mtx;
	rb_tree_t	psegs; /* tree of struct dma_pool_segment */
};

static bus_addr_t
dma_pool_segment_phys_addr(struct dma_pool_segment const *pseg)
{
	KASSERT(pseg->dmam->dm_nsegs == 1);
	return pseg->dmam->dm_segs[0].ds_addr;
}

static int
dma_pool_compare_psegs_nodes(void *ctx, const void *node1, const void *node2)
{
	const struct dma_pool_segment *pseg1 = node1;
	const struct dma_pool_segment *pseg2 = node2;
	const bus_addr_t paddr1 = dma_pool_segment_phys_addr(pseg1);
	const bus_addr_t paddr2 = dma_pool_segment_phys_addr(pseg2);

	return paddr1 < paddr2 ? -1
	     : paddr1 > paddr2 ?  1
	     : 0;
}

static int
dma_pool_compare_psegs_key(void *ctx, const void *node, const void *key)
{
	const struct dma_pool_segment *pseg = node;
	const bus_addr_t paddr1 = dma_pool_segment_phys_addr(pseg);
	const bus_addr_t paddr2 = (bus_addr_t)(*(const vmem_addr_t *)key);

	return paddr1 < paddr2 ? -1
	     : paddr1 > paddr2 ?  1
	     : 0;
}

static const rb_tree_ops_t dma_pool_psegs_ops = {
	.rbto_compare_nodes = &dma_pool_compare_psegs_nodes,
	.rbto_compare_key = &dma_pool_compare_psegs_key,
	.rbto_node_offset = offsetof(struct dma_pool_segment, node)
};

static int
dma_pool_alloc_segment(void *ctx, vmem_size_t size, vmem_size_t *actual_size,
    vm_flag_t flags, vmem_addr_t *addr)
{
	struct dma_pool *pool = ctx;
	int err = 0;

	struct dma_pool_segment *pseg = kmem_alloc(
	    sizeof(*pseg), ISSET(flags, VM_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	if (__predict_false(!pseg))
		return ENOMEM;

	pseg->size = round_page(size);

	const int bus_dma_flags = ISSET(flags, VM_SLEEP) ? BUS_DMA_WAITOK : 0;
	bool created = false, allocated = false;
	bool mapped = false, loaded = false;

	err = bus_dmamap_create(pool->dmat, pseg->size, 1, pseg->size, 0,
	    bus_dma_flags, &pseg->dmam);
	if (__predict_false(err))
		goto err;
	created = true;

	int nseg;
	err = bus_dmamem_alloc(pool->dmat, pseg->size, 1, 0, &pseg->dseg,
	    1, &nseg, bus_dma_flags);
	if (__predict_false(err))
		goto err;
	KASSERT(nseg == 1);
	allocated = true;

	err = bus_dmamem_map(pool->dmat, &pseg->dseg, 1, pseg->size,
	    &pseg->virt_addr, bus_dma_flags | BUS_DMA_COHERENT);
	if (__predict_false(err))
		goto err;
	mapped = true;

	err = bus_dmamap_load(pool->dmat, pseg->dmam, pseg->virt_addr,
	    pseg->size, NULL, bus_dma_flags);
	if (__predict_false(err))
		goto err;
	KASSERT(pseg->dmam->dm_nsegs == 1);
	loaded = true;

	/*
	 * We have successfully allocated a new segment. Now insert it into
	 * a tree so that we can find the segment by vmem_addr_t. We need
	 * to do it in order to translate a physical address to a virtual
	 * one.
	 */
	mutex_spin_enter(&pool->mtx);
	rb_tree_insert_node(&pool->psegs, pseg);
	mutex_spin_exit(&pool->mtx);

	*addr = (vmem_addr_t)pseg->dmam->dm_segs[0].ds_addr;
	*actual_size = (vmem_size_t)pseg->dmam->dm_segs[0].ds_len;
	return 0;
err:
	if (loaded)
		bus_dmamap_unload(pool->dmat, pseg->dmam);
	if (mapped)
		bus_dmamem_unmap(pool->dmat, &pseg->virt_addr, pseg->size);
	if (allocated)
		bus_dmamem_free(pool->dmat, &pseg->dseg, 1);
	if (created)
		bus_dmamap_destroy(pool->dmat, pseg->dmam);
	kmem_free(pseg, sizeof(*pseg));
	return err;
}

static void
dma_pool_free_segment(struct dma_pool *pool, struct dma_pool_segment *pseg)
{
	bus_dmamap_unload(pool->dmat, pseg->dmam);
	bus_dmamem_unmap(pool->dmat, pseg->virt_addr, pseg->size);
	bus_dmamem_free(pool->dmat, &pseg->dseg, 1);
	bus_dmamap_destroy(pool->dmat, pseg->dmam);
	kmem_free(pseg, sizeof(*pseg));
}

static struct dma_pool_segment *
dma_pool_find_segment(struct dma_pool *pool, vmem_addr_t addr)
{
	KASSERT(mutex_owned(&pool->mtx));

	struct dma_pool_segment *pseg =
		rb_tree_find_node_leq(&pool->psegs, &addr);

	if (__predict_true(
		    (bus_addr_t)addr >= pseg->dseg.ds_addr &&
		    (bus_addr_t)addr < pseg->dseg.ds_addr + pseg->dseg.ds_len))
		return pseg;

	panic("Pool segment not found: %p", (void*)addr);
}

struct dma_pool *
dma_pool_create(const char *name,
    bus_dma_tag_t dmat,
    size_t block_size,
    size_t align,
    size_t boundary)
{
	struct dma_pool *pool = kmem_alloc(sizeof(*pool), KM_SLEEP);
	if (__predict_false(!pool))
		return NULL;

	KASSERT(powerof2(align));
	pool->vm = vmem_xcreate(name, 0, 0, align,
	    &dma_pool_alloc_segment, NULL, pool, 0, VM_SLEEP, IPL_VM);
	if (__predict_false(!pool->vm))
		goto err;

	pool->dmat = dmat;
	pool->block_size = block_size;
	pool->align = align;
	pool->boundary = boundary;

	mutex_init(&pool->mtx, MUTEX_DEFAULT, IPL_VM);
	rb_tree_init(&pool->psegs, &dma_pool_psegs_ops);

	return pool;
err:
	kmem_free(pool, sizeof(*pool));
	return NULL;
}

void
dma_pool_destroy(struct dma_pool *pool)
{
	KASSERT(pool);

	struct dma_pool_segment *pseg, *tmp;
	RB_TREE_FOREACH_SAFE(pseg, &pool->psegs, tmp) {
		dma_pool_free_segment(pool, pseg);
	}

	mutex_destroy(&pool->mtx);
	vmem_destroy(pool->vm);
	kmem_free(pool, sizeof(*pool));
}

void*
dma_pool_alloc(struct dma_pool *pool, gfp_t gfp, bus_addr_t *handle)
{
	int ret;

	KASSERT(pool);
	KASSERT(handle);

	vmem_addr_t addr;
	ret = vmem_xalloc(pool->vm, pool->block_size, pool->align,
	    0, pool->boundary, VMEM_ADDR_MIN, VMEM_ADDR_MAX,
	    VM_BESTFIT | VM_SLEEP, &addr);
	if (__predict_false(ret))
		return NULL;

	/*
	 * Now we have a region of physical address space. Find the virtual
	 * address that corresponds to it.
	 */
	mutex_spin_enter(&pool->mtx);
	const struct dma_pool_segment *pseg =
		dma_pool_find_segment(pool, addr);
	KASSERT(pseg);
	mutex_spin_exit(&pool->mtx);

	*handle = (bus_addr_t)addr;

	const bus_addr_t phys_start = dma_pool_segment_phys_addr(pseg);
	const size_t offset = (size_t)((bus_addr_t)addr - phys_start);
	void *virt_addr = (void *)((uintptr_t)pseg->virt_addr + offset);

	/*
	 *  __GFP_ZERO is the only GFP flag that dma_pool_alloc() is
	 * expected to support.
	 */
	if ((gfp & __GFP_ZERO) != 0)
		memset(virt_addr, 0, pool->block_size);

	return virt_addr;
}

void *
dma_pool_zalloc(struct dma_pool *pool, gfp_t gfp, bus_addr_t *handle)
{
	return dma_pool_alloc(pool, gfp | __GFP_ZERO, handle);
}

void
dma_pool_free(struct dma_pool *pool, void* vaddr __unused, bus_addr_t handle)
{
	KASSERT(pool);
	vmem_xfree(pool->vm, (vmem_addr_t)handle, pool->block_size);
}

void
dma_pool_sync(struct dma_pool *pool, bus_addr_t handle, int ops)
{
	KASSERT(pool);

	mutex_spin_enter(&pool->mtx);
	struct dma_pool_segment *pseg =
		dma_pool_find_segment(pool, (vmem_addr_t)handle);
	KASSERT(pseg);
	mutex_spin_exit(&pool->mtx);

	/*
	 * Segments won't go away until the entire pool is destroyed. It's
	 * safe to do this outside of the critical section. We assume users
	 * don't free this block while also synchronizing it.
	 */
	const bus_addr_t phys_start = dma_pool_segment_phys_addr(pseg);
	const size_t offset = (size_t)((bus_addr_t)handle - phys_start);
	bus_dmamap_sync(pool->dmat, pseg->dmam, offset, pool->block_size, ops);
}
