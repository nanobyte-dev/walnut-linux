// SPDX-License-Identifier: GPL-2.0
/*
 * spe.c: ps3rsx's SPE client. One SPE, reserved from spufs at load (kernel local/0058,
 * spu_kernel_reserve), runs spu/vseal.c: it seals and opens 4 KiB pages with ChaCha20-Poly1305 for the
 * VRAM swap (vswap.c), so the PPE does no cryptography. The program, the job format and the LS layout:
 * vseal_abi.h; the decisions: spe_core.h; the job, the wait, the restart, the handlers: spe_run.c (also
 * run by the host tests). This file is the PS3 side of spe_run.h's spe_hw_*() calls, the load, the unload
 * and the interface for vswap.c (spe.h).
 *
 * Load. The embedded program (vseal_spu.h; the test image vseal_spu_test.h in test builds, which adds the
 * test ops) is checked against its SHA-256 once, copied into the LS with the parameter block (the job
 * ring's and the status line's addresses, the boot's key); then LS decode goes off (SR1 0x32), so nothing
 * reaches the LS through an effective address while the key is in it. The key is drawn at load, after
 * wait_for_random_bytes() succeeded (else no SPE is taken). Every load and restart ends with the known-
 * answer job (vseal_golden.h), or the SPE isn't used.
 *
 * Memory the SPE can reach. Its DMA targets are the job ring and the status line (pages this file
 * allocates) and the pages a job names. None of them is freed while the SPE may still run: the ring and
 * the status line are freed at unload only if spu_kernel_release() saw the SPE quiet and it was never
 * given up unquiet (spe_may_free); a job that returns SPE_RET_KEEP (-EHWPOISON) tells its caller the same
 * for its own pages, and ps3rsx_spe_must_keep() stays true for the boot.
 *
 * The SPE is reserved at load only when the VRAM swap's pool is enabled (vswap_vram_mb > 0) and vswap_seal
 * is on (the default). Without an SPE, vswap.c puts nothing in VRAM: the swap device is its disk.
 * The key is drawn here, at load, after wait_for_random_bytes() succeeded; vswap.c's rescue drain reads it
 * through ps3rsx_spe_key() if the SPE is ever given up.
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <crypto/sha2.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>

#include "ps3rsx.h"
#include "spe.h"
#include "spe_run.h"
#ifdef PS3RSX_VSEAL_TEST
#include "vseal_spu_test.h"
#define VSEAL_IMAGE		vseal_spu_test
#define VSEAL_IMAGE_SHA256	vseal_spu_test_sha256
#else
#include "vseal_spu.h"
#define VSEAL_IMAGE		vseal_spu
#define VSEAL_IMAGE_SHA256	vseal_spu_sha256
#endif

#if !defined(SPU_KERNEL_API_VERSION) || SPU_KERNEL_API_VERSION != 1
#error "ps3rsx's SPE client is written for local/0058's SPU_KERNEL_API_VERSION 1"
#endif

static bool vswap_seal = true;
module_param(vswap_seal, bool, 0444);
MODULE_PARM_DESC(vswap_seal, "Seal the VRAM swap's pages on an SPE reserved at load (with the pool enabled); "
		 "off, or without an SPE, the swap keeps nothing in VRAM");

#ifdef PS3RSX_VSEAL_TEST
static unsigned int test_vseal;
module_param(test_vseal, uint, 0644);
MODULE_PARM_DESC(test_vseal, "TEST BUILD (vseal): log every job (1)");
#endif

struct ps3rsx_spe {
	struct ps3rsx_device *rsx;
	struct mutex lock;		/* one job at a time; the ring's slots rotate */
	struct spu *spu;
	struct spe_ctl ctl;
	u8 key[32];
	struct vseal_param param;	/* the load's parameter block, built here (no allocation at a restart) */
	struct completion irq;
	int release_ret;
#ifdef PS3RSX_VSEAL_TEST
	struct dentry *file;
	char *report;
	size_t len;
	u8 *tpages;			/* the hook's pages: 3 x VSEAL_PAGES_MAX, kept like the ring */
	void *guard;			/* a vmalloc page: the guard page after it is badaddr's address */
#endif
};

/* the callbacks find their client here: one SPE per module */
static struct ps3rsx_spe *the_spe;

