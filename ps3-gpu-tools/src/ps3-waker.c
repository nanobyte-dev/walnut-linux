/*
 * ps3-waker: periodic wakeups and timer lateness, for finding out why the stall recorder
 * (ps3-stallrec) makes games stutter less (docs/STALLS.md, "The recorder removes stalls").
 * Run by ps3-bench.sh (BENCH_WAKER, BENCH_PROBE) or by hand, as root for -R.
 *
 *   ps3-waker [-m sleep|kick|probe] [-i MS] [-c CPUS] [-o OUT] [-s STOPFILE] [-t SECONDS]
 *             [-T MS] [-R] [-n]
 *
 *   -m sleep   one thread: clock_nanosleep to the next period, nothing else (no /proc reads, no
 *              writes): what is left of ps3-stallrec without its work. -c pins it (any CPU of
 *              the list); without -c the scheduler places it like the recorder
 *   -m kick    for every CPU in -c (default: all), a thread pinned to that CPU blocked in a read
 *              of an eventfd, and a timer thread pinned to the next CPU that writes the eventfd
 *              every period: a cross-CPU wakeup (an IPI) to each listed CPU every period, and
 *              nothing else
 *   -m probe   for every CPU in -c (default: all), a SCHED_FIFO thread pinned to it sleeping MS
 *              (relative) again and again and timing how late each wakeup is. A pinned thread's
 *              timer fires on its own CPU and wakes it there: it sends no IPI, so it doesn't
 *              change what it measures (except one more idle entry each period). Writes a
 *              histogram per CPU and every wakeup later than -T MS (default 20) to OUT
 *   -i MS      the period (default 10; probe: 20), fractions allowed, at least 0.05
 *   -c CPUS    a CPU list: 0, 1, 0,1, 0-1
 *   -o OUT     probe's report (default stdout); the other modes write their summary there
 *   -s FILE    stop when FILE exists (checked every 100 ms); SIGTERM/SIGINT stop too
 *   -t S       stop after S seconds
 *   -R         SCHED_FIFO 1 for sleep and kick threads (default: nice -10, as ps3-stallrec);
 *              probe threads are always SCHED_FIFO 1 when allowed
 *   -n         neither FIFO nor nice
 *
 * Report lines (probe): "# clock mono=S.NS real=S.NS" (the pair stalls.py uses to place things
 * on the client's clock), "late <cpu> <mono s> <late ms>" per late wakeup (mono: when the sleep
 * was due), and per CPU "cpu <n> wakeups=.. max_ms=.. ge1=.. ge10=.. ge100=.. ge1000=..".
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define MAXCPU 64
#define MAXLATE 4096

static volatile sig_atomic_t stop;
static double period_ms = -1, late_ms = 20;
static int prio = 1;	/* 0 none, 1 nice -10, 2 fifo */
static FILE *out;

static void on_sig(int s) { (void)s; stop = 1; }

static int64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static struct timespec ts_of(int64_t ns)
{
	return (struct timespec){ ns / 1000000000, ns % 1000000000 };
}
static void sleep_until(int64_t t)
{
	struct timespec ts = ts_of(t);
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR && !stop)
		;
}

static int parse_cpus(const char *s, int *cpus)
{
	int n = 0;
	while (*s && n < MAXCPU) {
		char *e;
		long a = strtol(s, &e, 10), b = a;
		if (e == s)
			return -1;
		if (*e == '-')
			b = strtol(e + 1, &e, 10);
		for (long c = a; c <= b && n < MAXCPU; c++)
			cpus[n++] = (int)c;
		s = *e == ',' ? e + 1 : e;
		if (*e && *e != ',')
			return -1;
	}
	return n;
}

static int pin(int cpu)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

/* the calling thread's priority: SCHED_FIFO 1 when fifo (else nice -10 if nice) */
static const char *set_prio(int want)
{
	if (want == 2) {
		struct sched_param sp = { .sched_priority = 1 };
		if (!pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp))
			return "fifo";
		want = 1;
	}
	if (want == 1) {
		/* nice is per thread on Linux: setpriority on the thread id */
		if (!setpriority(PRIO_PROCESS, (id_t)gettid(), -10))
			return "nice-10";
	}
	return "none";
}

/* --- sleep ------------------------------------------------------------------------------ */

static long sleep_wakeups, sleep_late;
static const char *sleep_prio = "none";
struct sleeper { int ncpu, cpus[MAXCPU]; };

