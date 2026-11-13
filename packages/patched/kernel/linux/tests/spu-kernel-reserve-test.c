/* spu-kernel-reserve-test: local/0058's reserve, reset, LS decode, release and reboot code
 * (arch/powerpc/platforms/cell/spu_base.c, the block between its "SPEs reserved for a kernel
 * driver" markers) compiled in userspace against a model of six SPEs, spufs's list walks and
 * spu_base's interrupt handlers:
 *
 *  - the registers the block touches (run control and status, MFC control with its suspend,
 *    purge and queue-empty fields, SLB invalidate, LSLR, privileged control, NPC, the LS), and
 *    stuck variants of each quiesce step;
 *  - the priv1 operations as PS3's: interrupt masks and status per class, SR1 with ps3/spu.c's
 *    BUG_ON when any bit but problem state and LS decode changes, TCLASS;
 *  - interrupts: a class whose status and mask meet runs the handler's callback calls at the
 *    next register access outside register_lock (as the other hardware thread would), and a
 *    "late" interrupt runs a handler with nothing pending, which for class 0 still calls
 *    stop_callback (spu_base.c's spu_irq_class_0);
 *  - spufs's walks of the node list: spu_get_idle, find_victim, do_notify_spus_active,
 *    spusched_thread's walk that drops list_mutex with its cursor parked on an SPE, and the
 *    affinity path (ctx_location over aff_list, then spu_get_idle's alloc_state test);
 *  - the outbound mailboxes and status that latches while a class is masked (a stop, a mailbox
 *    write), and late class 0 deliveries during each grace period.
 *
 * packages/patched/kernel/linux/tests/spu-kernel-reserve.sh extracts the block and the header
 * part from the patch, builds this, and runs the fix-offs (each must fail). */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef unsigned long long u64;	/* as the kernel's (int-ll64) */
typedef uint32_t u32;
typedef uint8_t u8;
typedef long long ktime_t;

