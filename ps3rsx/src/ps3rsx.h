/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps3rsx: a DRM driver for the PS3's RSX on top of the lv1 hypervisor's GPU calls. It speaks
 * nouveau's userspace ABI (driver name "nouveau"), so Mesa's nv30 driver can run on it with a
 * small patch for the hypervisor's fixed object handles. Plan: docs/DRIVER-PLAN.md.
 */
#ifndef PS3RSX_H
#define PS3RSX_H

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
 * unpatched Mesa fails at init instead of submitting streams with handles the RSX lacks. */
#define PS3RSX_GETPARAM_PS3_ABI	0x1000
#define PS3RSX_ABI_VERSION	1

#define CPU_VRAM_LIMIT		0xe000000ULL	/* ps3fb's VRAM mapping size: CPU access above it is unproven */

/* GART IO layout: reset program, then the ring, then BOs (exec.c) */
#define RING_START		0x1000
#define RING_END		0x200000
#define RING_RESERVE		RING_END
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
	bool wedged;			/* a graph exception or a stall: nothing runs until reboot */

	struct mutex chan_lock;
	DECLARE_BITMAP(chan_ids, PS3RSX_MAX_CHANNELS);
	struct ps3rsx_shadow *hw_shadow;	/* what the GPU holds (exec_lock), except: */
	struct ps3rsx_shadow *hw_behind;	/* the last channel's shadow, whose pushes since it
						 * took the GPU are not in hw_shadow yet */
	int last_chan;				/* channel whose state the GPU holds, or -1 */
	pid_t last_pid;				/* last submitter (for exception reports) */
	char last_comm[TASK_COMM_LEN];
	struct ps3rsx_chan *prim_owner;		/* channel with a primitive open on the GPU */
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
	u32 domain;			/* NOUVEAU_GEM_DOMAIN_VRAM or _GART */
	u64 offset;			/* GPU offset in its domain's ctxdma */
	struct drm_mm_node node;
};

#define to_bo(o) container_of(o, struct ps3rsx_bo, base)

struct drm_nouveau_gem_info;
int ps3rsx_mem_init(struct ps3rsx_device *rsx);
void ps3rsx_mem_fini(struct ps3rsx_device *rsx);
struct ps3rsx_bo *ps3rsx_bo_new(struct ps3rsx_device *rsx, u32 domain, size_t size, u32 align);
struct ps3rsx_bo *ps3rsx_bo_new_reports(struct ps3rsx_device *rsx);
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

struct ps3rsx_chan {
	struct list_head head;
	u32 id;
	struct ps3rsx_vstate vstate;
	struct ps3rsx_shadow *shadow;	/* the state this channel's pushes set (shadow.c) */
	bool broken;			/* its open primitive was closed under it: refuse it */
};

/* Per open file */
struct ps3rsx_file {
	struct mutex lock;
	bool abi_ok;			/* PS3_ABI was queried */
	struct list_head objects;	/* NVIF objects (device, engine objects) */
	struct list_head chans;
	bool was_master;		/* counted in masters_open */
};

int ps3rsx_ioctl_getparam(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_nvif(struct drm_file *file, void __user *user, u32 size);
void ps3rsx_file_objects_free(struct ps3rsx_file *pf);

#endif
