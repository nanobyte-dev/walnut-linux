// SPDX-License-Identifier: GPL-2.0
/*
 * Same-owner reuse of freed VRAM (the rules: vclear.h). Most of what a desktop frees is the same
 * process allocating the same sizes again: glamor's pixmaps in X, a game's level textures. Clearing
 * those cost the GPU its fill time for data that never leaves its owner. So a freed VRAM BO whose
 * data only its own file could have written is kept, uncleared and still allocated in its heap,
 * and the file's next allocation of exactly that size takes the BO struct and its node back as
 * they are (no node is moved, so the range is never free in between).
 *
 * The owner is a refcounted record per open file (struct ps3rsx_owner), never a pointer that a
 * later file could reuse: every BO holds a reference until it is freed, so a free after its file's
 * postclose (a framebuffer an atomic commit still held) sees the owner closed and clears the
 * range. fork, dup and a passed fd share one file, so one owner: everyone holding the file can
 * mmap all its live BOs already. A BO anything else can reach is shared, for good: a handle in
 * another file (GEM_OPEN of a flink name, a PRIME import, GETFB: the .open hook) or a dma-buf
 * export (bo_export). Shared BOs, dumb buffers and BOs without an owner are always cleared.
 *
 * A kept entry has one claimant: whoever takes it off vc.kept under mm_lock (the owner's reuse,
 * the bounds, the expiry, pressure, the owner's close). Everything that isn't taken back whole by
 * its owner becomes a zombie (vclear.c), so a kept range never reaches the free heap uncleared.
 * Expiry by age whether or not anyone draws, so the clears don't pile up onto a game's first big
 * allocation; pressure (an allocation that found no room, the VRAM swap unable to grow) converts
 * everything kept.
 */
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <drm/drm_print.h>

#include "ps3rsx.h"
#include "vclear.h"

#ifdef PS3RSX_TEST_CLEAR
/* boot tests only: every free cleared again, to attribute the reuse's gain */
static bool test_reuse_off;
module_param(test_reuse_off, bool, 0444);
MODULE_PARM_DESC(test_reuse_off, "TEST BUILD: no same-owner reuse of freed VRAM (every free cleared)");
#else
#define test_reuse_off false
#endif

enum { CONV_CAP, CONV_AGE, CONV_PRESSURE, CONV_CLOSED, CONV_N };

static atomic64_t owner_ids;

struct ps3rsx_owner *ps3rsx_owner_new(void)
{
	struct ps3rsx_owner *o = kzalloc(sizeof(*o), GFP_KERNEL);

	if (!o)
		return NULL;
	kref_init(&o->ref);
	o->id = atomic64_inc_return(&owner_ids);
	o->tgid = task_tgid_nr(current);
	return o;
}

static void owner_release(struct kref *ref)
{
	kfree(container_of(ref, struct ps3rsx_owner, ref));
}

void ps3rsx_owner_put(struct ps3rsx_owner *o)
{
	if (o)
		kref_put(&o->ref, owner_release);
}

struct ps3rsx_owner *ps3rsx_owner_get(struct ps3rsx_owner *o)
{
	if (o)
		kref_get(&o->ref);
	return o;
}

static int size_bucket(u64 size)
{
	return min_t(int, ilog2(max_t(u64, size, SZ_4K)) - 12, KEEP_HIST - 1);
}

/* the per-tgid table (debugfs only): the slot of tgid, or the least used one taken over */
static struct ps3rsx_keep_tg *tg_slot(struct ps3rsx_keep *k, pid_t tgid)
{
	struct ps3rsx_keep_tg *t, *least = &k->tg[0];

	for (t = k->tg; t < k->tg + KEEP_TG; t++) {
		if (t->tgid == tgid)
			return t;
		if (t->freed < least->freed)
			least = t;
	}
	memset(least, 0, sizeof(*least));
	least->tgid = tgid;
	return least;
}

/* A kept entry goes to the clear (mm_lock held, the entry already off vc.kept) */
static void convert(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo, int why)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_owner *o = bo->owner;

	k->bytes -= bo->base.size;
	k->n--;
	o->kept_bytes -= bo->base.size;
	o->kept_n--;
	k->conv[why] += bo->base.size;
	tg_slot(k, o->tgid)->cleared += bo->base.size;
	bo->owner = NULL;
	ps3rsx_owner_put(o);
	/* only VRAM is ever kept; a GART BO here would be a bug: cleared through the GART path all
	 * the same, its reap left to the worker (it takes mm_lock, held here) */
	WARN_ON_ONCE(bo->kind != BO_VRAM && bo->kind != BO_VRAM_GPU_ONLY);
	if (!ps3rsx_vclear_bo_free_locked(rsx, bo, true)) {	/* test_clear_off: freed at once */
		drm_mm_remove_node(&bo->node);
		kfree(bo);
	}
}