#define __iomem
#define __init
#define MAX_NUMNODES 2
#define NSPE 6
#define EXPORT_SYMBOL_GPL(x) extern int spu_test_export_##x
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define eieio() do { } while (0)
#define NOTIFY_DONE 0
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); \
	printf("  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

/* --- the kernel's lists, locks, time ------------------------------------------------------ */
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(n) struct list_head n = { &(n), &(n) }
static void INIT_LIST_HEAD(struct list_head *l) { l->next = l->prev = l; }
static void list_add(struct list_head *n, struct list_head *h)
{
	n->next = h->next; n->prev = h; h->next->prev = n; h->next = n;
}
static void list_del_init(struct list_head *e)
{
	e->prev->next = e->next; e->next->prev = e->prev; INIT_LIST_HEAD(e);
}
static struct list_head poison2;
static void list_del_rcu(struct list_head *e)	/* ->next kept, ->prev poisoned (LIST_POISON2) */
{
	e->prev->next = e->next; e->next->prev = e->prev; e->prev = &poison2;
}
#define list_for_each_entry(p, h, m) \
	for (p = container_of((h)->next, __typeof__(*p), m); &p->m != (h); \
	     p = container_of(p->m.next, __typeof__(*p), m))

struct mutex { int held; const char *name; };
#define DEFINE_MUTEX(n) struct mutex n = { 0, #n }
static int might_sleep_calls;
#define might_sleep() (might_sleep_calls++)
static void mutex_lock(struct mutex *m)
{
	CHECK(!m->held, "mutex %s taken twice (deadlock)", m->name);
	m->held = 1;
}
static void mutex_unlock(struct mutex *m)
{
	CHECK(m->held, "mutex %s released unheld", m->name);
	m->held = 0;
}

typedef struct { int held; } spinlock_t;
static void deliver_pending(void);
struct spu;
static void late_irq(struct spu *spu, int c);
#define spin_lock_irqsave(l, f) do { (f) = 0; CHECK(!(l)->held, "register_lock taken twice"); (l)->held = 1; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); (l)->held = 0; deliver_pending(); } while (0)

static ktime_t now_us;
static ktime_t ktime_get(void) { return ++now_us; }
static ktime_t ktime_add_us(ktime_t t, long us) { return t + us; }
static bool ktime_after(ktime_t a, ktime_t b) { return a > b; }
static void udelay(unsigned long us) { now_us += us; }
static int msleep_ms;		/* the grace periods slept, in total */
static void late_in_grace(void);
static void msleep(unsigned int ms) { now_us += 1000LL * ms; msleep_ms += ms; late_in_grace(); }
struct module { int refs; };
static void __module_get(struct module *m) { m->refs++; }

static int allocs;	/* nothing on the reserve, reset or release path may allocate */
#define GFP_KERNEL 0
#define GFP_NOIO 0
#define kmalloc(n, f) (allocs++, (void *)0)
#define kzalloc(n, f) (allocs++, (void *)0)
#define vmalloc(n) (allocs++, (void *)0)
#define request_firmware(fw, name, dev) (allocs++, -ENOENT)

static char log_buf[256];
#define pr_info(...) snprintf(log_buf, sizeof(log_buf), __VA_ARGS__)
#define pr_err(...) snprintf(log_buf, sizeof(log_buf), __VA_ARGS__)
#define pr_warn(...) snprintf(log_buf, sizeof(log_buf), __VA_ARGS__)

#define IS_ERR(p) ((unsigned long)(p) >= (unsigned long)-4095)
#define PTR_ERR(p) ((long)(p))
#define ERR_PTR(e) ((void *)(long)(e))

/* --- asm/spu.h's constants (the values as in 6.12) ----------------------------------------- */
#define LS_SIZE (256 * 1024)
#define LS_ADDR_MASK (LS_SIZE - 1)
#define SPU_RUNCNTL_STOP	0L
#define SPU_RUNCNTL_RUNNABLE	1L
#define SPU_STATUS_RUNNING		0x1
#define SPU_STATUS_STOPPED_BY_STOP	0x2
#define SPU_STATUS_ISOLATED_STATE       0x80
#define SPU_STATUS_ISOLATED_LOAD_STATUS 0x200
#define SPU_STATUS_ISOLATED_EXIT_STATUS 0x400
#define MFC_CNTL_RESUME_DMA_QUEUE		(0ull << 0)
#define MFC_CNTL_SUSPEND_DMA_QUEUE		(1ull << 0)
#define MFC_CNTL_SUSPEND_MASK			(1ull << 4)
#define MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION	(0ull << 8)
#define MFC_CNTL_SUSPEND_IN_PROGRESS		(1ull << 8)
#define MFC_CNTL_SUSPEND_COMPLETE		(3ull << 8)
#define MFC_CNTL_SUSPEND_DMA_STATUS_MASK	(3ull << 8)
#define MFC_CNTL_DMA_QUEUES_EMPTY		(1ull << 14)
#define MFC_CNTL_PURGE_DMA_REQUEST		(1ull << 15)
#define MFC_CNTL_PURGE_DMA_IN_PROGRESS		(1ull << 24)
#define MFC_CNTL_PURGE_DMA_COMPLETE		(3ull << 24)
#define MFC_CNTL_PURGE_DMA_STATUS_MASK		(3ull << 24)
#define SPU_PRIVCNTL_MODE_NORMAL		(0x0ull << 0)
#define MFC_STATE1_LOCAL_STORAGE_DECODE_MASK	0x01ull
#define MFC_STATE1_BUS_TLBIE_MASK		0x02ull
#define MFC_STATE1_PROBLEM_STATE_MASK		0x08ull
#define MFC_STATE1_RELOCATE_MASK		0x10ull
#define MFC_STATE1_MASTER_RUN_CONTROL_MASK	0x20ull
#define CLASS0_INTR_MASK	0x7L
#define CLASS1_STORAGE_FAULT_INTR 0x2L
#define CLASS1_INTR_MASK	0xfL
#define CLASS2_MAILBOX_INTR	0x1L
#define CLASS2_SPU_STOP_INTR	0x2L
#define CLASS2_SPU_HALT_INTR	0x4L
#define CLASS2_SPU_DMA_TAG_GROUP_COMPLETE_INTR 0x8L
#define CLASS2_MAILBOX_THRESHOLD_INTR 0x10L
#define CLASS2_INTR_MASK	0x1fL

/* the header part of the patch: enum spu_kernel_state, struct spu_kernel_ops */
#include "spu-h.h"

struct spu_problem { u32 spu_runcntl_RW, spu_status_R, spu_npc_RW, mb_stat_R, pu_mb_R; };
struct spu_priv2 { u64 mfc_control_RW, slb_invalidate_all_W, spu_lslr_RW, spu_privcntl_RW, puint_mb_R; };
struct device { int id; };
struct mm_struct { int x; };
struct spu_context { int x; };

struct spu {
	u8 *local_store;
	struct spu_problem __iomem *problem;
	struct spu_priv2 __iomem *priv2;
	struct list_head cbe_list;
	struct list_head full_list;
	enum { SPU_FREE, SPU_USED } alloc_state;
	enum spu_kernel_state kernel_state;
	struct module *kernel_owner;
	int number;
	unsigned int irqs[3];
	u32 node;
	unsigned long flags;
	unsigned int slb_replace;
	struct mm_struct *mm;
	struct spu_context *ctx;
	pid_t pid, tgid;
	spinlock_t register_lock;
	void (*wbox_callback)(struct spu *spu);
	void (*ibox_callback)(struct spu *spu);
	void (*stop_callback)(struct spu *spu, int irq);
	void (*mfc_callback)(struct spu *spu);
	struct device dev;
};

struct cbe_spu_info { struct mutex list_mutex; struct list_head spus; int n_spus; };
static struct cbe_spu_info cbe_spu_info[MAX_NUMNODES];
static LIST_HEAD(spu_full_list);
static DEFINE_MUTEX(spu_full_list_mutex);

/* --- the hardware model ---------------------------------------------------------------------- */
static struct hw {
	struct spu spu;
	struct spu_problem prob;
	struct spu_priv2 priv2;
	u8 ls[LS_SIZE];
	bool running, stuck_run, stuck_suspend, stuck_purge, stuck_queue, isolated;
	int suspend;		/* 0 normal, 1 in progress, 3 complete */
	int purge;		/* 0 none, 1 in progress, 3 complete */
	int queued;		/* DMA commands in the queue */
	u64 sr1, tclass, masks[3], stat[3], level[3];
	bool slb_valid, bug;
	int sr1_writes, slb_invalidates, channel_inits, sync_irqs, suspend_writes, purge_writes;
	int pu_mb, puint_mb;	/* entries in the outbound mailboxes (one deep each) */
	bool resumed_after_purge;
	bool program;		/* a program that stops with a stop-and-signal when STOP comes */
} hw[NSPE];

static struct hw *hw_of_ptr(const void *p)
{
	for (int i = 0; i < NSPE; i++)
		if ((const char *)p >= (const char *)&hw[i] && (const char *)p < (const char *)(&hw[i] + 1))
			return &hw[i];
	fprintf(stderr, "access outside the model\n");
	abort();
}

static struct hw *hw_of(struct spu *spu) { return container_of(spu, struct hw, spu); }

/* the driver's callbacks and their bookkeeping */
static int drv_calls, drv_calls_bad;
static bool drv_gone;		/* released or given up: never called again */
static bool in_reserve;		/* a reserve is running */
static bool handed[NSPE];	/* reserve returned this SPE to the driver */
static int null_calls;
static void drv_stop(struct spu *spu, int irq)
{
	(void)irq;
	drv_calls++;
	if (drv_gone || !handed[spu->number])
		drv_calls_bad++;
}
static void drv_box(struct spu *spu) { drv_stop(spu, 2); }
static struct module drv_module;
static const struct spu_kernel_ops drv_ops = { drv_stop, drv_box, drv_box, drv_box, &drv_module };

/* lv1 delivers a class 0 late, with nothing pending, while an SPE changes owner */
static void late_in_grace(void)
{
	for (int i = 0; i < NSPE; i++)
		if (hw[i].spu.kernel_state != SPU_KERNEL_NONE || in_reserve)
			late_irq(&hw[i].spu, 0);
}
static int spufs_calls;		/* spufs's leftovers in the "used" start state */
static void spufs_stop(struct spu *spu, int irq) { (void)spu; (void)irq; spufs_calls++; }
static void spufs_box(struct spu *spu) { (void)spu; spufs_calls++; }

/* spu_base.c's handlers, as far as they call the callbacks */
static void cb_stop(struct spu *spu, int irq)
{
	if (!spu->stop_callback) { null_calls++; return; }
	spu->stop_callback(spu, irq);
}
static void cb_box(struct spu *spu, void (*f)(struct spu *))
{
	if (!f) { null_calls++; return; }
	f(spu);
}
static void handler(struct hw *h, int c)
{
	struct spu *spu = &h->spu;
	u64 stat = (h->stat[c] | h->level[c]) & h->masks[c];

	h->spu.register_lock.held = 1;
	if (c == 0)
		cb_stop(spu, 0);		/* unconditional in spu_irq_class_0 */
	if (c == 1 && (stat & CLASS1_STORAGE_FAULT_INTR))
		cb_stop(spu, 1);
	if (c == 2) {
		if (stat & (CLASS2_MAILBOX_INTR | CLASS2_MAILBOX_THRESHOLD_INTR))
			h->masks[2] &= ~(stat & (CLASS2_MAILBOX_INTR | CLASS2_MAILBOX_THRESHOLD_INTR));
		if (stat & CLASS2_MAILBOX_INTR)
			cb_box(spu, spu->ibox_callback);
		if (stat & (CLASS2_SPU_STOP_INTR | CLASS2_SPU_HALT_INTR))
			cb_stop(spu, 2);
		if (stat & CLASS2_SPU_DMA_TAG_GROUP_COMPLETE_INTR)
			cb_box(spu, spu->mfc_callback);
		if (stat & CLASS2_MAILBOX_THRESHOLD_INTR)
			cb_box(spu, spu->wbox_callback);
	}
	h->stat[c] &= ~stat;
	h->spu.register_lock.held = 0;
}

/* an interrupt whose status and mask meet is taken as soon as register_lock is free */
static void deliver_pending(void)
{
	for (int i = 0; i < NSPE; i++)
		for (int c = 0; c < 3; c++) {
			struct hw *h = &hw[i];

			if (h->spu.register_lock.held)
				continue;
			if ((h->stat[c] | h->level[c]) & h->masks[c])
				handler(h, c);
		}
}

/* an interrupt lv1 delivers late: the handler runs with the masks as they are now */
static void late_irq(struct spu *spu, int c)
{
	handler(hw_of(spu), c);
}

static u32 in_be32(const volatile u32 *p)
{
	struct hw *h = hw_of_ptr((const void *)p);

	deliver_pending();
	if (p == &h->prob.spu_status_R)
		return (h->running ? SPU_STATUS_RUNNING : SPU_STATUS_STOPPED_BY_STOP) |
		       (h->isolated ? SPU_STATUS_ISOLATED_STATE : 0);
	if (p == &h->prob.mb_stat_R)
		return (u32)h->pu_mb | (u32)h->puint_mb << 16;
	if (p == &h->prob.pu_mb_R) {
		if (h->pu_mb)
			h->pu_mb--;
		return 0x1234;
	}
	return *p;
}
static void out_be32(volatile u32 *p, u32 v)
{
	struct hw *h = hw_of_ptr((const void *)p);

	*p = v;
	if (p == &h->prob.spu_runcntl_RW) {
		if (v == SPU_RUNCNTL_STOP && !h->stuck_run) {
			if (h->running && h->program)	/* its stop-and-signal latches, masked or not */
				h->stat[2] |= CLASS2_SPU_STOP_INTR;
			h->running = false;
		}
		if (v == SPU_RUNCNTL_RUNNABLE)
			h->running = true;
	}
	deliver_pending();
}
static u64 in_be64(const volatile u64 *p)
{
	struct hw *h = hw_of_ptr((const void *)p);

	deliver_pending();
	if (p == &h->priv2.mfc_control_RW)
		return ((u64)h->suspend << 8) | (h->queued ? 0 : MFC_CNTL_DMA_QUEUES_EMPTY) |
		       ((u64)h->purge << 24);
	if (p == &h->priv2.puint_mb_R) {
		if (h->puint_mb)
			h->puint_mb--;
		return 0x5678;
	}
	return *p;
}
static void out_be64(volatile u64 *p, u64 v)
{
	struct hw *h = hw_of_ptr((const void *)p);

	*p = v;
	if (p == &h->priv2.mfc_control_RW) {
		if (!(v & MFC_CNTL_SUSPEND_MASK)) {
			if (v & MFC_CNTL_SUSPEND_DMA_QUEUE)
				h->suspend_writes++;
			if (v & MFC_CNTL_SUSPEND_DMA_QUEUE)
				h->suspend = h->stuck_suspend ? 1 : 3;
			else {
				if (h->purge == 3)
					h->resumed_after_purge = true;
				h->suspend = 0;
				h->purge = 0;
			}
		}
		if (v & MFC_CNTL_PURGE_DMA_REQUEST) {
			h->purge_writes++;
			h->purge = h->stuck_purge ? 1 : 3;
			if (!h->stuck_purge && !h->stuck_queue)
				h->queued = 0;
		}
	}
	deliver_pending();
}
static void memset_io(volatile void *p, int c, size_t n) { memset((void *)p, c, n); }

/* the priv1 operations, as ps3/spu.c's */
static void spu_int_mask_set(struct spu *spu, int c, u64 m) { hw_of(spu)->masks[c] = m; }
static void spu_int_stat_clear(struct spu *spu, int c, u64 s) { hw_of(spu)->stat[c] &= ~s; }
static u64 spu_mfc_sr1_get(struct spu *spu) { return hw_of(spu)->sr1; }
static void spu_mfc_sr1_set(struct spu *spu, u64 sr1)
{
	struct hw *h = hw_of(spu);
	const u64 allowed = ~(MFC_STATE1_LOCAL_STORAGE_DECODE_MASK | MFC_STATE1_PROBLEM_STATE_MASK);

	if ((sr1 & allowed) != (h->sr1 & allowed)) {	/* ps3/spu.c: BUG_ON */
		h->bug = true;
		return;
	}
	h->sr1 = sr1;
	h->sr1_writes++;
}
static void spu_mfc_tclass_id_set(struct spu *spu, u64 t) { hw_of(spu)->tclass = t; }
/* spu_base.c's own, outside the block */
static void spu_invalidate_slbs(struct spu *spu)
{
	struct hw *h = hw_of(spu);

	if (h->sr1 & MFC_STATE1_RELOCATE_MASK) {
		h->slb_valid = false;
		h->slb_invalidates++;
	}
}
static void spu_init_channels(struct spu *spu) { hw_of(spu)->channel_inits++; }
static void spu_associate_mm(struct spu *spu, struct mm_struct *mm) { spu->mm = mm; }
static void synchronize_irq(unsigned int irq) { (void)irq; hw[0].sync_irqs++; }

/* sysfs and the reboot notifier */
struct device_attribute { const char *name; ssize_t (*show)(struct device *, struct device_attribute *, char *); };
#define DEVICE_ATTR(n, mode, sh, st) struct device_attribute dev_attr_##n = { #n, sh }
#define sysfs_emit sprintf
static struct device_attribute *added_attr;
static int spu_add_dev_attr(struct device_attribute *a) { added_attr = a; return 0; }
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); int priority; };
static struct notifier_block *reboot_nb;
static int register_reboot_notifier(struct notifier_block *nb) { reboot_nb = nb; return 0; }

