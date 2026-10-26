/* speq-bench: the probes of the SPU offload plan (P1-P5) on libspeq's queue and bench.spu.
 * One line per measurement on stdout, then the verdicts. Run it through ps3-bench.sh run,
 * with the desktop stopped:
 *   speq-bench --quick            smoke run: few samples, short gaps, no 10 ms jobs
 *   speq-bench                    the full probes
 * Options: --max-seconds S  all-SPE busy time allowed (default 60; heavy phases are skipped
 *                           past it), --gap S  idle seconds between heavy phases (default 30,
 *                           --quick 3), --probes 41325  which probes, in that order,
 *                           --fault  also a fatal SPU fault (P3: the context is recreated),
 *                           --prog PATH  bench.spu (default: /usr/lib/ps3/spu/bench.spu).
 * Linux can't read the Cell's temperatures: heavy phases are short, 1-SPE before 6-SPE, with
 * gaps, and the total is capped. A watchdog ends the run with "HANG" (exit 3) when anything
 * waits longer than it should, instead of blocking forever.
 * Built with -DSPEQ_FAKE (speq-bench-fake) it runs on the host with SPEQ_FAKE=1. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "bench.h"
#include "speq-int.h"

#define LOAD(p)		__atomic_load_n((p), __ATOMIC_ACQUIRE)

static int quick, with_fault, fake;
static double max_busy = 60, gap_s = -1, busy;	/* all-SPE seconds */
static int heavy_done;
static const char *prog = "bench.spu";
static unsigned nspe;
static double tb;	/* decrementer ticks per us */

/* verdict inputs */
static double p1_wake99 = -1, p1_wake50 = -1, p2_futex50 = -1, p2_spin50 = -1;
static double p3_unpark50 = -1, p3_respawn50 = -1, p3_open1 = -1, p4_ratio = -1;

/* --- watchdog --- */

static int64_t wd_deadline;	/* us, 0: off */
static const char *wd_what = "";
static struct speq *wd_q;

static void wd(double seconds, const char *what)
{
	wd_what = what;
	__atomic_store_n(&wd_deadline, seconds ? (int64_t)(spq_now_us() + seconds * 1e6) : 0,
			 __ATOMIC_RELEASE);
}

static void dump_stats(struct speq *q)
{
	struct spq_stats s;

	spq_stats(q, &s);
	printf("    stats: workers %u alive %u spawns %lu parks %lu unparks %lu frees %lu faults "
	       "%lu failed %lu | SPU: claims %lu wakes %lu spurious %lu lost %lu parks %lu "
	       "timer %lu | tail_parked 0x%08x head %u\n", s.workers, s.alive, s.spawns, s.parks,
	       s.unparks, s.frees, s.faults, s.failed, s.claims, s.wakes, s.spurious, s.lost,
	       s.spu_parks, s.timer, s.tail_parked, s.head);
}

static void *watchdog(void *arg)
{
	struct timespec ts = { 0, 100000000 };
	int64_t d;

	for (;;) {
		nanosleep(&ts, NULL);
		d = __atomic_load_n(&wd_deadline, __ATOMIC_ACQUIRE);
		if (d && spq_now_us() > d) {
			printf("HANG: %s: nothing for too long; giving up (SPUs may still run "
			       "until the process exits)\n", wd_what);
			if (wd_q)
				dump_stats(wd_q);
			fflush(stdout);
			_exit(3);
		}
	}
	return NULL;
}

static void hang(struct speq *q, const char *what)
{
	printf("HANG: %s: timed out\n", what);
	dump_stats(q);
	fflush(stdout);
	_exit(3);
}

/* --- helpers --- */

static struct speq *open_q(unsigned n)
{
	struct speq *q;
	int ret;

	wd(30, "speq_open");
	ret = speq_open(&q, prog, n, 0);
	wd(0, "");
	if (ret) {
		printf("speq_open(%s, %u): %s\n", prog, n, strerror(-ret));
		fflush(stdout);
		exit(1);
	}
	wd_q = q;
	return q;
}

