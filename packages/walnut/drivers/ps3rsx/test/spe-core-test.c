/* spe-core-test: the SPE client's decisions (src/spe_core.h) on the PC: the descriptor check (windows,
 * refused ranges that always win, the per-page RAM predicate, the window list's validation), poll or
 * interrupt, budgets, spe_on_timeout over a fake clock and fake SPEs (one that finishes, one slow but
 * progressing, one that hangs, one whose interrupt never comes though its done word is written, one that
 * reports an error), the restart count, what a reset's result means for the pages, and the DMA targets'
 * lifetime. (The code acting on these decisions is run by spe-run-test.c.) Built with SPE_FIXOFF_NO_CHECK,
 * SPE_FIXOFF_NO_REFUSED, SPE_FIXOFF_NO_TIMEOUT, SPE_FIXOFF_RESTART_ON_SLOW or SPE_FIXOFF_FREE_UNQUIET it
 * must fail. */
#include <stdio.h>
#include <string.h>

#include "../src/spe_core.h"

static int fails, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		printf("FAIL: %s\n", what);
		fails++;
	}
}

/* --- the descriptor check -------------------------------------------------------------------- */
#define RAM 0x10000000ull
#define VRAM 0xd000000000000000ull
#define BOUNCE 0x20000000ull
static const struct spe_window win[] = {
	{ RAM, 64 << 20, SPE_WIN_RAM }, { VRAM, 96 << 20, SPE_WIN_VRAM }, { BOUNCE, 1 << 20, SPE_WIN_BOUNCE },
};

static struct vseal_job J;
static struct spe_bounds B;

static vs_u64 not_gpu = RAM + (8 << 20);	/* a page the predicate says is mapped for the GPU */
static vs_u64 not_slot = VRAM + (4 << 20);	/* a VRAM page the predicate says is no slot of the pool */
static int page_ok(void *arg, vs_u64 ea, unsigned int kind)
{
	return kind == SPE_WIN_RAM ? ea != *(vs_u64 *)arg : ea != not_slot;
}

static void bounds(void)
{
	memset(&B, 0, sizeof(B));
	memcpy(B.win, win, sizeof(win));
	B.nwin = 3;
	/* the ring and the status line (in RAM), a GART chunk (in RAM), the VRAM pool's plaintext-free part */
	B.refused[0] = (struct spe_window){ RAM + (16 << 20), 4 * 4096, 0 };
	B.refused[1] = (struct spe_window){ RAM + (16 << 20) + 4 * 4096, 4096, 0 };
	B.refused[2] = (struct spe_window){ RAM + (32 << 20), 1 << 20, 0 };
	B.nrefused = 3;
	B.page_ok = page_ok;
	B.page_ok_arg = &not_gpu;
}

static void mk(unsigned int op, unsigned int n)
{
	memset(&J, 0, sizeof(J));
	J.magic = VSEAL_MAGIC;
	J.op = op;
	J.npages = n;
}