/* spu_base.enum_shared (spu_base.c, above the block) */
static int enum_shared;
static int spu_enum_shared(void) { return enum_shared; }

/* the code under test */
#include "block.c"

/* --- spufs's walks of the node list (spufs/sched.c) ----------------------------------------- */
static struct spu *spufs_get_idle(void)
{
	struct spu *spu;

	for (int node = 0; node < MAX_NUMNODES; node++)
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list)
			if (spu->alloc_state == SPU_FREE) {
				spu->alloc_state = SPU_USED;
				return spu;
			}
	return NULL;
}
static bool spufs_sees(struct spu *want)	/* find_victim and the rest walk this list */
{
	struct spu *spu;

	for (int node = 0; node < MAX_NUMNODES; node++)
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list)
			if (spu == want)
				return true;
	return false;
}
static bool spufs_notify_ok(void)		/* do_notify_spus_active: ctx of every non-free SPE */
{
	struct spu *spu;

	for (int node = 0; node < MAX_NUMNODES; node++)
		list_for_each_entry(spu, &cbe_spu_info[node].spus, cbe_list)
			if (spu->alloc_state != SPU_FREE && !spu->ctx)
				return false;
	return true;
}

/* spusched_thread (sched.c): it drops list_mutex with its cursor parked on an SPE that has a
 * context, runs spusched_tick, re-takes the mutex and goes on from that SPE's ->next. IN_GAP runs
 * in that gap. Returns the steps the walk took, or -1 when it doesn't end (a hang on the PS3). */
