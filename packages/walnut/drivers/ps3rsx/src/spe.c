// SPDX-License-Identifier: GPL-2.0
/*
 * spe.c: ps3rsx's SPE client. One SPE, reserved from spufs at load (kernel local/0058,
 * spu_kernel_reserve), runs spu/vseal.c: it seals and opens 4 KiB pages with ChaCha20-Poly1305 for the
 * VRAM swap (vswap.c), so the PPE does no cryptography. The program, the job format and the LS layout:
 * vseal_abi.h; the decisions (descriptor check, poll or interrupt, budgets, restarts): spe_core.h.
 *
 * Load. The embedded program (vseal_spu.h) is checked against its SHA-256 once, copied into the LS with
 * the parameter block (the job ring's and the status line's addresses, the boot's key); then LS decode
 * goes off (SR1 0x32), so nothing reaches the LS through an effective address while the key is in it.
 * Every load and restart ends with VSEAL_OP_GOLDEN: the program seals its built-in page and the answer
 * must equal vseal_golden.h, or the SPE isn't used (the swap stays on the disk).
 *
 * Memory the SPE can reach. Its DMA targets are the job ring and the status line (pages this file
 * allocates) and the pages a job names. None of them is freed while the SPE may still run: the ring and
 * the status line are freed at unload only if spu_kernel_release() saw the SPE quiet; otherwise they are
 * kept for the boot. A job's own pages are handed back to the caller only when the job is done, or after
 * a restart in which spu_kernel_reset() saw the SPE stop with an empty MFC queue; if that reset fails,
 * the SPE is given up (local/0058 keeps it from spufs) and the caller is told to keep the pages.
 *
 * Completion. The SPU writes the job's done word last (after every data put, an MFC sync and the
 * results), then the job's sequence number through the outbound interrupt mailbox. Short jobs are polled
 * on the done word; the rest, and every job whose pages the SPE reads from VRAM, sleep until the mailbox
 * interrupt. A sequence number carries the SPE's generation (bumped at every load), so nothing left over
 * from an earlier load is taken for a current job. A wait that runs out of time reads the done word and
 * the SPE's progress counter first: done is done, progress buys time, and only an SPE that made no
 * progress is restarted (spu_kernel_reset: no allocation, no firmware). Three such restarts in a row and
 * the SPE is given up for the boot.
 *
 * Locking: spe->lock serializes jobs (one job at a time; the ring's slots rotate); the interrupt
 * callbacks (hard IRQ, spu->register_lock held, see spu_base.c) only record and complete.
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <crypto/sha2.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>

#include "ps3rsx.h"
#include "spe.h"
#include "vseal_golden.h"
#include "vseal_spu.h"

static bool vswap_seal = true;
module_param(vswap_seal, bool, 0444);
MODULE_PARM_DESC(vswap_seal, "Reserve an SPE at load to seal the VRAM swap's pages (off: no SPE taken)");

#ifdef PS3RSX_VSEAL_TEST
static unsigned int test_vseal;
module_param(test_vseal, uint, 0644);
MODULE_PARM_DESC(test_vseal, "TEST BUILD (vseal): log every job (1)");
#endif

enum spe_state { SPE_OFF, SPE_READY, SPE_GONE };

struct ps3rsx_spe {
	struct ps3rsx_device *rsx;
	struct mutex lock;
	struct spu *spu;
	enum spe_state state;
	u32 gen, n;			/* the generation (loads), the job number */
	u8 key[32];
	struct vseal_job *ring;		/* VSEAL_JOBS pages */
	struct vseal_status *status;	/* one page */
	bool keep_pages;		/* the SPE was given up unquiet: never free what it could reach */
	struct vseal_param param;	/* the load's parameter block, built here (no allocation at a restart) */
	struct completion irq;
	u64 waiting;			/* the seq an interrupt may complete, or 0 */
	atomic_t err;			/* an error callback since the job started */
	u32 irq_value;
	struct spe_health health;
	struct spe_window win[8];
	unsigned int nwin;
	int test;			/* test ops and RAM-to-RAM seals allowed (test builds' hook) */
	u64 jobs, polled, interrupted, extended, restarts, stale_irqs, errors;
