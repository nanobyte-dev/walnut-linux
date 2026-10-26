// SPDX-License-Identifier: GPL-2.0
/*
 * Equivalence fuzz for shadow.c's fast replay (shadow_fast=1) against the word-by-word one:
 * the whole struct ps3rsx_shadow after every burst, push, restore and take, and the restore
 * streams, must be byte-identical. Built in userspace with inc/ (kernel stand-ins).
 *
 *   shadow-equiv [bursts] [submits] [seed]	fuzz (defaults 4000000 400000 1)
 *   shadow-equiv bench [pushes]		time both paths on Mesa-like pushes
 *   shadow-equiv count <fast> <kind> <reps>	one kind of traffic, for count.sh
 *   shadow-equiv rcount <fast> <reps>		restores, for count.sh
 * Built by the Makefile here (x86 with ASan/UBSan, plain, and ppc64 for qemu-ppc64-static).
 */
#include "shim.h"
#define PS3RSX_H	/* shadow.c's driver header: not in userspace */
#define RESTORE_MAX_WORDS	(16 * 1024)	/* as in ps3rsx.h */
#ifndef SHADOW_C
#define SHADOW_C "../src/shadow.c"
#endif
#include SHADOW_C

#include <time.h>

static u64 rng_state;

static u32 rnd(void)
{
	u64 x = rng_state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rng_state = x;
	return (x * 0x2545F4914F6CDD1DULL) >> 32;
}

static u32 below(u32 n) { return n ? rnd() % n : 0; }
static bool chance(u32 pct) { return below(100) < pct; }

static u32 value(void)
{
	switch (below(4)) {
	case 0: return below(4);		/* equal values make the restore skip */
	case 1: return below(1024);
	default: return rnd();
	}
}

/* A pointer for the upload windows: mostly in range, sometimes past it, rarely near 2^32 */
static u32 upload_ptr(u32 nslots)
{
	switch (below(20)) {
	case 0: return 0xffffffffu - below(4);
	case 1: return nslots - below(3);
	case 2: return nslots + below(8);
	case 3: return rnd();
	default: return below(nslots);
	}
}

static u32 handle(void)
{
	if (chance(80))
		return obj_handle[below(NOBJ)];
	return chance(50) ? 0 : 0x31337000 + below(16);
}

/* Interesting 3D methods: window edges, commands, pointers */
static u32 method3d(void)
{
	static const u32 pts[] = {
		0x100, 0x104, 0x110, 0x180, 0x200, 0x208, 0x300, 0x1710, 0x1718, 0x1740, 0x177c,
		0x17c8, 0x1800, 0x1804, 0x1808, 0x1810, 0x1818, 0x1824, 0x1828, 0x1880, 0x1900,
		0x1940, 0x1980, 0x1a00, 0x1b00, 0x1c00, 0x1d70, 0x1d94, 0x1e40, 0x1e94, 0x1e98,
		0x1e9c, 0x1ea0, 0x1ef8, 0x1efc, 0x1f00, 0x1f7c, 0x1f80, 0x1fd8, 0x1ffc,
		0x0b78, 0x0b7c, 0x0b80, 0x0bf0, 0x0bfc, 0x0c00, 0x1500, 0x1508,
	};
	u32 m;

	if (chance(50))
		m = pts[below(sizeof(pts) / 4)] + 4 * (below(9) - 4);
	else
		m = 0x100 + 4 * below((0x2000 - 0x100) / 4);
	if (m < 0x100 || m > 0x1ffc)
		m = 0x100;
	return m;
}

static u32 method2d(void)
{
	static const u32 pts[] = { 0x100, 0x104, 0x180, 0x184, 0x198, 0x300, 0x328, 0x32c,
				   0x400, 0x408, 0x40c, 0x410, 0x414 };

	if (chance(70))
		return pts[below(sizeof(pts) / 4)] - 4 * below(4);
	return 0x100 + 4 * below((0x2000 - 0x100) / 4);
}

#define MAXPUSH 8192