static int ticks_on_kernel_spe;
static int spusched_walk(struct spu *park_on, void (*in_gap)(struct spu *))
{
	struct list_head *head = &cbe_spu_info[0].spus;
	struct spu *spu;
	int steps = 0;

	for (spu = container_of(head->next, struct spu, cbe_list); &spu->cbe_list != head;
	     spu = container_of(spu->cbe_list.next, struct spu, cbe_list)) {
		if (++steps > 4 * NSPE)
			return -1;
		if (!spu->ctx)
			continue;
		if (spu->kernel_state != SPU_KERNEL_NONE)
			ticks_on_kernel_spe++;
		if (spu == park_on && in_gap)
			in_gap(spu);
	}
	return steps;
}

/* the affinity path: ctx_location walks aff_list (every SPE, reserved or not) to the OFFSET-th
 * schedulable one, then spu_get_idle takes it only if it is SPU_FREE */
static struct spu *spufs_affinity_take(int ref, int offset)
{
	for (int i = 0; i < NSPE; i++) {
		struct spu *spu = &hw[(ref + i) % NSPE].spu;

		if (offset-- == 0)
			return spu->alloc_state == SPU_FREE ? spu : NULL;
	}
	return NULL;
}

/* --- setup ------------------------------------------------------------------------------------ */
static struct spu_context ctx_a;
static struct mm_struct mm_a;

/* six SPEs as spufs leaves them after use: SR1 with problem state, stale SLBs, garbage LS */
static void boot(void)
{
	memset(hw, 0, sizeof(hw));
	for (int n = 0; n < MAX_NUMNODES; n++) {
		cbe_spu_info[n].list_mutex = (struct mutex){ 0, "list_mutex" };
		INIT_LIST_HEAD(&cbe_spu_info[n].spus);
		cbe_spu_info[n].n_spus = 0;
	}
	INIT_LIST_HEAD(&spu_full_list);
	for (int i = 0; i < NSPE; i++) {
		struct hw *h = &hw[i];
		struct spu *s = &h->spu;

		s->local_store = h->ls;
		s->problem = &h->prob;
		s->priv2 = &h->priv2;
		s->number = i;
		s->irqs[0] = 10 + 3 * i; s->irqs[1] = 11 + 3 * i; s->irqs[2] = 12 + 3 * i;
		s->alloc_state = SPU_FREE;
		s->flags = 1;			/* SPU_CONTEXT_SWITCH_PENDING */
		s->slb_replace = 5;
		s->mm = &mm_a;
		s->pid = 42;
		s->stop_callback = spufs_stop;
		s->ibox_callback = s->wbox_callback = s->mfc_callback = spufs_box;
		h->sr1 = 0x3b;
		h->slb_valid = true;
		h->tclass = 0;
		memset(h->ls, 0xa5, LS_SIZE);
		h->prob.spu_npc_RW = 0x1234;
		h->priv2.spu_lslr_RW = 0x1000;
		h->priv2.spu_privcntl_RW = 1;
		list_add(&s->cbe_list, &cbe_spu_info[0].spus);
		cbe_spu_info[0].n_spus++;
		list_add(&s->full_list, &spu_full_list);
	}
	spu_kernel_down = false;
	spu_kernel_mutex.held = 0;
	drv_calls = drv_calls_bad = null_calls = spufs_calls = allocs = msleep_ms = 0;
	drv_module.refs = 0;
	drv_gone = in_reserve = false;
	memset(handed, 0, sizeof(handed));
	spu_kernel_init();
}

static struct spu *reserve(void)
{
	struct spu *s;

	in_reserve = true;
	s = spu_kernel_reserve(&drv_ops);
	in_reserve = false;
	if (!IS_ERR(s))
		handed[s->number] = true;
	return s;
}

static bool ls_clear(struct hw *h)
{
	for (int i = 0; i < LS_SIZE; i++)
		if (h->ls[i])
			return false;
	return true;
}

static bool stubs_in(struct spu *s)
{
	return s->stop_callback == spu_kernel_stub_stop && s->ibox_callback == spu_kernel_stub_box &&
	       s->wbox_callback == spu_kernel_stub_box && s->mfc_callback == spu_kernel_stub_box;
}