static void close_q(struct speq *q)
{
	wd(30, "speq_close");
	speq_close(q);
	wd(0, "");
	wd_q = NULL;
}

static void *amalloc(size_t n)
{
	void *p;

	if (posix_memalign(&p, 128, n)) {
		printf("out of memory (%zu bytes)\n", n);
		exit(1);
	}
	memset(p, 0, n);
	return p;
}

/* a job line for bench.spu (a compound literal: lives to the end of the enclosing block) */
#define JOB(o, a0, a1, a2, a3, res) (&(struct speq_job){ .op = (o), \
	.arg = { (a0), (a1), (a2), (a3), [BENCH_RESULT] = (uintptr_t)(res) } })

/* submit one job on d (zeroed first) */
static void submit1(struct speq *q, struct speq_done *d, const struct speq_job *j)
{
	int ret;

	memset(d, 0, sizeof(*d));
	ret = speq_submit(q, j, 1, d);
	if (ret) {
		printf("speq_submit: %s\n", strerror(-ret));
		exit(1);
	}
}

/* wait with spin_us (-1: the queue's S), 5 s at most */
static int wait_d(struct speq *q, struct speq_done *d, int spin_us, const char *what)
{
	int ret = spq_wait_ex(q, d, spin_us, 5000000);

	if (ret == -ETIMEDOUT)
		hang(q, what);
	return ret;
}

static int run1(struct speq *q, const struct speq_job *j, const char *what)
{
	struct speq_done d __attribute__((aligned(128)));

	submit1(q, &d, j);
	return wait_d(q, &d, -1, what);
}

static int spin_until(uint32_t *p, uint32_t v, double us)
{
	double t0 = spq_now_us();

	while (LOAD(p) != v)
		if (spq_now_us() - t0 > us)
			return 0;
	return 1;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static double pct(double *v, unsigned n, unsigned p)
{
	unsigned i = (unsigned)((n - 1) * (uint64_t)p / 100);

	return n ? v[i] : 0;
}

/* "P1 what  n=..  p50 .. p99 .. max .. us", sorts v; returns p50 */
static double summary(const char *probe, const char *what, double *v, unsigned n,
		      const char *extra)
{
	if (!n) {
		printf("%s %-36s no samples%s%s\n", probe, what, extra ? "  " : "", extra ? extra : "");
		return -1;
	}
	qsort(v, n, sizeof(*v), cmp);
	printf("%s %-36s n=%-4u p50 %8.1f  p99 %8.1f  max %8.1f us%s%s\n", probe, what, n,
	       pct(v, n, 50), pct(v, n, 99), v[n - 1], extra ? "  " : "", extra ? extra : "");
	fflush(stdout);
	return pct(v, n, 50);
}

static void sleep_us(double us)
{
	struct timespec ts = { us / 1e6, ((long)us % 1000000) * 1000 };

	nanosleep(&ts, NULL);
}

/* Heavy phases: within the budget, and an idle gap before each but the first */
static int heavy(const char *what, double est_all_spe_s)
{
	if (busy + est_all_spe_s > max_busy) {
		printf("%s: skipped (all-SPE budget: %.1f of %.0f s used, needs %.1f)\n", what,
		       busy, max_busy, est_all_spe_s);
		return 0;
	}
	if (heavy_done++ && gap_s > 0) {
		printf("-- idle %.0f s before %s\n", gap_s, what);
		fflush(stdout);
		sleep_us(gap_s * 1e6);
	}
	return 1;
}

static long meminfo(const char *key)
{
	char line[128];
	long v = -1;
	size_t n = strlen(key);
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, key, n) && line[n] == ':') {
			v = strtol(line + n + 1, NULL, 10);
			break;
		}
	fclose(f);
	return v;
}

static double cpu_us(void)
{
	struct rusage ru;

	getrusage(RUSAGE_THREAD, &ru);
	return ru.ru_utime.tv_sec * 1e6 + ru.ru_utime.tv_usec + ru.ru_stime.tv_sec * 1e6 +
	       ru.ru_stime.tv_usec;
}