#define TPAGES_ORDER	get_order(3 * VSEAL_PAGES_MAX * VSEAL_PAGE)
#define RING_ORDER	get_order(VSEAL_JOBS * VSEAL_PAGE)

/* --- spe_run.h's hardware calls ------------------------------------------------------------- */

vs_u64 spe_hw_now_us(void *hw)
{
	return ktime_to_us(ktime_get());
}

void spe_hw_mb(void *hw)
{
	mb();
}

void spe_hw_rmb(void *hw)
{
	rmb();
}

void spe_hw_relax(void *hw)
{
	cpu_relax();
}

unsigned int spe_hw_inbox_free(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	return (in_be32(&spe->spu->problem->mb_stat_R) >> 8) & 0xff;
}

void spe_hw_inbox_write(void *hw, vs_u32 word)
{
	struct ps3rsx_spe *spe = hw;

	out_be32(&spe->spu->problem->spu_mb_W, word);
}

unsigned int spe_hw_ibox_count(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	return (in_be32(&spe->spu->problem->mb_stat_R) >> 16) & 0xff;
}

vs_u32 spe_hw_ibox_pop(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	return in_be64(&spe->spu->priv2->puint_mb_R);
}

void spe_hw_ibox_clear(void *hw)
{
	struct ps3rsx_spe *spe = hw;
	unsigned long flags;

	spin_lock_irqsave(&spe->spu->register_lock, flags);
	spu_int_stat_clear(spe->spu, 2, CLASS2_MAILBOX_INTR);
	spin_unlock_irqrestore(&spe->spu->register_lock, flags);
}

void spe_hw_ibox_arm(void *hw)
{
	struct ps3rsx_spe *spe = hw;
	unsigned long flags;

	spin_lock_irqsave(&spe->spu->register_lock, flags);
	spu_int_stat_clear(spe->spu, 2, CLASS2_MAILBOX_INTR);
	spu_int_mask_or(spe->spu, 2, CLASS2_ENABLE_MAILBOX_INTR);
	spin_unlock_irqrestore(&spe->spu->register_lock, flags);
}

void spe_hw_ibox_rearm_irq(void *hw)	/* spu->register_lock held by spu_base's handler */
{
	struct ps3rsx_spe *spe = hw;

	spu_int_mask_or(spe->spu, 2, CLASS2_ENABLE_MAILBOX_INTR);
}

void spe_hw_sleep_prepare(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	reinit_completion(&spe->irq);
}

int spe_hw_sleep(void *hw, vs_u64 us)
{
	struct ps3rsx_spe *spe = hw;

	return wait_for_completion_timeout(&spe->irq, usecs_to_jiffies(us)) != 0;
}

void spe_hw_wake(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	complete(&spe->irq);
}

/* an error the SPU or the MFC confirm: stopped, halted, not running, or the MFC queue suspended */
int spe_hw_trouble(void *hw)
{
	struct ps3rsx_spe *spe = hw;
	u32 st = in_be32(&spe->spu->problem->spu_status_R);
	u64 mc = in_be64(&spe->spu->priv2->mfc_control_RW);

	return !(st & SPU_STATUS_RUNNING) || (st & (SPU_STATUS_STOPPED_BY_STOP | SPU_STATUS_STOPPED_BY_HALT |
						      SPU_STATUS_INVALID_INSTR | SPU_STATUS_INVALID_CH)) ||
	       (mc & MFC_CNTL_SUSPEND_DMA_STATUS_MASK) != MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION;
}

int spe_hw_reset(void *hw)
{
	struct ps3rsx_spe *spe = hw;

	return spu_kernel_reset(spe->spu);	/* masks every class, drains, proves the MFC empty */
}

