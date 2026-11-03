/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps3rsx: a DRM driver for the PS3's RSX on top of the lv1 hypervisor's GPU calls. It speaks
 * nouveau's userspace ABI (driver name "nouveau"), so Mesa's nv30 driver can run on it with a
 * small patch for the hypervisor's fixed object handles. Plan: docs/DRIVER-PLAN.md.
 */
#ifndef PS3RSX_H
#define PS3RSX_H

#include "blame.h"
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_mm.h>
#include <drm/drm_connector.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/dma-fence.h>
#include <linux/hrtimer.h>
#include <drm/drm.h>
#include <drm/nouveau_drm.h>
#include <asm/ps3gpu.h>
#include "vclear.h"
#ifdef PS3RSX_PUSH_DUMP
#include "pushdump_scan.h"
#endif

struct ps3rsx_vstate;
struct ps3rsx_file;
struct ps3rsx_chan;

/* Deprecated nouveau ioctls Mesa's winsys still uses, no longer in the kernel's uapi header
 * (layouts as in libdrm's nouveau_drm.h) */
struct drm_nouveau_grobj_alloc {
	__s32 channel;
	__u32 handle;
	__s32 class;
};
#define DRM_IOCTL_NOUVEAU_GROBJ_ALLOC \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GROBJ_ALLOC, struct drm_nouveau_grobj_alloc)
#define DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, struct drm_nouveau_notifierobj_alloc)
#define DRM_IOCTL_NOUVEAU_GPUOBJ_FREE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GPUOBJ_FREE, struct drm_nouveau_gpuobj_free)

#define PS3RSX_CHIPSET		0x47		/* NV47-like: Mesa picks the 0x4097 3D class */
#define PS3RSX_PCI_VENDOR	0x104d		/* Sony: what the patched Mesa checks for */
/* Private GETPARAM: the patched Mesa queries it; channels are refused until it did, so an
 * unpatched Mesa fails at init instead of submitting streams with handles the RSX lacks.
 * Its value: 1, the first; 2, the validator also takes 0x1ee4 POINT_PARAMS_ENABLE (Mesa 0051 sends
 * it only from 2 on, so a newer Mesa on an older module keeps working). */
#define PS3RSX_GETPARAM_PS3_ABI	0x1000
#define PS3RSX_ABI_VERSION	2
/* Private GETPARAM, value 1: the client recovers from a cancelled submit. Asked (before its
 * channels submit), a rejected push makes its submit fail with -ECANCELED, and the channel takes
 * further submits (each validated as always); not asked, the channel is refused from then on
 * (-EIO). Mesa 0053 asks and, on -ECANCELED, drops its view of the GPU state and re-sends it
 * before its next draw (docs/DRIVER.md, the validator). Module parameter refuse_rejected=1
 * refuses every such channel. */
#define PS3RSX_GETPARAM_PS3_RECOVER	0x1001

#define CPU_VRAM_LIMIT		0xe000000ULL	/* ps3fb's VRAM mapping size: CPU access above it is unproven */

/* GART IO layout: reset program, then the ring, the VRAM clear's zero area (vclear.c), then
 * BOs (exec.c). RING_RESERVE is backed for good in whole 1 MiB chunks (gart.c). */
#define RING_START		0x1000
#define RING_END		0x1f0000
#define ZERO_START		RING_END
#define ZERO_END		0x200000
#define RING_RESERVE		0x200000
#define PS3RSX_MAX_CHANNELS	14	/* one 4 KiB query slice each in the 64 KiB reports area */

struct ps3rsx_fence {
	struct dma_fence base;
	struct list_head head;
};

struct ps3rsx_gart_chunk {
	struct page *page;		/* the 1 MiB of memory behind the IO page, or NULL */
	u32 users;			/* buffer objects in it */
	u32 free_seq;			/* the last fence that may have used it */
	unsigned long idle_since;	/* jiffies when it became empty */
	bool fixed;			/* backed for good (ring, boot reservation) */
};

/*
 * Lock order: drm filelist_mutex -> pf->lock -> own_lock -> exec_lock -> chan_lock / mm_lock
 * -> vc.lock (a spinlock). No path waits for the clear worker while holding exec_lock.
 */