static const char *sysfs(struct spu *s)
{
	static char b[16];

	added_attr->show(&s->dev, added_attr, b);
	return b;
}

/* every interrupt lv1 could still deliver, the masks as they are */
static void late_all(struct spu *s)
{
	for (int c = 0; c < 3; c++)
		late_irq(s, c);
}

/* --- the cases ----------------------------------------------------------------------------- */
static void t_reserve(void)
{
	struct spu *s;
	struct hw *h;
	int before;

	boot();
	CHECK(added_attr && !strcmp(added_attr->name, "reserved"), "no sysfs attribute \"reserved\"");
	CHECK(reboot_nb && reboot_nb->notifier_call, "no reboot notifier");
	/* a class left unmasked, its interrupt still asserted: masking must come before the driver's callbacks */
	hw[5].masks[2] = CLASS2_SPU_STOP_INTR;
	hw[5].level[2] = CLASS2_SPU_STOP_INTR;
	hw[5].running = true;
	hw[5].program = true;		/* its stop-and-signal latches while masked */
	hw[5].pu_mb = 1;		/* and it left both outbound mailboxes full */
	hw[5].puint_mb = 1;
	hw[5].queued = 3;
	before = hw[5].sr1_writes;
	s = reserve();
	CHECK(!IS_ERR(s), "reserve failed (%ld)", IS_ERR(s) ? PTR_ERR(s) : 0);
	if (IS_ERR(s))
		return;
	h = hw_of(s);
	CHECK(s == &hw[5].spu, "reserve took spu%d, not the first on the list (spu5)", s->number);
	CHECK(!spufs_sees(s), "the reserved SPE is still on the node list");
	CHECK(cbe_spu_info[0].n_spus == NSPE - 1, "n_spus %d, expected %d", cbe_spu_info[0].n_spus, NSPE - 1);
	for (int ref = 0; ref < NSPE; ref++)
		for (int off = 0; off < NSPE; off++)
			CHECK(spufs_affinity_take(ref, off) != s,
			      "the affinity path (ctx_location, then the alloc_state test) takes the reserved SPE");
	CHECK(s->kernel_state == SPU_KERNEL_RESERVED, "kernel_state %d", s->kernel_state);
	CHECK(!strcmp(sysfs(s), "1\n"), "sysfs reserved reads %s", sysfs(s));
	CHECK(!strcmp(sysfs(&hw[0].spu), "0\n"), "an unreserved SPE reads %s", sysfs(&hw[0].spu));
	CHECK(drv_calls_bad == 0, "a driver callback ran during reserve, before it got the SPE");
	CHECK(msleep_ms >= 50, "reserve: no grace period before the driver's callbacks (%d ms)", msleep_ms);
	CHECK(!h->stat[0] && !h->stat[1] && !h->stat[2], "reserve left interrupt status %llx %llx %llx",
	      (unsigned long long)h->stat[0], (unsigned long long)h->stat[1], (unsigned long long)h->stat[2]);
	CHECK(!h->pu_mb && !h->puint_mb, "reserve left the outbound mailboxes full (%d, %d)", h->pu_mb, h->puint_mb);
	CHECK(null_calls == 0, "a NULL callback was called");
	CHECK(s->stop_callback == drv_stop && s->ibox_callback == drv_box && s->wbox_callback == drv_box &&
	      s->mfc_callback == drv_box, "the driver's callbacks are not installed");
	CHECK(!h->masks[0] && !h->masks[1] && !h->masks[2], "a class is unmasked when the driver gets the SPE");
	CHECK(!h->bug, "SR1: a write ps3/spu.c's mfc_sr1_set BUG()s on");
	CHECK(h->sr1 == 0x33, "SR1 is %#llx, not 0x33", (unsigned long long)h->sr1);
	CHECK(h->sr1_writes > before, "SR1 not written explicitly");
	CHECK(h->tclass == 0x10000000, "TCLASS %#llx", (unsigned long long)h->tclass);
	CHECK(!h->slb_valid && h->slb_invalidates, "the SLBs were not invalidated");
	CHECK(!h->running, "the SPU was not stopped");
	CHECK(!h->queued && h->resumed_after_purge, "the MFC was not purged (or not resumed after)");
	CHECK(h->suspend == 0, "the MFC queue is left suspended");
	CHECK(h->priv2.spu_lslr_RW == LS_ADDR_MASK, "LSLR %#llx", (unsigned long long)h->priv2.spu_lslr_RW);
	CHECK(h->priv2.spu_privcntl_RW == SPU_PRIVCNTL_MODE_NORMAL, "privileged control not reset");
	CHECK(h->channel_inits, "the channels were not reset");
	CHECK(ls_clear(h), "the LS was not cleared at reserve");
	CHECK(h->prob.spu_npc_RW == 0, "NPC %#x", h->prob.spu_npc_RW);
	CHECK(s->flags == 0 && s->mm == NULL && s->slb_replace == 0 && s->pid == 0,
	      "flags %lu, mm %p, slb_replace %u, pid %d left from spufs", s->flags, (void *)s->mm,
	      s->slb_replace, s->pid);
	CHECK(allocs == 0, "reserve allocated");
	CHECK(!spu_kernel_mutex.held && !cbe_spu_info[0].list_mutex.held, "a mutex is left held");

	/* spufs never gets it; do_notify_spus_active doesn't trip over it */
	CHECK(spufs_notify_ok(), "do_notify_spus_active would dereference a NULL context");
	for (int i = 0; i < NSPE - 1; i++) {
		struct spu *g = spufs_get_idle();

		CHECK(g && g != s, "spu_get_idle: %s", g ? "gave the reserved SPE" : "no SPE");
	}
	CHECK(spufs_get_idle() == NULL, "spu_get_idle found a sixth SPE");

	/* the driver unmasks once its program is in; its callback gets the interrupt */
	h->level[2] = CLASS2_SPU_STOP_INTR;
	{ unsigned long f; spin_lock_irqsave(&s->register_lock, f); spu_int_mask_set(s, 2, CLASS2_SPU_STOP_INTR); spin_unlock_irqrestore(&s->register_lock, f); }
	CHECK(drv_calls > 0 && null_calls == 0, "the driver's interrupt didn't reach its callback");
}