int spe_hw_load(void *hw)
{
	struct ps3rsx_spe *spe = hw;
	struct spu *spu = spe->spu;
	struct vseal_param *p = &spe->param;
	unsigned long flags;
	int ret;

	memset(p, 0, sizeof(*p));
	p->magic = VSEAL_MAGIC;
	p->abi = VSEAL_ABI;
	p->ring_ea = (unsigned long)spe->ctl.ring;
	p->status_ea = (unsigned long)spe->ctl.status;
	memcpy(p->key, spe->key, sizeof(p->key));
	memcpy_toio((void __iomem *)spu->local_store, VSEAL_IMAGE, sizeof(VSEAL_IMAGE));
	memcpy_toio((void __iomem *)spu->local_store + VSEAL_LS_PARAM, p, sizeof(*p));
	memzero_explicit(p, sizeof(*p));	/* (the key stays in spe->key only) */
	out_be32(&spu->problem->spu_npc_RW, 0);
	mb();
	out_be32(&spu->problem->spu_runcntl_RW, SPU_RUNCNTL_RUNNABLE);
	ret = spu_kernel_ls_decode(spu, false);
	if (ret)
		return ret;
	spin_lock_irqsave(&spu->register_lock, flags);
	spu_int_mask_set(spu, 0, CLASS0_ENABLE_DMA_ALIGNMENT_INTR | CLASS0_ENABLE_INVALID_DMA_COMMAND_INTR |
			 CLASS0_ENABLE_SPU_ERROR_INTR);
	spu_int_mask_set(spu, 1, CLASS1_ENABLE_SEGMENT_FAULT_INTR | CLASS1_ENABLE_STORAGE_FAULT_INTR);
	spu_int_mask_set(spu, 2, CLASS2_ENABLE_SPU_STOP_INTR | CLASS2_ENABLE_SPU_HALT_INTR);
	spin_unlock_irqrestore(&spu->register_lock, flags);
	return 0;
}

void spe_hw_give_up(void *hw, int quiet)
{
	struct ps3rsx_spe *spe = hw;

	drm_err(&spe->rsx->drm, "vseal: the SPE is no longer used this boot (%s); the VRAM swap uses the disk\n",
		quiet ? "it was seen quiet" : "it was NOT seen quiet: every page it was given is kept");
	if (quiet && !spe->ctl.released) {	/* back to spufs at once (libspeq gets it at its next open) */
		spe->release_ret = spu_kernel_release(spe->spu);
		spe->ctl.released = true;
		if (spe->release_ret)
			spe->ctl.keep_pages = 1;
	}
}

/* --- callbacks: hard IRQ, spu->register_lock held ---------------------------------------------- */

static void cb_stop(struct spu *spu, int irq)
{
	struct ps3rsx_spe *spe = READ_ONCE(the_spe);

	if (spe && spe->spu == spu)
		spe_irq_stop(&spe->ctl, irq, irq == 0 ? spu->class_0_pending : irq == 1 ? spu->class_1_dsisr : 0);
}

static void cb_ibox(struct spu *spu)
{
	struct ps3rsx_spe *spe = READ_ONCE(the_spe);

	if (spe && spe->spu == spu)
		spe_irq_ibox(&spe->ctl);
}

static void cb_none(struct spu *spu)
{
}

static const struct spu_kernel_ops spe_ops = {
	.stop_callback = cb_stop,
	.ibox_callback = cb_ibox,
	.wbox_callback = cb_none,
	.mfc_callback = cb_none,
	/* An SPE given up unquiet pins this module, but that can't help a give-up inside our own module_exit
	 * (the module is freed anyway): so no DMA target ever lives in module memory. The ring, the status
	 * line and the test pages are page allocations kept by spe_may_free(); the image and the parameter
	 * block are copied in by the PPE, never fetched by the SPE's DMA. */
	.owner = THIS_MODULE,
};

/* --- the interface (spe.h) -------------------------------------------------------------------- */

int ps3rsx_spe_job(struct ps3rsx_device *rsx, unsigned int op, unsigned int npages,
		   const struct vseal_page *pages, struct vseal_result *res, enum spe_path path)
{
	struct ps3rsx_spe *spe = rsx->spe;
	struct vseal_job *j;
	unsigned int idx;
	int ret;

	if (!spe)
		return SPE_RET_NODEV;
#ifdef PS3RSX_VSEAL_TEST
	if (op == VSEAL_OP_TEST_COPY && npages && npages <= VSEAL_PAGES_MAX)
		goto ok;			/* test builds: test_vswap_seal=2, the paths without the cipher */
#endif
	if ((op != VSEAL_OP_SEAL && op != VSEAL_OP_OPEN) || !npages || npages > VSEAL_PAGES_MAX)
		return SPE_RET_INVAL;
#ifdef PS3RSX_VSEAL_TEST
ok:
#endif
	mutex_lock(&spe->lock);
	idx = spe->ctl.n % VSEAL_JOBS;
	j = &spe->ctl.ring[idx];
	memset(j, 0, VSEAL_IN_BYTES);
	j->op = op;
	j->npages = npages;
	memcpy(j->page, pages, npages * sizeof(*pages));
#ifdef PS3RSX_VSEAL_TEST
	spe->ctl.test = op == VSEAL_OP_TEST_COPY;
#endif
	ret = spe_run_job(&spe->ctl, idx, path);
#ifdef PS3RSX_VSEAL_TEST
	spe->ctl.test = 0;
#endif
	if (ret == 0 || ret == SPE_RET_BADMSG)		/* only the results, never the ring's inputs */
		memcpy(res, j->res, npages * sizeof(*res));
#ifdef PS3RSX_VSEAL_TEST
	if (test_vseal)
		pr_info("ps3rsx: vseal job op %u pages %u: %d\n", op, npages, ret);
#endif
	mutex_unlock(&spe->lock);
	return ret;
}