static void check_descriptors(void)
{
	unsigned int i;

	bounds();
	mk(VSEAL_OP_SEAL, 2);
	for (i = 0; i < 2; i++) {
		J.page[i].src = RAM + i * 4096;
		J.page[i].dst = VRAM + i * 4096;
	}
	check(spe_check_job(&J, &B, 0) == 0, "seal RAM -> VRAM accepted");
	J.page[1].dst = RAM + 0x100000;
	check(spe_check_job(&J, &B, 0) == 2, "seal into RAM refused outside test builds");
	check(spe_check_job(&J, &B, 1) == 0, "seal into RAM accepted in a test build");
	J.page[1].dst = BOUNCE;
	check(spe_check_job(&J, &B, 1) == 2, "seal into the bounce refused");
	J.page[1].dst = VRAM + 4096 + 16;
	check(spe_check_job(&J, &B, 0) == 2, "an unaligned destination refused");
	J.page[1].dst = VRAM + (96 << 20) - 2048;
	check(spe_check_job(&J, &B, 0) == 2, "a page across a window's end refused");
	J.page[1].dst = VRAM + (96 << 20);
	check(spe_check_job(&J, &B, 0) == 2, "a page past a window refused");
	J.page[1].dst = VRAM + 4096;
	J.page[0].src = VRAM;
	check(spe_check_job(&J, &B, 0) == 1, "plaintext from VRAM refused");
	J.page[0].src = 0x30000000ull;
	check(spe_check_job(&J, &B, 0) == 1, "an address in no window refused");
	J.page[0].src = RAM;
	J.page[0].dst = RAM;
	check(spe_check_job(&J, &B, 1) == 1, "source = destination refused");

	mk(VSEAL_OP_OPEN, 3);
	J.page[0].src = VRAM;
	J.page[1].src = BOUNCE + 4096;
	J.page[2].src = RAM + 8192;
	for (i = 0; i < 3; i++)
		J.page[i].dst = RAM + (1 << 20) + i * 4096;
	check(spe_check_job(&J, &B, 0) == 0, "open from VRAM, the bounce, RAM into RAM accepted");
	J.page[2].dst = VRAM + 8192;
	check(spe_check_job(&J, &B, 0) == 3, "plaintext into VRAM refused");
	J.page[2].dst = BOUNCE;
	check(spe_check_job(&J, &B, 0) == 3, "plaintext into the bounce refused");

	/* refused ranges win over the RAM window they lie in */
	mk(VSEAL_OP_OPEN, 1);
	J.page[0].src = VRAM;
	J.page[0].dst = RAM + (16 << 20) + 4096;
	check(spe_check_job(&J, &B, 1) == 1, "an open into the ring refused (inside the RAM window)");
	J.page[0].dst = RAM + (16 << 20) + 4 * 4096;
	check(spe_check_job(&J, &B, 1) == 1, "an open into the status line refused");
	J.page[0].dst = RAM + (32 << 20) + 8192;
	check(spe_check_job(&J, &B, 0) == 1, "plaintext into a GART-mapped chunk refused");
	J.page[0].dst = RAM + (32 << 20) - 4096;
	check(spe_check_job(&J, &B, 0) == 0, "the page just before the chunk accepted");
	J.page[0].dst = not_gpu;
	check(spe_check_job(&J, &B, 0) == 1, "plaintext into a page the predicate refuses: refused");
	mk(VSEAL_OP_SEAL, 1);
	J.page[0].src = RAM + (32 << 20);
	J.page[0].dst = VRAM;
	check(spe_check_job(&J, &B, 0) == 1, "a seal from a GART-mapped chunk refused");
	B.refused[3] = (struct spe_window){ VRAM, 4096, 0 };
	B.nrefused = 4;
	J.page[0].src = RAM;
	check(spe_check_job(&J, &B, 0) == 1, "a refused VRAM page refused as a destination too");
	bounds();
	{
		struct spe_window w[2] = { { RAM, 4096, SPE_WIN_RAM }, { VRAM, 8192, SPE_WIN_VRAM } };

		check(spe_windows_valid(w, 2) == 0, "a valid window list accepted");
		w[1].len = 0;
		check(spe_windows_valid(w, 2) != 0, "a zero-length window refused");
		w[1].len = 8192 + 16;
		check(spe_windows_valid(w, 2) != 0, "an unaligned window refused");
		w[1] = (struct spe_window){ ~0ull - 4095, 8192, SPE_WIN_VRAM };
		check(spe_windows_valid(w, 2) != 0, "a wrapping window refused");
		w[1] = (struct spe_window){ VRAM, 8192, 3 };
		check(spe_windows_valid(w, 2) != 0, "a window of two kinds refused");
		check(spe_windows_valid(w, SPE_NWIN + 1) != 0, "too many windows refused");
	}
	{
		struct spe_window w[2] = { { BOUNCE, 4096, SPE_WIN_BOUNCE }, { RAM, 4096, SPE_WIN_RAM } };
		struct spe_window r[1] = { { RAM + 8192, 4096, 0 } };

		check(spe_bounds_valid(w, 2, r, 1, 1) == 0, "bounds with a predicate accepted");
		check(spe_bounds_valid(w, 2, r, 1, 0) != 0, "a RAM window without a predicate refused");
		check(spe_bounds_valid(w, 1, r, 1, 0) == 0, "a bounce-only set needs no predicate");
		w[1].kind = SPE_WIN_VRAM;
		check(spe_bounds_valid(w, 2, r, 1, 0) != 0, "a VRAM window without a predicate refused");
		r[0] = (struct spe_window){ ~0ull - 4095, 8192, 0 };
		check(spe_bounds_valid(w, 2, r, 1, 1) != 0, "a refused entry that wraps refused");
		r[0] = (struct spe_window){ RAM + 100, 4096, 0 };
		check(spe_bounds_valid(w, 2, r, 1, 1) != 0, "an unaligned refused entry refused");
	}
	mk(VSEAL_OP_SEAL, 1);
	J.page[0].src = RAM;
	J.page[0].dst = not_slot;
	check(spe_check_job(&J, &B, 0) == 1, "a VRAM page the predicate refuses: refused");
	B.page_ok = NULL;
	J.page[0].dst = VRAM;
	check(spe_check_job(&J, &B, 0) == 1, "no predicate: RAM and VRAM pages refused");
	bounds();

	mk(VSEAL_OP_SEAL, 0);
	check(spe_check_job(&J, &B, 0) != 0, "0 pages refused");
	mk(VSEAL_OP_SEAL, VSEAL_PAGES_MAX + 1);
	check(spe_check_job(&J, &B, 0) != 0, "33 pages refused");
	mk(VSEAL_OP_ECHO, 0);
	check(spe_check_job(&J, &B, 0) == 0, "echo accepted");
	mk(VSEAL_OP_GOLDEN, 1);
	check(spe_check_job(&J, &B, 0) != 0, "golden with pages refused");
	mk(VSEAL_OP_TEST_HANG, 0);
	check(spe_check_job(&J, &B, 0) != 0, "a test op refused outside test builds");
	check(spe_check_job(&J, &B, 1) == 0, "a test op accepted in a test build");
	mk(VSEAL_OP_TEST_SLOW, 1);
	J.page[0].src = RAM;
	J.page[0].dst = RAM + 4096;
	check(spe_check_job(&J, &B, 0) != 0, "slow refused outside test builds");
	J.op = VSEAL_OP_TEST_DIFF;
	check(spe_check_job(&J, &B, 0) != 0, "diff refused outside test builds");
	check(spe_check_job(&J, &B, 1) == 0, "diff accepted in a test build");
	J.op = VSEAL_OP_TEST_COPY;
	check(spe_check_job(&J, &B, 0) != 0, "copy refused outside test builds");
	check(spe_check_job(&J, &B, 1) == 0, "copy accepted in a test build");
	mk(77, 0);
	check(spe_check_job(&J, &B, 1) != 0, "an unknown op refused");
	mk(VSEAL_OP_ECHO, 0);
	J.magic = 0;
	check(spe_check_job(&J, &B, 1) != 0, "a bad magic refused");
}