/* --- P4: the decrementer --- */

static void p4(void)
{
	struct speq *q = open_q(1);
	struct speq_done d __attribute__((aligned(128)));
	struct bench_result *r = amalloc(128);
	uint32_t *flag = amalloc(128);
	struct spq_stats s0, s1;
	double t0, t1, ms = quick ? 50 : 200, mhz;
	char extra[128];

	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P4 first job");
	t0 = spq_now_us();
	submit1(q, &d, JOB(BENCH_DEC, (uintptr_t)flag, 0, 0, 0, r));
	sleep_us(ms * 1000);
	t1 = spq_now_us();
	__atomic_store_n(flag, 1, __ATOMIC_SEQ_CST);
	wait_d(q, &d, -1, "P4 decrementer job");
	mhz = r->v[0] / (t1 - t0);
	if (!r->v[0]) {
		printf("P4 decrementer: NOT COUNTING (0x%08llx both times, %llu polls): workers "
		       "will never park, and the bench's SPU times are void\n",
		       (unsigned long long)r->v[2], (unsigned long long)r->v[1]);
		p4_ratio = 0;
	} else {
		p4_ratio = mhz / tb;
		printf("P4 decrementer: %llu ticks in %.1f ms = %.3f MHz; timebase %.3f MHz; ratio "
		       "%.4f (%llu polls)\n", (unsigned long long)r->v[0], (t1 - t0) / 1000, mhz,
		       tb, p4_ratio, (unsigned long long)r->v[1]);
	}
	/* the idle timer: does an idle worker park after 1 ms? */
	spq_tune(q, -1, 1000, -1);
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P4 nop");
	spq_stats(q, &s0);
	sleep_us(50000);
	spq_stats(q, &s1);
	snprintf(extra, sizeof(extra), "%s", s1.parks > s0.parks ? "yes" : "NO (idle timer dead?)");
	printf("P4 idle worker parks after 1 ms idle: %s (parked mask 0x%x)\n", extra,
	       s1.tail_parked & SPEQ_PARKED_MASK);
	close_q(q);
	free(r);
	free(flag);
}

/* --- P1: wake latency --- */

enum { W_STW, W_STWCX, W_OTHER, W_NEIGHBOUR };
static const char *const watch_name[] = {
	"store to the reserved word (stw)", "atomic add there (lwarx/stwcx.)",
	"store to another word of the line", "store to the next line",
};

static void p1_watch(void)
{
	struct speq *q = open_q(1);
	struct speq_done d __attribute__((aligned(128)));
	uint32_t *l = amalloc(3 * 128), *nb = l + 32, *flag = l + 64;
	struct bench_result *r = amalloc(128);
	unsigned k = quick ? 20 : 200, v, i, n, llr, dec, woke, multi, seq = 0;
	double *lat = calloc(k, sizeof(double)), t0;
	char extra[160];

	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P1 first job");
	for (v = W_STW; v <= W_NEIGHBOUR; v++) {
		n = llr = dec = woke = multi = 0;
		for (i = 0; i < k; i++) {
			__atomic_store_n(flag, 0, __ATOMIC_SEQ_CST);
			submit1(q, &d, JOB(BENCH_WATCH, (uintptr_t)l, (uintptr_t)flag,
					     v == W_NEIGHBOUR ? 3000 : 1000000, 0, r));
			if (!spin_until(flag, 1, 2e6))
				hang(q, "P1 watch: the SPU never armed");
			/* fresh values: the hardware wakes on any store, the fake only on a
			 * change */
			t0 = spq_now_us();
			switch (v) {
			case W_STW:
				*(volatile uint32_t *)l = ++seq;
				break;
			case W_STWCX:
				__atomic_fetch_add(l, 1, __ATOMIC_SEQ_CST);
				break;
			case W_OTHER:
				((volatile uint32_t *)l)[7] = ++seq;
				break;
			case W_NEIGHBOUR:
				*(volatile uint32_t *)nb = ++seq;
				break;
			}
			if (v == W_NEIGHBOUR) {
				if (spin_until(flag, 2, 1500))
					woke++;
				*(volatile uint32_t *)l = ++seq;	/* release it */
			}
			if (!spin_until(flag, 2, 2e6))
				hang(q, "P1 watch: no wake from a store to the reserved line");
			if (v != W_NEIGHBOUR)
				lat[n++] = spq_now_us() - t0;
			wait_d(q, &d, -1, "P1 watch job");
			llr += !!(r->v[1] & 0x400);
			dec += !!(r->v[1] & 0x20);
			multi += r->v[2] > 1;
		}
		if (v == W_NEIGHBOUR) {
			printf("P1 %-36s woke %u of %u times within 1.5 ms (want 0)\n", watch_name[v],
			       woke, k);
		} else {
			snprintf(extra, sizeof(extra), "LLR_LOST %u/%u, by the timer %u, "
				 "extra wakes %u", llr, k, dec, multi);
			summary("P1", watch_name[v], lat, n, extra);
		}
	}
	close_q(q);
	free(lat);
	free(l);
	free(r);
}