/* vclear.c: a freed range waiting for its clear (a zombie) */
enum zombie_kind { ZOMBIE_VRAM, ZOMBIE_GART };

struct ps3rsx_zombie {
	struct list_head link;		/* vc.vram or vc.gart (vc.lock) */
	struct drm_mm_node *node;	/* allocated in its heap until reaped */
	struct ps3rsx_bo *bo;		/* the freed BO it lives in, or NULL (then own) */
	struct drm_mm_node own;		/* a swap chunk's or a load-time range's node */
	u64 start, size;
	u32 free_seq;			/* the last reference submitted when it was freed */
	enum zombie_kind kind;
	bool from_vswap;
	bool at_load;			/* one of the load-time zombies */
	struct vclear_cursor cur;	/* the fill's progress (VRAM; the worker's) */
};

struct batch;
struct ps3rsx_vclear {
	spinlock_t lock;
	struct list_head vram, gart;	/* zombies */
	u64 cpu_bytes, gpu_bytes, gart_bytes, vswap_bytes, peak_bytes;
	u64 reaped;			/* bytes given back so far (waiters watch it) */
	wait_queue_head_t reaped_wq;
	atomic_t waiters;		/* allocations waiting */
	bool started;			/* after console_init: the worker may run */
	bool gpu_off;			/* a fill trapped the GPU: the PPE only, until unload */
	bool wait_prim;			/* the worker waits for a primitive to close */
	u32 batch_seq, batch_gen;	/* the last batch's reference and generation */
	unsigned int load_pending;	/* load-time zombies not cleared yet (the dmesg line) */
	unsigned int load_cpu_pending;	/* of them, CPU-visible (all the CPU path can clear) */
	bool load_logged;
	u64 load_bytes, load_cpu_bytes;
	ktime_t load_t0;
	struct workqueue_struct *wq;
	struct delayed_work work;
	struct batch *batch;
	/* debugfs counters (ps3rsx_mm) */
	u64 filled_bytes, batches, ppe_bytes, refills, dirty_zero;
	u64 alloc_waits, alloc_wait_max_ms, alloc_shrinks;
};

/* vkeep.c: an open file as the owner of its BOs (refcounted: BOs can outlive the file) */
struct ps3rsx_owner {
	struct kref ref;		/* the file's, and each of its BOs' */
	u64 id;				/* never reused (debugging) */
	pid_t tgid;			/* the opener (debugfs only) */
	bool closed;			/* postclose ran: frees are cleared (mm_lock) */
	u64 kept_bytes;			/* kept for it now (mm_lock) */
	u32 kept_n;
};

#define KEEP_HIST	15		/* VRAM frees by size: 4K .. 64M+ */
#define KEEP_TG		8		/* tgids in the debugfs table */
struct ps3rsx_keep_tg {
	pid_t tgid;
	u64 freed, reused, cleared;
};

struct ps3rsx_keep {
	struct list_head kept;		/* kept BOs, oldest first (mm_lock) */
	u64 bytes, peak;
	u32 n;				/* entries kept (VCLEAR_KEEP_TOTAL_N at most) */
	atomic_t pressure;		/* the VRAM swap couldn't grow: convert all */
	/* counters (mm_lock) */
	u64 reused_n, reused_bytes, shared_bytes, noowner_bytes, conv[4];
	u32 hist[KEEP_HIST], hist_reused[KEEP_HIST];
	struct ps3rsx_keep_tg tg[KEEP_TG];
};

struct ps3rsx_device {
	struct drm_device drm;
	struct ps3fb_gpu_info info;
	struct ps3fb_gpu_ctx ctx;	/* our lv1 context (ps3fb keeps it across module reloads) */
	u64 vram_size;			/* VRAM we manage: from info.reserved up to memory_size */
	u64 gart_size;