/* mm_lock held: the oldest entries until the bounds hold again */
static void enforce_bounds(struct ps3rsx_device *rsx, struct ps3rsx_owner *o)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_bo *bo, *tmp;

	list_for_each_entry_safe(bo, tmp, &k->kept, kept_link) {
		if (!vclear_keep_over(o->kept_bytes, o->kept_n, k->bytes, k->n))
			break;
		/* the owner's own oldest first while it is over its own bound */
		if (bo->owner != o && (o->kept_bytes > VCLEAR_KEEP_OWNER_BYTES ||
				       o->kept_n > VCLEAR_KEEP_OWNER_N))
			continue;
		list_del_init(&bo->kept_link);
		convert(rsx, bo, CONV_CAP);
	}
}

/*
 * bo_free, after drm_gem_object_release: keep the BO for its owner when the rules allow. Returns
 * true when kept (the struct and its node stay); else the caller drops the owner and clears.
 */
bool ps3rsx_keep_bo(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_owner *o = bo->owner;
	bool kept = false;

	if ((bo->kind != BO_VRAM && bo->kind != BO_VRAM_GPU_ONLY) || !drm_mm_node_allocated(&bo->node))
		return false;
	mutex_lock(&rsx->mm_lock);
	k->hist[size_bucket(bo->base.size)]++;
	if (o)
		tg_slot(k, o->tgid)->freed += bo->base.size;
	if (READ_ONCE(bo->shared))
		k->shared_bytes += bo->base.size;
	else if (!o)
		k->noowner_bytes += bo->base.size;
	if (vclear_may_keep(o != NULL, o && o->closed, READ_ONCE(bo->shared), bo->dumb,
			    test_reuse_off)) {
		bo->kept_at = jiffies;
		bo->kept_seq = READ_ONCE(rsx->seqno);
		list_add_tail(&bo->kept_link, &k->kept);
		k->bytes += bo->base.size;
		k->n++;
		o->kept_bytes += bo->base.size;
		o->kept_n++;
		if (k->bytes > k->peak)
			k->peak = k->bytes;
		enforce_bounds(rsx, o);
		kept = true;
	}
	mutex_unlock(&rsx->mm_lock);
	if (kept)
		ps3rsx_vclear_kick(rsx, msecs_to_jiffies(VCLEAR_KEEP_AGE_MS));
	return kept;
}

/*
 * The owner's allocation: a kept BO of its own that fits (vclear.h), taken off the list and made
 * a new BO in place (its node never leaves the heap). NULL when none fits.
 */
struct ps3rsx_bo *ps3rsx_reuse(struct ps3rsx_device *rsx, struct ps3rsx_owner *o, u64 size,
			       u64 align, bool mappable)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_bo *bo, *found = NULL;

	if (!o || test_reuse_off)
		return NULL;
	mutex_lock(&rsx->mm_lock);
	list_for_each_entry(bo, &k->kept, kept_link) {
		/* owner and size first: the reference is an MMIO read, only for a candidate */
		if (!vclear_reuse_candidate(bo->owner == o, bo->node.size, size))
			continue;
		if (vclear_reuse_fits(bo->owner == o, o->closed, ps3rsx_seq_done(rsx, bo->kept_seq),
				      bo->node.start, bo->node.size, size, align, mappable,
				      CPU_VRAM_LIMIT)) {
			found = bo;
			break;
		}
	}
	if (found) {
		list_del_init(&found->kept_link);
		k->bytes -= size;
		k->n--;
		o->kept_bytes -= size;
		o->kept_n--;
		k->reused_n++;
		k->reused_bytes += size;
		k->hist_reused[size_bucket(size)]++;
		tg_slot(k, o->tgid)->reused += size;
	}
	mutex_unlock(&rsx->mm_lock);
	if (!found)
		return NULL;
	/* the same struct, a new GEM object: its node, offset and owner stay */
	memset(&found->base, 0, sizeof(found->base));
	found->base.funcs = ps3rsx_bo_funcs();
	drm_gem_private_object_init(&rsx->drm, &found->base, size);
	INIT_LIST_HEAD(&found->reports_link);
	memset(&found->zombie, 0, sizeof(found->zombie));
	found->shared = false;
	found->dumb = false;
	found->domain = NOUVEAU_GEM_DOMAIN_VRAM;
	found->offset = found->node.start;
	found->kind = found->offset >= CPU_VRAM_LIMIT ? BO_VRAM_GPU_ONLY : BO_VRAM;
	return found;
}

/* every kept entry of owner o (NULL: of everyone) older than age_ms (0: all) to the clear */
static void convert_where(struct ps3rsx_device *rsx, struct ps3rsx_owner *o, unsigned int age_ms,
			  int why)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_bo *bo, *tmp;

	mutex_lock(&rsx->mm_lock);
	list_for_each_entry_safe(bo, tmp, &k->kept, kept_link) {
		if (o && bo->owner != o)
			continue;
		if (age_ms && jiffies_to_msecs(jiffies - bo->kept_at) < age_ms)
			break;		/* the list is in age order */
		list_del_init(&bo->kept_link);
		convert(rsx, bo, why);
	}
	mutex_unlock(&rsx->mm_lock);
}