static void t_skip(void)
{
	struct spu *s;

	boot();
	/* the list's order: spu5 first. spu5 SPU_USED unbound (find_victim's window), spu4 with a context,
	 * spu3 isolated, spu2 SPU_USED with a context: spu1 is the one to take */
	hw[5].spu.alloc_state = SPU_USED;
	hw[4].spu.ctx = &ctx_a;
	hw[3].isolated = true;
	hw[2].spu.alloc_state = SPU_USED;
	hw[2].spu.ctx = &ctx_a;
	s = reserve();
	CHECK(!IS_ERR(s) && s == &hw[1].spu, "took %s, expected spu1 (USED unbound, a context, isolated: skipped)",
	      hw[3].spu.kernel_state != SPU_KERNEL_NONE ? "spu3, isolated" :
	      IS_ERR(s) ? "nothing" : s == &hw[5].spu ? "spu5, kept SPU_USED by find_victim" :
	      s == &hw[4].spu ? "spu4, which has a context" : s == &hw[3].spu ? "spu3, isolated" : "another");
	s = reserve();
	CHECK(!IS_ERR(s) && s == &hw[0].spu, "second reserve: expected spu0");
	s = reserve();
	CHECK(IS_ERR(s) && PTR_ERR(s) == -EBUSY, "no free SPE left: expected -EBUSY");
	CHECK(spu_kernel_reserve(NULL) == ERR_PTR(-EINVAL), "reserve(NULL) not refused");
	/* a driver built against another struct spu_kernel_ops: refused, nothing taken */
	CHECK(spu_kernel_reserve_v1(&drv_ops, sizeof(drv_ops) - sizeof(void *)) == ERR_PTR(-EINVAL) &&
	      spu_kernel_reserve_v1(&drv_ops, sizeof(drv_ops) + sizeof(void *)) == ERR_PTR(-EINVAL),
	      "ops of another size not refused");
	CHECK(SPU_KERNEL_API_VERSION == 1, "SPU_KERNEL_API_VERSION %d", SPU_KERNEL_API_VERSION);
	{
		struct spu_kernel_ops part = drv_ops;

		part.wbox_callback = NULL;
		CHECK(spu_kernel_reserve(&part) == ERR_PTR(-EINVAL), "a NULL callback in ops not refused");
	}
	CHECK(cbe_spu_info[0].n_spus == NSPE - 2, "n_spus %d after the refusals", cbe_spu_info[0].n_spus);
}

/* five of six; the node's last SPE is never taken (spufs's affinity code reads the list's first entry
 * without its lock, so the list must never be empty) */
static void t_all_six(void)
{
	struct spu *s[NSPE], *last;

	boot();
	for (int i = 0; i < NSPE - 1; i++) {
		s[i] = reserve();
		CHECK(!IS_ERR(s[i]), "reserve %d of 5 failed", i + 1);
		for (int j = 0; j < i; j++)
			CHECK(s[i] != s[j], "the same SPE twice");
	}
	last = reserve();
	CHECK(IS_ERR(last) && PTR_ERR(last) == -EBUSY, "the node's last SPE was reserved");
	if (!IS_ERR(last))
		spu_kernel_release(last);
	CHECK(cbe_spu_info[0].n_spus == 1 && cbe_spu_info[0].spus.next != &cbe_spu_info[0].spus, "n_spus %d", cbe_spu_info[0].n_spus);
	for (int i = 0; i < NSPE - 1; i++)
		CHECK(!IS_ERR(s[i]) && spu_kernel_release(s[i]) == 0, "release %d", i);
	CHECK(cbe_spu_info[0].n_spus == NSPE, "n_spus %d after releasing all", cbe_spu_info[0].n_spus);
}

/* enum_shared=1: one of the SPEs is lv1's shared one, which lv1 may preempt: nothing is reserved */
static void t_shared(void)
{
	struct spu *s;

	boot();
	enum_shared = 1;
	s = reserve();
	CHECK(IS_ERR(s) && PTR_ERR(s) == -EBUSY, "an SPE was reserved with enum_shared=1");
	if (!IS_ERR(s))
		spu_kernel_release(s);
	CHECK(cbe_spu_info[0].n_spus == NSPE, "n_spus %d with enum_shared=1", cbe_spu_info[0].n_spus);
	enum_shared = 0;
	s = reserve();
	CHECK(!IS_ERR(s), "no SPE reserved with enum_shared=0");
	if (!IS_ERR(s))
		CHECK(spu_kernel_release(s) == 0, "release after enum_shared=0");
}

static void t_release(void)
{
	struct spu *s;
	struct hw *h;

	boot();
	s = reserve();
	if (IS_ERR(s))
		return;
	h = hw_of(s);
	memset(h->ls, 0x5a, 4096);			/* the driver's program and data */
	CHECK(spu_kernel_ls_decode(s, false) == 0 && h->sr1 == 0x32, "LS decode off: SR1 %#llx",
	      (unsigned long long)h->sr1);
	h->running = true;
	h->queued = 2;
	h->masks[0] = CLASS0_INTR_MASK;
	h->masks[2] = CLASS2_MAILBOX_INTR | CLASS2_SPU_STOP_INTR;
	msleep_ms = 0;
	CHECK(spu_kernel_release(s) == 0, "release of a quiet SPE failed");
	drv_gone = true;
	CHECK(msleep_ms >= 50, "release: no grace period before spufs gets the SPE (%d ms)", msleep_ms);
	CHECK(drv_module.refs == 0, "release of a quiet SPE pinned the driver's module");
	CHECK(!h->bug, "release: an SR1 write ps3/spu.c BUG()s on");
	CHECK(h->sr1 == 0x33, "SR1 %#llx after release", (unsigned long long)h->sr1);
	CHECK(ls_clear(h), "the LS was not cleared at release");
	CHECK(!h->masks[0] && !h->masks[1] && !h->masks[2], "release left a class unmasked");
	CHECK(stubs_in(s), "release left a callback that isn't spu_base's stub (NULL or the driver's)");
	/* lv1 can deliver an interrupt late: a stop after the SPU was seen stopped, class 0 with nothing pending */
	h->stat[2] = CLASS2_SPU_STOP_INTR;
	late_all(s);
	CHECK(null_calls == 0, "a late interrupt after release called a NULL callback");
	CHECK(drv_calls_bad == 0, "a late interrupt after release reached the driver's callback");
	CHECK(spufs_sees(s) && s->alloc_state == SPU_FREE && s->kernel_state == SPU_KERNEL_NONE,
	      "the released SPE is not spufs's again");
	CHECK(cbe_spu_info[0].n_spus == NSPE, "n_spus %d after release", cbe_spu_info[0].n_spus);
	CHECK(!strcmp(sysfs(s), "0\n"), "sysfs reads %s after release", sysfs(s));
	CHECK(spu_kernel_release(s) == -EINVAL, "a second release not refused");
	CHECK(spu_kernel_reset(s) == -EINVAL, "reset of a released SPE not refused");
	CHECK(spu_kernel_ls_decode(s, true) == -EINVAL, "LS decode of a released SPE not refused");
	CHECK(allocs == 0, "release allocated");
}

