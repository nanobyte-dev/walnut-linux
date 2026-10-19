#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../ref.h"
#include "spusim.h"

#define LSLR	0x3FFFFu

typedef struct { uint32_t w[4]; } qw;

enum fmt { F_RRR, F_RI18, F_RI10, F_RI16, F_RI8, F_RI7, F_RR, F_LBT, F_LBTI };
enum mn {
#define OPC(tag, f, op, name) tag,
#include "spu-opc.inc"
#undef OPC
	M_COUNT
};

static const struct { const char *name; int fmt; unsigned op; } optab[] = {
#define OPC(tag, f, op, name) { name, f, op },
#include "spu-opc.inc"
#undef OPC
};

static int16_t dtab[2048];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void init_tab(void)
{
	static const int bits[] = { [F_RRR] = 4, [F_RI18] = 7, [F_RI10] = 8, [F_RI16] = 9, [F_RI8] = 10,
				    [F_RI7] = 11, [F_RR] = 11, [F_LBT] = 7, [F_LBTI] = 11 };
	unsigned i, k;

	for (i = 0; i < 2048; i++)
		dtab[i] = -1;
	for (i = 0; i < M_COUNT; i++) {
		int b = bits[optab[i].fmt];
		unsigned prefix = optab[i].op >> (11 - b), n = 1u << (11 - b);

		for (k = 0; k < n; k++) {
			unsigned idx = (prefix << (11 - b)) | k;

			if (dtab[idx] < 0)
				dtab[idx] = (int16_t)i;
		}
	}
}

struct sim_spe {
	uint8_t ls[LSLR + 1];
	qw r[128];
	uint32_t pc;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	uint32_t inq[4];
	int inn;
	uint32_t outm, outi;
	int outv, outiv;
	uint32_t sig[2];
	int sigv[2], sigor[2];
	volatile int intr;
	int stopped;
	uint32_t evmask, evack_pending;
	uint32_t dec;
	struct timespec dec_t0;
	int dec_on, dec_event_armed;
	uint32_t tagmask, tagupd, tagstat_ready;
	uint32_t lsa, eah, eal, size, tag;
	uint32_t atomstat;
	uint8_t resv[128];
	uint64_t resv_ea;
	int resv_valid;
	unsigned event;
	uint64_t icount;
	unsigned tb_hz;
	unsigned dma_n;
	struct sim_fault f;
	uint32_t srr0;
};

static inline uint32_t ld32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline void st32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (uint8_t)v;
}

static inline qw ldq(const uint8_t *p)
{
	qw q = { { ld32(p), ld32(p + 4), ld32(p + 8), ld32(p + 12) } };

	return q;
}

static inline void stq(uint8_t *p, qw q)
{
	st32(p, q.w[0]);
	st32(p + 4, q.w[1]);
	st32(p + 8, q.w[2]);
	st32(p + 12, q.w[3]);
}

static inline uint8_t gb8(qw q, int i) { return (uint8_t)(q.w[i >> 2] >> (24 - 8 * (i & 3))); }
static inline void sb8(qw *q, int i, uint8_t v)
{
	int sh = 24 - 8 * (i & 3);

	q->w[i >> 2] = (q->w[i >> 2] & ~(0xFFu << sh)) | ((uint32_t)v << sh);
}
static inline uint16_t gh(qw q, int i) { return (uint16_t)(q.w[i >> 1] >> (16 - 16 * (i & 1))); }
static inline void sh16(qw *q, int i, uint16_t v)
{
	int sh = 16 - 16 * (i & 1);

	q->w[i >> 1] = (q->w[i >> 1] & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
}
static inline uint64_t gd(qw q, int i) { return (uint64_t)q.w[2 * i] << 32 | q.w[2 * i + 1]; }
static inline void sd(qw *q, int i, uint64_t v) { q->w[2 * i] = (uint32_t)(v >> 32); q->w[2 * i + 1] = (uint32_t)v; }
static inline double gdbl(qw q, int i) { uint64_t u = gd(q, i); double d; memcpy(&d, &u, 8); return d; }
static inline void sdbl(qw *q, int i, double d) { uint64_t u; memcpy(&u, &d, 8); sd(q, i, u); }
static inline float gf(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
static inline uint32_t fw(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }

typedef unsigned __int128 u128;
static inline u128 toq(qw q) { return (u128)q.w[0] << 96 | (u128)q.w[1] << 64 | (u128)q.w[2] << 32 | q.w[3]; }
static inline qw fromq(u128 v)
{
	qw q = { { (uint32_t)(v >> 96), (uint32_t)(v >> 64), (uint32_t)(v >> 32), (uint32_t)v } };

	return q;
}

static inline int32_t sx(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

static void splat(qw *q, uint32_t v) { q->w[0] = q->w[1] = q->w[2] = q->w[3] = v; }

static long now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000L + t.tv_nsec;
}

static uint32_t dec_now(struct sim_spe *s)
{
	long el = now_ns() - (s->dec_t0.tv_sec * 1000000000L + s->dec_t0.tv_nsec);
	unsigned long long ticks = (unsigned long long)el * s->tb_hz / 1000000000ULL;

	if (s->f.dec_div)
		ticks /= s->f.dec_div;
	return s->dec - (uint32_t)ticks;
}

/* ---- construction ---- */
struct sim_spe *sim_new(const uint8_t *elf, size_t size, uint64_t r3, uint64_t r4, uint64_t r5,
			const struct sim_fault *f, unsigned tb_hz)
{
	struct sim_spe *s = calloc(1, sizeof(*s));
	const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
	const Elf32_Phdr *ph;
	unsigned i;

	pthread_once(&once, init_tab);
	if (!s)
		return NULL;
	pthread_mutex_init(&s->mu, NULL);
	pthread_cond_init(&s->cv, NULL);
	if (f)
		s->f = *f;
	else
		s->f.ls_addr = s->f.reg_stuck = -1;
	s->tb_hz = tb_hz ? tb_hz : 79800000;
	if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG)) {
		free(s);
		return NULL;
	}
	ph = (const Elf32_Phdr *)(elf + ld32((const uint8_t *)&eh->e_phoff));
	for (i = 0; i < ((unsigned)elf[44] << 8 | elf[45]); i++) {
		const uint8_t *p = (const uint8_t *)&ph[i];
		uint32_t type = ld32(p), off = ld32(p + 4), va = ld32(p + 8), fsz = ld32(p + 16), msz = ld32(p + 20);

		if (type != 1 || !msz)
			continue;
		if (va + msz > LSLR + 1u || off + fsz > size) {
			free(s);
			return NULL;
		}
		memcpy(s->ls + va, elf + off, fsz);
	}
	s->pc = ld32((const uint8_t *)&eh->e_entry);
	s->r[3].w[0] = (uint32_t)(r3 >> 32);
	s->r[3].w[1] = (uint32_t)r3;
	s->r[4].w[0] = (uint32_t)(r4 >> 32);
	s->r[4].w[1] = (uint32_t)r4;
	s->r[5].w[0] = (uint32_t)(r5 >> 32);
	s->r[5].w[1] = (uint32_t)r5;
	return s;
}