/* The owner's file is closing (postclose): its kept BOs to the clear, later frees cleared too */
void ps3rsx_owner_close(struct ps3rsx_device *rsx, struct ps3rsx_owner *o)
{
	if (!o)
		return;
	mutex_lock(&rsx->mm_lock);
	o->closed = true;
	mutex_unlock(&rsx->mm_lock);
	convert_where(rsx, o, 0, CONV_CLOSED);
	ps3rsx_owner_put(o);
}

/*
 * An allocation by o found no room for need bytes: kept BOs to the clear, o's own first, then
 * everyone's, the oldest first in each, until need bytes are on their way (then the allocator
 * waits for their fill). Returns true if anything was converted.
 */
bool ps3rsx_keep_pressure(struct ps3rsx_device *rsx, struct ps3rsx_owner *o, u64 need)
{
	struct ps3rsx_keep *k = &rsx->keep;
	struct ps3rsx_bo *bo, *tmp;
	u64 got = 0;
	int pass;

	if (list_empty_careful(&k->kept))
		return false;
	mutex_lock(&rsx->mm_lock);
	for (pass = 0; pass < 2 && got < need; pass++)
		list_for_each_entry_safe(bo, tmp, &k->kept, kept_link) {
			if (got >= need)
				break;
			if (pass == 0 && (!o || bo->owner != o))
				continue;
			got += bo->base.size;
			list_del_init(&bo->kept_link);
			convert(rsx, bo, CONV_PRESSURE);
		}
	mutex_unlock(&rsx->mm_lock);
	return got > 0;
}

/* The VRAM swap couldn't grow (its I/O path: no lock taken here): the worker converts */
void ps3rsx_keep_pressure_async(struct ps3rsx_device *rsx)
{
	if (!list_empty_careful(&rsx->keep.kept)) {
		atomic_set(&rsx->keep.pressure, 1);
		ps3rsx_vclear_kick(rsx, 0);
	}
}

/* The worker: aged entries, and a pressure the swap asked for */
void ps3rsx_keep_age(struct ps3rsx_device *rsx)
{
	if (list_empty_careful(&rsx->keep.kept))
		return;
	if (atomic_xchg(&rsx->keep.pressure, 0))
		convert_where(rsx, NULL, 0, CONV_PRESSURE);
	else
		convert_where(rsx, NULL, VCLEAR_KEEP_AGE_MS, CONV_AGE);
	if (!list_empty_careful(&rsx->keep.kept))
		ps3rsx_vclear_kick(rsx, msecs_to_jiffies(VCLEAR_KEEP_AGE_MS / 2));
}

void ps3rsx_keep_init(struct ps3rsx_device *rsx)
{
	INIT_LIST_HEAD(&rsx->keep.kept);
}

/* Unload (before vclear_fini): every kept node back (the next load clears that VRAM) */
void ps3rsx_keep_fini(struct ps3rsx_device *rsx)
{
	struct ps3rsx_bo *bo, *tmp;

	mutex_lock(&rsx->mm_lock);
	list_for_each_entry_safe(bo, tmp, &rsx->keep.kept, kept_link) {
		list_del(&bo->kept_link);
		drm_mm_remove_node(&bo->node);
		ps3rsx_owner_put(bo->owner);
		kfree(bo);
	}
	rsx->keep.bytes = 0;
	mutex_unlock(&rsx->mm_lock);
}

/* debugfs ps3rsx_mm (mm_lock not held) */
void ps3rsx_keep_show(struct seq_file *m, struct ps3rsx_device *rsx)
{
	struct ps3rsx_keep *k = &rsx->keep;
	int i;

	mutex_lock(&rsx->mm_lock);
	seq_printf(m, "reuse: %llu KiB kept now (peak %llu KiB)%s; %llu reused (%llu KiB); cleared instead: shared %llu KiB, no owner or dumb %llu KiB\n",
		   k->bytes >> 10, k->peak >> 10, test_reuse_off ? ", OFF (test switch)" : "",
		   k->reused_n, k->reused_bytes >> 10, k->shared_bytes >> 10, k->noowner_bytes >> 10);
	seq_printf(m, "reuse: kept, then cleared: over a bound %llu KiB, aged %llu KiB, pressure %llu KiB, owner closed %llu KiB\n",
		   k->conv[CONV_CAP] >> 10, k->conv[CONV_AGE] >> 10, k->conv[CONV_PRESSURE] >> 10,
		   k->conv[CONV_CLOSED] >> 10);
	seq_puts(m, "reuse: VRAM frees by size (4K, 8K, ... 64M+), reused:");
	for (i = 0; i < KEEP_HIST; i++)
		seq_printf(m, " %u/%u", k->hist[i], k->hist_reused[i]);
	seq_puts(m, "\n");
	for (i = 0; i < KEEP_TG; i++)
		if (k->tg[i].tgid)
			seq_printf(m, "reuse: tgid %d freed %llu KiB, reused %llu KiB, kept then cleared %llu KiB\n",
				   k->tg[i].tgid, k->tg[i].freed >> 10, k->tg[i].reused >> 10,
				   k->tg[i].cleared >> 10);
	mutex_unlock(&rsx->mm_lock);
}