int ps3rsx_spe_set_bounds(struct ps3rsx_device *rsx, const struct spe_window *w, unsigned int nw,
			  const struct spe_window *refused, unsigned int nr,
			  int (*page_ok)(void *arg, u64 ea, unsigned int kind), void *arg)
{
	struct ps3rsx_spe *spe = rsx->spe;
	struct spe_bounds *b;

	if (!spe || nr + 2 > SPE_NREFUSED || spe_bounds_valid(w, nw, refused, nr, page_ok != NULL))
		return -EINVAL;
	mutex_lock(&spe->lock);
	b = &spe->ctl.bounds;
	memcpy(b->win, w, nw * sizeof(*w));
	b->nwin = nw;
	/* the ring and the status line are always refused, whatever the caller gives */
	b->refused[0] = (struct spe_window){ (unsigned long)spe->ctl.ring, VSEAL_JOBS * VSEAL_PAGE, 0 };
	b->refused[1] = (struct spe_window){ (unsigned long)spe->ctl.status, PAGE_SIZE, 0 };
	memcpy(b->refused + 2, refused, nr * sizeof(*refused));
	b->nrefused = nr + 2;
	b->page_ok = page_ok;
	b->page_ok_arg = arg;
	mutex_unlock(&spe->lock);
	return 0;
}

const u8 *ps3rsx_spe_key(struct ps3rsx_device *rsx)
{
	return rsx->spe ? rsx->spe->key : NULL;
}

bool ps3rsx_spe_ready(struct ps3rsx_device *rsx)
{
	return rsx->spe && READ_ONCE(rsx->spe->ctl.state) == SPE_READY;
}

bool ps3rsx_spe_must_keep(struct ps3rsx_device *rsx)
{
	return rsx->spe && READ_ONCE(rsx->spe->ctl.keep_pages);
}

#ifdef PS3RSX_VSEAL_TEST
#include "spe_test.h"
#endif

/* --- load and unload --------------------------------------------------------------------------- */

static bool image_ok(void)
{
	u8 d[SHA256_DIGEST_SIZE];

	sha256(VSEAL_IMAGE, sizeof(VSEAL_IMAGE), d);
	return !memcmp(d, VSEAL_IMAGE_SHA256, sizeof(d)) && sizeof(VSEAL_IMAGE) <= VSEAL_LS_PROG_MAX;
}

static void spe_free(struct ps3rsx_spe *spe)
{
	if (spe->ctl.ring)
		free_pages((unsigned long)spe->ctl.ring, RING_ORDER);
	if (spe->ctl.status)
		free_page((unsigned long)spe->ctl.status);
#ifdef PS3RSX_VSEAL_TEST
	if (spe->tpages)
		free_pages((unsigned long)spe->tpages, TPAGES_ORDER);
	vfree(spe->guard);
	kfree(spe->report);
#endif
	memzero_explicit(spe->key, sizeof(spe->key));
	kfree(spe);
}

void ps3rsx_spe_init(struct ps3rsx_device *rsx)
{
	struct ps3rsx_spe *spe;
	struct spu *spu;
	int ret;

	if (!vswap_seal || !ps3rsx_vswap_pool_enabled())
		return;
	if (!image_ok()) {
		drm_err(&rsx->drm, "vseal: the embedded SPU program doesn't match its hash: no SPE taken\n");
		return;
	}
	if (wait_for_random_bytes()) {
		drm_warn(&rsx->drm, "vseal: no random key yet: no SPE taken\n");
		return;
	}
	spe = kzalloc(sizeof(*spe), GFP_KERNEL);
	if (!spe)
		return;
	spe->rsx = rsx;
	mutex_init(&spe->lock);
	init_completion(&spe->irq);
	spe->ctl.hw = spe;
	spe->ctl.ring = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, RING_ORDER);
	spe->ctl.status = (void *)get_zeroed_page(GFP_KERNEL);