	struct mutex mm_lock;
	struct drm_mm vram_cpu;		/* VRAM the CPU may map (below CPU_VRAM_LIMIT) */
	struct drm_mm vram_gpu;		/* VRAM only the GPU touches */
	struct drm_mm gart;		/* GART IO addresses (gart.c) */
	struct ps3rsx_gart_chunk *gart_chunks;	/* one per 1 MiB IO page */
	unsigned int gart_nchunks;
	bool gart_dynamic;		/* chunks from the CMA area on demand (else boot reservation) */
	struct page *gart_scratch;	/* behind IO pages without a chunk */
	u64 gart_backed;		/* bytes of the GART backed by memory now */
	struct delayed_work gart_reclaim;
	struct ps3rsx_vswap *vswap;	/* swap in spare VRAM (vswap.c), or NULL */
	struct ps3rsx_vclear vc;	/* freed VRAM cleared before reuse (vclear.c) */
	struct ps3rsx_keep keep;	/* and kept for its owner instead (vkeep.c) */

	/* execution (exec.c) */
	void __iomem *ctrl;		/* our context's FIFO control: put 0x40, get 0x44, ref 0x48 */
	void __iomem *dinfo;		/* our context's driver info (display heads at 0x10b8) */
	struct ps3fb_gpu_hook hook;
	struct mutex exec_lock;		/* ring writes */
	u32 ring_put;			/* IO address the next command goes to */
	bool ring_lapping;		/* wrapped, and the GPU may not have taken the JMP yet */
	u32 seqno;			/* last SET_REFERENCE value submitted */
	u64 fence_context;
	spinlock_t fence_lock;
	struct list_head fences;	/* pending, in seqno order */
	struct hrtimer fence_timer;
	bool fast_poll;			/* someone waits on a fence: poll fast (fence_lock) */
	u32 last_ref;
	ktime_t last_progress;
	bool wedged;			/* a graph exception or a stall: nothing runs until a recovery */
	/* unwedge.c: in-place recovery after a graph exception */
	struct work_struct recover_work;
	wait_queue_head_t recover_wq;
	bool recovering;		/* the work runs: submits wait for its outcome */
	u32 recover_gen;		/* bumped at each recovery: kernel copies from before fail */
	u32 kcopy_gen;			/* recover_gen when the pending kernel copy was queued */
	bool vblank_irq_on;		/* enable_vblank set the vsync mask (kms.c) */
	bool uw_ready;			/* exec_init done: only then may the recovery run */
	int fault_chan;			/* the faulting commit's channel (blame.h): refused; -1 none */
	pid_t fault_pid;		/* its pid and comm (crash record) */
	u32 fault_ref;			/* the reference register at the exception (hard IRQ) */
	u32 fault_seq;			/* the trapping commit (blame.h), if fault_known */
	bool fault_known;
	struct ps3rsx_blame blame;	/* the last commits, in ring order (exec_lock) */
	int blame_chan;			/* the channel the next commit belongs to, -1 the kernel */
	u64 blame_serial;		/* and its serial (0: the kernel) */
	u64 fault_serial;		/* the trapping commit's channel serial (mark_clients) */
	u64 chan_serial;		/* the last channel serial handed out (chan_lock) */
	struct list_head reports_bos;	/* every BO_REPORTS (mm_lock): zapped when the context moves */
	struct rw_semaphore reports_lock;	/* faults (read) against the switch and its zap (write) */
	u64 reports_lpar, reports_size;	/* the current context's reports area (reports_lock) */
	char fault_comm[TASK_COMM_LEN];

	struct mutex chan_lock;
	DECLARE_BITMAP(chan_ids, PS3RSX_MAX_CHANNELS);
	struct ps3rsx_shadow *hw_shadow;	/* what the GPU holds (exec_lock), except: */
	struct ps3rsx_shadow *hw_behind;	/* the last channel's shadow, whose pushes since it
						 * took the GPU are not in hw_shadow yet */
	int last_chan;				/* channel whose state the GPU holds, or -1 */
	pid_t last_pid;				/* last submitter (for exception reports) */
	char last_comm[TASK_COMM_LEN];
	struct ps3rsx_chan *prim_owner;		/* channel with a primitive open on the GPU */
	unsigned long prim_since;		/* jiffies of its submit that left it open */
	unsigned long last_submit;		/* jiffies of the last client submit */
	wait_queue_head_t prim_wq;
	u32 *restore_buf;

