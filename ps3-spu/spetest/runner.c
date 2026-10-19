/* spetest runner (PPE): one spufs context per test, run in parallel on every SPE, each attributed
 * to the physical SPE it ran on. See README.md. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "gen.h"
#include "ref.h"
#include "runner.h"

#define MAXW	16	/* contexts at once (SPEs) */
#define SRC_LEN	65536u
#define OUT_LEN	(OPX_N * EX_OP_BYTES + 4096u)	/* the exec test's results; the MFC test uses the first 128 KB */
_Static_assert(OPX_N * EX_OP_BYTES + 4096u >= 131072u, "out buffer holds the MFC test's layout");
/* the MFC test's layout of its out buffer (spu/t_mfc.c) */
#define MF_TBL_OFF	0u
#define MF_DST_OFF	8192u
#define MF_GUARD	256u
#define MF_LST_OFF	32768u
#define MF_LSLOT	1536u
#define MF_FEN_OFF	73728u

static const char *const test_name[T_N] = { "ls", "reg", "exec", "chan", "mfc" };
static const char *const sub_name[T_N][SPT_NSUB] = {
	[T_LS] = { "march C-", "address in data", "random fill", "setup" },
	[T_REG] = { "zeros", "ones", "0x55", "0xAA", "register number", "unique words", "walking one",
		    "walking zero", "random 1", "random 2", "random 3", "setup" },
	[T_EXEC] = { "int", "sp", "dp", "shuf", "branch kernel", "unaligned access kernel", "repeat differs" },
	[T_CHAN] = { "mailboxes", "signals", "decrementer", "decrementer event", "rate" },
	[T_MFC] = { "get", "put", "small and aligned", "lists", "fence/barrier", "tag status",
		    "atomic", "alignment" },
};

struct spt_cfg {
	const struct spt_be *be;
	FILE *out;
	char progdir[256], mnt[128], hwmon[256];
	unsigned nspe, seed, tb_hz;
	unsigned tests;		/* bit mask */
	unsigned rounds;	/* runs wanted per SPE and test */
	unsigned stress_min, stress_s, slice_s, passes;
	int temp_limit, no_temp;
	unsigned timeout_s;
	int verbose, dry;
};

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---- temperatures ---- */
struct temps {
	int n;
	char label[8][32];
	int mc[8];
};

static int read_file(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	size_t n;

	if (!f)
		return -1;
	n = fread(buf, 1, len - 1, f);
	fclose(f);
	buf[n] = 0;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = 0;
	return (int)n;
}

/* the hwmon with the PS3 sensors (name ps3_sm), or the directory given with --hwmon */
static int temps_find(const char *root, char *dir, size_t len)
{
	struct dirent *d;
	DIR *s;
	char p[600], name[64];

	if (root && *root && !access(root, R_OK)) {
		snprintf(p, sizeof(p), "%s/temp1_input", root);
		if (!access(p, R_OK)) {
			snprintf(dir, len, "%s", root);
			return 0;
		}
	}
	s = opendir(root && *root ? root : "/sys/class/hwmon");
	if (!s)
		return -1;
	while ((d = readdir(s))) {
		if (strncmp(d->d_name, "hwmon", 5))
			continue;
		snprintf(p, sizeof(p), "%s/%s/name", root && *root ? root : "/sys/class/hwmon", d->d_name);
		if (read_file(p, name, sizeof(name)) > 0 && !strcmp(name, "ps3_sm")) {
			snprintf(dir, len, "%s/%s", root && *root ? root : "/sys/class/hwmon", d->d_name);
			closedir(s);
			return 0;
		}
	}
	closedir(s);
	return -1;
}

static int temps_read(const char *dir, struct temps *t)
{
	int i;
	char p[600], b[64];

	t->n = 0;
	for (i = 1; i <= 8; i++) {
		snprintf(p, sizeof(p), "%s/temp%d_input", dir, i);
		if (read_file(p, b, sizeof(b)) <= 0)
			continue;
		t->mc[t->n] = atoi(b);
		snprintf(p, sizeof(p), "%s/temp%d_label", dir, i);
		if (read_file(p, t->label[t->n], sizeof(t->label[0])) <= 0)
			snprintf(t->label[t->n], sizeof(t->label[0]), "temp%d", i);
		t->n++;
	}
	return t->n ? 0 : -1;
}

static int temps_max(const struct temps *t)
{
	int i, m = -300000;

	for (i = 0; i < t->n; i++)
		if (t->mc[i] > m)
			m = t->mc[i];
	return m;
}

static void temps_print(FILE *o, const char *what, const struct temps *t)
{
	int i;

	fprintf(o, "temperatures %s:", what);
	for (i = 0; i < t->n; i++)
		fprintf(o, "%s %s %.1f C", i ? "," : "", t->label[i], t->mc[i] / 1000.0);
	fprintf(o, "\n");
}

/* ---- jobs ---- */
struct run {
	const struct spt_cfg *cfg;
	uint8_t *src;
	struct spt_job *jobs;
	unsigned njobs, capjobs;
	pthread_mutex_t mu;
	/* a round's start barrier and creation order */
	pthread_cond_t cv;
	unsigned expect, arrived, ticket;
	volatile int abort_run;	/* temperature limit */
	char hwdir[512];
	int have_hw;
	int max_temp_seen;
	void *mon;
	int stress;
	unsigned slice_s;
	unsigned long long tick_hz;
};

struct jobx {
	struct run *R;
	unsigned w;
	struct spt_job *j;
	struct spt_ctx c;
	void *mem;		/* ctl + res */
	struct spt_ctl *ctl;
	struct spt_res *res;
	uint8_t *out, *line;
	pthread_t th;		/* the job's thread */
	pthread_t rth;		/* the thread inside spu_run */
	long deadline;
	int timed_out, created, started, abandoned;
	long status;
	unsigned event;
	struct { pthread_t th; volatile int stop, run; uint32_t count; } cont;
	uint32_t s;		/* the conductor's random state */
	long t_start, t_stop;
	uint32_t rate_ticks, lost_n;
	int rate_have_stop;
};

static void jfail(struct jobx *x, const char *fmt, ...)
{
	va_list ap;
	struct spt_job *j = x->j;

	j->nfail++;
	if (j->nmsg < MAXMSG) {
		va_start(ap, fmt);
		vsnprintf(j->msg[j->nmsg], sizeof(j->msg[0]), fmt, ap);
		va_end(ap);
		j->nmsg++;
	}
}

static uint32_t be32(uint32_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap32(v);
#else
	return v;
#endif
}

static uint32_t rd_be(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return be32(v);
}

static void sample_phys(struct jobx *x)
{
	int p = x->R->cfg->be->physid(&x->c);

	if (p < 0)
		return;
	if (x->j->phys < 0)
		x->j->phys = p;
	else if (x->j->phys != p)
		x->j->phys_moved = 1;
}