/* --- poll, budget, timeouts --------------------------------------------------------------------- */
static void check_modes(void)
{
	check(spe_poll(VSEAL_OP_OPEN, 1, SPE_PATH_RAM), "a one-page open from RAM or the bounce is polled");
	check(spe_poll(VSEAL_OP_SEAL, 3, SPE_PATH_RAM), "a three-page seal is polled");
	check(!spe_poll(VSEAL_OP_SEAL, 4, SPE_PATH_RAM), "a four-page seal waits for the interrupt");
	check(!spe_poll(VSEAL_OP_OPEN, 1, SPE_PATH_VRAM), "an open whose pages the SPE reads from VRAM: interrupt");
	check(spe_poll_spin_us(VSEAL_OP_SEAL, 1, SPE_PATH_RAM) < 100, "a poll spins briefly");
	check(spe_budget_us(VSEAL_OP_OPEN, 32, SPE_PATH_VRAM) >= 20000 + 32 * 1000,
	      "a 32-page VRAM-read budget covers its reads");
	check(spe_budget_us(VSEAL_OP_SEAL, 1, SPE_PATH_RAM) >= 20000, "the budget's floor");
}

/* fake SPEs: progress(t) and done(t) in microseconds since submission */
enum fake { FAST, SLOW, HUNG, LATE_IRQ, ERROR };

struct outcome {
	enum spe_verdict v;
	unsigned long t;	/* when it was decided */
};

/* spe.c's wait loop, on a fake clock: the wait ends at the done time if the interrupt comes, else at the
 * budget; then spe_on_timeout decides */
static struct outcome loop(enum fake f, unsigned int op, unsigned int n, enum spe_path path)
{
	struct spe_wait w;
	unsigned long t = 0, done_at, budget = spe_budget_us(op, n, path);
	vs_u64 seq = spe_seq(3, 41);