/* One burst appended at w: returns the words written (header + data) */
static u32 gen_burst(u32 *w, u32 room)
{
	u32 subch = chance(70) ? below(3) : below(8), m, n, i;
	bool noninc = false;
	u32 *d = w + 1;

	switch (below(16)) {
	case 0:		/* bind, sometimes as the start of a longer burst */
		m = 0;
		n = chance(85) ? 1 : 1 + below(80);
		noninc = n > 1 && chance(30);
		for (i = 0; i < n; i++)
			d[i] = i == 0 || noninc ? handle() : value();
		break;
	case 1:		/* host methods, sometimes running into 0x100 */
		m = 4 * below(0x100 / 4);
		n = 1 + below(chance(20) ? 80 : 4);
		noninc = chance(30);
		for (i = 0; i < n; i++)
			d[i] = value();
		break;
	case 2:		/* vertex program load pointer, then the program */
		m = 0x1e9c;
		n = 1;
		d[0] = upload_ptr(VP_SLOTS);
		break;
	case 3: case 4:	/* vertex program words */
		m = chance(80) ? 0xb80 : 0xb80 + 4 * below(32);
		n = chance(60) ? 4 * (1 + below(8)) : 1 + below(48);
		noninc = chance(20);
		for (i = 0; i < n; i++)
			d[i] = value();
		break;
	case 5: case 6:	/* constants: pointer and values in one burst, as Mesa does */
		m = chance(70) ? 0x1efc : 0x1f00 + 4 * below(33) - 4;
		n = chance(60) ? 1 + 4 * (1 + below(8)) : 1 + below(40);
		noninc = chance(10);
		for (i = 0; i < n; i++)
			d[i] = value();
		if (m == 0x1efc)
			d[0] = upload_ptr(CONST_SLOTS);
		break;
	case 7:		/* draws: begin/end, inline data, noninc */
		m = chance(50) ? 0x1808 : 0x1818;
		n = 1 + below(m == 0x1818 ? 300 : 2);
		noninc = m == 0x1818;
		for (i = 0; i < n; i++)
			d[i] = value();
		break;
	case 8: case 9:	/* copy/2D methods */
		m = method2d();
		n = chance(10) ? 1 + below(300) : 1 + below(12);
		noninc = chance(15);
		for (i = 0; i < n; i++)
			d[i] = value();
		break;
	case 10:	/* count 0, NOP header */
		if (chance(50)) {
			w[0] = 0;
			return 1;
		}
		m = method3d();
		n = 0;
		break;
	default:	/* 3D state runs, short mostly; anything noninc */
		m = method3d();
		n = chance(5) ? 1 + below(600) : 1 + below(10);
		noninc = chance(10);
		for (i = 0; i < n && i < 2047; i++)
			d[i] = value();
		break;
	}
	if (n > 2047)
		n = 2047;
	if (!noninc && n && m + 4 * (n - 1) > 0x1ffc)
		n = (0x2000 - m) / 4;			/* the validator refuses those */
	if (1 + n > room)
		n = room - 1;
	w[0] = (noninc ? 0x40000000 : 0) | n << 18 | subch << 13 | m;
	return 1 + n;
}

static u32 gen_push(u32 *w, u32 max)
{
	u32 nb = 1 + below(40), i = 0;

	while (nb-- && i + 2 < max)	/* buffers have 4096 words of slack past max */
		i += gen_burst(w + i, max - i);
	return i;
}

static void set_fast(bool on) { *test_param_shadow_fast = on; }

static unsigned long nfail;

static void check(const struct ps3rsx_shadow *a, const struct ps3rsx_shadow *b,
		  const char *what, u64 it)
{
	if (!memcmp(a, b, sizeof(*a)))
		return;
	{
		const u8 *x = (const void *)a, *y = (const void *)b;
		size_t i;

		for (i = 0; i < sizeof(*a) && x[i] == y[i]; i++)
			;
		fprintf(stderr, "MISMATCH %s at case %llu: byte %zu (%02x vs %02x)\n", what,
			(unsigned long long)it, i, x[i], y[i]);
	}
	if (++nfail > 5)
		exit(1);
}