#ifdef PS3RSX_VSEAL_TEST
	spe->report = kzalloc(4096, GFP_KERNEL);
	spe->tpages = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, TPAGES_ORDER);
	spe->guard = vmalloc(PAGE_SIZE);
	if (!spe->report || !spe->tpages || !spe->guard)
		goto fail;
#endif
	if (!spe->ctl.ring || !spe->ctl.status)
		goto fail;
	spe->ctl.bounds.refused[0] = (struct spe_window){ (unsigned long)spe->ctl.ring, VSEAL_JOBS * VSEAL_PAGE, 0 };
	spe->ctl.bounds.refused[1] = (struct spe_window){ (unsigned long)spe->ctl.status, PAGE_SIZE, 0 };
	spe->ctl.bounds.nrefused = 2;
	get_random_bytes(spe->key, sizeof(spe->key));
	WRITE_ONCE(the_spe, spe);
	spu = spu_kernel_reserve(&spe_ops);
	if (IS_ERR(spu)) {
		drm_warn(&rsx->drm, "vseal: no SPE reserved (%ld): the VRAM swap won't be sealed\n", PTR_ERR(spu));
		WRITE_ONCE(the_spe, NULL);
		goto fail;
	}
	spe->spu = spu;
	mutex_lock(&spe->lock);
	ret = spe_start(&spe->ctl);
	mutex_unlock(&spe->lock);
	if (ret) {
		/* a load that didn't answer: give the SPE back now, if it was seen quiet */
		mutex_lock(&spe->lock);
		if (!spe->ctl.keep_pages && !spe->ctl.released) {
			spe->ctl.state = SPE_GONE;
			spe->release_ret = spu_kernel_release(spu);
			spe->ctl.released = true;
		}
		mutex_unlock(&spe->lock);
		drm_err(&rsx->drm, "vseal: the SPU program didn't load or answer (%d): the SPE isn't used\n", ret);
		if (spe_may_free(spe->release_ret, spe->ctl.keep_pages)) {
			WRITE_ONCE(the_spe, NULL);
			spe_free(spe);
			return;
		}
		rsx->spe = spe;		/* kept: fini keeps the memory */
		return;
	}
	rsx->spe = spe;
	drm_info(&rsx->drm, "vseal: SPE %d reserved, its program (%zu bytes) loaded and checked\n", spu->number,
		 sizeof(VSEAL_IMAGE));
	return;
fail:
	spe_free(spe);
}

/* After drm_dev_register: the test hook's file (test builds) */
void ps3rsx_spe_init_debugfs(struct ps3rsx_device *rsx)
{
#ifdef PS3RSX_VSEAL_TEST
	if (rsx->spe && READ_ONCE(rsx->spe->ctl.state) == SPE_READY)
		spe_test_init(rsx->spe);
#endif
}

/* Before drm_dev_unregister: the test hook's file */
void ps3rsx_spe_fini_debugfs(struct ps3rsx_device *rsx)
{
#ifdef PS3RSX_VSEAL_TEST
	if (rsx->spe)
		debugfs_remove(rsx->spe->file);
#endif
}

/* After vswap.c is gone (nothing submits any more) */
void ps3rsx_spe_fini(struct ps3rsx_device *rsx)
{
	struct ps3rsx_spe *spe = rsx->spe;
	int ret;

	if (!spe)
		return;
	mutex_lock(&spe->lock);
	/* release masks every class, installs spu_base's stubs, and gives the SPE back only if quiet */
	ret = spe->ctl.released ? spe->release_ret : spu_kernel_release(spe->spu);
	spe->ctl.released = true;
	mutex_unlock(&spe->lock);
	WRITE_ONCE(the_spe, NULL);
	rsx->spe = NULL;
	if (spe_may_free(ret, spe->ctl.keep_pages)) {
		spe_free(spe);
		return;
	}
	pr_err("ps3rsx: vseal: the SPE wasn't quiet: its ring and pages are kept for the boot\n");
	memzero_explicit(spe->key, sizeof(spe->key));	/* (spe itself stays: nothing frees what it points at) */
}