	switch (f) {
	case FAST:	done_at = 300; break;
	case SLOW:	done_at = 3 * budget; break;		/* three budgets, progressing all along */
	default:	done_at = ~0ul; break;
	}
	spe_wait_start(&w, seq, budget, 0);
	for (;;) {
		unsigned long end = t + w.budget_us;
		int irq = f != LATE_IRQ && done_at <= end;
		vs_u64 done, progress;
		enum spe_verdict v;

		if (end > 100000000ul)		/* 100 s: the loop never decided */
			return (struct outcome){ SPE_WAIT_MORE, end };
		if (irq)
			return (struct outcome){ SPE_DONE, done_at };
		t = end;
		done = (f == LATE_IRQ && t >= 200) || done_at <= t ? seq : spe_seq(2, 41);	/* a stale done */
		progress = f == SLOW ? t / 1000 : f == LATE_IRQ ? 1 : 0;
		v = spe_on_timeout(&w, done, progress, f == ERROR);
		if (v != SPE_WAIT_MORE)
			return (struct outcome){ v, t };
	}
}

static void check_waits(void)
{
	struct outcome o;
	struct spe_health h = { 0, 0 };
	int i;

	o = loop(FAST, VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(o.v == SPE_DONE, "a fast job is done");
	o = loop(SLOW, VSEAL_OP_OPEN, 32, SPE_PATH_VRAM);
	check(o.v == SPE_DONE, "a slow job that progresses is waited for, not restarted");
	o = loop(HUNG, VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(o.v == SPE_RESTART && o.t <= 2 * spe_budget_us(VSEAL_OP_SEAL, 8, SPE_PATH_RAM),
	      "a hung job is restarted within its budget");
	o = loop(LATE_IRQ, VSEAL_OP_SEAL, 2, SPE_PATH_RAM);
	check(o.v == SPE_DONE, "a done word without its interrupt is done, not a timeout");
	o = loop(ERROR, VSEAL_OP_SEAL, 2, SPE_PATH_RAM);
	check(o.v == SPE_RESTART, "an SPE error restarts");
	check(spe_seq(2, 41) != spe_seq(3, 41), "a stale generation's done word never matches");

	for (i = 0; i < SPE_GONE_AFTER - 1; i++)
		spe_restarted(&h, 0, 1);
	check(!h.gone, "two no-progress restarts: not gone yet");
	spe_restarted(&h, 1, 1);
	check(!h.gone && h.noprogress_restarts == 0, "a restart after progress doesn't count");
	for (i = 0; i < SPE_GONE_AFTER; i++)
		spe_restarted(&h, 0, 1);
	check(h.gone, "three no-progress restarts in a row: gone");
	h = (struct spe_health){ 0, 0 };
	spe_restarted(&h, 1, 0);
	check(h.gone, "a reset that failed: gone at once");
	h = (struct spe_health){ 2, 0 };
	spe_job_finished(&h, VSEAL_OP_GOLDEN);
	spe_job_finished(&h, VSEAL_OP_ECHO);
	check(h.noprogress_restarts == 2, "the known answer and an echo don't clear the count");
	spe_job_finished(&h, VSEAL_OP_OPEN);
	check(h.noprogress_restarts == 0, "a good open clears it");

	check(spe_after_reset(0, 0) == SPE_FREE_OK && spe_after_reset(-5, 0) == SPE_KEEP &&
	      spe_after_reset(-16, 1) == SPE_KEEP && spe_after_reset(-22, 1) == SPE_RELEASED,
	      "a reset's result: quiet, kept, released");
	check(spe_after_reset(-22, 0) == SPE_KEEP, "-EINVAL without our own release: kept");
	check(spe_ret_after_reset_failed() == SPE_RET_KEEP, "a failed reset's job returns -EHWPOISON");

	/* the DMA targets' lifetime */
	check(spe_may_free(0, 0), "a quiet release frees the ring and the pages");
	check(!spe_may_free(-16, 0), "an unquiet release (-EBUSY) keeps them for the boot");
	check(!spe_may_free(0, 1), "an SPE once given up unquiet keeps them even if released later");
	check(!spe_may_free(-22, 0), "any other release failure keeps them");
}

int main(void)
{
	check_descriptors();
	check_modes();
	check_waits();
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