#ifdef PS3RSX_VSEAL_TEST
	struct dentry *file;
	char *report;
	size_t len;
	u8 *tpages;			/* the hook's pages: 3 x VSEAL_PAGES_MAX, kept like the ring */
#endif
};

/* the callbacks find their client here: one SPE per module */
static struct ps3rsx_spe *the_spe;

/* --- callbacks: hard IRQ, spu->register_lock held ---------------------------------------------- */

static void cb_stop(struct spu *spu, int irq)
{
	struct ps3rsx_spe *spe = READ_ONCE(the_spe);

	if (!spe || spe->spu != spu)
		return;
	/* class 0 (DMA alignment, invalid command, SPU error), a class-1 fault the kernel couldn't
	 * resolve, or a stop or halt: the program doesn't stop on its own, so each is an error */
	atomic_inc(&spe->err);
	complete(&spe->irq);
}

static void cb_ibox(struct spu *spu)
{
	struct ps3rsx_spe *spe = READ_ONCE(the_spe);
	u32 v;

	if (!spe || spe->spu != spu)
		return;
	v = in_be64(&spu->priv2->puint_mb_R);	/* pops it, so the SPU can write the next */
	if (spe->waiting && v == (u32)spe->waiting) {
		spe->irq_value = v;
		complete(&spe->irq);
	} else {
		spe->stale_irqs++;
	}
}

static void cb_none(struct spu *spu)
{
}

static const struct spu_kernel_ops spe_ops = {
	.owner = THIS_MODULE,		/* an SPE given up unquiet keeps the module loaded */
	.stop_callback = cb_stop,
	.ibox_callback = cb_ibox,
	.wbox_callback = cb_none,
	.mfc_callback = cb_none,
};

/* --- the program ---------------------------------------------------------------------------- */

static void mask_or2(struct spu *spu, u64 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&spu->register_lock, flags);
	spu_int_mask_or(spu, 2, bits);
	spin_unlock_irqrestore(&spu->register_lock, flags);
}