/* ---- the PPE side of the conversations ---- */
static void atom_add_be(uint32_t *p)
{
	uint32_t o = __atomic_load_n(p, __ATOMIC_RELAXED);

	while (!__atomic_compare_exchange_n(p, &o, be32(be32(o) + 1), 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
		;
}

static void *cont_main(void *p)
{
	struct jobx *x = p;
	uint32_t *w1 = (uint32_t *)x->line + 1;

	while (!x->cont.stop) {
		int i;

		for (i = 0; i < 16; i++) {
			atom_add_be(w1);
			x->cont.count++;
		}
		sched_yield();
	}
	return NULL;
}

static void chan_msg(struct jobx *x, uint32_t m)
{
	const struct spt_be *be = x->R->cfg->be;
	uint32_t w, k;
	unsigned i, n;
	int rc;

	if ((m & 0xFF000000u) == MSG_PING) {
		uint32_t xs[4];

		n = m & 0xFF;
		if (n < 1 || n > 3) {
			jfail(x, "mailbox: odd round size %u", n);
			return;
		}
		for (i = 0; i < n; i++) {
			xs[i] = spt_rnd(&x->s);
			rc = be->mbox_put(&x->c, xs[i], 2000);
			if (rc) {
				jfail(x, "mailbox: write to the SPU's inbound mailbox failed (%d)", rc);
				return;
			}
		}
		for (i = 0; i < n; i++) {
			rc = be->mbox_get(&x->c, &w, 2000);
			if (rc) {
				jfail(x, "mailbox: no reply %u in the outbound mailbox (%d)", i, rc);
				return;
			}
			if (w != xs[i] * 0x9E3779B1u + 1u)
				jfail(x, "mailbox: outbound reply %u is 0x%08x, want 0x%08x", i, w, xs[i] * 0x9E3779B1u + 1u);
			rc = be->ibox_get(&x->c, &w, 2000);
			if (rc) {
				jfail(x, "mailbox: no reply %u in the interrupt mailbox (%d)", i, rc);
				return;
			}
			if (w != (~xs[i] ^ 0x5A5Au))
				jfail(x, "mailbox: interrupt reply %u is 0x%08x, want 0x%08x", i, w, ~xs[i] ^ 0x5A5Au);
		}
	} else if ((m & 0xFF000000u) == MSG_SIG) {
		uint32_t a1 = spt_rnd(&x->s), b1 = spt_rnd(&x->s), c2[3], or2 = 0, v[3];

		for (i = 0; i < 3; i++) {
			c2[i] = spt_rnd(&x->s) & spt_rnd(&x->s);
			or2 |= c2[i];
		}
		if (be->signal_put(&x->c, 1, a1) || be->signal_put(&x->c, 1, b1)) {
			jfail(x, "signal: writing signal1 failed");
			return;
		}
		for (i = 0; i < 3; i++)
			if (be->signal_put(&x->c, 2, c2[i])) {
				jfail(x, "signal: writing signal2 failed");
				return;
			}
		if (be->mbox_put(&x->c, 1, 2000)) {
			jfail(x, "signal: ack to the SPU failed");
			return;
		}
		for (i = 0; i < 3; i++)
			if (be->mbox_get(&x->c, &v[i], 2000)) {
				jfail(x, "signal: no reply word %u", i);
				return;
			}
		if (v[0] != b1)
			jfail(x, "signal1 (overwrite mode) read 0x%08x, want the last write 0x%08x", v[0], b1);
		if (v[1] != or2)
			jfail(x, "signal2 (OR mode) read 0x%08x, want 0x%08x", v[1], or2);
		k = 1 | (1 << 4);	/* both pending before the read, none after */
		if (v[2] != k)
			jfail(x, "signal: channel counts 0x%04x, want 0x%04x", v[2], k);
	} else if (m == MSG_RATE_START) {
		x->t_start = now_ms();
		x->rate_have_stop = 0;
	} else if (m == MSG_RATE_STOP) {
		struct timespec t;
		double dt;

		/* the timestamp of the STOP word, as close to its arrival as we can: take it again here */
		clock_gettime(CLOCK_MONOTONIC, &t);
		x->t_stop = t.tv_sec * 1000 + t.tv_nsec / 1000000;
		if (be->mbox_get(&x->c, &w, 2000)) {
			jfail(x, "rate: no tick count after STOP");
			return;
		}
		dt = (x->t_stop - x->t_start) / 1000.0;
		if (dt <= 0.05) {
			jfail(x, "rate: window of %.3f s", dt);
			return;
		}
		x->j->rate_hz = w / dt;
		if (fabs(x->j->rate_hz / x->R->cfg->tb_hz - 1.0) > 0.03)
			jfail(x, "decrementer ran at %.3f MHz against the timebase's %.3f MHz (%+.2f %%)",
			      x->j->rate_hz / 1e6, x->R->cfg->tb_hz / 1e6,
			      100.0 * (x->j->rate_hz / x->R->cfg->tb_hz - 1.0));
	} else {
		jfail(x, "chan: unexpected message 0x%08x from the SPU", m);
	}
}

static void mfc_msg(struct jobx *x, uint32_t m)
{
	uint32_t *line = (uint32_t *)x->line;

	if (m == MSG_ATOM_START) {
		x->cont.stop = 0;
		x->cont.run = !pthread_create(&x->cont.th, NULL, cont_main, x);
	} else if (m == MSG_ATOM_DONE) {
		if (x->cont.run) {
			x->cont.stop = 1;
			pthread_join(x->cont.th, NULL);
			x->cont.run = 0;
		}
	} else if (m == MSG_ATOM_LOST) {
		__atomic_store_n(&line[2], be32(ATOM_MARK + ++x->lost_n), __ATOMIC_SEQ_CST);	/* a store into the reserved line */
		if (x->R->cfg->be->mbox_put(&x->c, 1, 2000))
			jfail(x, "atomic: ack to the SPU failed");
	} else {
		jfail(x, "mfc: unexpected message 0x%08x from the SPU", m);
	}
}

static void handle_msg(struct jobx *x, uint32_t m)
{
	sample_phys(x);
	if (x->j->test == T_CHAN)
		chan_msg(x, m);
	else if (x->j->test == T_MFC)
		mfc_msg(x, m);
	else
		jfail(x, "unexpected message 0x%08x from the SPU", m);
}

/* ---- verification of the result blocks ---- */
static void note_spu_fails(struct jobx *x)
{
	struct spt_res *r = x->res;
	unsigned i, n = r->nfail < SPT_NFAIL ? r->nfail : SPT_NFAIL, t = x->j->test;

	for (i = 0; i < n; i++) {
		unsigned what = r->f[i].what;
		const char *nm = what < SPT_NSUB && sub_name[t][what] ? sub_name[t][what] : "?";

		if (what == 0xBAD)
			jfail(x, "start-up: control block magic 0x%08x, go word 0x%08x", r->f[i].got, r->f[i].want);
		else if (t == T_LS || t == T_REG)
			jfail(x, "%s: %s 0x%05x read 0x%08x, wrote 0x%08x", test_name[t], nm, r->f[i].addr,
			      r->f[i].got, r->f[i].want);
		else if (t == T_MFC)
			jfail(x, "mfc %s: case %u offset %u read 0x%02x, want 0x%02x", nm, r->f[i].addr >> 16,
			      r->f[i].addr & 0xFFFF, r->f[i].got, r->f[i].want);
		else
			jfail(x, "%s %s: [%u] got 0x%08x want 0x%08x", test_name[t], nm, r->f[i].addr,
			      r->f[i].got, r->f[i].want);
	}
	if (r->nfail > n)
		jfail(x, "... and %u more failures on the SPU", r->nfail - n);
}

static uint32_t digest_words(const uint32_t *w, unsigned n)
{
	uint32_t h = 2166136261u;
	unsigned i;

	for (i = 0; i < n; i++)
		h = (h ^ w[i]) * 16777619u + (h >> 13);
	return h;
}

static void verify_exec(struct jobx *x)
{
	uint32_t seed = x->R->cfg->seed;
	unsigned op, bad_ops = 0, shown = 0;
	char names[160] = "";

	for (op = 0; op < OPX_N; op++) {
		const uint8_t *blk = x->out + (size_t)op * EX_OP_BYTES;
		uint32_t s = spt_seed(seed, op), a[4], b[4], c[4], want[4], got[4], all[EX_QW * 4];
		unsigned q, i, skip, bad = 0, first = 0;
		uint32_t gfirst[4] = { 0 }, wfirst[4] = { 0 };

		for (q = 0; q < EX_QW; q++) {
			spt_gen(spt_ops[op].in, &s, a, b, c);
			spt_ref(op, a, b, c, want, &skip);
			for (i = 0; i < 4; i++) {
				got[i] = rd_be(blk + q * 16 + i * 4);
				all[q * 4 + i] = got[i];
			}
			if (!spt_cmp(spt_ops[op].cmp, got, want, skip)) {
				if (!bad) {
					first = q;
					memcpy(gfirst, got, 16);
					memcpy(wfirst, want, 16);
				}
				bad++;
			}
		}
		x->j->sig[op] = digest_words(all, EX_QW * 4);
		if (!bad)
			continue;
		bad_ops++;
		if (strlen(names) < 120)
			snprintf(names + strlen(names), sizeof(names) - strlen(names), "%s%s", *names ? " " : "", spt_ops[op].name);
		if (shown < 4) {
			shown++;
			jfail(x, "exec %s: %u of %u quadwords differ; first #%u got %08x %08x %08x %08x want %08x %08x %08x %08x",
			      spt_ops[op].name, bad, EX_QW, first, gfirst[0], gfirst[1], gfirst[2], gfirst[3],
			      wfirst[0], wfirst[1], wfirst[2], wfirst[3]);
		} else {
			x->j->nfail++;
		}
	}
	if (bad_ops > shown)
		jfail(x, "exec: %u operations differ from the reference: %s", bad_ops, names);
	x->j->has_sig = 1;
	if (x->res->aux[0] != spt_ref_branch(seed))
		jfail(x, "exec branch kernel: SPU 0x%08x, PPE 0x%08x", x->res->aux[0], spt_ref_branch(seed));
	if (x->res->aux[1] != spt_ref_mem(seed))
		jfail(x, "exec unaligned access kernel: SPU 0x%08x, PPE 0x%08x", x->res->aux[1], spt_ref_mem(seed));
	if (x->res->aux[2] != OPX_N)
		jfail(x, "exec: the SPU ran %u operations, the PPE knows %u (rebuild both)", x->res->aux[2], OPX_N);
}

static void verify_mfc(struct jobx *x)
{
	uint32_t seed = x->R->cfg->seed, k, i;
	const uint8_t *o = x->out;
	uint32_t *line = (uint32_t *)x->line, passes = x->res->passes;
	unsigned bad = 0;

	/* the checksum table of the get test */
	{
		uint8_t pat[16384 + 128];
		uint32_t n;

		for (n = 0; n < sizeof(pat); n++)
			pat[n] = spt_pat(seed, n);
		for (k = 1; k <= 1024 && bad < 3; k++) {
			uint32_t size = 16 * k, eo = 16 * (k % 8), h = spt_ck32(pat + eo, size);

			if (rd_be(o + MF_TBL_OFF + 4 * (k - 1)) != h) {
				jfail(x, "mfc get: size %u checksum 0x%08x, want 0x%08x", size, rd_be(o + MF_TBL_OFF + 4 * (k - 1)), h);
				bad++;
			}
		}
	}
	/* the last put: size 16384 at offset 0, guards untouched */
	for (i = 0; i < 16384 && bad < 6; i++)
		if (o[MF_DST_OFF + MF_GUARD + i] != spt_pat(seed, i)) {
			jfail(x, "mfc put: byte %u of the 16 KB put in main memory is 0x%02x, want 0x%02x", i,
			      o[MF_DST_OFF + MF_GUARD + i], spt_pat(seed, i));
			bad++;
			break;
		}
	/* (the first 128 bytes before the data are the small-put test's: only the rest is a guard) */
	for (i = 128; i < MF_GUARD && bad < 6; i++)
		if (o[MF_DST_OFF + i] != 0xA5 || o[MF_DST_OFF + MF_GUARD + 16384 + i] != 0xA5) {
			jfail(x, "mfc put: guard byte %u around the last put was overwritten", i);
			bad++;
			break;
		}
	/* the last list (l = 3): slot k holds pattern (seed + 3, k * slot + i) for size bytes */
	for (k = 0; k < LST_ELEMS && bad < 6; k++) {
		uint32_t off, size;

		spt_lst_elem(seed, 3, k, &off, &size);
		for (i = 0; i < size; i++)
			if (o[MF_LST_OFF + k * MF_LSLOT + i] != spt_pat(seed, k * MF_LSLOT + 48 + i)) {
				jfail(x, "mfc list: slot %u byte %u is 0x%02x, want 0x%02x", k, i,
				      o[MF_LST_OFF + k * MF_LSLOT + i], spt_pat(seed, k * MF_LSLOT + 48 + i));
				bad++;
				break;
			}
	}
	/* the tag test's put of 0x3C bytes */
	for (i = 0; i < 8192 && bad < 6; i++)
		if (o[MF_FEN_OFF + i] != 0x3C) {
			jfail(x, "mfc tags: byte %u of the 8 KB put on tag 17 is 0x%02x, want 0x3c", i, o[MF_FEN_OFF + i]);
			bad++;
			break;
		}
	/* the atomic line */
	{
		uint32_t w0 = be32(line[0]), w1 = be32(line[1]), w2 = be32(line[2]), w3 = be32(line[3]);

		if (w0 != (ATOM_N + 1) * passes)
			jfail(x, "mfc atomic: SPU's counter is %u after %u passes, want %u", w0, passes, (ATOM_N + 1) * passes);
		if (w1 != x->cont.count)
			jfail(x, "mfc atomic: PPE's counter is %u, it added %u (an update was lost)", w1, x->cont.count);
		if (w2 != ATOM_MARK + x->lost_n)
			jfail(x, "mfc atomic: marker word 0x%08x, want 0x%08x", w2, ATOM_MARK + x->lost_n);
		if (w3 != ~ATOM_UNCOND)
			jfail(x, "mfc atomic: word 3 is 0x%08x, want 0x%08x (putqlluc)", w3, ~ATOM_UNCOND);
	}
}

static void classify_status(struct jobx *x)
{
	struct spt_job *j = x->j;
	long st = x->status;
	unsigned code;

	j->status = st;
	j->event = x->event;
	if (st < 0) {
		j->outcome = R_CRASH;
		jfail(x, "spu_run failed: %s", strerror((int)-st));
		return;
	}
	if (x->event) {
		j->outcome = R_CRASH;
		jfail(x, "SPE event 0x%x (a DMA fault or MFC error), spu_run status 0x%lx", x->event, st);
		return;
	}
	code = (unsigned)st >> 16;
	if ((st & 2) && (code & 0xFF00) == 0x2000) {
		if ((code & 0xFF) > 1) {
			j->outcome = R_CRASH;
			jfail(x, "exit code %u", code & 0xFF);
		}
		return;
	}
	j->outcome = R_CRASH;
	if (st & 4)
		jfail(x, "the SPU halted (status 0x%lx)", st);
	else if (st & 0x20)
		jfail(x, "invalid instruction (status 0x%lx)", st);
	else if (st & 0x40)
		jfail(x, "invalid channel access (status 0x%lx)", st);
	else if (st & 2)
		jfail(x, "stopped with stop code 0x%04x (status 0x%lx)", code, st);
	else
		jfail(x, "unexpected spu_run status 0x%lx", st);
}

static void *run_main(void *p)
{
	struct jobx *x = p;

	x->status = x->R->cfg->be->run(&x->c, &x->event);
	__atomic_store_n(&x->c.done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void wait_done(struct jobx *x, int ms)
{
	long end = now_ms() + ms;

	while (!x->c.done && now_ms() < end)
		usleep(1000);
}

static void *job_main(void *p)
{
	struct jobx *x = p;
	struct run *R = x->R;
	const struct spt_cfg *cfg = R->cfg;
	const struct spt_be *be = cfg->be;
	struct spt_job *j = x->j;
	struct spt_ctl *ctl;
	char elf[600];
	uint64_t ea;
	long t0 = now_ms();
	int rc = 0, tries;
	uint32_t w;
	unsigned timeout = cfg->timeout_s + (R->stress ? R->slice_s : 0);

	j->phys = -1;
	if (posix_memalign(&x->mem, 128, 512) || posix_memalign((void **)&x->out, 128, OUT_LEN) ||
	    posix_memalign((void **)&x->line, 128, 128)) {
		j->outcome = R_NOLOAD;
		jfail(x, "out of memory");
		rc = -ENOMEM;
	} else {
		ctl = x->ctl = x->mem;
		x->res = (struct spt_res *)((char *)x->mem + SPT_OFF_RES);
		memset(x->mem, 0, 512);
		ctl->magic = SPT_MAGIC;
		ctl->test = j->test;
		ctl->iters = cfg->passes;
		ctl->seed = cfg->seed;
		ctl->slot = x->w;
		ctl->nspe = cfg->nspe;
		ctl->tb_hz = cfg->tb_hz;
		ctl->flags = R->stress ? SPT_F_STRESS : 0;
		ctl->run_ticks = R->stress ? (uint32_t)(R->slice_s * (unsigned long long)cfg->tb_hz) : 0;
		ea = (uintptr_t)x->out;
		ctl->out_hi = (uint32_t)(ea >> 32);
		ctl->out_lo = (uint32_t)ea;
		ctl->out_len = OUT_LEN;
		ea = (uintptr_t)R->src;
		ctl->src_hi = (uint32_t)(ea >> 32);
		ctl->src_lo = (uint32_t)ea;
		ctl->src_len = SRC_LEN;
		ea = (uintptr_t)x->line;
		ctl->line_hi = (uint32_t)(ea >> 32);
		ctl->line_lo = (uint32_t)ea;
		spt_swap_words(ctl, SPT_CTL_BYTES);
		memset(x->line, 0, 128);
		memset(x->out, 0xA5, OUT_LEN);
	}

	/* contexts are created one after the other in worker order */
	pthread_mutex_lock(&R->mu);
	while (R->ticket != x->w)
		pthread_cond_wait(&R->cv, &R->mu);
	pthread_mutex_unlock(&R->mu);
	if (!rc) {
		snprintf(elf, sizeof(elf), "%s/spetest-%s.spu", cfg->progdir, test_name[j->test]);
		rc = be->create(&x->c, elf, x->w, (uintptr_t)x->mem, cfg->seed);
		if (rc) {
			j->outcome = R_NOLOAD;
			jfail(x, "cannot create the context for %s: %s", elf, strerror(-rc));
		} else {
			x->created = 1;
			x->c.done = 0;
			if (j->test == T_CHAN) {
				be->signal_type(&x->c, 1, 0);	/* overwrite */
				be->signal_type(&x->c, 2, 1);	/* OR */
			}
			rc = pthread_create(&x->rth, NULL, run_main, x);
			if (rc) {
				j->outcome = R_NOLOAD;
				jfail(x, "cannot start the run thread");
			} else {
				x->started = 1;
				x->c.runner = x->rth;
			}
		}
	}
	pthread_mutex_lock(&R->mu);
	R->ticket++;
	pthread_cond_broadcast(&R->cv);
	pthread_mutex_unlock(&R->mu);

	if (x->started) {
		/* the SPU waits for the go word: it is on an SPE by now, or queued behind others */
		for (tries = 0; tries < 3000 && !x->c.done; tries++) {
			sample_phys(x);
			if (j->phys >= 0)
				break;
			usleep(1000);
		}
		if (j->phys < 0 && !x->c.done) {
			j->outcome = R_NOLOAD;
			jfail(x, "never scheduled on an SPE within 3 s (all SPEs busy?)");
			be->interrupt(&x->c);
			wait_done(x, 3000);
		}
	}
	/* everyone ready: now the go words */
	pthread_mutex_lock(&R->mu);
	R->arrived++;
	pthread_cond_broadcast(&R->cv);
	{
		struct timespec to;

		clock_gettime(CLOCK_REALTIME, &to);
		to.tv_sec += 8;
		while (R->arrived < R->expect)
			if (pthread_cond_timedwait(&R->cv, &R->mu, &to))
				break;
	}
	pthread_mutex_unlock(&R->mu);

	if (x->started && j->outcome != R_NOLOAD && !x->c.done) {
		x->deadline = now_ms() + timeout * 1000L;
		if (be->mbox_put(&x->c, SPT_GO | j->test, 2000)) {
			jfail(x, "go word not accepted");
			j->outcome = R_CRASH;
			be->interrupt(&x->c);
		}
		sample_phys(x);
		while (!x->c.done) {
			if (R->abort_run || now_ms() > x->deadline) {
				x->timed_out = 1;
				break;
			}
			if (!be->mbox_get(&x->c, &w, 10))
				handle_msg(x, w);
		}
		if (x->timed_out) {
			be->interrupt(&x->c);
			wait_done(x, 3000);
			j->outcome = R_HUNG;
			if (!x->c.done)
				jfail(x, "the SPU did not stop after SIGUSR1 (context abandoned)");
			else if (R->abort_run)
				jfail(x, "stopped: temperature limit");
			else
				jfail(x, "timed out after %u s and was killed", timeout);
		}
		/* messages left in the mailbox when it stopped */
		while (x->c.done && !be->mbox_get(&x->c, &w, 0))
			handle_msg(x, w);
	}
	if (x->cont.run) {
		x->cont.stop = 1;
		pthread_join(x->cont.th, NULL);
		x->cont.run = 0;
	}
	if (x->started && x->c.done)
		pthread_join(x->rth, NULL);
	if (x->started && !x->c.done) {
		/* the thread still sits in the kernel: keep the context and its buffers */
		x->abandoned = 1;
	} else {
		if (x->started && j->outcome == R_PASS) {
			classify_status(x);
			if (j->outcome == R_PASS) {
				struct spt_res *r = x->res;

				spt_swap_words(r, SPT_RES_BYTES);
				if (r->magic != SPT_MAGIC || r->state != 1 || r->test != (unsigned)j->test) {
					jfail(x, "no result block from the SPU (magic 0x%08x state %u)", r->magic, r->state);
				} else {
					note_spu_fails(x);
					if (j->test == T_LS)
						snprintf(j->info, sizeof(j->info), "local store 0x%05x-0x%05x tested (%u KB of 256)",
							 r->aux[0], r->aux[1], (r->aux[1] - r->aux[0]) / 1024);
					if (j->test == T_EXEC)
						verify_exec(x);
					else if (j->test == T_MFC)
						verify_mfc(x);
				}
			}
		}
		if (x->created)
			be->destroy(&x->c);
		free(x->mem);
		free(x->out);
		free(x->line);
	}
	if (j->outcome == R_PASS && j->nfail)
		j->outcome = R_FAIL;
	j->secs = (now_ms() - t0) / 1000.0;
	return NULL;
}

static void run_round(struct run *R, const int *tests, unsigned n)
{
	struct jobx *xs = calloc(n, sizeof(*xs));
	struct spt_job *js;
	unsigned i;

	pthread_mutex_lock(&R->mu);
	if (R->njobs + n > R->capjobs) {
		R->capjobs = (R->njobs + n) * 2;
		R->jobs = realloc(R->jobs, R->capjobs * sizeof(*R->jobs));
	}
	js = R->jobs + R->njobs;
	R->njobs += n;
	memset(js, 0, n * sizeof(*js));
	R->expect = n;
	R->arrived = 0;
	R->ticket = 0;
	pthread_mutex_unlock(&R->mu);
	for (i = 0; i < n; i++) {
		xs[i].R = R;
		xs[i].w = i;
		xs[i].j = &js[i];
		js[i].test = tests[i];
		js[i].slot = i;
		js[i].phys = -1;
		js[i].outcome = R_PASS;
		xs[i].s = spt_seed(R->cfg->seed, 1000 + i);
		pthread_create(&xs[i].th, NULL, job_main, &xs[i]);
	}
	for (i = 0; i < n; i++)
		pthread_join(xs[i].th, NULL);
	for (i = 0; i < n; i++)
		if (xs[i].abandoned)
			return;	/* its run thread still uses xs: leak it */
	free(xs);
}

/* ---- scheduling, report ---- */
#define MAXPHYS 16

struct cell {
	unsigned runs, pass, fail, crash, hung;
	int first;		/* index of the first non-passing job, or -1 */
	double rate_sum;
	unsigned rate_n;
};

struct cov {
	struct cell c[T_N][MAXPHYS];
	int seen[MAXPHYS];	/* physical SPEs seen */
};

static void cov_build(const struct run *R, struct cov *cv)
{
	unsigned i, t, p;

	memset(cv, 0, sizeof(*cv));
	for (t = 0; t < T_N; t++)
		for (p = 0; p < MAXPHYS; p++)
			cv->c[t][p].first = -1;
	for (i = 0; i < R->njobs; i++) {
		const struct spt_job *j = &R->jobs[i];
		struct cell *c;

		if (j->outcome == R_NOLOAD || j->phys < 0 || j->phys >= MAXPHYS)
			continue;
		cv->seen[j->phys] = 1;
		c = &cv->c[j->test][j->phys];
		c->runs++;
		if (j->outcome == R_PASS)
			c->pass++;
		else {
			c->fail++;
			if (j->outcome == R_CRASH)
				c->crash++;
			if (j->outcome == R_HUNG)
				c->hung++;
			if (c->first < 0)
				c->first = (int)i;
		}
		if (j->rate_hz > 0) {
			c->rate_sum += j->rate_hz;
			c->rate_n++;
		}
	}
}

/* the exec test's digests: an SPE whose results differ from the others' (a majority of at least
 * three SPEs), or from its own earlier run, fails even if the reference agrees */
static void cross_check(struct run *R)
{
	unsigned i, k, op, p;
	const struct spt_job *ref[MAXPHYS] = { 0 };
	unsigned nph = 0;

	for (i = 0; i < R->njobs; i++) {
		struct spt_job *j = &R->jobs[i];

		if (j->test != T_EXEC || !j->has_sig || j->phys < 0 || j->phys >= MAXPHYS)
			continue;
		if (!ref[j->phys]) {
			ref[j->phys] = j;
			nph++;
		} else if (memcmp(ref[j->phys]->sig, j->sig, sizeof(j->sig))) {
			for (op = 0; op < OPX_N; op++)
				if (ref[j->phys]->sig[op] != j->sig[op])
					break;
			j->nfail++;
			if (j->nmsg < MAXMSG)
				snprintf(j->msg[j->nmsg++], sizeof(j->msg[0]),
					 "exec: operation %s gave different results than in an earlier run on this SPE",
					 spt_ops[op].name);
			if (j->outcome == R_PASS)
				j->outcome = R_FAIL;
		}
	}
	if (nph < 3)
		return;
	for (op = 0; op < OPX_N; op++) {
		for (p = 0; p < MAXPHYS; p++) {
			unsigned same = 0;

			if (!ref[p])
				continue;
			for (k = 0; k < MAXPHYS; k++)
				if (ref[k] && ref[k]->sig[op] == ref[p]->sig[op])
					same++;
			if (same * 2 > nph || same == nph)
				continue;
			/* p differs from a majority: flag it (once per op, on its first job) */
			{
				struct spt_job *j = (struct spt_job *)ref[p];

				j->nfail++;
				if (j->nmsg < MAXMSG)
					snprintf(j->msg[j->nmsg++], sizeof(j->msg[0]),
						 "exec: operation %s differs from the majority of the other SPEs",
						 spt_ops[op].name);
				if (j->outcome == R_PASS)
					j->outcome = R_FAIL;
			}
		}
	}
}

static const char *outcome_name(int o)
{
	return o == R_PASS ? "pass" : o == R_FAIL ? "FAIL" : o == R_CRASH ? "CRASH" : o == R_HUNG ? "HUNG" : "not run";
}

static int report(struct run *R, FILE *o, const unsigned *spe_list, unsigned nlist, unsigned want_runs)
{
	struct cov cv;
	unsigned t, p, k, i;
	int bad = 0, incomplete = 0;

	cross_check(R);
	cov_build(R, &cv);
	fprintf(o, "\nresults by physical SPE (runs: passed/total)\n");
	fprintf(o, "SPE ");
	for (t = 0; t < T_N; t++)
		if (R->cfg->tests >> t & 1)
			fprintf(o, " %-9s", test_name[t]);
	fprintf(o, "\n");
	for (k = 0; k < nlist; k++) {
		p = spe_list[k];
		fprintf(o, "%3u ", p);
		for (t = 0; t < T_N; t++) {
			char b[24];
			struct cell *c = &cv.c[t][p];

			if (!(R->cfg->tests >> t & 1))
				continue;
			if (!c->runs)
				snprintf(b, sizeof(b), "-");
			else
				snprintf(b, sizeof(b), "%s %u/%u", c->fail ? (c->hung ? "HUNG" : c->crash ? "CRASH" : "FAIL") : "ok",
					 c->pass, c->runs);
			fprintf(o, " %-9s", b);
		}
		fprintf(o, "\n");
	}
	/* details of what failed */
	for (i = 0; i < R->njobs; i++) {
		const struct spt_job *j = &R->jobs[i];
		unsigned m;

		if (j->outcome == R_PASS)
			continue;
		if (j->outcome != R_NOLOAD && !R->cfg->verbose && j->phys >= 0) {
			/* one entry per SPE and test: the first failing run, and how many failed */
			unsigned q, same = 0, first = 1;

			for (q = 0; q < R->njobs; q++)
				if (R->jobs[q].test == j->test && R->jobs[q].phys == j->phys && R->jobs[q].outcome != R_PASS &&
				    R->jobs[q].outcome != R_NOLOAD) {
					same++;
					if (q < i)
						first = 0;
				}
			if (!first)
				continue;
			if (same > 1)
				fprintf(o, "\n(%u runs of %s on SPE %d failed; the first:)", same, test_name[j->test], j->phys);
		}
		if (j->outcome == R_NOLOAD && !R->cfg->verbose) {
			/* contexts that never started: say why once per reason */
			unsigned q;
			int seen = 0;

			for (q = 0; q < i; q++)
				if (R->jobs[q].outcome == R_NOLOAD && R->jobs[q].nmsg && j->nmsg &&
				    !strcmp(R->jobs[q].msg[0], j->msg[0]))
					seen = 1;
			if (seen || !j->nmsg)
				continue;
			fprintf(o, "\n%s: not started (%.1f s)\n  %s\n", test_name[j->test], j->secs, j->msg[0]);
			continue;
		}
		if (j->phys >= 0)
			fprintf(o, "\n%s on SPE %d: %s (%.1f s)%s\n", test_name[j->test], j->phys, outcome_name(j->outcome),
				j->secs, j->phys_moved ? ", the context moved to another SPE during the run" : "");
		else
			fprintf(o, "\n%s, no SPE: %s (%.1f s)\n", test_name[j->test], outcome_name(j->outcome), j->secs);
		for (m = 0; m < j->nmsg; m++)
			fprintf(o, "  %s\n", j->msg[m]);
		if (j->nfail > j->nmsg)
			fprintf(o, "  (%u failures in all)\n", j->nfail);
	}
	if (R->cfg->verbose) {
		for (i = 0; i < R->njobs; i++)
			if (R->jobs[i].info[0] && R->jobs[i].phys >= 0) {
				fprintf(o, "SPE %d %s: %s\n", R->jobs[i].phys, test_name[R->jobs[i].test], R->jobs[i].info);
				break;
			}
		for (k = 0; k < nlist; k++) {
			struct cell *c = &cv.c[T_CHAN][spe_list[k]];

			if (c->rate_n)
				fprintf(o, "SPE %u decrementer: %.4f MHz (timebase %.4f MHz)\n", spe_list[k],
					c->rate_sum / c->rate_n / 1e6, R->cfg->tb_hz / 1e6);
		}
	}
	fprintf(o, "\n");
	for (k = 0; k < nlist; k++) {
		char fails[200] = "", missing[80] = "";

		p = spe_list[k];
		for (t = 0; t < T_N; t++) {
			struct cell *c = &cv.c[t][p];

			if (!(R->cfg->tests >> t & 1))
				continue;
			if (c->fail)
				snprintf(fails + strlen(fails), sizeof(fails) - strlen(fails), " %s(%u/%u %s)", test_name[t],
					 c->fail, c->runs, c->hung ? "hung" : c->crash ? "crash" : "failed");
			if (c->runs < want_runs)
				snprintf(missing + strlen(missing), sizeof(missing) - strlen(missing), " %s", test_name[t]);
		}
		if (*fails) {
			fprintf(o, "SPE %u: FAIL%s\n", p, fails);
			bad = 1;
		} else if (*missing) {
			fprintf(o, "SPE %u: INCOMPLETE, not run enough:%s\n", p, missing);
			incomplete = 1;
		} else {
			fprintf(o, "SPE %u: PASS\n", p);
		}
	}
	return bad ? 1 : incomplete ? 3 : 0;
}

/* samples the temperatures once a second while tests run; stress stops at the limit */
struct mon {
	struct run *R;
	volatile int stop;
	pthread_t th;
};

static void *mon_main(void *p)
{
	struct mon *m = p;
	struct run *R = m->R;
	struct temps t;
	int tick = 0;

	while (!m->stop) {
		usleep(100000);
		if (++tick % 10)
			continue;
		if (!temps_read(R->hwdir, &t)) {
			int mx = temps_max(&t);

			if (mx > R->max_temp_seen)
				R->max_temp_seen = mx;
			if (mx >= R->cfg->temp_limit * 1000 && R->stress)
				R->abort_run = 1;
		}
	}
	return NULL;
}

static unsigned parse_tests(const char *s)
{
	unsigned m = 0, t;
	char *dup = strdup(s), *tok, *sv = NULL;

	for (tok = strtok_r(dup, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
		for (t = 0; t < T_N; t++)
			if (!strcmp(tok, test_name[t]))
				m |= 1u << t;
		if (!strcmp(tok, "all"))
			m = (1u << T_N) - 1;
	}
	free(dup);
	return m;
}

static unsigned read_tb_hz(void)
{
	FILE *f = fopen("/proc/cpuinfo", "r");
	char l[256];
	unsigned hz = 0;

	if (f) {
		while (fgets(l, sizeof(l), f))
			if (!strncmp(l, "timebase", 8)) {
				char *c = strchr(l, ':');

				if (c)
					hz = (unsigned)strtoul(c + 1, NULL, 10);
				break;
			}
		fclose(f);
	}
	return hz ? hz : 79800000u;
}

static void usage(FILE *o)
{
	fprintf(o,
"usage: spetest [options]\n"
"  --tests LIST      comma separated: ls,reg,exec,chan,mfc (default all)\n"
"  --rounds N        every test at least N times on every SPE (default 1)\n"
"  --passes N        passes of each test inside one run (default 2)\n"
"  --stress MIN      all tests on all SPEs at once for MIN minutes (not run unless asked)\n"
"  --slice SEC       length of one stress slice (default 4)\n"
"  --spes N          contexts per round (default: all SPEs Linux has)\n"
"  --dir DIR         where spetest-*.spu are (default /usr/lib/ps3/spetest)\n"
"  --seed N          pseudo-random seed (default 0x5eed)\n"
"  --timeout SEC     per context (default 30)\n"
"  --temp-limit C    stop and refuse stress above this (default 80)\n"
"  --no-temp         no temperature checks (stress then needs this to run)\n"
"  --hwmon DIR       hwmon directory with temp*_input (default: the ps3_sm device)\n"
"  -v                more output\n"
"exit: 0 pass, 1 failure, 2 usage or setup problem, 3 incomplete, 4 temperature limit\n");
}

int spt_main(int argc, char **argv, const struct spt_be *be, FILE *out)
{
	struct spt_cfg cfg;
	struct run R;
	struct temps t0, t1;
	unsigned spe_list[MAXPHYS], nlist = 0, t, p, round = 0, maxrounds;
	struct cov cv;
	int c, rc, have_t = 0, tmax;
	unsigned cur, last_covered = 0, stale = 0;
	void *raw_src;
	char mnt[128] = "";
	long start;
	static const struct option lo[] = {
		{ "tests", 1, 0, 't' }, { "passes", 1, 0, 'p' }, { "stress-seconds", 1, 0, 'z' }, { "rounds", 1, 0, 'r' }, { "stress", 1, 0, 's' }, { "slice", 1, 0, 'l' },
		{ "spes", 1, 0, 'n' }, { "dir", 1, 0, 'd' }, { "seed", 1, 0, 'S' }, { "timeout", 1, 0, 'T' },
		{ "temp-limit", 1, 0, 'L' }, { "no-temp", 0, 0, 'N' }, { "hwmon", 1, 0, 'H' },
		{ "help", 0, 0, 'h' }, { 0, 0, 0, 0 } };

	memset(&cfg, 0, sizeof(cfg));
	cfg.be = be;
	cfg.out = out;
	snprintf(cfg.progdir, sizeof(cfg.progdir), "/usr/lib/ps3/spetest");
	cfg.seed = 0x5eed;
	cfg.tests = (1u << T_N) - 1;
	cfg.rounds = 1;
	cfg.passes = 2;
	cfg.slice_s = 4;
	cfg.temp_limit = 80;
	cfg.timeout_s = 30;
	optind = 0;
	while ((c = getopt_long(argc, argv, "v", lo, NULL)) != -1) {
		switch (c) {
		case 't': cfg.tests = parse_tests(optarg); break;
		case 'r': cfg.rounds = (unsigned)atoi(optarg); break;
		case 's': cfg.stress_min = (unsigned)atoi(optarg); cfg.stress_s = cfg.stress_min * 60; break;
		case 'z': cfg.stress_s = (unsigned)atoi(optarg); cfg.stress_min = 1; break;
		case 'p': cfg.passes = (unsigned)atoi(optarg); break;
		case 'l': cfg.slice_s = (unsigned)atoi(optarg); break;
		case 'n': cfg.nspe = (unsigned)atoi(optarg); break;
		case 'd': snprintf(cfg.progdir, sizeof(cfg.progdir), "%s", optarg); break;
		case 'S': cfg.seed = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'T': cfg.timeout_s = (unsigned)atoi(optarg); break;
		case 'L': cfg.temp_limit = atoi(optarg); break;
		case 'N': cfg.no_temp = 1; break;
		case 'H': snprintf(cfg.hwmon, sizeof(cfg.hwmon), "%s", optarg); break;
		case 'v': cfg.verbose = 1; break;
		case 'h': usage(out); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (!cfg.tests || !cfg.rounds || !cfg.passes || cfg.slice_s < 1 || cfg.slice_s > 30) {
		usage(stderr);
		return 2;
	}
	cfg.tb_hz = read_tb_hz();
	{
		unsigned n = (unsigned)be->probe(mnt, sizeof(mnt));

		if (!n) {
			fprintf(stderr, "spetest: no SPEs or no spufs (modprobe spufs; mount -t spufs spufs /spu)\n");
			return 2;
		}
		if (!cfg.nspe || cfg.nspe > n)
			cfg.nspe = n;
		if (cfg.nspe > MAXW)
			cfg.nspe = MAXW;
	}
	memset(&R, 0, sizeof(R));
	R.cfg = &cfg;
	pthread_mutex_init(&R.mu, NULL);
	pthread_cond_init(&R.cv, NULL);
	R.stress = cfg.stress_min != 0;
	R.slice_s = cfg.slice_s;
	raw_src = malloc(SRC_LEN + 128);
	if (!raw_src)
		return 2;
	R.src = (uint8_t *)(((uintptr_t)raw_src + 127) & ~(uintptr_t)127);
	for (p = 0; p < SRC_LEN; p++)
		R.src[p] = spt_pat(cfg.seed, p);
	/* the SPE numbers Linux lists */
	{
		unsigned ids[MAXPHYS], n = (unsigned)be->spe_ids(ids, MAXPHYS);

		for (p = 0; p < n && p < MAXPHYS; p++)
			spe_list[nlist++] = ids[p];
		if (!nlist)
			for (p = 0; p < cfg.nspe && p < MAXPHYS; p++)
				spe_list[nlist++] = p;
	}
	/* the programs */
	for (t = 0; t < T_N; t++) {
		char f[600];

		snprintf(f, sizeof(f), "%s/spetest-%s.spu", cfg.progdir, test_name[t]);
		if ((cfg.tests >> t & 1) && access(f, R_OK)) {
			fprintf(stderr, "spetest: %s not found (ps3-spu-programs installs it in /usr/lib/ps3/spetest; --dir DIR)\n", f);
			return 2;
		}
	}

	fprintf(out, "spetest: %s backend (%s), %u SPEs, tests", be->name, mnt, cfg.nspe);
	for (t = 0; t < T_N; t++)
		if (cfg.tests >> t & 1)
			fprintf(out, " %s", test_name[t]);
	fprintf(out, ", seed 0x%x, timebase %.3f MHz\n", cfg.seed, cfg.tb_hz / 1e6);

	/* temperatures */
	if (!cfg.no_temp && !temps_find(cfg.hwmon, R.hwdir, sizeof(R.hwdir)) && !temps_read(R.hwdir, &t0)) {
		have_t = 1;
		R.have_hw = 1;
		temps_print(out, "at start", &t0);
		tmax = temps_max(&t0);
		if (tmax >= cfg.temp_limit * 1000) {
			fprintf(out, "spetest: %s at %.1f C is at or above the %d C limit; not %s\n",
				"the hottest sensor", tmax / 1000.0, cfg.temp_limit,
				cfg.stress_min ? "running the stress test" : "running");
			if (cfg.stress_min)
				return 4;
		}
	} else if (!cfg.no_temp) {
		fprintf(out, "temperatures: no ps3_sm hwmon device found\n");
		if (cfg.stress_min) {
			fprintf(out, "spetest: refusing to stress without temperatures (--no-temp to override)\n");
			return 4;
		}
	}

	{
		static struct mon mon;

		mon.R = &R;
		mon.stop = 0;
		if (R.have_hw)
			pthread_create(&mon.th, NULL, mon_main, &mon);
		R.mon = &mon;
	}
	start = now_ms();
	maxrounds = 4 * T_N + 8 + cfg.rounds * T_N;
	cur = cfg.nspe;
	for (;;) {
		int tests[MAXW];
		unsigned w, ntl = 0, tl[T_N], before;

		for (t = 0; t < T_N; t++)
			if (cfg.tests >> t & 1)
				tl[ntl++] = t;
		if (cfg.stress_min) {
			if ((unsigned long)(now_ms() - start) >= cfg.stress_s * 1000UL || R.abort_run)
				break;
			for (w = 0; w < cur; w++)
				tests[w] = (int)tl[(round + w) % ntl];
		} else {
			unsigned taken[T_N] = { 0 }, done = 1;

			cov_build(&R, &cv);
			for (p = 0; p < nlist; p++)
				for (t = 0; t < T_N; t++)
					if ((cfg.tests >> t & 1) && cv.c[t][spe_list[p]].runs < cfg.rounds)
						done = 0;
			if (done || round >= maxrounds)
				break;
			{
				unsigned covered = 0;

				for (p = 0; p < nlist; p++)
					for (t = 0; t < T_N; t++)
						if ((cfg.tests >> t & 1) && cv.c[t][spe_list[p]].runs >= cfg.rounds)
							covered++;
				stale = covered > last_covered ? 0 : stale + 1;
				last_covered = covered;
				if (stale >= 4) {
					fprintf(out, "spetest: no new SPE/test pair in %u rounds, giving up on the rest\n", stale);
					break;
				}
			}
			/* each context gets the test that most SPEs still lack, distinct tests where
			 * possible: the kernel decides which SPE a context lands on, so this is a
			 * coupon collector's game, played greedily */
			for (w = 0; w < cur; w++) {
				int pick = -1;
				double best = -1;
				unsigned q;

				for (q = 0; q < ntl; q++) {
					unsigned lack = 0;
					double sc;

					t = tl[(q + round + w) % ntl];
					for (p = 0; p < nlist; p++)
						if (cv.c[t][spe_list[p]].runs < cfg.rounds)
							lack++;
					sc = (double)lack / (1 + taken[t]);
					if (sc > best) {
						best = sc;
						pick = (int)t;
					}
				}
				taken[pick]++;
				tests[w] = pick;
			}
		}
		before = R.njobs;
		run_round(&R, tests, cur);
		round++;
		{
			unsigned pass = 0, w2, loaded = 0;
			struct temps tt;

			for (w2 = 0; w2 < cur; w2++) {
				pass += R.jobs[before + w2].outcome == R_PASS;
				loaded += R.jobs[before + w2].outcome != R_NOLOAD;
			}
			if (loaded < cur && loaded > 0)
				cur = loaded;	/* some SPEs are held by others: ask for fewer */
			fprintf(out, "round %u:", round);
			for (w2 = 0; w2 < R.njobs - before; w2++) {
				const struct spt_job *j = &R.jobs[before + w2];

				fprintf(out, " %s@%d%s", test_name[j->test], j->phys, j->outcome == R_PASS ? "" : j->outcome == R_NOLOAD ? "?" : "!");
			}
			fprintf(out, "  (%u/%u ok)", pass, R.njobs - before);
			if (R.have_hw && !temps_read(R.hwdir, &tt)) {
				fprintf(out, "  max %.1f C", temps_max(&tt) / 1000.0);
				if (temps_max(&tt) > R.max_temp_seen)
					R.max_temp_seen = temps_max(&tt);
				if (temps_max(&tt) >= cfg.temp_limit * 1000 && cfg.stress_min) {
					fprintf(out, "  LIMIT");
					R.abort_run = 1;
				}
			}
			fprintf(out, "\n");
			fflush(out);
		}
	}
	{
		unsigned i, k;

		for (i = 0; i < R.njobs; i++) {
			int ph = R.jobs[i].phys;

			if (ph < 0 || ph >= MAXPHYS)
				continue;
			for (k = 0; k < nlist; k++)
				if (spe_list[k] == (unsigned)ph)
					break;
			if (k == nlist && nlist < MAXPHYS)
				spe_list[nlist++] = (unsigned)ph;	/* an SPE sysfs did not list */
		}
	}
	if (R.have_hw) {
		struct mon *m = R.mon;

		m->stop = 1;
		pthread_join(m->th, NULL);
	}
	rc = report(&R, out, spe_list, nlist, cfg.stress_min ? 0 : cfg.rounds);
	if (have_t && !temps_read(R.hwdir, &t1)) {
		temps_print(out, "at end", &t1);
		if (R.max_temp_seen)
			fprintf(out, "hottest reading during the run: %.1f C\n", R.max_temp_seen / 1000.0);
	}
	if (R.abort_run) {
		fprintf(out, "spetest: stopped at the %d C temperature limit\n", cfg.temp_limit);
		rc = 4;
	}
	fprintf(out, "RESULT: %s\n", rc == 0 ? "PASS" : rc == 1 ? "FAIL" : rc == 3 ? "INCOMPLETE" : "STOPPED");
	{
		unsigned i;
		int abandoned = 0;

		for (i = 0; i < R.njobs; i++)
			abandoned |= R.jobs[i].outcome == R_HUNG;
		if (!abandoned)	/* (a context that would not die may still DMA from src) */
			free(raw_src);
		free(R.jobs);
	}
	return rc;
}