/* A submit against workers idle in the event wait (not parked: gaps below the idle time) */
static void p1_submit(unsigned nw)
{
	struct speq *q = open_q(nw);
	struct speq_done d __attribute__((aligned(128)));
	struct spq_stats s0, s1;
	unsigned k = quick ? 50 : 500, i, pokes = quick ? 10 : 50;
	double *lat = calloc(k, sizeof(double)), t0;
	char what[64], extra[160];

	nw = q->nworkers;
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P1 first job");
	spq_stats(q, &s0);
	for (i = 0; i < k; i++) {
		sleep_us(2000);
		t0 = spq_now_us();
		submit1(q, &d, JOB(BENCH_NOP, 0, 0, 0, 0, NULL));
		wait_d(q, &d, 1000000, "P1 submit");	/* spin: the wake, not the waiter */
		lat[i] = spq_now_us() - t0;
	}
	/* idle past the park time: every worker puts its line, so the counters are current */
	sleep_us(40000);
	spq_stats(q, &s1);
	snprintf(what, sizeof(what), "submit wakes idle worker (%u SPE%s)", nw, nw > 1 ? "s" : "");
	snprintf(extra, sizeof(extra), "per job: %.2f wakes, %.2f spurious, %.2f lost claims",
		 (double)(s1.wakes - s0.wakes) / k, (double)(s1.spurious - s0.spurious) / k,
		 (double)(s1.lost - s0.lost) / k);
	t0 = summary("P1", what, lat, k, extra);
	if (pct(lat, k, 99) > p1_wake99) {	/* the verdict takes the worse of 1 and all */
		p1_wake50 = t0;
		p1_wake99 = pct(lat, k, 99);
	}

	/* back to back: the worker barely idles */
	for (i = 0; i < k; i++) {
		t0 = spq_now_us();
		submit1(q, &d, JOB(BENCH_NOP, 0, 0, 0, 0, NULL));
		wait_d(q, &d, 1000000, "P1 submit hot");
		lat[i] = spq_now_us() - t0;
	}
	snprintf(what, sizeof(what), "back-to-back nop (%u SPE%s)", nw, nw > 1 ? "s" : "");
	summary("P1", what, lat, k, NULL);

	/* A store to another word of line 0: every idle worker wakes and finds nothing. They
	 * idle 300 ms meanwhile, so none parks before the last poke; then they park, which
	 * puts their counters. */
	spq_tune(q, -1, 300000, -1);
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P1 nop");
	sleep_us(1000);
	spq_stats(q, &s0);
	for (i = 0; i < pokes; i++) {
		sleep_us(2000);
		__atomic_store_n(&q->ctl->poke, i + 1, __ATOMIC_SEQ_CST);
	}
	sleep_us(400000);
	spq_stats(q, &s1);
	printf("P1 %-36s %.2f spurious wakes per poke per worker (want 1: line 0 wakes all)\n",
	       "store to line 0's spare word", (double)(s1.spurious - s0.spurious) / pokes / nw);
	close_q(q);
	free(lat);
}