/* LS decode is on (reserve and reset leave it so): program, parameters, run, then decode off */
static int spe_load(struct ps3rsx_spe *spe)
{
	struct spu *spu = spe->spu;
	struct vseal_param *p = &spe->param;
	unsigned long flags;
	int ret;

	memset(p, 0, sizeof(*p));
	p->magic = VSEAL_MAGIC;
	p->abi = VSEAL_ABI;
	p->ring_ea = (unsigned long)spe->ring;
	p->status_ea = (unsigned long)spe->status;
	memcpy(p->key, spe->key, sizeof(p->key));
	memcpy_toio((void __iomem *)spu->local_store, vseal_spu, sizeof(vseal_spu));
	memcpy_toio((void __iomem *)spu->local_store + VSEAL_LS_PARAM, p, sizeof(*p));
	memzero_explicit(p, sizeof(*p));	/* (the key stays in spe->key only) */
	spe->gen++;
	spe->n = 0;
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

static int run_job(struct ps3rsx_spe *spe, unsigned int idx, enum spe_path path);

static int spe_golden(struct ps3rsx_spe *spe)
{
	unsigned int idx = spe->n % VSEAL_JOBS;
	struct vseal_job *j = &spe->ring[idx];
	int ret;

	memset(j, 0, sizeof(*j));
	j->op = VSEAL_OP_GOLDEN;
	ret = run_job(spe, idx, SPE_PATH_RAM);
	if (ret)
		return ret;
	if (memcmp(j->page[0].tag, vseal_golden_tag, 16) || j->fnv != VSEAL_GOLDEN_FNV) {
		drm_err(&spe->rsx->drm, "vseal: the SPU program's known answer is wrong: not used\n");
		return -EIO;
	}
	return 0;
}

/* --- jobs ---------------------------------------------------------------------------------- */

static void give_up(struct ps3rsx_spe *spe, bool quiet, const char *why)
{
	spe->state = SPE_GONE;
	if (!quiet)
		spe->keep_pages = true;
	drm_err(&spe->rsx->drm, "vseal: %s: the SPE is no longer used this boot (the VRAM swap stays on the disk)\n",
		why);
}

/* after a timeout or an error: true if the SPE runs again */
static bool restart(struct ps3rsx_spe *spe, bool progressed)
{
	int ret;

	spe->restarts++;
	WRITE_ONCE(spe->waiting, 0);
	ret = spu_kernel_reset(spe->spu);	/* masks every class; stops, purges, proves the MFC empty */
	spe_restarted(&spe->health, progressed, !ret);
	if (ret) {
		give_up(spe, false, "the SPE didn't stop at a restart");
		return false;
	}
	if (spe->health.gone) {
		give_up(spe, true, "three restarts without progress");
		return false;
	}
	if (spe_load(spe) || spe_golden(spe)) {
		if (spe->state != SPE_GONE)
			give_up(spe, true, "the reloaded program didn't answer");
		return false;
	}
	return true;
}

/* how many SPU interrupt-mailbox entries wait (mb_stat bits 16-23) */
static unsigned int intr_mbox_count(struct spu *spu)
{
	return (in_be32(&spu->problem->mb_stat_R) >> 16) & 0xff;
}

static void drain_intr_mbox(struct ps3rsx_spe *spe, u64 seq)
{
	struct spu *spu = spe->spu;
	ktime_t end = ktime_add_us(ktime_get(), 200);
	unsigned long flags;

	/* the SPU writes the mailbox just after the done word: wait for it briefly (unless an armed
	 * interrupt already took it), then take it */
	while (!intr_mbox_count(spu) && READ_ONCE(spe->irq_value) != (u32)seq && !ktime_after(ktime_get(), end))
		cpu_relax();
	spin_lock_irqsave(&spu->register_lock, flags);
	while (intr_mbox_count(spu)) {
		u32 v = in_be64(&spu->priv2->puint_mb_R);

		if (v != (u32)seq)
			spe->stale_irqs++;
	}
	spin_unlock_irqrestore(&spu->register_lock, flags);
}

/*
 * Run descriptor idx (op, npages, pages filled by the caller). Called with spe->lock held. 0 (done;
 * j->status says whether the SPU accepted it), -EINVAL (refused by the check), -ETIMEDOUT (restarted),
 * -ENODEV (the SPE is gone).
 */
static int run_job(struct ps3rsx_spe *spe, unsigned int idx, enum spe_path path)
{
	struct vseal_job *j = &spe->ring[idx];
	struct spu *spu = spe->spu;
	unsigned int op = j->op, n = j->npages;
	struct spe_wait w;
	u64 seq, p0;
	bool poll;

	if (spe->state != SPE_READY)
		return -ENODEV;
	j->magic = VSEAL_MAGIC;
	if (spe_check_job(j, spe->win, spe->nwin, spe->test))
		return -EINVAL;
	seq = spe_seq(spe->gen, ++spe->n);
	j->seq = seq;
	j->done = 0;
	j->status = VSEAL_ST_NONE;
	p0 = READ_ONCE(spe->status->progress);
	poll = spe_poll(op, n, path);
	reinit_completion(&spe->irq);
	atomic_set(&spe->err, 0);
	WRITE_ONCE(spe->irq_value, 0);
	WRITE_ONCE(spe->waiting, seq);
	mb();				/* the descriptor in memory before the SPU hears of it */
	if (!poll)
		mask_or2(spu, CLASS2_ENABLE_MAILBOX_INTR);
	if (!((in_be32(&spu->problem->mb_stat_R) >> 8) & 0xff)) {	/* the inbound mailbox full: stuck */
		return restart(spe, false) ? -ETIMEDOUT : -ENODEV;
	}
	out_be32(&spu->problem->spu_mb_W, idx);
	spe->jobs++;
	spe_wait_start(&w, seq, spe_budget_us(op, n, path), p0);

	if (poll) {
		ktime_t end = ktime_add_us(ktime_get(), spe_poll_spin_us(op, n, path));

		while (READ_ONCE(j->done) != seq && !atomic_read(&spe->err) && !ktime_after(ktime_get(), end))
			cpu_relax();
		if (READ_ONCE(j->done) == seq) {
			rmb();
			drain_intr_mbox(spe, seq);
			spe->polled++;
			goto done;
		}
		mask_or2(spu, CLASS2_ENABLE_MAILBOX_INTR);	/* not done yet: sleep from here */
	}
	for (;;) {
		enum spe_verdict v;

		wait_for_completion_timeout(&spe->irq, usecs_to_jiffies(w.budget_us));
		if (READ_ONCE(j->done) == seq && !atomic_read(&spe->err))
			break;
		v = spe_on_timeout(&w, READ_ONCE(j->done), READ_ONCE(spe->status->progress),
				   atomic_read(&spe->err));
		if (v == SPE_DONE)
			break;
		if (v == SPE_WAIT_MORE) {
			spe->extended++;
			reinit_completion(&spe->irq);
			continue;
		}
		if (atomic_read(&spe->err))
			spe->errors++;
		return restart(spe, READ_ONCE(spe->status->progress) != p0) ? -ETIMEDOUT : -ENODEV;
	}
	rmb();				/* the results after the done word */
	spe->interrupted++;
	if (intr_mbox_count(spu))	/* the done word was seen before the interrupt: take its entry */
		drain_intr_mbox(spe, seq);
done:
	WRITE_ONCE(spe->waiting, 0);
	spe_job_succeeded(&spe->health);
#ifdef PS3RSX_VSEAL_TEST
	if (test_vseal)
		pr_info("ps3rsx: vseal job %llx op %u pages %u status %u\n", seq, op, n, j->status);
#endif
	return j->status == VSEAL_ST_OK ? 0 : -EIO;
}

int ps3rsx_spe_job(struct ps3rsx_device *rsx, unsigned int op, unsigned int npages,
		   struct vseal_page *pages, enum spe_path path)
{
	struct ps3rsx_spe *spe = rsx->spe;
	struct vseal_job *j;
	unsigned int idx;
	int ret;

	if (!spe || (op != VSEAL_OP_SEAL && op != VSEAL_OP_OPEN) || !npages || npages > VSEAL_PAGES_MAX)
		return spe ? -EINVAL : -ENODEV;
	mutex_lock(&spe->lock);
	idx = spe->n % VSEAL_JOBS;
	j = &spe->ring[idx];
	memset(j, 0, offsetof(struct vseal_job, page));
	j->op = op;
	j->npages = npages;
	memcpy(j->page, pages, npages * sizeof(*pages));
	ret = run_job(spe, idx, path);
	if (ret == 0 || ret == -EIO)
		memcpy(pages, j->page, npages * sizeof(*pages));
	mutex_unlock(&spe->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ps3rsx_spe_job);

int ps3rsx_spe_set_windows(struct ps3rsx_device *rsx, const struct spe_window *w, unsigned int n)
{
	struct ps3rsx_spe *spe = rsx->spe;

	if (!spe || n > ARRAY_SIZE(spe->win))
		return -EINVAL;
	mutex_lock(&spe->lock);
	memcpy(spe->win, w, n * sizeof(*w));
	spe->nwin = n;
	mutex_unlock(&spe->lock);
	return 0;
}

bool ps3rsx_spe_ready(struct ps3rsx_device *rsx)
{
	return rsx->spe && READ_ONCE(rsx->spe->state) == SPE_READY;
}

#ifdef PS3RSX_VSEAL_TEST
#include "spe_test.h"
#endif

/* --- load and unload --------------------------------------------------------------------------- */

static bool image_ok(void)
{
	u8 d[SHA256_DIGEST_SIZE];

	sha256(vseal_spu, sizeof(vseal_spu), d);
	return !memcmp(d, vseal_spu_sha256, sizeof(d)) && sizeof(vseal_spu) <= VSEAL_LS_PROG_MAX;
}

void ps3rsx_spe_init(struct ps3rsx_device *rsx)
{
	struct ps3rsx_spe *spe;
	struct spu *spu;
	int ret;

	if (!vswap_seal)
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
	spe->ring = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(VSEAL_JOBS * VSEAL_PAGE));
	spe->status = (void *)get_zeroed_page(GFP_KERNEL);
#ifdef PS3RSX_VSEAL_TEST
	spe->test = 1;
	spe->report = kzalloc(4096, GFP_KERNEL);
	spe->tpages = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					       get_order(3 * VSEAL_PAGES_MAX * VSEAL_PAGE));
	if (!spe->report || !spe->tpages)
		goto fail_mem;
#endif
	if (!spe->ring || !spe->status)
		goto fail_mem;
	get_random_bytes(spe->key, sizeof(spe->key));
	WRITE_ONCE(the_spe, spe);
	spu = spu_kernel_reserve(&spe_ops);
	if (IS_ERR(spu)) {
		drm_warn(&rsx->drm, "vseal: no SPE reserved (%ld): the VRAM swap won't be sealed\n", PTR_ERR(spu));
		WRITE_ONCE(the_spe, NULL);
		goto fail_mem;
	}
	spe->spu = spu;
	rsx->spe = spe;
	ret = spe_load(spe);
	spe->state = ret ? SPE_GONE : SPE_READY;
	if (!ret) {
		mutex_lock(&spe->lock);
		ret = spe_golden(spe);
		mutex_unlock(&spe->lock);
	}
	if (ret) {
		give_up(spe, true, "the SPU program didn't load or answer");
		return;
	}
	drm_info(&rsx->drm, "vseal: SPE %d reserved, its program (%zu bytes) loaded and checked\n", spu->number,
		 sizeof(vseal_spu));
#ifdef PS3RSX_VSEAL_TEST
	spe_test_init(spe);
#endif
	return;
fail_mem:
#ifdef PS3RSX_VSEAL_TEST
	kfree(spe->report);
	if (spe->tpages)
		free_pages((unsigned long)spe->tpages, get_order(3 * VSEAL_PAGES_MAX * VSEAL_PAGE));
#endif
	if (spe->ring)
		free_pages((unsigned long)spe->ring, get_order(VSEAL_JOBS * VSEAL_PAGE));
	if (spe->status)
		free_page((unsigned long)spe->status);
	memzero_explicit(spe->key, sizeof(spe->key));
	kfree(spe);
}