static void *sleep_thread(void *a)
{
	struct sleeper *sl = a;
	if (sl->ncpu) {
		cpu_set_t set;
		CPU_ZERO(&set);
		for (int i = 0; i < sl->ncpu; i++)
			CPU_SET(sl->cpus[i], &set);
		if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set))
			fprintf(stderr, "ps3-waker: pinning failed\n");
	}
	sleep_prio = set_prio(prio);
	int64_t period = (int64_t)(period_ms * 1e6), next = now_ns();
	while (!stop) {
		next += period;
		sleep_until(next);
		int64_t t = now_ns();
		sleep_wakeups++;
		if (t - next > period / 2)
			sleep_late++;
		if (t - next > period)		/* missed periods: skip them, keep the phase */
			next += (t - next) / period * period;
	}
	return NULL;
}

/* --- kick --------------------------------------------------------------------------------- */

struct kick { int target, from, efd; long sent, got; pthread_t catcher, timer; };

static void *kick_catcher(void *a)
{
	struct kick *k = a;
	uint64_t v;
	pin(k->target);
	set_prio(prio);
	while (!stop) {
		if (read(k->efd, &v, sizeof(v)) == sizeof(v))
			k->got++;
		else if (errno != EINTR)
			break;
	}
	return NULL;
}

static void *kick_timer(void *a)
{
	struct kick *k = a;
	uint64_t one = 1;
	int64_t period = (int64_t)(period_ms * 1e6), next = now_ns();
	pin(k->from);
	set_prio(prio);
	while (!stop) {
		next += period;
		sleep_until(next);
		int64_t t = now_ns();
		if (t - next > period)
			next += (t - next) / period * period;
		if (write(k->efd, &one, sizeof(one)) == sizeof(one))
			k->sent++;
	}
	if (write(k->efd, &one, sizeof(one)) < 0)	/* let the catcher see stop */
		k->sent = -k->sent;
	return NULL;
}

/* --- probe -------------------------------------------------------------------------------- */

struct probe {
	int cpu; pthread_t th; const char *prio;
	long wakeups, ge1, ge10, ge100, ge1000; int64_t max;
	int nlate; int64_t late_at[MAXLATE], late_by[MAXLATE];
};