/* --- P3: costs --- */

static void p3(void)
{
	struct speq *q;
	struct speq_done d __attribute__((aligned(128)));
	struct bench_result *r = amalloc(128);
	struct spq_stats s0, s1;
	unsigned n, i, k = quick ? 10 : 40, calls = quick ? 100 : 1000, got;
	long vm0, vm1, vm2;
	double t0, t1, t2, t3, *lat = calloc(k, sizeof(double));
	char extra[160];
	int ret;

	for (n = 1; n <= nspe; n++) {
		vm0 = meminfo("VmallocUsed");
		t0 = spq_now_us();
		q = open_q(n);
		t1 = spq_now_us();
		vm1 = meminfo("VmallocUsed");
		run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P3 first job");
		t2 = spq_now_us();
		close_q(q);
		t3 = spq_now_us();
		vm2 = meminfo("VmallocUsed");
		if (n == 1)
			p3_open1 = t1 - t0;
		printf("P3 open %u worker%s: open %8.0f us, first job %6.0f us, close %6.0f us; "
		       "VmallocUsed +%ld KB (%ld per context), after close %+ld KB\n", n,
		       n > 1 ? "s" : " ", t1 - t0, t2 - t1, t3 - t2, vm1 - vm0, (vm1 - vm0) / n,
		       vm2 - vm0);
		fflush(stdout);
	}

	q = open_q(1);
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P3 nop");
	/* unpark: the worker parks after 1 ms idle; the next job resumes it from stop 0x2110 */
	spq_tune(q, -1, 1000, 60000);
	for (i = got = 0; i < k; i++) {
		spq_stats(q, &s0);
		sleep_us(10000);
		spq_stats(q, &s1);
		if (!(s1.tail_parked & 1))
			continue;	/* not parked: the decrementer (P4) */
		t0 = spq_now_us();
		run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P3 unpark");
		lat[got++] = spq_now_us() - t0;
	}
	snprintf(extra, sizeof(extra), "(%u of %u parked)", got, k);
	p3_unpark50 = summary("P3", "job to a parked worker (unpark)", lat, got, extra);

	/* respawn: parked longer than the free time, the context is gone */
	spq_tune(q, -1, 1000, 5);
	for (i = got = 0; i < k; i++) {
		spq_stats(q, &s0);
		sleep_us(30000);
		spq_stats(q, &s1);
		if (s1.frees == s0.frees)
			continue;
		t0 = spq_now_us();
		run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P3 respawn");
		lat[got++] = spq_now_us() - t0;
	}
	snprintf(extra, sizeof(extra), "(%u of %u freed)", got, k);
	p3_respawn50 = summary("P3", "job to a freed worker (new context)", lat, got, extra);
	spq_tune(q, -1, 10000, 5000);

	/* stop 0x2104: the kernel serves getpid on the worker thread, then restarts the SPU */
	ret = run1(q, JOB(BENCH_GETPID, calls, 0, 0, 0, r), "P3 getpid");
	printf("P3 0x2104 getpid round trip: %.2f us per call (%u calls), pid %llu %s\n",
	       r->v[0] / tb / calls, calls, (unsigned long long)r->v[1],
	       ret ? strerror(-ret) : (pid_t)r->v[1] == getpid() ? "(ours: ok)" : "(NOT ours)");

	if (with_fault) {
		/* a put to an unmapped EA: DATA_STORAGE, the context is dead and recreated */
		spq_stats(q, &s0);
		t0 = spq_now_us();
		submit1(q, &d, JOB(BENCH_TOUCH, 0x1000, 16, 16, 0, r));
		ret = wait_d(q, &d, -1, "P3 fault");
		t1 = spq_now_us();
		spq_stats(q, &s1);
		t2 = spq_now_us();
		i = run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P3 after the fault");
		t3 = spq_now_us();
		printf("P3 fatal fault: job error %s (want EFAULT) in %.0f us, faults +%lu; next "
		       "job %s in %.0f us (respawn)\n", ret ? strerror(-ret) : "none", t1 - t0,
		       s1.faults - s0.faults, i ? "FAILED" : "ok", t3 - t2);
	}
	close_q(q);
	free(lat);
	free(r);
}

