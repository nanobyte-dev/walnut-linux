// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of ps3rsx_vswap_init's unwind (src/vswap.c): the function itself, cut out of the
 * source by vswap-init-test.sh (VSWAP_INIT_C), against stand-ins that fail one fallible call at a
 * time. Each failing init must return an error with nothing left behind: no GART node pinned
 * (ps3rsx_gart_pin without its unpin leaves the bounce node in rsx->gart's lists while kfree(vs)
 * frees it: ps3rsx_mm's walk then reads freed memory, the 2026-11-13 oops), and every allocation
 * freed. A successful init must leave rsx->vswap set. The same stand-ins cover main's init and
 * the sealed swap's (vswap_seal_init, more fields).
 *   vswap-init-test.sh OUTDIR [VSWAP_C]
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the fallible calls, counted; call number fail_at fails (0: none) */
static int calls, fail_at;
static bool fail_now(void) { return ++calls == fail_at; }
static int live_allocs, pinned;
static void *pinned_node;

#define GFP_KERNEL 0
#define PAGE_SIZE 4096u
#define PAGE_SHIFT 12
#define UINT_MAX 0xffffffffu
#define NUMA_NO_NODE (-1)
#define DISK_NAME_LEN 32
#define BIOSET_NEED_BVECS 1
#define WQ_MEM_RECLAIM 1
#define WQ_HIGHPRI 2
#define MAX_RQ_PAGES 32
#define BIO_RESERVE (2 * MAX_RQ_PAGES)
#define BUILD_BUG_ON(c) ((void)0)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define mutex_init(m) ((void)(m))
#define spin_lock_init(l) ((void)(l))
#define bio_list_init(l) ((void)(l))
#define INIT_LIST_HEAD(l) ((void)(l))
#define INIT_WORK(w, f) ((void)(w))
#define INIT_DELAYED_WORK(w, f) ((void)(w))
#define atomic_set(a, v) ((void)(a))
#define strscpy(d, s, n) snprintf(d, n, "%s", s)
#define set_capacity(d, n) ((void)(d))
#define drm_warn(d, ...) ((void)(d))

/* the sealed swap's constants (its chunk metadata) */
#define VSX_CLASSES 8
#define VSX_ON_DISK 0xfffffff0u
#define STRIDE 256
#define META_PAGES 2
#define META_PER_PAGE 128
#define SLOTS 256		/* pages per 1 MiB chunk */
struct vsx_meta { char b[32]; };

struct drm_mm_node { u64 start, size; void *mm; };
struct queue_limits {
	unsigned int logical_block_size, physical_block_size, io_min, max_hw_sectors,
		max_hw_discard_sectors, max_write_zeroes_sectors, discard_granularity, dma_alignment;
};
struct blk_mq_tag_set {
	const void *ops;
	unsigned int nr_hw_queues, queue_depth, cmd_size;
	int numa_node;
	bool allocated;
};
struct gendisk {
	int major, first_minor, minors;
	const void *fops;
	void *private_data;
	char disk_name[DISK_NAME_LEN];
	bool added;
};
struct bio_set { bool inited; };
struct workqueue_struct { int x; };
struct vchunk { struct drm_mm_node node; };
struct vswap_cmd { int x; };
struct ps3rsx_device;
struct ps3rsx_vswap {
	struct ps3rsx_device *rsx;
	struct gendisk *disk;
	struct blk_mq_tag_set tags;
	struct bio_set bios;
	int lock, shrink_lock, pending_lock, deferred, pending;
	int io_work, demote_work, drain_work, giveback_work, shrinkers;
	int hint[VSX_CLASSES + 1];
	struct workqueue_struct *io_wq, *demote_wq;
	unsigned int nchunks;
	struct vchunk *chunks;
	struct drm_mm_node bounce_node;
	u32 bounce_io;
	void *bounce;
	bool bounce_ok;
	bool sealed;		/* the sealed swap: vswap_seal_init ran */
};
struct ps3rsx_device {
	struct { void *dev; } drm;
	struct ps3rsx_vswap *vswap;
};

static unsigned int vswap_vram_mb = 96, vswap_vram_max_mb = 0;
static int vswap_major;
static const int vswap_mq_ops, vswap_fops;
static const void *vswap_groups;
static void vswap_io_work(void *w) { (void)w; }
static void vswap_demote_work(void *w) { (void)w; }
static void vswap_drain_work(void *w) { (void)w; }
static void vswap_giveback_work(void *w) { (void)w; }

static void *kzalloc(size_t n, int gfp)
{
	(void)gfp;
	if (fail_now())
		return NULL;
	live_allocs++;
	return calloc(1, n);
}
static void *kvcalloc(size_t n, size_t sz, int gfp) { return kzalloc(n * sz, gfp); }
static void kfree(void *p)
{
	if (!p)
		return;
	/* the 2026-11-13 oops: the struct goes while a node inside it is still in a heap */
	CHECK(!pinned || (char *)pinned_node < (char *)p ||
	      (char *)pinned_node >= (char *)p + sizeof(struct ps3rsx_vswap),
	      "failure at call %d: vs freed with its GART bounce node still in rsx->gart", fail_at);
	live_allocs--;
	free(p);
}
static void kvfree(void *p) { kfree(p); }
static int bioset_init(struct bio_set *b, int n, int f, int flags)
{
	(void)n; (void)f; (void)flags;
	if (fail_now())
		return -ENOMEM;
	b->inited = true;
	live_allocs++;
	return 0;
}
static void bioset_exit(struct bio_set *b)
{
	if (b->inited)
		live_allocs--;
	b->inited = false;
}
static struct workqueue_struct *alloc_ordered_workqueue(const char *n, int f)
{
	(void)n; (void)f;
	return kzalloc(sizeof(struct workqueue_struct), 0);
}
static void destroy_workqueue(struct workqueue_struct *w) { kfree(w); }
static int ps3rsx_gart_pin(struct ps3rsx_device *rsx, struct drm_mm_node *node, u32 *io)
{
	(void)rsx;
	if (fail_now())
		return -ENOSPC;
	pinned++;
	pinned_node = node;
	*io = 0x3e00000;
	return 0;
}
static void ps3rsx_gart_unpin(struct ps3rsx_device *rsx, struct drm_mm_node *node)
{
	(void)rsx;
	CHECK(pinned && node == pinned_node, "unpin of a node that isn't pinned");
	pinned--;
	pinned_node = NULL;
}
static char gart_mem[1 << 20];
static void *ps3rsx_gart_ptr(struct ps3rsx_device *rsx, u64 io) { (void)rsx; (void)io; return gart_mem; }
static int vswap_seal_init(struct ps3rsx_vswap *vs)
{
	if (fail_now())
		return -ENODEV;
	vs->sealed = true;
	live_allocs++;
	return 0;
}
static void ps3rsx_spe_clear_bounds(struct ps3rsx_device *rsx) { (void)rsx; }
static void vswap_seal_fini(struct ps3rsx_vswap *vs)
{
	if (vs->sealed)
		live_allocs--;
	vs->sealed = false;
}
static int register_blkdev(unsigned int major, const char *name)
{
	(void)major; (void)name;
	if (fail_now())
		return -EBUSY;
	live_allocs++;
	return 250;
}
static void unregister_blkdev(unsigned int major, const char *name) { (void)major; (void)name; live_allocs--; }
static int blk_mq_alloc_tag_set(struct blk_mq_tag_set *t)
{
	if (fail_now())
		return -ENOMEM;	/* "blk-mq: failed to allocate request map" */
	t->allocated = true;
	live_allocs++;
	return 0;
}
static void blk_mq_free_tag_set(struct blk_mq_tag_set *t)
{
	if (t->allocated)
		live_allocs--;
	t->allocated = false;
}
static struct gendisk *blk_mq_alloc_disk(struct blk_mq_tag_set *t, struct queue_limits *l, void *q)
{
	(void)t; (void)l; (void)q;
	struct gendisk *d = kzalloc(sizeof(*d), 0);

	return d ? d : ERR_PTR(-ENOMEM);
}
static void put_disk(struct gendisk *d) { kfree(d); }
static int device_add_disk(void *parent, struct gendisk *d, const void *groups)
{
	(void)parent; (void)groups;
	if (fail_now())
		return -ENOMEM;
	d->added = true;
	return 0;
}

#include VSWAP_INIT_C

/* a successful init's struct isn't torn down here (that is ps3rsx_vswap_fini's job) */
const char *__asan_default_options(void) { return "detect_leaks=0"; }

int main(void)
{
	struct ps3rsx_device rsx = { 0 };
	int k, n, ret;

	setvbuf(stdout, NULL, _IOLBF, 0);
	/* count the fallible calls of a successful init */
	calls = 0;
	fail_at = 0;
	ret = ps3rsx_vswap_init(&rsx);
	n = calls;
	CHECK(ret == 0 && rsx.vswap && pinned == 1, "init without failures: %d", ret);
	CHECK(n >= 6, "only %d fallible calls seen", n);
	/* (the success path's teardown is ps3rsx_vswap_fini's: not modelled here) */
	pinned = 0;
	pinned_node = NULL;
	live_allocs = 0;
	for (k = 1; k <= n; k++) {
		struct ps3rsx_device r = { 0 };

		calls = 0;
		fail_at = k;
		live_allocs = 0;
		ret = ps3rsx_vswap_init(&r);
		if (ret == 0) {		/* the bounce pin's failure isn't one: PPE copies */
			CHECK(r.vswap && !pinned, "failure at call %d: init succeeded with a pin", k);
			pinned = 0;
			pinned_node = NULL;
			continue;
		}
		CHECK(!r.vswap, "failure at call %d: rsx->vswap set after an error", k);
		CHECK(!pinned, "failure at call %d (%d): the GART bounce stays pinned after the error", k, ret);
		CHECK(live_allocs == 0, "failure at call %d (%d): %d allocations left", k, ret, live_allocs);
		pinned = 0;
		pinned_node = NULL;
	}
	printf("vswap-init-test: %d fallible calls, %d checks, %d failed\n", n, checks, fails);
	return fails != 0;
}