/* Part 1: single bursts and pushes into one shadow pair */
static void fuzz_bursts(u64 cases)
{
	struct ps3rsx_shadow *a = ps3rsx_shadow_new(), *b = ps3rsx_shadow_new();
	static u32 w[MAXPUSH + 4096];
	u64 it;

	for (it = 0; it < cases; it++) {
		u32 n, h;

		if (chance(1) && chance(10)) {
			ps3rsx_shadow_clear(a);
			ps3rsx_shadow_clear(b);
		}
		if (chance(90)) {
			n = gen_burst(w, 2048);
			h = w[0];
			set_fast(false);
			ps3rsx_shadow_burst(a, (h >> 13) & 7, h & 0x1ffc, h & 0x40000000, &w[1],
					    (h >> 18) & 0x7ff);
			set_fast(true);
			ps3rsx_shadow_burst(b, (h >> 13) & 7, h & 0x1ffc, h & 0x40000000, &w[1],
					    (h >> 18) & 0x7ff);
			(void)n;
		} else {
			n = gen_push(w, MAXPUSH);
			set_fast(false);
			ps3rsx_shadow_push(a, w, n);
			set_fast(true);
			ps3rsx_shadow_push(b, w, n);
		}
		check(a, b, "burst", it);
	}
	ps3rsx_shadow_free(a);
	ps3rsx_shadow_free(b);
}

/* Part 2: submits on several channels as submit.c runs them: restore, take_restored, pushes,
 * take at the next sync; also kernel copies (shadow_write into hw) and forgetting the GPU */
#define NCH 3

struct world {
	struct ps3rsx_shadow *ch[NCH], *hw;
	int behind, last;
	u32 buf[RESTORE_MAX_WORDS];
	int nres;
};

static void world_init(struct world *wd)
{
	int i;

	for (i = 0; i < NCH; i++)
		wd->ch[i] = ps3rsx_shadow_new();
	wd->hw = ps3rsx_shadow_new();
	wd->behind = wd->last = -1;
}

static void sync(struct world *wd)
{
	if (wd->behind >= 0) {
		ps3rsx_shadow_take(wd->ch[wd->behind], wd->hw);
		wd->behind = -1;
	}
}

static void submit(struct world *wd, bool fast, int c, const u32 *push, u32 n, u32 max_words)
{
	bool switched = wd->last != c;

	set_fast(fast);
	wd->nres = 0;
	memset(wd->buf, 0xa5, sizeof(wd->buf));
	if (switched) {
		sync(wd);
		wd->nres = ps3rsx_shadow_restore(wd->ch[c], wd->hw, wd->buf, max_words);
		if (wd->nres < 0)
			return;			/* the submit fails, nothing changes */
	}
	if (wd->behind != c)
		sync(wd);
	if (switched)
		ps3rsx_shadow_take_restored(wd->ch[c], wd->hw, wd->buf, wd->nres);
	ps3rsx_shadow_push(wd->ch[c], push, n);
	wd->behind = c;
	wd->last = c;
}

static void kcopy(struct world *wd, bool fast, const u32 *v, u32 n, u32 m)
{
	u32 i;

	set_fast(fast);
	sync(wd);
	for (i = 0; i < n; i++)
		ps3rsx_shadow_write(wd->hw, 7, m ? m + 4 * i : 0, v[i]);
}

static void forget(struct world *wd, bool fast)
{
	set_fast(fast);
	sync(wd);
	ps3rsx_shadow_clear(wd->hw);
}

static void check_worlds(struct world *a, struct world *b, u64 it)
{
	int i;

	if (a->nres != b->nres || memcmp(a->buf, b->buf, sizeof(a->buf))) {
		fprintf(stderr, "MISMATCH restore at case %llu: %d vs %d words\n",
			(unsigned long long)it, a->nres, b->nres);
		if (++nfail > 5)
			exit(1);
	}
	if (a->behind != b->behind || a->last != b->last) {
		fprintf(stderr, "MISMATCH world at case %llu\n", (unsigned long long)it);
		exit(1);
	}
	check(a->hw, b->hw, "hw", it);
	for (i = 0; i < NCH; i++)
		check(a->ch[i], b->ch[i], "ch", it);
}

static u64 restores, restore_words, overflows;