void sim_set_fault(struct sim_spe *s, const struct sim_fault *f) { s->f = *f; }
void sim_free(struct sim_spe *s) { free(s); }
uint64_t sim_icount(const struct sim_spe *s) { return s->icount; }

void sim_interrupt(struct sim_spe *s)
{
	pthread_mutex_lock(&s->mu);
	s->intr = 1;
	pthread_cond_broadcast(&s->cv);
	pthread_mutex_unlock(&s->mu);
}

int sim_mbox_put(struct sim_spe *s, uint32_t w)
{
	int rc = -EAGAIN;

	pthread_mutex_lock(&s->mu);
	if (s->inn < 4) {
		s->inq[s->inn++] = w;
		rc = 0;
		pthread_cond_broadcast(&s->cv);
	}
	pthread_mutex_unlock(&s->mu);
	return rc;
}

int sim_mbox_get(struct sim_spe *s, uint32_t *w)
{
	int rc = -EAGAIN;

	pthread_mutex_lock(&s->mu);
	if (s->outv) {
		*w = s->outm;
		s->outv = 0;
		rc = 0;
		pthread_cond_broadcast(&s->cv);
	}
	pthread_mutex_unlock(&s->mu);
	return rc;
}

int sim_ibox_get(struct sim_spe *s, uint32_t *w)
{
	int rc = -EAGAIN;

	pthread_mutex_lock(&s->mu);
	if (s->outiv) {
		*w = s->outi;
		s->outiv = 0;
		rc = 0;
		pthread_cond_broadcast(&s->cv);
	}
	pthread_mutex_unlock(&s->mu);
	return rc;
}

void sim_signal(struct sim_spe *s, int which, uint32_t v)
{
	int i = which - 1;

	pthread_mutex_lock(&s->mu);
	s->sig[i] = (s->sigor[i] && s->sigv[i]) ? (s->sig[i] | v) : v;
	s->sigv[i] = 1;
	pthread_cond_broadcast(&s->cv);
	pthread_mutex_unlock(&s->mu);
}

void sim_signal_type(struct sim_spe *s, int which, int or_mode)
{
	s->sigor[which - 1] = or_mode;
}

/* ---- channels ---- */
#define CH_EVSTAT 0
#define CH_EVMASK_W 1
#define CH_EVACK 2
#define CH_SIG1 3
#define CH_SIG2 4
#define CH_WRDEC 7
#define CH_RDDEC 8
#define CH_MSSYNC 9
#define CH_EVMASK_R 11
#define CH_TAGMASK_R 12
#define CH_MACHSTAT 13
#define CH_SRR0_W 14
#define CH_SRR0_R 15
#define CH_LSA 16
#define CH_EAH 17
#define CH_EAL 18
#define CH_SIZE 19
#define CH_TAGID 20
#define CH_CMD 21
#define CH_TAGMASK_W 22
#define CH_TAGUPD 23
#define CH_TAGSTAT 24
#define CH_LISTSTALL 25
#define CH_LISTACK 26
#define CH_ATOMSTAT 27
#define CH_OUTMBOX 28
#define CH_INMBOX 29
#define CH_OUTINTR 30

#define EV_TM 0x20u

static uint32_t ev_status(struct sim_spe *s)
{
	uint32_t e = s->evack_pending;

	if (s->dec_on && s->dec_event_armed) {
		/* the decrementer went through zero since it was set */
		long el = now_ns() - (s->dec_t0.tv_sec * 1000000000L + s->dec_t0.tv_nsec);
		unsigned long long ticks = (unsigned long long)el * s->tb_hz / 1000000000ULL;

		if (s->f.dec_div)
			ticks /= s->f.dec_div;
		if (ticks > s->dec)
			e |= EV_TM;
	}
	if (s->sigv[0])
		e |= 0x200;
	if (s->sigv[1])
		e |= 0x100;
	if (s->inn < 4)
		e |= 0x10;
	if (!s->outv)
		e |= 0x80;
	if (!s->outiv)
		e |= 0x40;
	return e;
}

/* wait (mutex held) until cond() or an interrupt */
#define WAIT_UNTIL(s, cond) do { \
	while (!(cond) && !(s)->intr) { \
		struct timespec ts; \
		clock_gettime(CLOCK_REALTIME, &ts); \
		ts.tv_nsec += 2000000; \
		if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; } \
		pthread_cond_timedwait(&(s)->cv, &(s)->mu, &ts); \
	} } while (0)

static int dma_bad(struct sim_spe *s, uint32_t lsa, uint64_t ea, uint32_t size, int list)
{
	if (size == 0 && !list)
		return 0;
	if (size > 16384)
		return 1;
	if (size == 1 || size == 2 || size == 4 || size == 8) {
		if ((lsa & (size - 1)) || (ea & (size - 1)))
			return 1;
		if ((lsa & 15) != (ea & 15))
			return 1;
		return 0;
	}
	if (size % 16 || (lsa & 15) || (ea & 15))
		return 1;
	(void)s;
	return 0;
}

static void dma_copy(struct sim_spe *s, int put, uint32_t lsa, uint64_t ea, uint32_t size)
{
	uint8_t *host = (uint8_t *)(uintptr_t)ea;

	if (lsa + size > LSLR + 1u)
		size = LSLR + 1u - lsa;
	if (put) {
		memcpy(host, s->ls + lsa, size);
	} else {
		memcpy(s->ls + lsa, host, size);
		if (s->f.dma_flip && size && ++s->dma_n % (unsigned)s->f.dma_flip == 0)
			s->ls[lsa + size / 2] ^= 0x10;
	}
}