void ps3rsx_spe_fini(struct ps3rsx_device *rsx)
{
	struct ps3rsx_spe *spe = rsx->spe;
	bool quiet;

	if (!spe)
		return;
#ifdef PS3RSX_VSEAL_TEST
	debugfs_remove(spe->file);
#endif
	mutex_lock(&spe->lock);
	/* release masks every class, installs spu_base's stubs, and gives the SPE back only if quiet */
	quiet = spe_may_free(spu_kernel_release(spe->spu), spe->keep_pages);
	mutex_unlock(&spe->lock);
	WRITE_ONCE(the_spe, NULL);
	rsx->spe = NULL;
	memzero_explicit(spe->key, sizeof(spe->key));
	if (quiet) {
		free_pages((unsigned long)spe->ring, get_order(VSEAL_JOBS * VSEAL_PAGE));
		free_page((unsigned long)spe->status);
#ifdef PS3RSX_VSEAL_TEST
		free_pages((unsigned long)spe->tpages, get_order(3 * VSEAL_PAGES_MAX * VSEAL_PAGE));
#endif
	} else {
		pr_err("ps3rsx: vseal: the SPE wasn't quiet at unload: its ring and pages are kept for the boot\n");
	}
#ifdef PS3RSX_VSEAL_TEST
	kfree(spe->report);
#endif
	kfree(spe);	/* (the callbacks are spu_base's stubs now: nothing reads this) */
}