/* an SPE not proven quiet is never given back */
static void t_not_quiet(void)
{
	static const char *what[] = { "the SPU doesn't stop", "the MFC doesn't suspend",
				      "the purge doesn't complete", "the queue stays full after the purge" };

	for (int k = 0; k < 4; k++) {
		struct spu *s;
		struct hw *h;

		boot();
		s = reserve();
		if (IS_ERR(s))
			return;
		h = hw_of(s);
		CHECK(spu_kernel_ls_decode(s, false) == 0, "LS decode off");
		memset(h->ls, 0x5a, 4096);
		h->running = true;
		h->queued = 4;
		h->stuck_run = k == 0;
		h->stuck_suspend = k == 1;
		h->stuck_purge = k == 2;
		h->stuck_queue = k == 3;
		h->slb_valid = true;
		{
			int sus = h->suspend_writes, pur = h->purge_writes;

			msleep_ms = 0;
			CHECK(spu_kernel_release(s) == -EBUSY, "%s: release gave the SPE back", what[k]);
			drv_gone = true;
			CHECK(h->suspend_writes > sus && h->purge_writes > pur,
			      "%s: no last suspend and purge when given up", what[k]);
		}
		CHECK(!h->slb_valid, "%s: the SLBs still valid when given up", what[k]);
		CHECK(msleep_ms >= 50, "%s: no grace period when given up", what[k]);
		CHECK(drv_module.refs == 1, "%s: the driver's module not pinned (%d)", what[k], drv_module.refs);
		CHECK(!spufs_sees(s) && s->alloc_state == SPU_USED, "%s: the SPE is spufs's again", what[k]);
		CHECK(s->kernel_state == SPU_KERNEL_DEAD, "%s: not marked dead", what[k]);
		CHECK(cbe_spu_info[0].n_spus == NSPE - 1, "%s: n_spus %d", what[k], cbe_spu_info[0].n_spus);
		CHECK(!strcmp(sysfs(s), "1\n"), "%s: sysfs reads %s", what[k], sysfs(s));
		CHECK(stubs_in(s), "%s: a callback that isn't spu_base's stub", what[k]);
		CHECK(!h->masks[0] && !h->masks[1] && !h->masks[2], "%s: a class unmasked", what[k]);
		CHECK(ls_clear(h), "%s: the LS was not cleared", what[k]);
		CHECK(!h->bug, "%s: an SR1 write ps3/spu.c BUG()s on", what[k]);
		late_all(s);
		CHECK(null_calls == 0 && drv_calls_bad == 0, "%s: a late interrupt reached %s", what[k],
		      null_calls ? "a NULL callback" : "the driver");
		CHECK(spu_kernel_release(s) == -EBUSY, "%s: a second release not -EBUSY", what[k]);
		CHECK(spu_kernel_reset(s) == -EIO, "%s: reset of a dead SPE not -EIO", what[k]);
		/* the notifier handles a given-up SPE too, and says when it won't stop */
		memset(h->ls, 0x77, 4096);
		h->running = k == 0;
		log_buf[0] = 0;
		reboot_nb->notifier_call(reboot_nb, 0, NULL);
		CHECK(ls_clear(h), "%s: the reboot notifier skipped the given-up SPE", what[k]);
		CHECK(strstr(log_buf, "not quiet at shutdown") != NULL, "%s: the notifier didn't log the failed quiesce",
		      what[k]);
		spu_kernel_down = false;
		h->stuck_run = h->stuck_suspend = h->stuck_purge = h->stuck_queue = false;
		CHECK(spu_kernel_release(s) == -EBUSY, "%s: a dead SPE came back once quiet", what[k]);
		CHECK(!spufs_sees(s), "%s: on the list", what[k]);
		CHECK(!spu_kernel_mutex.held, "%s: the mutex is left held", what[k]);
	}
}

/* the restart path */
static void t_reset(void)
{
	struct spu *s;
	struct hw *h;

	boot();
	s = reserve();
	if (IS_ERR(s))
		return;
	h = hw_of(s);
	spu_kernel_ls_decode(s, false);
	memset(h->ls, 0x5a, LS_SIZE);
	h->running = true;
	h->queued = 5;
	h->masks[1] = CLASS1_INTR_MASK;
	h->slb_valid = true;
	h->priv2.spu_lslr_RW = 0;
	h->program = true;
	h->pu_mb = h->puint_mb = 1;
	h->stat[0] = CLASS0_INTR_MASK;
	allocs = 0;
	CHECK(spu_kernel_reset(s) == 0, "reset of a busy but stoppable SPE failed");
	CHECK(!h->stat[0] && !h->stat[1] && !h->stat[2], "reset left interrupt status %llx %llx %llx",
	      (unsigned long long)h->stat[0], (unsigned long long)h->stat[1], (unsigned long long)h->stat[2]);
	CHECK(!h->pu_mb && !h->puint_mb, "reset left the outbound mailboxes full");
	CHECK(allocs == 0, "allocated on the reset path");
	CHECK(h->sr1 == 0x33 && !h->bug, "SR1 %#llx after reset", (unsigned long long)h->sr1);
	CHECK(ls_clear(h), "the LS was not cleared at reset");
	CHECK(!h->running && !h->queued && h->suspend == 0, "not stopped, purged and resumed");
	CHECK(!h->slb_valid, "the SLBs were not invalidated at reset");
	CHECK(h->priv2.spu_lslr_RW == LS_ADDR_MASK, "LSLR not reset");
	CHECK(!h->masks[0] && !h->masks[1] && !h->masks[2], "reset left a class unmasked");
	CHECK(s->stop_callback == drv_stop, "reset changed the driver's callbacks");
	CHECK(spu_kernel_ls_decode(s, true) == 0 && h->sr1 == 0x33, "LS decode on");
	/* a reset that can't quiesce gives the SPE up */
	h->stuck_suspend = true;
	h->queued = 1;
	CHECK(spu_kernel_reset(s) == -EIO, "reset of an SPE that won't suspend: not -EIO");
	CHECK(s->kernel_state == SPU_KERNEL_DEAD && stubs_in(s) && !spufs_sees(s), "the failed reset didn't bury it");
	CHECK(drv_module.refs == 1, "the failed reset didn't pin the driver's module");
	CHECK(allocs == 0, "allocated on the failed reset path");
}