/* --- P2: completion --- */

enum { M_SPIN, M_YIELD, M_FUTEX, M_HYBRID, M_IBOX };
static const char *const mode_name[] = { "spin", "yield", "futex", "hybrid", "ibox" };

static void p2(void)
{
	static const unsigned len_us[] = { 0, 100, 1000, 10000 };
	struct speq *q;
	struct speq_done d __attribute__((aligned(128)));
	unsigned li, m, i, k, nlen = quick ? 3 : 4;
	double *lat, t0, c0, cpu, est = 0;
	char what[64], extra[96], path[128];
	uint32_t v, p;
	int fd = -1;

	for (li = 0; li < nlen; li++)
		est += (len_us[li] >= 10000 ? (quick ? 5 : 50) : (quick ? 20 : 200)) * len_us[li];
	est = est * 5 / 1e6 / nspe;	/* 5 modes, one SPE of nspe */
	if (!heavy("P2", est))
		return;
	q = open_q(1);
	lat = calloc(200, sizeof(double));
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P2 first job");
	t0 = spq_now_us();
	for (li = 0; li < nlen; li++) {
		k = len_us[li] >= 10000 ? (quick ? 5 : 50) : (quick ? 20 : 200);
		for (m = M_SPIN; m <= M_IBOX; m++) {
			if (m == M_IBOX) {
				if (fake) {
					printf("P2 %-36s n/a in fake mode\n", "ibox");
					continue;
				}
				snprintf(path, sizeof(path), "%s/ibox", q->w[0].path);
				fd = open(path, O_RDONLY | O_CLOEXEC);
				if (fd < 0) {
					printf("P2 ibox: %s: %s\n", path, strerror(errno));
					continue;
				}
			}
			c0 = cpu_us();
			for (i = 0; i < k; i++) {
				double s = spq_now_us();

				submit1(q, &d, JOB(BENCH_SPIN, len_us[li], m == M_IBOX, 0, 0,
						     NULL));
				switch (m) {
				case M_SPIN:
					wait_d(q, &d, 1000000, "P2 spin");
					break;
				case M_YIELD:
					wd(5, "P2 yield");
					while ((p = LOAD(&d.pending)) & ~SPEQ_WAITERS)
						sched_yield();
					wd(0, "");
					break;
				case M_FUTEX:
					wait_d(q, &d, 0, "P2 futex");
					break;
				case M_HYBRID:
					wait_d(q, &d, -1, "P2 hybrid");
					break;
				case M_IBOX:
					wd(5, "P2 ibox read");
					if (read(fd, &v, 4) != 4)
						hang(q, "P2 ibox read failed");
					wd(0, "");
					break;
				}
				lat[i] = spq_now_us() - s - len_us[li];
				if (m == M_IBOX)
					wait_d(q, &d, 1000000, "P2 ibox done");
			}
			cpu = (cpu_us() - c0) / k;
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}
			snprintf(what, sizeof(what), "%s, %u us job: completion overhead",
				 mode_name[m], len_us[li]);
			snprintf(extra, sizeof(extra), "waiter CPU %.1f us/job", cpu);
			t0 = summary("P2", what, lat, k, extra);
			if (len_us[li] == 1000 && m == M_FUTEX)
				p2_futex50 = t0;
			if (len_us[li] == 1000 && m == M_SPIN)
				p2_spin50 = t0;
		}
	}
	busy += est;
	close_q(q);
	free(lat);
}

/* --- P5: DMA --- */