	/* KMS (kms.c) */
	struct drm_connector connector;
	struct drm_plane primary, cursor;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct hrtimer vblank_timer;		/* fallback vblank (vblank_irq=0) */
	unsigned int vsync_virq;		/* our context's IRQ outlet: vsync (0 if not plugged) */
	struct drm_pending_vblank_event *flip_event;	/* sent once the flip latched (event_lock) */
	bool video_muted;			/* DPMS off: ps3av's video mute (kms.c) */
	unsigned int flip_wait;			/* vblanks waited for it */

	/* GPU shared with ps3fb's consoles (console.c) */
	struct mutex own_lock;
	wait_queue_head_t own_wq;
	struct delayed_work release_work;
	bool gpu_owned;				/* we hold it (ps3fb_gpu_takeover) */
	bool master_active;			/* a DRM master (X) is in front */
	bool console_pending;			/* hand it to ps3fb once idle */
	int masters_open;			/* open files that have been master */
	int submitting;				/* submits between hold and unhold */
	ktime_t vblank_period;
};

#define to_rsx(d) container_of(d, struct ps3rsx_device, drm)

enum bo_kind { BO_VRAM, BO_VRAM_GPU_ONLY, BO_GART, BO_REPORTS };

struct ps3rsx_bo {
	struct drm_gem_object base;
	enum bo_kind kind;
	struct list_head reports_link;	/* BO_REPORTS: on rsx->reports_bos (mm_lock) */
	u32 domain;			/* NOUVEAU_GEM_DOMAIN_VRAM or _GART */
	u64 offset;			/* GPU offset in its domain's ctxdma */
	struct drm_mm_node node;
	struct ps3rsx_zombie zombie;	/* its range after the free, until cleared (vclear.c) */
	struct ps3rsx_owner *owner;	/* the file that made it (a reference), or NULL */
	bool shared;			/* another file or a dma-buf could reach it: always cleared */
	bool dumb;			/* a dumb buffer: always cleared */
	struct list_head kept_link;	/* on keep.kept while kept for its owner (mm_lock) */
	unsigned long kept_at;		/* jiffies of the free */
	u32 kept_seq;			/* the reference submitted at the free */
};

#define to_bo(o) container_of(o, struct ps3rsx_bo, base)

struct drm_nouveau_gem_info;
int ps3rsx_mem_init(struct ps3rsx_device *rsx);
void ps3rsx_mem_fini(struct ps3rsx_device *rsx);
struct ps3rsx_bo *ps3rsx_bo_new(struct ps3rsx_device *rsx, u32 domain, size_t size, u32 align,
				 struct ps3rsx_owner *owner);
const struct drm_gem_object_funcs *ps3rsx_bo_funcs(void);
/* vkeep.c */
struct ps3rsx_owner *ps3rsx_owner_new(void);
struct ps3rsx_owner *ps3rsx_owner_get(struct ps3rsx_owner *o);
void ps3rsx_owner_put(struct ps3rsx_owner *o);
void ps3rsx_owner_close(struct ps3rsx_device *rsx, struct ps3rsx_owner *o);
bool ps3rsx_keep_bo(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo);
struct ps3rsx_bo *ps3rsx_reuse(struct ps3rsx_device *rsx, struct ps3rsx_owner *o, u64 size,
			       u64 align, bool mappable);
bool ps3rsx_keep_pressure(struct ps3rsx_device *rsx, struct ps3rsx_owner *o, u64 need);
void ps3rsx_keep_pressure_async(struct ps3rsx_device *rsx);
void ps3rsx_keep_age(struct ps3rsx_device *rsx);
void ps3rsx_keep_init(struct ps3rsx_device *rsx);
void ps3rsx_keep_fini(struct ps3rsx_device *rsx);
void ps3rsx_keep_show(struct seq_file *m, struct ps3rsx_device *rsx);
struct ps3rsx_bo *ps3rsx_bo_new_reports(struct ps3rsx_device *rsx);
void ps3rsx_reports_moved(struct ps3rsx_device *rsx, u64 lpar, u64 size);
int ps3rsx_bo_handle(struct drm_file *file, struct ps3rsx_bo *bo,
		     struct drm_nouveau_gem_info *info);