static void fuzz_submits(u64 cases)
{
	static struct world A, B;
	static u32 w[MAXPUSH + 4096];
	u64 it;

	world_init(&A);
	world_init(&B);
	for (it = 0; it < cases; it++) {
		u32 r = below(100);

		if (r < 2) {
			forget(&A, false);
			forget(&B, true);
		} else if (r < 6) {
			/* a kernel copy: bind the M2MF on subchannel 7, set its methods */
			u32 v[8], i, n = 1 + below(8), m = chance(30) ? 0 : 0x300 + 4 * below(16);

			for (i = 0; i < n; i++)
				v[i] = i == 0 && !m ? obj_handle[OBJ_M2MF_IN] : value();
			kcopy(&A, false, v, n, m);
			kcopy(&B, true, v, n, m);
		} else {
			int c = chance(60) && A.last >= 0 ? A.last : (int)below(NCH);
			u32 n = gen_push(w, MAXPUSH);
			u32 max = chance(3) ? below(600) : RESTORE_MAX_WORDS;

			submit(&A, false, c, w, n, max);
			submit(&B, true, c, w, n, max);
			if (A.last == c && A.nres > 0) {
				restores++;
				restore_words += A.nres;
			}
			if (A.nres < 0)
				overflows++;
		}
		check_worlds(&A, &B, it);
	}
}

/* --- microbenchmark: Mesa-like pushes (state runs, constants, a VP now and then, draws) --- */

static u32 gen_mesa_push(u32 *w)
{
	u32 i = 0, k, j;

	w[i++] = 1 << 18 | 0 << 13 | 0;			/* bind 3D on subch 0 */
	w[i++] = obj_handle[OBJ_3D];
	w[i++] = 1 << 18 | 1 << 13 | 0;			/* a 2D object on subch 1 */
	w[i++] = obj_handle[OBJ_SIFM];
	for (k = 0; k < 40; k++) {
		u32 n = 1 + below(6), m = 0x200 + 4 * below(0x500 / 4);

		w[i++] = n << 18 | m;
		for (j = 0; j < n; j++)
			w[i++] = rnd();
		if (k % 8 == 0) {			/* constants */
			w[i++] = 17 << 18 | 0x1efc;
			w[i++] = below(CONST_SLOTS - 4);
			for (j = 0; j < 16; j++)
				w[i++] = rnd();
		}
		if (k % 20 == 0) {			/* a vertex program */
			w[i++] = 1 << 18 | 0x1e9c;
			w[i++] = below(VP_SLOTS - 8);
			w[i++] = 32 << 18 | 0xb80;
			for (j = 0; j < 32; j++)
				w[i++] = rnd();
		}
		if (k % 10 == 0) {			/* a 2D blit setup */
			w[i++] = 8 << 18 | 1 << 13 | 0x300;
			for (j = 0; j < 8; j++)
				w[i++] = rnd();
		}
		w[i++] = 1 << 18 | 0x1808;
		w[i++] = 5;
		w[i++] = 0x40000000 | 4 << 18 | 0x1814;
		for (j = 0; j < 4; j++)
			w[i++] = rnd();
		w[i++] = 1 << 18 | 0x1808;
		w[i++] = 0;
	}
	return i;
}

/* One push of one kind of traffic only, for counting instructions per word (count mode) */
static u32 gen_kind_push(u32 *w, int kind, u32 *payload)
{
	u32 i = 0, k, j, n;

	w[i++] = 1 << 18 | 0 << 13 | 0;
	w[i++] = obj_handle[OBJ_3D];
	w[i++] = 1 << 18 | 1 << 13 | 0;
	w[i++] = obj_handle[OBJ_SIFM];
	*payload = 0;
	for (k = 0; k < 100; k++) {
		switch (kind) {
		case 1:		/* 2D: an 8-method run to the SIFM */
			w[i++] = 8 << 18 | 1 << 13 | 0x300;
			n = 8;
			break;
		case 2:		/* a vertex program: pointer, 8 slots */
			w[i++] = 1 << 18 | 0x1e9c;
			w[i++] = below(VP_SLOTS - 8);
			w[i++] = 32 << 18 | 0xb80;
			n = 32;
			break;
		case 3:		/* constants: pointer and 4 slots in one burst */
			w[i++] = 17 << 18 | 0x1efc;
			w[i++] = below(CONST_SLOTS - 4);
			n = 16;
			break;
		default:	/* 3D state runs of 1-6 methods */
			n = 1 + below(6);
			w[i++] = n << 18 | (0x200 + 4 * below(0x500 / 4));
			break;
		}
		for (j = 0; j < n; j++)
			w[i++] = rnd();
		*payload += n;
	}
	return i;
}