static void p5_dma(unsigned nw)
{
	size_t per = (quick ? 1 : 8) << 20;
	unsigned passes = quick ? 16 : 32, reps = quick ? 1 : 5, i, dir, rep;
	struct speq *q;
	struct speq_done d __attribute__((aligned(128)));
	struct speq_job jobs[SPEQ_MAX_WORKERS];
	struct bench_result *r;
	uint8_t *buf;
	double t0, wall, spu_gbs, est;
	char what[64], extra[128];

	est = (quick ? 0.2 : 2.0) * nw / nspe;
	snprintf(what, sizeof(what), "P5 DMA on %u SPE%s", nw, nw > 1 ? "s" : "");
	if (!heavy(what, est))
		return;
	q = open_q(nw);
	nw = q->nworkers;
	buf = amalloc(per * nw);	/* touched: no faults in the timed part */
	r = amalloc(128 * nw);
	run1(q, JOB(BENCH_NOP, 0, 0, 0, 0, NULL), "P5 first job");
	t0 = spq_now_us();
	for (dir = 0; dir < 2; dir++) {
		double best = 0, worst = 1e9;

		wall = 0;
		for (rep = 0; rep < reps; rep++) {
			for (i = 0; i < nw; i++)
				jobs[i] = *JOB(BENCH_DMA, (uintptr_t)(buf + i * per), per, dir, passes,
						(char *)r + i * 128);
			memset(&d, 0, sizeof(d));
			double s = spq_now_us();

			speq_submit(q, jobs, nw, &d);
			wait_d(q, &d, 1000000, "P5 DMA");
			wall += spq_now_us() - s;
			for (i = 0; i < nw; i++) {
				struct bench_result *ri = (void *)((char *)r + i * 128);

				spu_gbs = ri->v[1] / (ri->v[0] / tb) / 1e3;
				if (spu_gbs > best)
					best = spu_gbs;
				if (spu_gbs < worst)
					worst = spu_gbs;
			}
		}
		snprintf(what, sizeof(what), "%s %u SPE%s", dir ? "put to XDR" : "get from XDR",
			 nw, nw > 1 ? "s" : "");
		snprintf(extra, sizeof(extra), "%zu MB x %u passes each", per >> 20, passes);
		printf("P5 %-36s per SPE %.2f-%.2f GB/s, together %.2f GB/s (%s, %u runs)\n",
		       what, worst, best, (double)per * passes * nw * reps / wall / 1e3, extra, reps);
		fflush(stdout);
	}
	busy += (spq_now_us() - t0) / 1e6 * nw / nspe;
	close_q(q);
	free(buf);
	free(r);
}

/* Puts into pages never touched: each one faults through the worker's spu_run */
static void p5_fresh(void)
{
	size_t size = (quick ? 4 : 16) << 20, page = sysconf(_SC_PAGESIZE);
	struct speq *q = open_q(1);
	struct bench_result *r = amalloc(128);
	uint8_t *m;
	double t0, t1, t2;
	int ret;

	for (int pass = 0; pass < 2; pass++) {
		m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (m == MAP_FAILED) {
			printf("P5 mmap %zu MB: %s\n", size >> 20, strerror(errno));
			break;
		}
		if (!pass) {
			t0 = spq_now_us();
			ret = run1(q, JOB(BENCH_DMA, (uintptr_t)m, size, 1, 1, r), "P5 fresh put");
			t1 = spq_now_us();
			ret = ret ? ret : run1(q, JOB(BENCH_DMA, (uintptr_t)m, size, 1, 1, r),
					       "P5 put again");
			t2 = spq_now_us();
			printf("P5 %-36s %.0f MB/s (%zu MB, %zu-byte pages: %.1f us each); the same "
			       "again: %.0f MB/s%s\n", "put into a fresh mmap", size / (t1 - t0),
			       size >> 20, page, (t1 - t0) / (size / page), size / (t2 - t1),
			       ret ? " (FAILED)" : "");
		} else {
			t0 = spq_now_us();
			ret = run1(q, JOB(BENCH_TOUCH, (uintptr_t)m, size, page, 0, r),
				   "P5 touch");
			t1 = spq_now_us();
			printf("P5 %-36s %.1f us per page (%zu pages)%s\n",
			       "16-byte put per fresh page", (t1 - t0) / (size / page), size / page,
			       ret ? " (FAILED)" : "");
		}
		munmap(m, size);
	}
	close_q(q);
	free(r);
}