static void *probe_thread(void *a)
{
	struct probe *p = a;
	int64_t period = (int64_t)(period_ms * 1e6), thr = (int64_t)(late_ms * 1e6);
	if (pin(p->cpu)) {
		p->prio = "unpinned";
		return NULL;
	}
	p->prio = set_prio(2);
	while (!stop) {
		int64_t due = now_ns() + period;
		struct timespec ts = ts_of(period);
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);	/* relative: a fresh phase each time */
		int64_t late = now_ns() - due;
		p->wakeups++;
		if (late >= 1000000) p->ge1++;
		if (late >= 10000000) p->ge10++;
		if (late >= 100000000) p->ge100++;
		if (late >= 1000000000) p->ge1000++;
		if (late > p->max) p->max = late;
		if (late >= thr && p->nlate < MAXLATE) {
			p->late_at[p->nlate] = due;
			p->late_by[p->nlate++] = late;
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	const char *mode = "sleep", *outp = NULL, *stopfile = NULL;
	int cpus[MAXCPU], ncpu = 0, opt;
	double max_s = 0;

	while ((opt = getopt(argc, argv, "m:i:c:o:s:t:T:Rn")) != -1) {
		switch (opt) {
		case 'm': mode = optarg; break;
		case 'i': period_ms = atof(optarg); break;
		case 'c':
			if ((ncpu = parse_cpus(optarg, cpus)) <= 0) {
				fprintf(stderr, "ps3-waker: -c %s: not a CPU list\n", optarg);
				return 2;
			}
			break;
		case 'o': outp = optarg; break;
		case 's': stopfile = optarg; break;
		case 't': max_s = atof(optarg); break;
		case 'T': late_ms = atof(optarg); break;
		case 'R': prio = 2; break;
		case 'n': prio = 0; break;
		default:
			fprintf(stderr, "usage: see the head of ps3-waker.c (docs/STALLS.md)\n");
			return 2;
		}
	}
	int is_probe = !strcmp(mode, "probe"), is_kick = !strcmp(mode, "kick");
	if (!is_probe && !is_kick && strcmp(mode, "sleep")) {
		fprintf(stderr, "ps3-waker: -m %s: sleep, kick or probe\n", mode);
		return 2;
	}
	if (period_ms < 0)
		period_ms = is_probe ? 20 : 10;
	if (period_ms < 0.05) {
		fprintf(stderr, "ps3-waker: -i: at least 0.05 ms\n");
		return 2;
	}
	int online = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (online < 1)
		online = 1;
	if (!ncpu && (is_probe || is_kick))
		for (ncpu = 0; ncpu < online && ncpu < MAXCPU; ncpu++)
			cpus[ncpu] = ncpu;
	out = outp ? fopen(outp, "w") : stdout;
	if (!out) {
		fprintf(stderr, "ps3-waker: %s: %s\n", outp, strerror(errno));
		return 1;
	}
	/* no SA_RESTART: a signal ends the sleeps and reads (pthread_kill at the end) */
	struct sigaction sa = { .sa_handler = on_sig };
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	struct timespec m, r;
	clock_gettime(CLOCK_MONOTONIC, &m);
	clock_gettime(CLOCK_REALTIME, &r);
	fprintf(out, "# ps3-waker mode=%s interval_us=%ld cpus=", mode, (long)(period_ms * 1000));
	for (int i = 0; i < ncpu; i++)
		fprintf(out, "%s%d", i ? "," : "", cpus[i]);
	fprintf(out, "%s online=%d late_ms=%g\n", ncpu ? "" : "any", online, late_ms);
	fprintf(out, "# clock mono=%lld.%09ld real=%lld.%09ld\n", (long long)m.tv_sec, m.tv_nsec,
		(long long)r.tv_sec, r.tv_nsec);
	fflush(out);

	/* the main thread watches the stop file and the time limit; workers do the rest */
	static struct probe probes[MAXCPU];
	static struct kick kicks[MAXCPU];
	pthread_t sleeper;
	int64_t t0 = now_ns();
	if (is_probe) {
		for (int i = 0; i < ncpu; i++) {
			probes[i].cpu = cpus[i];
			pthread_create(&probes[i].th, NULL, probe_thread, &probes[i]);
		}
	} else if (is_kick) {
		for (int i = 0; i < ncpu; i++) {
			struct kick *k = &kicks[i];
			k->target = cpus[i];
			k->from = (cpus[i] + 1) % online;
			if (k->from == k->target) {
				fprintf(stderr, "ps3-waker: kick needs two CPUs\n");
				return 1;
			}
			k->efd = eventfd(0, EFD_CLOEXEC);
			pthread_create(&k->catcher, NULL, kick_catcher, k);
			pthread_create(&k->timer, NULL, kick_timer, k);
		}
	} else {
		static struct sleeper sl;
		sl.ncpu = ncpu;
		memcpy(sl.cpus, cpus, sizeof(cpus));
		pthread_create(&sleeper, NULL, sleep_thread, &sl);
	}
	while (!stop) {
		usleep(100000);
		if ((stopfile && !access(stopfile, F_OK)) ||
		    (max_s > 0 && now_ns() - t0 >= (int64_t)(max_s * 1e9)))
			stop = 1;
	}
	double secs = (now_ns() - t0) / 1e9;

	if (is_probe) {
		for (int i = 0; i < ncpu; i++)
			pthread_kill(probes[i].th, SIGTERM);	/* out of a long sleep */
		for (int i = 0; i < ncpu; i++)
			pthread_join(probes[i].th, NULL);
		for (int i = 0; i < ncpu; i++) {
			struct probe *p = &probes[i];
			for (int j = 0; j < p->nlate; j++)
				fprintf(out, "late %d %lld.%06lld %.1f\n", p->cpu,
					(long long)(p->late_at[j] / 1000000000),
					(long long)(p->late_at[j] % 1000000000 / 1000), p->late_by[j] / 1e6);
		}
		for (int i = 0; i < ncpu; i++) {
			struct probe *p = &probes[i];
			fprintf(out, "cpu %d prio=%s wakeups=%ld max_ms=%.1f ge1=%ld ge10=%ld ge100=%ld "
				"ge1000=%ld late_logged=%d\n", p->cpu, p->prio ? p->prio : "?", p->wakeups,
				p->max / 1e6, p->ge1, p->ge10, p->ge100, p->ge1000, p->nlate);
		}
	} else if (is_kick) {
		for (int i = 0; i < ncpu; i++) {
			pthread_kill(kicks[i].catcher, SIGTERM);
			pthread_kill(kicks[i].timer, SIGTERM);
		}
		for (int i = 0; i < ncpu; i++) {
			pthread_join(kicks[i].timer, NULL);
			pthread_join(kicks[i].catcher, NULL);
			fprintf(out, "kick %d from %d sent=%ld got=%ld\n", kicks[i].target, kicks[i].from,
				kicks[i].sent, kicks[i].got);
		}
	} else {
		pthread_kill(sleeper, SIGTERM);
		pthread_join(sleeper, NULL);
		fprintf(out, "sleep prio=%s wakeups=%ld late=%ld\n", sleep_prio, sleep_wakeups, sleep_late);
	}
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	fprintf(out, "# end t=%.3f self_user_us=%lld self_sys_us=%lld\n", secs,
		(long long)ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec,
		(long long)ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec);
	fclose(out);
	return 0;
}