/* count <fast> <kind> <reps>: replay one kind's pushes (for instruction counting in qemu) */
static void count(bool fast, int kind, u32 reps)
{
	struct ps3rsx_shadow *sh = ps3rsx_shadow_new();
	static u32 w[MAXPUSH + 4096];
	u32 n, payload, r;

	n = gen_kind_push(w, kind, &payload);
	set_fast(fast);
	for (r = 0; r < reps; r++)
		ps3rsx_shadow_push(sh, w, n);
	printf("kind %d: %u payload words x %u\n", kind, payload, reps);
	ps3rsx_shadow_free(sh);
}

/* rcount <fast> <reps>: restores between two channels whose state mostly matches */
static void rcount(bool fast, u32 reps)
{
	struct ps3rsx_shadow *ch = ps3rsx_shadow_new(), *hw = ps3rsx_shadow_new();
	static u32 w[MAXPUSH + 4096], buf[RESTORE_MAX_WORDS];
	u32 n, payload, r;
	int kind, words = 0;

	for (kind = 0; kind < 4; kind++) {
		n = gen_kind_push(w, kind, &payload);
		ps3rsx_shadow_push(ch, w, n);
	}
	memcpy(hw, ch, sizeof(*ch));
	n = gen_kind_push(w, 0, &payload);	/* the other channel's state writes */
	ps3rsx_shadow_push(hw, w, 40);
	set_fast(fast);
	for (r = 0; r < reps; r++)
		words = ps3rsx_shadow_restore(ch, hw, buf, RESTORE_MAX_WORDS);
	printf("restore: %d words, 3d regs set %u\n", words, bitmap_weight(ch->reg3d_set, NREG));
}

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static void bench(u32 pushes)
{
	struct ps3rsx_shadow *sh = ps3rsx_shadow_new();
	static u32 w[64][MAXPUSH + 4096];
	u32 len[64], p, words = 0;
	int f;

	for (p = 0; p < 64; p++) {
		len[p] = gen_mesa_push(w[p]);
		words += len[p];
	}
	for (f = 0; f < 2; f++) {
		double t0, t;

		set_fast(f);
		t0 = now();
		for (p = 0; p < pushes; p++)
			ps3rsx_shadow_push(sh, w[p % 64], len[p % 64]);
		t = now() - t0;
		printf("%s: %.2f ns/word\n", f ? "fast" : "slow",
		       t * 1e9 / ((double)pushes * words / 64));
	}
	ps3rsx_shadow_free(sh);
}

int main(int argc, char **argv)
{
	u64 bursts = 4000000, submits = 400000;

	ps3rsx_shadow_init();
	if (argc > 3 && !strcmp(argv[1], "rcount")) {
		rng_state = 1;
		rcount(atoi(argv[2]), atoi(argv[3]));
		return 0;
	}
	if (argc > 4 && !strcmp(argv[1], "count")) {
		rng_state = 1;
		count(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "bench")) {
		rng_state = 1;
		bench(argc > 2 ? strtoul(argv[2], NULL, 0) : 20000);
		return 0;
	}
	if (argc > 1)
		bursts = strtoull(argv[1], NULL, 0);
	if (argc > 2)
		submits = strtoull(argv[2], NULL, 0);
	rng_state = argc > 3 ? strtoull(argv[3], NULL, 0) : 1;
	if (!rng_state)
		rng_state = 1;
	fuzz_bursts(bursts);
	fuzz_submits(submits);
	printf("%llu bursts/pushes, %llu submits (%llu restores, %llu words, %llu overflowed): "
	       "%s\n", (unsigned long long)bursts, (unsigned long long)submits,
	       (unsigned long long)restores, (unsigned long long)restore_words,
	       (unsigned long long)overflows, nfail ? "MISMATCH" : "identical");
	return nfail ? 1 : 0;
}