static void verdicts(void)
{
	printf("--\n");
	if (p4_ratio >= 0)
		printf("VERDICT P4: %s\n", p4_ratio == 0 ? "the decrementer does not count: no "
		       "parking (idle workers spin in the event wait), SPU times void" :
		       p4_ratio > 0.98 && p4_ratio < 1.02 ? "the decrementer counts at the "
		       "timebase rate: the idle timer holds" : "the decrementer counts at another "
		       "rate: scale idle_ticks by the ratio");
	if (p1_wake99 >= 0)
		printf("VERDICT P1: submit to an idle worker p50 %.1f / p99 %.1f us: %s\n",
		       p1_wake50, p1_wake99, p1_wake99 < 20 ? "under 20 us, the design holds" :
		       "20 us or more: consider decrementer polling");
	if (p2_futex50 >= 0 && p2_spin50 >= 0)
		printf("VERDICT P2: sleeping costs %.1f us over spinning (1 ms jobs: futex p50 %.1f, "
		       "spin p50 %.1f): set the spin S near it (now %s)\n", p2_futex50 - p2_spin50,
		       p2_futex50, p2_spin50, getenv("SPEQ_SPIN_US") ? getenv("SPEQ_SPIN_US") : "20");
	if (p3_unpark50 >= 0 || p3_respawn50 >= 0)
		printf("VERDICT P3: unpark p50 %.0f us, new context p50 %.0f us, open(1) %.0f us: "
		       "the park timeout (idle 10 ms, free 5 s) should make both rare\n",
		       p3_unpark50, p3_respawn50, p3_open1);
	printf("all-SPE busy time: %.1f s of %.0f\n", busy, max_busy);
}

int main(int argc, char **argv)
{
	const char *probes = "41325";
	pthread_t th;
	int i;

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--quick"))
			quick = 1;
		else if (!strcmp(argv[i], "--fault"))
			with_fault = 1;
		else if (!strcmp(argv[i], "--max-seconds") && i + 1 < argc)
			max_busy = atof(argv[++i]);
		else if (!strcmp(argv[i], "--gap") && i + 1 < argc)
			gap_s = atof(argv[++i]);
		else if (!strcmp(argv[i], "--probes") && i + 1 < argc)
			probes = argv[++i];
		else if (!strcmp(argv[i], "--prog") && i + 1 < argc)
			prog = argv[++i];
		else {
			fprintf(stderr, "usage: %s [--quick] [--max-seconds S] [--gap S] "
				"[--probes 41325] [--fault] [--prog bench.spu]\n", argv[0]);
			return 2;
		}
	}
	if (gap_s < 0)
		gap_s = quick ? 3 : 30;
	fake = getenv("SPEQ_FAKE") && strcmp(getenv("SPEQ_FAKE"), "0");
	tb = spq_tb_hz() / 1e6;
	pthread_create(&th, NULL, watchdog, NULL);
	{
		struct speq *q = open_q(0);

		nspe = q->nworkers;
		printf("speq-bench: %s, %u SPEs, timebase %.3f MHz, spin %u us, idle %u us, free "
		       "%u ms, %s%s\n", q->be->name, nspe, tb, q->spin_us,
		       (unsigned)(q->ctl->idle_ticks / tb), q->free_ms, quick ? "quick" : "full",
		       with_fault ? ", with a fatal fault" : "");
		if (fake)
			printf("speq-bench: fake backend: the logic runs, the times are the "
			       "emulation's (its event wait polls every 50 us)\n");
		close_q(q);
	}
	for (; *probes; probes++)
		switch (*probes) {
		case '1':
			p1_watch();
			p1_submit(1);
			if (nspe > 1)
				p1_submit(nspe);
			break;
		case '2':
			p2();
			break;
		case '3':
			p3();
			break;
		case '4':
			p4();
			break;
		case '5':
			p5_fresh();
			p5_dma(1);
			if (nspe > 1)
				p5_dma(nspe);
			break;
		}
	verdicts();
	return 0;
}