/* SR1 with a bit lv1 won't let us change: refused, never a BUG */
static void t_sr1(void)
{
	struct spu *s;

	boot();
	hw[5].sr1 = 0x13;			/* no master run control */
	s = reserve();
	CHECK(IS_ERR(s) && PTR_ERR(s) == -EIO, "reserve with SR1 0x13: expected -EIO");
	CHECK(!hw[5].bug, "SR1 0x13: an SR1 write ps3/spu.c BUG()s on");
	CHECK(hw[5].spu.kernel_state == SPU_KERNEL_NONE && spufs_sees(&hw[5].spu) &&
	      hw[5].spu.alloc_state == SPU_FREE && cbe_spu_info[0].n_spus == NSPE,
	      "SR1 0x13, quiet: the SPE isn't given back to spufs");
	CHECK(stubs_in(&hw[5].spu), "SR1 0x13: not the stubs");
	CHECK(drv_module.refs == 0, "SR1 0x13, quiet: the driver's module pinned");
	hw[5].sr1 = 0x3b;
	s = reserve();
	CHECK(!IS_ERR(s) && s == &hw[5].spu, "the SPE given back can be reserved again");
	s = reserve();
	CHECK(!IS_ERR(s) && s == &hw[4].spu, "the next reserve doesn't take the next SPE");
	if (IS_ERR(s))
		return;
	CHECK(spu_kernel_ls_decode(s, false) == 0 && hw[4].sr1 == 0x32 && !hw[4].bug, "LS decode off");
	CHECK(spu_kernel_ls_decode(s, true) == 0 && hw[4].sr1 == 0x33 && !hw[4].bug, "LS decode on");
}

static void t_reboot(void)
{
	struct spu *s, *t;
	struct hw *h;

	boot();
	s = reserve();
	t = reserve();
	if (IS_ERR(s) || IS_ERR(t))
		return;
	CHECK(spu_kernel_release(t) == 0, "release before the reboot");
	h = hw_of(s);
	spu_kernel_ls_decode(s, false);
	memset(h->ls, 0x5a, LS_SIZE);
	h->running = true;
	h->masks[2] = CLASS2_MAILBOX_INTR;
	reboot_nb->notifier_call(reboot_nb, 0, NULL);
	drv_gone = true;
	CHECK(!h->running, "reboot: the reserved SPE still runs");
	CHECK(ls_clear(h), "reboot: the LS was not cleared");
	CHECK(!h->masks[0] && !h->masks[1] && !h->masks[2], "reboot: a class unmasked");
	CHECK(stubs_in(s), "reboot: not the stubs");
	late_all(s);
	CHECK(drv_calls_bad == 0 && null_calls == 0, "reboot: a late interrupt reached the driver or NULL");
	CHECK(spu_kernel_reset(s) != 0, "reset after the reboot notifier succeeded");
	CHECK(spu_kernel_ls_decode(s, true) != 0, "LS decode after the reboot notifier succeeded");
	CHECK(spufs_sees(t) && t->kernel_state == SPU_KERNEL_NONE, "reboot touched an SPE spufs has");
	CHECK(spu_kernel_reserve(&drv_ops) == ERR_PTR(-ESHUTDOWN), "reserve after the reboot notifier");
	CHECK(!spu_kernel_mutex.held && !spu_full_list_mutex.held, "reboot: a mutex left held");
	CHECK(reboot_nb->priority < 0, "the reboot notifier doesn't run after the drivers' (priority %d)",
	      reboot_nb->priority);
}

/* spusched_thread parked on an SPE that is reserved in the gap: its walk must end, and it never
 * ticks a reserved SPE */
static struct spu *gap_second;
static void gap_close_reserve(struct spu *spu)
{
	struct spu *got;

	spu->ctx = NULL;			/* the context closes: spu_unschedule */
	spu->alloc_state = SPU_FREE;
	got = reserve();
	CHECK(got == spu, "park: reserve didn't take the parked SPE");
	if (gap_second) {			/* its successor goes the same way */
		gap_second->ctx = NULL;
		gap_second->alloc_state = SPU_FREE;
		got = reserve();
		CHECK(got == gap_second, "park: reserve didn't take the parked SPE's successor");
	}
}
static void gap_reserve_release(struct spu *spu)
{
	gap_close_reserve(spu);
	CHECK(spu_kernel_release(spu) == 0, "park: release");
}
static void t_park(void)
{
	static const struct { const char *what; void (*gap)(struct spu *); int second; } c[] = {
		{ "the parked SPE reserved", gap_close_reserve, 0 },
		{ "the parked SPE and its successor reserved", gap_close_reserve, 1 },
		{ "the parked SPE reserved and given back", gap_reserve_release, 0 },
	};

	for (unsigned k = 0; k < sizeof(c) / sizeof(c[0]); k++)
		for (int at = 0; at < NSPE - c[k].second; at++) {
			struct spu *park;
			int steps;

			boot();
			for (int i = 0; i < NSPE; i++) {
				hw[i].spu.ctx = &ctx_a;
				hw[i].spu.alloc_state = SPU_USED;
			}
			/* the list runs spu5 .. spu0 */
			park = &hw[NSPE - 1 - at].spu;
			gap_second = c[k].second ? &hw[NSPE - 2 - at].spu : NULL;
			ticks_on_kernel_spe = 0;
			steps = spusched_walk(park, c[k].gap);
			CHECK(steps >= 0, "park: %s (at %d): spusched_thread's walk doesn't end", c[k].what, at);
			CHECK(ticks_on_kernel_spe == 0, "park: %s: spusched_tick on a reserved SPE", c[k].what);
		}
}

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);	/* a fix-off may crash the model: keep what it printed */
	t_reserve();
	t_skip();
	t_all_six();
	t_shared();
	t_release();
	t_not_quiet();
	t_reset();
	t_sr1();
	t_reboot();
	t_park();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok: spu_kernel_reserve, reset, ls_decode, release, reboot (10 cases)\n");
	return 0;
}