int ps3rsx_ioctl_gem_new(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_info(struct drm_device *dev, void *data, struct drm_file *file);

struct drm_mode_create_dumb;
int ps3rsx_kms_init(struct ps3rsx_device *rsx);
void ps3rsx_kms_fini(struct ps3rsx_device *rsx);
void ps3rsx_kms_reflip(struct ps3rsx_device *rsx);
void ps3rsx_kms_show_console(struct ps3rsx_device *rsx);
bool ps3rsx_exec_idle(struct ps3rsx_device *rsx);
void ps3rsx_exec_forget_hw_state(struct ps3rsx_device *rsx);
void ps3rsx_shadow_clear(struct ps3rsx_shadow *sh);
struct ps3rsx_chan;
struct ps3rsx_file;
int ps3rsx_console_hold(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch,
			struct ps3rsx_file *pf);
void ps3rsx_console_unhold(struct ps3rsx_device *rsx);
void ps3rsx_master_set(struct drm_device *dev, struct drm_file *file, bool from_open);
void ps3rsx_master_drop(struct drm_device *dev, struct drm_file *file);
void ps3rsx_console_file_closed(struct ps3rsx_device *rsx, struct ps3rsx_file *pf);
void ps3rsx_console_init(struct ps3rsx_device *rsx);
void ps3rsx_console_fini(struct ps3rsx_device *rsx);
int ps3rsx_dumb_create(struct drm_file *file, struct drm_device *dev,
		       struct drm_mode_create_dumb *args);

int ps3rsx_exec_init(struct ps3rsx_device *rsx);
/* unwedge.c: the in-place recovery after a graph exception, display-only mode, the crash record */
bool ps3rsx_uw_active(void);
void ps3rsx_uw_exception(struct ps3rsx_device *rsx);
void ps3rsx_uw_dev_init(struct ps3rsx_device *rsx);
void ps3rsx_uw_dev_fini(struct ps3rsx_device *rsx);
void ps3rsx_uw_sysfs_init(struct ps3rsx_device *rsx);
void ps3rsx_uw_quiesce(struct ps3rsx_device *rsx);
void ps3rsx_exec_fail_fences(struct ps3rsx_device *rsx);
void ps3rsx_exec_cancel_poll(struct ps3rsx_device *rsx);
void ps3rsx_exec_restart_poll(struct ps3rsx_device *rsx);
int ps3rsx_gart_remap_all(struct ps3rsx_device *rsx);
void ps3rsx_kms_vsync_irq_fini(struct ps3rsx_device *rsx);
void ps3rsx_kms_vsync_irq_init(struct ps3rsx_device *rsx);
extern bool ps3rsx_test_allow_rt12;
bool ps3rsx_uw_display_only(void);
void ps3rsx_uw_start(void);
void ps3rsx_uw_stop(void);
void ps3rsx_uw_exit(void);
void ps3rsx_exec_fini(struct ps3rsx_device *rsx);
u32 *ps3rsx_ring_begin(struct ps3rsx_device *rsx, u32 len, int *err);
struct dma_fence *ps3rsx_ring_commit(struct ps3rsx_device *rsx, u32 len, bool kick);

struct ps3rsx_shadow *ps3rsx_shadow_new(void);
void ps3rsx_shadow_free(struct ps3rsx_shadow *sh);
int ps3rsx_shadow_restore(const struct ps3rsx_shadow *ch, const struct ps3rsx_shadow *hw,
			  u32 *buf, u32 max_words);
void ps3rsx_shadow_take(struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw);
void ps3rsx_shadow_take_restored(struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw,
				 const u32 *buf, u32 nwords);
struct seq_file;
void ps3rsx_shadow_show(struct seq_file *m, const struct ps3rsx_shadow *sh);
void ps3rsx_submit_debugfs_init(struct ps3rsx_device *rsx);
#define RESTORE_MAX_WORDS	(16 * 1024)

bool ps3rsx_known_object(u32 handle);
/* kcopy.c: kernel copies VRAM -> GART for the swap path */
struct kcopy_run {
	u32 vram;		/* VRAM offset */
	u32 gart;		/* GART IO address */
	u32 npages;		/* 4 KiB pages, contiguous on both sides */
};
int ps3rsx_kcopy_begin(struct ps3rsx_device *rsx);
int ps3rsx_kcopy_to_gart(struct ps3rsx_device *rsx, const struct kcopy_run *r, unsigned int n,
			 u32 *seq);
void ps3rsx_kcopy_end(struct ps3rsx_device *rsx);
int ps3rsx_kcopy_wait(struct ps3rsx_device *rsx, u32 seq, unsigned int timeout_us);
u32 ps3rsx_ring_commit_seq(struct ps3rsx_device *rsx, u32 len);
void ps3rsx_hw_shadow_sync(struct ps3rsx_device *rsx);
bool ps3rsx_kcopy_allowed(void);
int ps3rsx_gart_pin(struct ps3rsx_device *rsx, struct drm_mm_node *node, u32 *io);
void ps3rsx_gart_unpin(struct ps3rsx_device *rsx, struct drm_mm_node *node);
int ps3rsx_kcopy_fill(struct ps3rsx_device *rsx, const struct vclear_launch *l, unsigned int n,
		      u32 zero_io, u32 *seq);
int ps3rsx_console_hold_kernel(struct ps3rsx_device *rsx);
bool ps3rsx_console_wanted(struct ps3rsx_device *rsx);
int ps3rsx_close_stale_primitive(struct ps3rsx_device *rsx);
bool ps3rsx_dry_run(void);
/* vclear.c */
int ps3rsx_vclear_init(struct ps3rsx_device *rsx, u64 cpu_start, u64 cpu_end, u64 gpu_end);
void ps3rsx_vclear_start(struct ps3rsx_device *rsx);
void ps3rsx_vclear_stop(struct ps3rsx_device *rsx);
void ps3rsx_vclear_fini(struct ps3rsx_device *rsx);
void ps3rsx_vclear_kick(struct ps3rsx_device *rsx, unsigned long delay);
bool ps3rsx_vclear_bo_free(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo);
/* the same with mm_lock held by the caller: a GART zombie is then reaped later, not inline */
bool ps3rsx_vclear_bo_free_locked(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo, bool locked);
void ps3rsx_vclear_reap_gart(struct ps3rsx_device *rsx);
struct ps3rsx_zombie *ps3rsx_vclear_zombie_new(void);
int ps3rsx_vclear_take_node(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z,
			    struct drm_mm_node *node);
void ps3rsx_vclear_add(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z);
void ps3rsx_vclear_pending(struct ps3rsx_device *rsx, u64 *cpu, bool *cpu_ok, u64 *gpu,
			   bool *gpu_ok, u64 *shrunk);
bool ps3rsx_vclear_gart_pending(struct ps3rsx_device *rsx);
int ps3rsx_vclear_wait(struct ps3rsx_device *rsx, unsigned int ms);
void ps3rsx_vclear_show(struct seq_file *m, struct ps3rsx_device *rsx);
/* vswap.c */
struct ps3rsx_vswap;
int ps3rsx_vswap_init(struct ps3rsx_device *rsx);
void ps3rsx_vswap_fini(struct ps3rsx_device *rsx);
bool ps3rsx_vswap_shrink(struct ps3rsx_device *rsx, u64 bytes);
void ps3rsx_vswap_show(struct seq_file *m, struct ps3rsx_device *rsx);
/* gart.c */
int ps3rsx_gart_init(struct ps3rsx_device *rsx);
void ps3rsx_gart_fini(struct ps3rsx_device *rsx);
int ps3rsx_gart_get(struct ps3rsx_device *rsx, u64 io, u64 size);
void ps3rsx_gart_put(struct ps3rsx_device *rsx, u64 io, u64 size, u32 seq);
void *ps3rsx_gart_ptr(struct ps3rsx_device *rsx, u64 io);
unsigned long ps3rsx_gart_pfn(struct ps3rsx_device *rsx, u64 io);
void ps3rsx_gart_read(struct ps3rsx_device *rsx, void *dst, u64 io, size_t len);
void ps3rsx_gart_clear(struct ps3rsx_device *rsx, u64 io, size_t len);
bool ps3rsx_seq_done(struct ps3rsx_device *rsx, u32 seq);
u32 ps3rsx_ref_now(struct ps3rsx_device *rsx);

int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_shadow_write(struct ps3rsx_shadow *sh, u32 subch, u32 m, u32 v);
void ps3rsx_shadow_burst(struct ps3rsx_shadow *sh, u32 subch, u32 mthd, bool noninc,
			 const u32 *v, u32 n);
void ps3rsx_shadow_push(struct ps3rsx_shadow *sh, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);
void ps3rsx_shadow_init(void);

struct ps3rsx_chan *ps3rsx_chan_find(struct ps3rsx_file *pf, u32 id);
void ps3rsx_file_chans_free(struct ps3rsx_device *rsx, struct ps3rsx_file *pf);
int ps3rsx_ioctl_channel_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_channel_free(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_notifierobj_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_grobj_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gpuobj_free(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_pushbuf(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_cpu_prep(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_cpu_fini(struct drm_device *dev, void *data, struct drm_file *file);

/* Per channel validator state: which object this channel bound on each subchannel */
struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];		/* validate.c's class of each bound object */
	bool prim_open;		/* the channel's last BEGIN_END opened a primitive */
};

struct ps3rsx_shadow;

/* the crash record: the channel's last submits (unwedge.c prints them at an exception) */
#define PS3RSX_HIST	8
struct ps3rsx_subhist {
	u32 seq;		/* its fence */
	pid_t pid;
	u32 len;		/* bytes of pushes */
	u32 first, last;	/* the first and the last word of the pushes */
};

struct ps3rsx_chan {
	struct list_head head;		/* pf->chans (pf->lock), until CHANNEL_FREE */
	struct kref ref;		/* the file's, and each submit's in flight (submit.c) */
	struct ps3rsx_device *rsx;
	bool dead;			/* freed: its submits still in the kernel refuse (exec_lock) */
	u32 id;
	u64 serial;			/* unique per device, never reused (blame.h) */
	struct ps3rsx_vstate vstate;
	struct ps3rsx_shadow *shadow;	/* the state this channel's pushes set (shadow.c) */
	struct ps3rsx_file *pf;		/* the file it belongs to */
	bool broken;			/* refused: its open primitive was closed under it, or a
				 * push of it was rejected (its later ones count on it), and
				 * its client doesn't recover (PS3_RECOVER) */
	u32 cancelled;			/* submits cancelled for a rejected push (it recovers) */
	bool fatal;			/* its client's push trapped the GPU: -ENODEV from now on */
	bool lost;			/* the GPU was recovered under it: -ECANCELED once (PS3_RECOVER) or -EIO */
	struct ps3rsx_subhist hist[PS3RSX_HIST];	/* the last submits (exec_lock) */
	u32 hist_n;
#ifdef PS3RSX_PUSH_DUMP
	struct pd_chan_state pd;	/* the push dump's view of its binds (pushdump.c, exec_lock) */
#endif
};

/* Per open file */
struct ps3rsx_file {
	struct mutex lock;
	bool abi_ok;			/* PS3_ABI was queried */
	bool recover;			/* PS3_RECOVER was queried: -ECANCELED, not refusal */
	bool refused;			/* its push trapped the GPU, or it was lost under a
					 * recovery without PS3_RECOVER: no new channel or buffer */
	struct list_head objects;	/* NVIF objects (device, engine objects) */
	struct list_head chans;
	bool was_master;		/* counted in masters_open */
	struct ps3rsx_owner *owner;	/* of the BOs it makes (vkeep.c) */
};

int ps3rsx_ioctl_getparam(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_nvif(struct drm_file *file, void __user *user, u32 size);
void ps3rsx_file_objects_free(struct ps3rsx_file *pf);

#endif