static void dma_error(struct sim_spe *s, uint32_t cmd, const char *why)
{
	if (getenv("SPT_SIM_DEBUG"))
		fprintf(stderr, "spusim: MFC error (%s): cmd 0x%x lsa 0x%x ea 0x%x%08x size %u tag %u\n", why, cmd, s->lsa,
			s->eah, s->eal, s->size, s->tag);
	s->event |= 8;
}

static void mfc_cmd(struct sim_spe *s, uint32_t cmd)
{
	uint32_t op = cmd & 0xFF, base = op & ~7u;
	uint64_t ea = (uint64_t)s->eah << 32 | s->eal;

	if (op == 0xD0) {	/* getllar */
		ea = ((uint64_t)s->eah << 32 | s->eal) & ~127ull;
		memcpy(s->resv, (void *)(uintptr_t)ea, 128);	/* one snapshot: the line is read atomically */
		memcpy(s->ls + (s->lsa & ~127u), s->resv, 128);
		s->resv_ea = ea;
		s->resv_valid = 1;
		s->atomstat |= 4;
	} else if (op == 0xB4) {	/* putllc */
		uint32_t *mem = (uint32_t *)(uintptr_t)(s->resv_ea);
		const uint8_t *nw = s->ls + (s->lsa & ~127u);
		int ok = s->resv_valid, i;

		ea = ((uint64_t)s->eah << 32 | s->eal) & ~127ull;
		if (ea != s->resv_ea)
			ok = 0;
		/* the reservation holds while the line is as it was when we read it */
		for (i = 0; ok && i < 32; i++) {
			uint32_t cur = __atomic_load_n(&mem[i], __ATOMIC_SEQ_CST);

			if (cur != (uint32_t)(((uint32_t *)s->resv)[i]))
				ok = 0;
		}
		for (i = 0; ok && i < 32; i++) {
			uint32_t was = ((uint32_t *)s->resv)[i], raw;

			/* the PPE's memory: we move raw bytes, whatever the host's byte order */
			memcpy(&raw, nw + 4 * i, 4);
			if (raw != was && !__atomic_compare_exchange_n(&mem[i], &was, raw, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
				ok = 0;
		}
		s->resv_valid = 0;
		s->atomstat |= ok ? 0 : 1;
	} else if (op == 0xB0 || op == 0xB8) {	/* putlluc, putqlluc */
		ea = ((uint64_t)s->eah << 32 | s->eal) & ~127ull;
		memcpy((void *)(uintptr_t)ea, s->ls + (s->lsa & ~127u), 128);
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (op == 0xB0)
			s->atomstat |= 2;
	} else if (base == 0x20 || base == 0x40) {
		int put = base == 0x20;

		if (op & 4) {	/* list */
			uint32_t list = s->eal, n = s->size / 8, i, ls = s->lsa;
			uint64_t hi = (uint64_t)s->eah << 32;

			if ((list & 7) || s->size % 8 || s->size > 16384) {
				dma_error(s, cmd, "list");
				return;
			}
			for (i = 0; i < n; i++) {
				uint32_t w0 = ld32(s->ls + ((list + 8 * i) & LSLR)), eal = ld32(s->ls + ((list + 8 * i + 4) & LSLR));
				uint32_t sz = w0 & 0x7FFF;

				if (dma_bad(s, ls, hi | eal, sz, 1)) {
					dma_error(s, cmd, "list element");
					return;
				}
				if (sz)
					dma_copy(s, put, ls, hi | eal, sz);
				ls += (sz + 15) & ~15u;
			}
		} else {
			if (dma_bad(s, s->lsa, ea, s->size, 0)) {
				dma_error(s, cmd, "alignment or size");
				return;
			}
			dma_copy(s, put, s->lsa, ea, s->size);
		}
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
	} else if (op == 0xC0 || op == 0xC8 || op == 0xCC) {
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
	} else {
		dma_error(s, cmd, "unknown command");
	}
}

/* returns 1 to continue, 0 when interrupted */
static int rdch(struct sim_spe *s, int ch, uint32_t *v)
{
	int ok = 1;

	pthread_mutex_lock(&s->mu);
	switch (ch) {
	case CH_EVSTAT:
		*v = ev_status(s) & s->evmask;
		/* poll: the decrementer event comes from the clock, not from a signal */
		while (!*v && !s->intr) {
			pthread_mutex_unlock(&s->mu);
			usleep(20);
			pthread_mutex_lock(&s->mu);
			*v = ev_status(s) & s->evmask;
		}
		break;
	case CH_SIG1:
	case CH_SIG2: {
		int i = ch - CH_SIG1;

		WAIT_UNTIL(s, s->sigv[i]);
		*v = s->sig[i];
		s->sigv[i] = 0;
		s->sig[i] = 0;
		break;
	}
	case CH_RDDEC:
		*v = dec_now(s);
		break;
	case CH_EVMASK_R:
		*v = s->evmask;
		break;
	case CH_TAGMASK_R:
		*v = s->tagmask;
		break;
	case CH_MACHSTAT:
		*v = 0;
		break;
	case CH_SRR0_R:
		*v = s->srr0;
		break;
	case CH_TAGSTAT:
		*v = s->tagmask;	/* every command has completed */
		break;
	case CH_LISTSTALL:
		*v = 0;
		break;
	case CH_ATOMSTAT:
		*v = s->atomstat;
		s->atomstat = 0;
		break;
	case CH_INMBOX:
		WAIT_UNTIL(s, s->inn > 0);
		if (s->inn) {
			*v = s->inq[0];
			memmove(s->inq, s->inq + 1, 3 * sizeof(uint32_t));
			s->inn--;
		}
		pthread_cond_broadcast(&s->cv);
		break;
	default:
		*v = 0;
	}
	if (s->intr && ch != CH_RDDEC)
		ok = (ch == CH_EVMASK_R || ch == CH_TAGMASK_R || ch == CH_TAGSTAT || ch == CH_ATOMSTAT || ch == CH_MACHSTAT ||
		      ch == CH_SRR0_R || ch == CH_LISTSTALL);
	pthread_mutex_unlock(&s->mu);
	return ok;
}

static int rchcnt(struct sim_spe *s, int ch)
{
	int n;

	pthread_mutex_lock(&s->mu);
	switch (ch) {
	case CH_EVSTAT: n = (ev_status(s) & s->evmask) != 0; break;
	case CH_SIG1: n = s->sigv[0]; break;
	case CH_SIG2: n = s->sigv[1]; break;
	case CH_INMBOX: n = s->inn; break;
	case CH_OUTMBOX: n = !s->outv; break;
	case CH_OUTINTR: n = !s->outiv; break;
	case CH_CMD: n = 16; break;
	case CH_TAGSTAT: n = 1; break;
	case CH_ATOMSTAT: n = s->atomstat != 0; break;
	case CH_TAGUPD: n = 1; break;
	default: n = 1;
	}
	pthread_mutex_unlock(&s->mu);
	return n;
}

static int wrch(struct sim_spe *s, int ch, uint32_t v)
{
	int ok = 1;

	switch (ch) {
	case CH_EVMASK_W: s->evmask = v; break;
	case CH_EVACK:
		if (v & EV_TM)
			s->dec_event_armed = 0;
		s->evack_pending &= ~v;
		break;
	case CH_WRDEC:
		s->dec = v;
		clock_gettime(CLOCK_MONOTONIC, &s->dec_t0);
		s->dec_on = 1;
		s->dec_event_armed = 1;
		break;
	case CH_SRR0_W: s->srr0 = v; break;
	case CH_LSA: s->lsa = v; break;
	case CH_EAH: s->eah = v; break;
	case CH_EAL: s->eal = v; break;
	case CH_SIZE: s->size = v & 0xFFFF; break;
	case CH_TAGID: s->tag = v & 31; break;
	case CH_CMD: mfc_cmd(s, v); break;
	case CH_TAGMASK_W: s->tagmask = v; break;
	case CH_TAGUPD: s->tagupd = v; break;
	case CH_LISTACK: break;
	case CH_MSSYNC: break;
	case CH_OUTMBOX:
		pthread_mutex_lock(&s->mu);
		WAIT_UNTIL(s, !s->outv);
		if (!s->outv) {
			s->outm = v;
			s->outv = 1;
		} else {
			ok = 0;
		}
		pthread_cond_broadcast(&s->cv);
		pthread_mutex_unlock(&s->mu);
		break;
	case CH_OUTINTR:
		pthread_mutex_lock(&s->mu);
		WAIT_UNTIL(s, !s->outiv);
		if (!s->outiv) {
			s->outi = v;
			s->outiv = 1;
		} else {
			ok = 0;
		}
		pthread_cond_broadcast(&s->cv);
		pthread_mutex_unlock(&s->mu);
		break;
	default:
		break;
	}
	return ok;
}

/* ---- the interpreter ---- */
static void sp_op3(uint32_t a, uint32_t b, uint32_t c, int *ok, uint32_t *out)
{
	*ok = spt_sp_fma(a, b, c, out);
}

static uint32_t spf(uint32_t a, uint32_t b, uint32_t c, int kind)
{
	uint32_t r;
	int ok;

	switch (kind) {
	case 0: sp_op3(a, 0x3F800000u, b, &ok, &r); break;			/* fa */
	case 1: sp_op3(a, 0x3F800000u, b ^ 0x80000000u, &ok, &r); break;	/* fs */
	case 2: sp_op3(a, b, 0, &ok, &r); break;				/* fm */
	case 3: sp_op3(a, b, c, &ok, &r); break;				/* fma */
	case 4: sp_op3(a, b, c ^ 0x80000000u, &ok, &r); break;		/* fms */
	default: sp_op3(a ^ 0x80000000u, b, c, &ok, &r); break;		/* fnms */
	}
	if (!ok) {	/* out of the model's range: the host's float */
		float x = gf(a), y = gf(b), z = gf(c);

		switch (kind) {
		case 0: r = fw(x + y); break;
		case 1: r = fw(x - y); break;
		case 2: r = fw(x * y); break;
		case 3: r = fw(fmaf(x, y, z)); break;
		case 4: r = fw(fmaf(x, y, -z)); break;
		default: r = fw(fmaf(-x, y, z)); break;
		}
	}
	return r;
}

#define EACHW(expr) for (i = 0; i < 4; i++) { uint32_t a = ra.w[i], b = rb.w[i], c = rc.w[i], t = rt.w[i]; (void)a; (void)b; (void)c; (void)t; out.w[i] = (expr); }
#define EACHH(expr) for (i = 0; i < 8; i++) { uint16_t a = gh(ra, i), b = gh(rb, i), t = gh(rt, i); (void)a; (void)b; (void)t; sh16(&out, i, (uint16_t)(expr)); }
#define EACHB(expr) for (i = 0; i < 16; i++) { uint8_t a = gb8(ra, i), b = gb8(rb, i); (void)a; (void)b; sb8(&out, i, (uint8_t)(expr)); }

long sim_run(struct sim_spe *s, unsigned *event)
{
	unsigned i;
	uint32_t *pcp = &s->pc;
	uint32_t pc = *pcp;
	static unsigned long trace = ~0ul, watch;

	if (trace == ~0ul)
		watch = getenv("SPT_SIM_WATCH") ? strtoul(getenv("SPT_SIM_WATCH"), NULL, 0) : 0;
	if (trace == ~0ul)
		trace = getenv("SPT_SIM_TRACE") ? strtoul(getenv("SPT_SIM_TRACE"), NULL, 0) : 0;
	*event = 0;
	if (s->f.hang) {
		pthread_mutex_lock(&s->mu);
		WAIT_UNTIL(s, 0);
		pthread_mutex_unlock(&s->mu);
		return -EINTR;
	}
	for (;;) {
		uint32_t insn, rtn, ran, rbn, rcn;
		int idx;
		qw ra, rb, rc, rt, out;
		int writes = 1, m;

		if (s->intr) {
			*pcp = pc;
			return -EINTR;
		}
		insn = ld32(s->ls + (pc & LSLR));
		idx = dtab[insn >> 21];
		s->icount++;
		if (idx < 0) {
			fprintf(stderr, "spusim: undecodable instruction 0x%08x at 0x%x\n", insn, pc);
			*pcp = pc;
			return 0x20;
		}
		m = idx;
		rtn = insn & 127;
		ran = (insn >> 7) & 127;
		rbn = (insn >> 14) & 127;
		rcn = (insn >> 21) & 127;
		if (optab[m].fmt == F_RRR) {	/* op | RT | RB | RA | RC */
			rtn = (insn >> 21) & 127;
			rcn = insn & 127;
		}
		ra = s->r[ran];
		rb = s->r[rbn];
		rc = s->r[rcn];
		rt = s->r[rtn];
		out = rt;
		{
			int32_t i7 = (insn >> 14) & 127, i10 = sx((insn >> 14) & 0x3FF, 10), i16 = (int16_t)((insn >> 7) & 0xFFFF);
			uint32_t u18 = (insn >> 7) & 0x3FFFF, next = pc + 4;
			uint32_t i8 = (insn >> 14) & 0xFF;
			int taken = 0;
			uint32_t target = 0;

			switch (m) {
			/* constants */
			case M_IL: splat(&out, (uint32_t)(int32_t)i16); break;
			case M_ILH: splat(&out, (uint32_t)(uint16_t)i16 * 0x10001u); break;
			case M_ILHU: splat(&out, (uint32_t)(uint16_t)i16 << 16); break;
			case M_ILA: splat(&out, u18); break;
			case M_IOHL: for (i = 0; i < 4; i++) out.w[i] = rt.w[i] | (uint16_t)i16; break;
			case M_FSMBI:
				for (i = 0; i < 16; i++)
					sb8(&out, i, (insn >> 7 & 0xFFFF) >> (15 - i) & 1 ? 0xFF : 0);
				break;
			/* loads and stores */
			case M_LQD: case M_LQX: case M_LQA: case M_LQR: {
				uint32_t addr = m == M_LQD ? ra.w[0] + (uint32_t)(i10 * 16) : m == M_LQX ? ra.w[0] + rb.w[0] :
					m == M_LQA ? (uint32_t)(i16 * 4) : pc + (uint32_t)(i16 * 4);

				addr &= LSLR & ~15u;
				out = ldq(s->ls + addr);
				if (s->f.ls_addr >= 0 && (unsigned)s->f.ls_addr >= addr && (unsigned)s->f.ls_addr < addr + 16) {
					unsigned b = (unsigned)s->f.ls_addr - addr;

					sb8(&out, b, gb8(out, b) | s->f.ls_or);
				}
				break;
			}
			case M_STQD: case M_STQX: case M_STQA: case M_STQR: {
				uint32_t addr = m == M_STQD ? ra.w[0] + (uint32_t)(i10 * 16) : m == M_STQX ? ra.w[0] + rb.w[0] :
					m == M_STQA ? (uint32_t)(i16 * 4) : pc + (uint32_t)(i16 * 4);

				stq(s->ls + (addr & LSLR & ~15u), rt);
				if (watch && (addr & LSLR & ~15u) == watch)
					fprintf(stderr, "WATCH store pc %05x  %08x %08x %08x %08x\n", pc, rt.w[0], rt.w[1], rt.w[2], rt.w[3]);
				writes = 0;
				break;
			}
			/* arithmetic */
			case M_A: EACHW(a + b); break;
			case M_AI: EACHW(a + (uint32_t)i10); break;
			case M_AH: EACHH(a + b); break;
			case M_AHI: EACHH(a + (uint16_t)i10); break;
			case M_SF: EACHW(b - a); break;
			case M_SFI: EACHW((uint32_t)i10 - a); break;
			case M_SFH: EACHH(b - a); break;
			case M_SFHI: EACHH((uint16_t)i10 - a); break;
			case M_ADDX: EACHW(a + b + (t & 1)); break;
			case M_CG: EACHW(((uint64_t)a + b) >> 32); break;
			case M_CGX: EACHW(((uint64_t)a + b + (t & 1)) >> 32); break;
			case M_BG: EACHW(b >= a ? 1 : 0); break;
			case M_BGX: EACHW((uint64_t)b >= (uint64_t)a + (1 - (t & 1)) ? 1 : 0); break;
			case M_SFX: EACHW(b + ~a + (t & 1)); break;
			case M_MPY: EACHW((uint32_t)((int32_t)(int16_t)a * (int32_t)(int16_t)b)); break;
			case M_MPYU: EACHW((a & 0xFFFF) * (b & 0xFFFF)); break;
			case M_MPYI: EACHW((uint32_t)((int32_t)(int16_t)a * i10)); break;
			case M_MPYUI: EACHW((a & 0xFFFF) * (uint16_t)i10); break;
			case M_MPYH: EACHW((uint32_t)((int32_t)(int16_t)(a >> 16) * (int32_t)(int16_t)b) << 16); break;
			case M_MPYS: EACHW((uint32_t)(((int32_t)(int16_t)a * (int32_t)(int16_t)b) >> 16)); break;
			case M_MPYHH: EACHW((uint32_t)((int32_t)(int16_t)(a >> 16) * (int32_t)(int16_t)(b >> 16))); break;
			case M_MPYHHU: EACHW((a >> 16) * (b >> 16)); break;
			case M_MPYHHA: EACHW(t + (uint32_t)((int32_t)(int16_t)(a >> 16) * (int32_t)(int16_t)(b >> 16))); break;
			case M_MPYHHAU: EACHW(t + (a >> 16) * (b >> 16)); break;
			case M_MPYA: EACHW((uint32_t)((int32_t)(int16_t)a * (int32_t)(int16_t)b) + c); break;
			case M_CLZ: EACHW(a ? (uint32_t)__builtin_clz(a) : 32); break;
			case M_CNTB: for (i = 0; i < 16; i++) sb8(&out, i, (uint8_t)__builtin_popcount(gb8(ra, i))); break;
			case M_XSBH: for (i = 0; i < 8; i++) sh16(&out, i, (uint16_t)(int16_t)(int8_t)gb8(ra, 2 * i + 1)); break;
			case M_XSHW: for (i = 0; i < 4; i++) out.w[i] = (uint32_t)(int32_t)(int16_t)gh(ra, 2 * i + 1); break;
			case M_XSWD:
				for (i = 0; i < 2; i++)
					sd(&out, i, (uint64_t)(int64_t)(int32_t)ra.w[2 * i + 1]);
				break;
			case M_ABSDB: EACHB(a > b ? a - b : b - a); break;
			case M_AVGB: EACHB((a + b + 1) >> 1); break;
			case M_SUMB:
				for (i = 0; i < 4; i++) {
					uint32_t sa = gb8(ra, 4 * i) + gb8(ra, 4 * i + 1) + gb8(ra, 4 * i + 2) + gb8(ra, 4 * i + 3);
					uint32_t sb = gb8(rb, 4 * i) + gb8(rb, 4 * i + 1) + gb8(rb, 4 * i + 2) + gb8(rb, 4 * i + 3);

					out.w[i] = sb << 16 | sa;
				}
				break;
			/* logic */
			case M_AND: EACHW(a & b); break;
			case M_ANDC: EACHW(a & ~b); break;
			case M_OR: EACHW(a | b); break;
			case M_ORC: EACHW(a | ~b); break;
			case M_XOR: EACHW(a ^ b); break;
			case M_NAND: EACHW(~(a & b)); break;
			case M_NOR: EACHW(~(a | b)); break;
			case M_EQV: EACHW(~(a ^ b)); break;
			case M_SELB: EACHW((a & ~c) | (b & c)); break;
			case M_ANDBI: EACHW(a & ((i10 & 0xFF) * 0x01010101u)); break;
			case M_ORBI: EACHW(a | ((i10 & 0xFF) * 0x01010101u)); break;
			case M_XORBI: EACHW(a ^ ((i10 & 0xFF) * 0x01010101u)); break;
			case M_ANDHI: EACHW(a & ((uint16_t)i10 * 0x10001u)); break;
			case M_ORHI: EACHW(a | ((uint16_t)i10 * 0x10001u)); break;
			case M_XORHI: EACHW(a ^ ((uint16_t)i10 * 0x10001u)); break;
			case M_ANDI: EACHW(a & (uint32_t)i10); break;
			case M_ORI: EACHW(a | (uint32_t)i10); break;
			case M_XORI: EACHW(a ^ (uint32_t)i10); break;
			case M_ORX:
				out.w[0] = ra.w[0] | ra.w[1] | ra.w[2] | ra.w[3];
				out.w[1] = out.w[2] = out.w[3] = 0;
				break;
			/* shifts and rotates on elements */
			case M_SHLH: EACHH((b & 31) > 15 ? 0 : a << (b & 31)); break;
			case M_SHLHI: EACHH((i7 & 31) > 15 ? 0 : a << (i7 & 31)); break;
			case M_SHL: EACHW((b & 63) > 31 ? 0 : a << (b & 63)); break;
			case M_SHLI: EACHW((i7 & 63) > 31 ? 0 : a << (i7 & 63)); break;
			case M_ROTH: EACHH((a << (b & 15)) | (a >> ((16 - (b & 15)) & 15) >> 0)); break;
			case M_ROTHI: EACHH((a << (i7 & 15)) | (a >> ((16 - (i7 & 15)) & 15) >> 0)); break;
			case M_ROT: EACHW((a << (b & 31)) | ((b & 31) ? a >> (32 - (b & 31)) : 0)); break;
			case M_ROTI: EACHW((a << (i7 & 31)) | ((i7 & 31) ? a >> (32 - (i7 & 31)) : 0)); break;
			case M_ROTHM: EACHH(((0u - b) & 31) > 15 ? 0 : a >> ((0u - b) & 31)); break;
			case M_ROTHMI: EACHH(((0u - i7) & 31) > 15 ? 0 : a >> ((0u - i7) & 31)); break;
			case M_ROTM: EACHW(((0u - b) & 63) > 31 ? 0 : a >> ((0u - b) & 63)); break;
			case M_ROTMI: EACHW(((0u - i7) & 63) > 31 ? 0 : a >> ((0u - i7) & 63)); break;
			case M_ROTMAH: EACHH((uint16_t)((int16_t)a >> (((0u - b) & 31) > 15 ? 15 : ((0u - b) & 31)))); break;
			case M_ROTMAHI: EACHH((uint16_t)((int16_t)a >> (((0u - i7) & 31) > 15 ? 15 : ((0u - i7) & 31)))); break;
			case M_ROTMA: EACHW((uint32_t)((int32_t)a >> (((0u - b) & 63) > 31 ? 31 : ((0u - b) & 63)))); break;
			case M_ROTMAI: EACHW((uint32_t)((int32_t)a >> (((0u - i7) & 63) > 31 ? 31 : ((0u - i7) & 63)))); break;
			/* quadword shifts */
			case M_SHLQBI: case M_SHLQBII: {
				unsigned n = (m == M_SHLQBI ? rb.w[0] : (unsigned)i7) & 7;

				out = fromq(toq(ra) << n);
				break;
			}
			case M_ROTQBI: case M_ROTQBII: {
				unsigned n = (m == M_ROTQBI ? rb.w[0] : (unsigned)i7) & 7;
				u128 v = toq(ra);

				out = fromq(n ? (v << n) | (v >> (128 - n)) : v);
				break;
			}
			case M_ROTQMBI: case M_ROTQMBII: {
				unsigned n = (0u - (m == M_ROTQMBI ? rb.w[0] : (unsigned)i7)) & 7;

				out = fromq(toq(ra) >> n);
				break;
			}
			case M_SHLQBY: case M_SHLQBYI: case M_SHLQBYBI: {
				unsigned n = m == M_SHLQBY ? rb.w[0] & 31 : m == M_SHLQBYI ? (unsigned)i7 & 31 : (rb.w[0] >> 3) & 31;

				for (i = 0; i < 16; i++)
					sb8(&out, i, i + n < 16 ? gb8(ra, i + n) : 0);
				break;
			}
			case M_ROTQBY: case M_ROTQBYI: case M_ROTQBYBI: {
				unsigned n = m == M_ROTQBY ? rb.w[0] & 15 : m == M_ROTQBYI ? (unsigned)i7 & 15 : (rb.w[0] >> 3) & 15;

				for (i = 0; i < 16; i++)
					sb8(&out, i, gb8(ra, (i + n) & 15));
				break;
			}
			case M_ROTQMBY: case M_ROTQMBYI: case M_ROTQMBYBI: {
				unsigned n = m == M_ROTQMBY ? (0u - rb.w[0]) & 31 : m == M_ROTQMBYI ? (0u - (unsigned)i7) & 31 :
					(0u - (rb.w[0] >> 3)) & 31;

				for (i = 0; i < 16; i++)
					sb8(&out, i, (n < 16 && i >= n) ? gb8(ra, i - n) : 0);
				break;
			}
			/* compares */
			case M_CEQB: EACHB(a == b ? 0xFF : 0); break;
			case M_CEQBI: EACHB(a == (uint8_t)i10 ? 0xFF : 0); break;
			case M_CEQH: EACHH(a == b ? 0xFFFF : 0); break;
			case M_CEQHI: EACHH(a == (uint16_t)i10 ? 0xFFFF : 0); break;
			case M_CEQ: EACHW(a == b ? ~0u : 0); break;
			case M_CEQI: EACHW(a == (uint32_t)i10 ? ~0u : 0); break;
			case M_CGTB: EACHB((int8_t)a > (int8_t)b ? 0xFF : 0); break;
			case M_CGTBI: EACHB((int8_t)a > (int8_t)i10 ? 0xFF : 0); break;
			case M_CGTH: EACHH((int16_t)a > (int16_t)b ? 0xFFFF : 0); break;
			case M_CGTHI: EACHH((int16_t)a > (int16_t)i10 ? 0xFFFF : 0); break;
			case M_CGT: EACHW((int32_t)a > (int32_t)b ? ~0u : 0); break;
			case M_CGTI: EACHW((int32_t)a > i10 ? ~0u : 0); break;
			case M_CLGTB: EACHB(a > b ? 0xFF : 0); break;
			case M_CLGTBI: EACHB(a > (uint8_t)i10 ? 0xFF : 0); break;
			case M_CLGTH: EACHH(a > b ? 0xFFFF : 0); break;
			case M_CLGTHI: EACHH(a > (uint16_t)i10 ? 0xFFFF : 0); break;
			case M_CLGT: EACHW(a > b ? ~0u : 0); break;
			case M_CLGTI: EACHW(a > (uint32_t)i10 ? ~0u : 0); break;
			/* branches */
			case M_BR: taken = 1; target = pc + (uint32_t)(i16 * 4); writes = 0; break;
			case M_BRA: taken = 1; target = (uint32_t)(i16 * 4); writes = 0; break;
			case M_BRSL: taken = 1; target = pc + (uint32_t)(i16 * 4); out.w[0] = next; out.w[1] = out.w[2] = out.w[3] = 0; break;
			case M_BRASL: taken = 1; target = (uint32_t)(i16 * 4); out.w[0] = next; out.w[1] = out.w[2] = out.w[3] = 0; break;
			case M_BI: taken = 1; target = ra.w[0]; writes = 0; break;
			case M_BISL: taken = 1; target = ra.w[0]; out.w[0] = next; out.w[1] = out.w[2] = out.w[3] = 0; break;
			case M_BRZ: taken = rt.w[0] == 0; target = pc + (uint32_t)(i16 * 4); writes = 0; break;
			case M_BRNZ: taken = rt.w[0] != 0; target = pc + (uint32_t)(i16 * 4); writes = 0; break;
			case M_BRHZ: taken = (rt.w[0] & 0xFFFF) == 0; target = pc + (uint32_t)(i16 * 4); writes = 0; break;
			case M_BRHNZ: taken = (rt.w[0] & 0xFFFF) != 0; target = pc + (uint32_t)(i16 * 4); writes = 0; break;
			case M_BIZ: taken = rt.w[0] == 0; target = ra.w[0]; writes = 0; break;
			case M_BINZ: taken = rt.w[0] != 0; target = ra.w[0]; writes = 0; break;
			case M_BIHZ: taken = (rt.w[0] & 0xFFFF) == 0; target = ra.w[0]; writes = 0; break;
			case M_BIHNZ: taken = (rt.w[0] & 0xFFFF) != 0; target = ra.w[0]; writes = 0; break;
			case M_HBRA: case M_HBRR: case M_HBR: case M_NOP: case M_LNOP: case M_SYNC: case M_DSYNC:
				writes = 0;
				break;
			case M_STOP: case M_STOP2:
				*pcp = next;
				return (long)((insn & 0x3FFF) << 16) | 2;
			case M_HEQ: case M_HEQI:
				writes = 0;
				if (ra.w[0] == (m == M_HEQ ? rb.w[0] : (uint32_t)i10)) {
					*pcp = pc;
					return 4;
				}
				break;
			case M_HGT: case M_HGTI:
				writes = 0;
				if ((int32_t)ra.w[0] > (m == M_HGT ? (int32_t)rb.w[0] : i10)) {
					*pcp = pc;
					return 4;
				}
				break;
			case M_HLGT: case M_HLGTI:
				writes = 0;
				if (ra.w[0] > (m == M_HLGT ? rb.w[0] : (uint32_t)i10)) {
					*pcp = pc;
					return 4;
				}
				break;
			/* shuffles and gathers */
			case M_SHUFB:
				for (i = 0; i < 16; i++) {
					uint8_t k = gb8(rc, i), v;

					if ((k & 0xC0) == 0x80) v = 0x00;
					else if ((k & 0xE0) == 0xC0) v = 0xFF;
					else if ((k & 0xE0) == 0xE0) v = 0x80;
					else v = (k & 0x1F) < 16 ? gb8(ra, k & 0x1F) : gb8(rb, (k & 0x1F) - 16);
					sb8(&out, i, v);
				}
				break;
			case M_CBD: case M_CHD: case M_CWD: case M_CDD:
			case M_CBX: case M_CHX: case M_CWX: case M_CDX: {
				int sz = (m == M_CBD || m == M_CBX) ? 1 : (m == M_CHD || m == M_CHX) ? 2 : (m == M_CWD || m == M_CWX) ? 4 : 8;
				uint32_t t0 = (m >= M_CBX && m <= M_CDX) ? ra.w[0] + rb.w[0] : ra.w[0] + (uint32_t)i7;
				unsigned pos = t0 & 15 & ~(unsigned)(sz - 1);

				for (i = 0; i < 16; i++)
					sb8(&out, i, (uint8_t)(0x10 + i));
				for (i = 0; i < (unsigned)sz; i++)
					sb8(&out, pos + i, (uint8_t)i + (sz == 1 ? 3 : sz == 2 ? 2 : 0));
				break;
			}
			case M_GB:
				out.w[0] = (ra.w[0] & 1) << 3 | (ra.w[1] & 1) << 2 | (ra.w[2] & 1) << 1 | (ra.w[3] & 1);
				out.w[1] = out.w[2] = out.w[3] = 0;
				break;
			case M_GBH:
				out.w[0] = 0;
				for (i = 0; i < 8; i++)
					out.w[0] |= (uint32_t)(gh(ra, i) & 1) << (7 - i);
				out.w[1] = out.w[2] = out.w[3] = 0;
				break;
			case M_GBB:
				out.w[0] = 0;
				for (i = 0; i < 16; i++)
					out.w[0] |= (uint32_t)(gb8(ra, i) & 1) << (15 - i);
				out.w[1] = out.w[2] = out.w[3] = 0;
				break;
			case M_FSM: for (i = 0; i < 4; i++) out.w[i] = (ra.w[0] >> (3 - i)) & 1 ? ~0u : 0; break;
			case M_FSMH: for (i = 0; i < 8; i++) sh16(&out, i, (ra.w[0] >> (7 - i)) & 1 ? 0xFFFF : 0); break;
			case M_FSMB: for (i = 0; i < 16; i++) sb8(&out, i, (ra.w[0] >> (15 - i)) & 1 ? 0xFF : 0); break;
			/* channels */
			case M_RDCH: {
				uint32_t v = 0;

				if (!rdch(s, (insn >> 7) & 127, &v)) {
					*pcp = pc;
					return -EINTR;
				}
				splat(&out, 0);
				out.w[0] = v;
				break;
			}
			case M_RCHCNT:
				splat(&out, 0);
				out.w[0] = (uint32_t)rchcnt(s, (insn >> 7) & 127);
				break;
			case M_WRCH:
				writes = 0;
				if (!wrch(s, (insn >> 7) & 127, rt.w[0])) {
					*pcp = pc;
					return -EINTR;
				}
				if (s->event) {
					*event = s->event;
					*pcp = next;
					return 1;	/* running, suspended by the MFC error: the caller sees the event */
				}
				break;
			case M_MFSPR: case M_MTSPR:
				writes = m == M_MFSPR;
				if (writes)
					splat(&out, 0);
				break;
			/* single precision */
			case M_FA: EACHW(spf(a, b, 0, 0)); if (s->f.fp_bit) out.w[0] ^= 1; break;
			case M_FS: EACHW(spf(a, b, 0, 1)); break;
			case M_FM: EACHW(spf(a, b, 0, 2)); break;
			case M_FMA: EACHW(spf(a, b, c, 3)); break;
			case M_FMS: EACHW(spf(a, b, c, 4)); break;
			case M_FNMS: EACHW(spf(a, b, c, 5)); break;
			case M_FCEQ: EACHW(gf(a) == gf(b) ? ~0u : 0); break;
			case M_FCGT: EACHW(gf(a) > gf(b) ? ~0u : 0); break;
			case M_FCMEQ: EACHW(fabsf(gf(a)) == fabsf(gf(b)) ? ~0u : 0); break;
			case M_FCMGT: EACHW(fabsf(gf(a)) > fabsf(gf(b)) ? ~0u : 0); break;
			case M_FREST: EACHW(fw(gf(a) != 0 ? 1.0f / gf(a) : 0) & 0xFFFFF000u); break;
			case M_FRSQEST: EACHW(fw(gf(a) != 0 ? 1.0f / sqrtf(fabsf(gf(a))) : 0) & 0xFFFFF000u); break;
			case M_FI: EACHW(b); break;
			case M_CFLTS: EACHW(({ double v = trunc((double)gf(a) * ldexp(1.0, 173 - (int)i8));
					       v >= 2147483647.0 ? 0x7FFFFFFFu : v <= -2147483648.0 ? 0x80000000u : (uint32_t)(int32_t)v; })); break;
			case M_CFLTU: EACHW(({ double v = trunc((double)gf(a) * ldexp(1.0, 173 - (int)i8));
					       v >= 4294967295.0 ? 0xFFFFFFFFu : v <= 0 ? 0u : (uint32_t)v; })); break;
			case M_CSFLT: EACHW(fw((float)ldexp((double)(int32_t)a, -(155 - (int)i8)))); break;
			case M_CUFLT: EACHW(fw((float)ldexp((double)a, -(155 - (int)i8)))); break;
			/* double precision */
			case M_DFA: for (i = 0; i < 2; i++) sdbl(&out, i, gdbl(ra, i) + gdbl(rb, i)); break;
			case M_DFS: for (i = 0; i < 2; i++) sdbl(&out, i, gdbl(ra, i) - gdbl(rb, i)); break;
			case M_DFM: for (i = 0; i < 2; i++) sdbl(&out, i, gdbl(ra, i) * gdbl(rb, i)); break;
			case M_DFMA: for (i = 0; i < 2; i++) sdbl(&out, i, fma(gdbl(ra, i), gdbl(rb, i), gdbl(rt, i))); break;
			case M_DFMS: for (i = 0; i < 2; i++) sdbl(&out, i, fma(gdbl(ra, i), gdbl(rb, i), -gdbl(rt, i))); break;
			case M_DFNMS: for (i = 0; i < 2; i++) sdbl(&out, i, fma(-gdbl(ra, i), gdbl(rb, i), gdbl(rt, i))); break;
			case M_DFNMA: for (i = 0; i < 2; i++) sdbl(&out, i, -fma(gdbl(ra, i), gdbl(rb, i), gdbl(rt, i))); break;
			case M_DFCEQ: for (i = 0; i < 2; i++) sd(&out, i, gdbl(ra, i) == gdbl(rb, i) ? ~0ull : 0); break;
			case M_DFCGT: for (i = 0; i < 2; i++) sd(&out, i, gdbl(ra, i) > gdbl(rb, i) ? ~0ull : 0); break;
			case M_DFCMEQ: for (i = 0; i < 2; i++) sd(&out, i, fabs(gdbl(ra, i)) == fabs(gdbl(rb, i)) ? ~0ull : 0); break;
			case M_DFCMGT: for (i = 0; i < 2; i++) sd(&out, i, fabs(gdbl(ra, i)) > fabs(gdbl(rb, i)) ? ~0ull : 0); break;
			case M_FESD:
				sdbl(&out, 0, (double)gf(ra.w[0]));
				sdbl(&out, 1, (double)gf(ra.w[2]));
				break;
			case M_FRDS:
				out.w[0] = fw((float)gdbl(ra, 0));
				out.w[1] = 0;
				out.w[2] = fw((float)gdbl(ra, 1));
				out.w[3] = 0;
				break;
			case M_FSCRRD: splat(&out, 0); break;
			case M_FSCRWR: writes = 0; break;
			default:
				fprintf(stderr, "spusim: %s (0x%08x at 0x%x) is not implemented\n", optab[m].name, insn, pc);
				*pcp = pc;
				return 0x20;
			}
			if (trace && s->icount < trace) {
				fprintf(stderr, "%05x %-8s r%-3u <- %08x %08x %08x %08x\n", pc, optab[m].name, rtn,
					writes ? out.w[0] : 0, writes ? out.w[1] : 0, writes ? out.w[2] : 0, writes ? out.w[3] : 0);
			}
			if (writes) {
				if (s->f.reg_stuck >= 0 && (int)rtn == s->f.reg_stuck)
					out.w[0] |= 1;
				s->r[rtn] = out;
			}
			pc = taken ? target & LSLR & ~3u : next;
		}
	}
}
