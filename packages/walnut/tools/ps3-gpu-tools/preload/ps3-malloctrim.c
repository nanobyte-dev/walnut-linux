/*
 * ps3-malloctrim.so: gives a game's free malloc memory back to the kernel every few seconds
 * (docs/MEMORY.md). glibc keeps freed blocks inside its arenas: the main heap returns only its
 * top, a thread's arena only the end of its heap, and a hole in the middle stays resident until
 * the kernel swaps it out (a dirty page of garbage, written to zram or the disk and read back if
 * malloc hands it out again). malloc_trim(0) walks every arena's free chunks and drops the whole
 * pages inside them (MADV_DONTNEED: no write-back, swapped ones just lose their swap slot); a page
 * malloc reuses later comes back as a zero page (a minor fault, no I/O).
 *
 *   LD_PRELOAD=/usr/lib/ps3/lib/ps3-malloctrim.so PS3_MALLOC_TRIM=<seconds> <game>
 *
 * PS3_MALLOC_TRIM       the period in seconds (decimals allowed, at least 0.1); unset, empty or 0:
 *                       the library does nothing
 * PS3_MALLOC_TRIM_COMM  only in the process with this name (/proc/self/comm, 15 characters), so a
 *                       launcher (stdbuf, ps3-game, a shell) that passes LD_PRELOAD on is left alone
 * PS3_MALLOC_TRIM_PAD   bytes left at the top of the main heap (malloc_trim's argument, default 0)
 * PS3_MALLOC_TRIM_LOG   1: a line on stderr per trim: "ps3-malloctrim: <n> <ms> ms rss <kB> -> <kB>
 *                       swap <kB> -> <kB> freed <0|1>" (the game's VmRSS and VmSwap around it)
 *
 * One thread with a 64 KiB stack and every signal blocked, asleep between trims. It never calls
 * malloc (the log is formatted on its stack and written with write(2)), so glibc gives it no arena
 * of its own. malloc_trim takes each arena's lock while it walks that arena: a thread of the game
 * that mallocs from the same arena meanwhile waits (the log's ms is the whole walk, all arenas).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct timespec period;
static size_t pad;
static int log_on;

/* VmRSS and VmSwap (kB) from /proc/self/status, without malloc */
static void vm(long *rss, long *swap)
{
	char buf[4096], *p;
	ssize_t n;
	int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);

	*rss = *swap = -1;
	if (fd < 0)
		return;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	if ((p = strstr(buf, "\nVmRSS:")))
		*rss = strtol(p + 7, NULL, 10);
	if ((p = strstr(buf, "\nVmSwap:")))
		*swap = strtol(p + 8, NULL, 10);
}

static double ms_since(const struct timespec *a)
{
	struct timespec b;

	clock_gettime(CLOCK_MONOTONIC, &b);
	return (b.tv_sec - a->tv_sec) * 1e3 + (b.tv_nsec - a->tv_nsec) / 1e6;
}

static void *run(void *arg)
{
	struct timespec next, t0;
	unsigned long n = 0;

	(void)arg;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;) {
		long r0 = 0, s0 = 0, r1 = 0, s1 = 0;
		int freed;

		next.tv_sec += period.tv_sec;
		next.tv_nsec += period.tv_nsec;
		if (next.tv_nsec >= 1000000000L) {
			next.tv_sec++;
			next.tv_nsec -= 1000000000L;
		}
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
			;
		if (log_on)
			vm(&r0, &s0);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		freed = malloc_trim(pad);
		n++;
		if (log_on) {
			char line[200];
			double ms = ms_since(&t0);
			int len;

			vm(&r1, &s1);
			len = snprintf(line, sizeof(line), "ps3-malloctrim: %lu %d.%03d ms rss %ld -> %ld swap %ld -> %ld freed %d\n",
				       n, (int)ms, (int)((ms - (int)ms) * 1000), r0, r1, s0, s1, freed);
			if (len > 0 && write(2, line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1) < 0)
				log_on = 0;
		}
		/* a trim that took longer than the period: start the next period from now */
		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (t0.tv_sec > next.tv_sec || (t0.tv_sec == next.tv_sec && t0.tv_nsec > next.tv_nsec))
			next = t0;
	}
	return NULL;
}

__attribute__((constructor)) static void start(void)
{
	const char *s = getenv("PS3_MALLOC_TRIM"), *want = getenv("PS3_MALLOC_TRIM_COMM");
	const char *p = getenv("PS3_MALLOC_TRIM_PAD"), *l = getenv("PS3_MALLOC_TRIM_LOG");
	char comm[32] = "", *end;
	pthread_attr_t a;
	pthread_t t;
	sigset_t all, old;
	double sec;
	int fd;

	if (!s || !*s)
		return;
	sec = strtod(s, &end);
	if (end == s || !(sec > 0))
		return;
	if (sec < 0.1)
		sec = 0.1;
	if (want && *want) {
		fd = open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			ssize_t r = read(fd, comm, sizeof(comm) - 1);

			close(fd);
			comm[r > 0 ? r : 0] = 0;
			comm[strcspn(comm, "\n")] = 0;
		}
		if (strncmp(comm, want, 15))	/* the kernel keeps 15 characters of the name */
			return;
	}
	period.tv_sec = (time_t)sec;
	period.tv_nsec = (long)((sec - (double)period.tv_sec) * 1e9);
	pad = p && *p ? strtoul(p, NULL, 10) : 0;
	log_on = l && *l == '1';
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 64 << 10);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	/* the thread takes no signals: the game's handlers stay on its own threads */
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	if (pthread_create(&t, &a, run, NULL) == 0)
		pthread_setname_np(t, "ps3-malloctrim");
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	pthread_attr_destroy(&a);
}
