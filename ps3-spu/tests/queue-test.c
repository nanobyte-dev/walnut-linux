/* queue-test: libspeq's queue against the fake backend (SPEQ_FAKE=1: bench.spu's ops built for
 * the host, one thread per "SPE"): errors, parking, freeing and respawn, a fault, many
 * submitters and waiters at once, close with jobs queued, fork. Then, given bench.spu, the
 * ELF and ABI check speq_open does. Exits nonzero on any failure. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "bench.h"
#include "speq-int.h"

static int failures;
static struct speq *q;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

static struct speq_job job(unsigned op, uint64_t a0, uint64_t a1)
{
	struct speq_job j;

	memset(&j, 0, sizeof(j));
	j.op = op;
	j.arg[0] = a0;
	j.arg[1] = a1;
	return j;
}

/* a job that waits must still end: 10 s is a hang */
static int wait10(struct speq_done *d)
{
	return spq_wait_ex(q, d, -1, 10000000);
}

#define THREADS	8
#define ROUNDS	300

static uint32_t counter[32] __attribute__((aligned(128)));
static unsigned long expected;
static int use_dl;	/* submitters give random deadlines (a deadline-ordered queue) */

/* A submitter: its own done word, batches of 1..20 jobs (nops, short spins, adds, sometimes
 * a failure), waited for with the default spin or none (futex at once) */
static void *submitter(void *arg)
{
	struct speq_done d __attribute__((aligned(128)));
	struct speq_job jobs[20];
	unsigned seed = (uintptr_t)arg, r, n, i, fail, k;
	int ret, bad = 0;

	for (r = 0; r < ROUNDS; r++) {
		memset(&d, 0, sizeof(d));
		n = 1 + rand_r(&seed) % 20;
		fail = rand_r(&seed) % 10 == 0 ? rand_r(&seed) % n : n;
		for (i = 0; i < n; i++) {
			k = rand_r(&seed) % 3;
			jobs[i] = i == fail ? job(BENCH_FAIL, -1000 - i, 0) :
				  k == 0 ? job(BENCH_NOP, 0, 0) :
				  k == 1 ? job(BENCH_SPIN, rand_r(&seed) % 50, 0) :
					   job(BENCH_ADD, (uintptr_t)counter, 0);
			if (i != fail && k == 2)
				__atomic_fetch_add(&expected, 1, __ATOMIC_RELAXED);
		}
		if (use_dl ? spq_submit_dl(q, jobs, n, &d, spq_deadline(rand_r(&seed) % 5000)) :
			     speq_submit(q, jobs, n, &d)) {
			bad++;
			continue;
		}
		ret = spq_wait_ex(q, &d, rand_r(&seed) % 2 ? -1 : 0, 10000000);
		if (ret == -ETIMEDOUT || d.pending || ret != (fail < n ? (int)(-1000 - fail) : 0))
			bad++;
	}
	return (void *)(uintptr_t)bad;
}

/* Two threads wait on one done word while a third submits to it */
static struct speq_done shared __attribute__((aligned(128)));

static void *waiter(void *arg)
{
	return (void *)(uintptr_t)(wait10(&shared) != 0);
}

int main(int argc, char **argv)
{
	struct speq_done d __attribute__((aligned(128)));
	struct speq_job jobs[200];
	struct spq_stats s, s0;
	struct speq *q2 = NULL;
	pthread_t th[THREADS];
	void *rv;
	unsigned i, bad;
	int ret, status;
	pid_t pid;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!getenv("SPEQ_FAKE")) {
		fprintf(stderr, "%s: run with SPEQ_FAKE=1\n", argv[0]);
		return 2;
	}
	check(speq_open(&q, "bench.spu", 4, 0) == 0 && q, "open 4 workers (fake)");
	if (!q)
		return 1;
	spq_tune(q, -1, 1000, 50);	/* park after 1 ms idle, free after 50 ms parked */

	ret = speq_run(q, &(struct speq_job){ .op = BENCH_NOP });
	check(ret == 0, "nop");
	ret = speq_run(q, &(struct speq_job){ .op = BENCH_FAIL, .arg = { -5 } });
	check(ret == -5, "an op's error comes back");
	ret = speq_run(q, &(struct speq_job){ .op = 99 });
	check(ret == -38, "unknown op: -ENOSYS");
	check(speq_submit(q, jobs, 1, (struct speq_done *)((char *)&d + 8)) == -EINVAL,
	      "unaligned done: -EINVAL");

	memset(&d, 0, sizeof(d));
	memset(counter, 0, sizeof(counter));
	for (i = 0; i < 200; i++)
		jobs[i] = job(BENCH_ADD, (uintptr_t)counter, 0);
	ret = speq_submit(q, jobs, 200, &d);
	check(ret == 0 && wait10(&d) == 0 && counter[0] == 200,
	      "200 jobs in one submit (ring of 64): each ran once");

	usleep(20000);
	spq_stats(q, &s);
	check(s.parks >= 4 && (s.tail_parked & SPEQ_PARKED_MASK) == 0xf, "idle workers park");
	check(speq_run(q, &(struct speq_job){ .op = BENCH_NOP }) == 0, "a job unparks them");
	usleep(200000);
	spq_stats(q, &s0);
	check(s0.frees >= 4, "parked workers free their contexts");
	check(speq_run(q, &(struct speq_job){ .op = BENCH_NOP }) == 0, "a job respawns one");
	spq_stats(q, &s);
	check(s.spawns > s0.spawns, "... a new context");

	spq_stats(q, &s0);
	ret = speq_run(q, &(struct speq_job){ .op = BENCH_TOUCH, .arg = { 0x1000, 16, 16 } });
	spq_stats(q, &s);
	check(ret == -EFAULT && s.faults == s0.faults + 1 && s.failed == s0.failed + 1,
	      "a fault fails its job (-EFAULT), the context is closed");
	ret = speq_run(q, &(struct speq_job){ .op = BENCH_SPIN, .arg = { 1 } });
	check(ret == 0, "... and the queue goes on");

	spq_tune(q, -1, 2000, 5000);
	memset(counter, 0, sizeof(counter));
	expected = 0;
	for (i = 0; i < THREADS; i++)
		pthread_create(&th[i], NULL, submitter, (void *)(uintptr_t)(i + 1));
	bad = 0;
	for (i = 0; i < THREADS; i++) {
		pthread_join(th[i], &rv);
		bad += (uintptr_t)rv;
	}
	check(!bad, "8 submitters x 300 batches: every batch's error and count right");
	check(counter[0] == expected, "... every add ran exactly once");

	memset(&shared, 0, sizeof(shared));
	for (i = 0; i < 50; i++)
		jobs[i] = job(BENCH_SPIN, 200, 0);
	speq_submit(q, jobs, 50, &shared);
	pthread_create(&th[0], NULL, waiter, NULL);
	pthread_create(&th[1], NULL, waiter, NULL);
	pthread_join(th[0], &rv);
	bad = (uintptr_t)rv;
	pthread_join(th[1], &rv);
	bad += (uintptr_t)rv;
	check(!bad && !shared.pending, "two waiters on one done word both wake");

	/* speq_wait_timeout: a deadline, no spin, a poll */
	{
		struct speq *q2 = NULL, *q3 = NULL;
		double t0, dt;

		memset(&d, 0, sizeof(d));
		jobs[0] = job(BENCH_SPIN, 300000, 0);
		speq_submit(q, jobs, 1, &d);
		t0 = spq_now_us();
		ret = speq_wait_timeout(q, &d, 20000, SPEQ_WAIT_NOSPIN);
		dt = spq_now_us() - t0;
		check(ret == -ETIMEDOUT && dt >= 19000 && dt < 200000 && (d.pending & ~SPEQ_WAITERS),
		      "wait_timeout 20 ms on a 300 ms job: -ETIMEDOUT in time, the job still runs");
		t0 = spq_now_us();
		ret = speq_wait_timeout(q, &d, 0, 0);
		check(ret == -ETIMEDOUT && spq_now_us() - t0 < 5000, "... a poll: -ETIMEDOUT at once");
		check(speq_wait_timeout(q, &d, -1, SPEQ_WAIT_NOSPIN) == 0 && !d.pending,
		      "... then waiting it out: 0");
		memset(&d, 0, sizeof(d));
		speq_submit(q, jobs, 0, &d);
		check(speq_wait_timeout(q, &d, 0, 0) == 0, "a poll of nothing pending: 0");
		check(speq_wait_timeout(q, &d, 0, 0x8) == -EINVAL, "unknown flags: -EINVAL");
		memset(&d, 0, sizeof(d));
		jobs[0] = job(BENCH_SPIN, 1000, 0);
		speq_submit(q, jobs, 1, &d);
		check(speq_wait_timeout(q, &d, 1000000, SPEQ_WAIT_NOSPIN) == 0,
		      "a 1 ms job, no spin, 1 s deadline: 0");

		/* the SPE budget: nworkers 0 takes what the open queues don't hold */
		check(speq_open(&q2, "bench.spu", 0, 0) == 0 && speq_nworkers(q2) == 2,
		      "next to 4 workers of 6 SPEs, nworkers 0 gets 2");
		check(speq_open(&q3, "bench.spu", 0, 0) == 0 && speq_nworkers(q3) == 1,
		      "... and with none left, 1");
		check(q3 && speq_run(q3, &(struct speq_job){ .op = BENCH_NOP }) == 0 &&
		      q2 && speq_run(q2, &(struct speq_job){ .op = BENCH_NOP }) == 0,
		      "... both run jobs");
		speq_close(q3);
		speq_close(q2);
		check(speq_open(&q2, "bench.spu", 0, SPEQ_OPEN_BULK) == 0 && speq_nworkers(q2) == 2,
		      "bulk, next to 4: 2");
		speq_close(q2);
		check(speq_open(&q2, "bench.spu", 0, 0x10) == -EINVAL, "unknown open flags: -EINVAL");
		speq_tune(q, -1, 2000, 5000);	/* the public spq_tune */
	}

	fflush(stdout);
	pid = fork();
	if (!pid) {
		memset(&d, 0, sizeof(d));
		ret = speq_submit(q, jobs, 1, &d);
		speq_close(q);
		_exit(ret == -EIO ? 0 : 1);
	}
	waitpid(pid, &status, 0);
	check(WIFEXITED(status) && !WEXITSTATUS(status), "a forked child gets -EIO");
	check(speq_run(q, &(struct speq_job){ .op = BENCH_NOP }) == 0, "... the parent goes on");

	spq_stats(q, &s);
	printf("stats: spawns %lu parks %lu unparks %lu frees %lu faults %lu claims %lu "
	       "wakes %lu spurious %lu lost %lu\n", s.spawns, s.parks, s.unparks, s.frees,
	       s.faults, s.claims, s.wakes, s.spurious, s.lost);

	memset(&d, 0, sizeof(d));
	for (i = 0; i < 64; i++)
		jobs[i] = job(BENCH_SPIN, 2000, 0);
	speq_submit(q, jobs, 64, &d);
	speq_close(q);
	check(!d.pending && d.error == -ECANCELED, "close: queued jobs fail with -ECANCELED");

	check(speq_open(&q, "bench.spu", 0, SPEQ_OPEN_BULK) == 0 && speq_nworkers(q) == 4,
	      "bulk queue alone, nworkers 0: 4 of 6 (two left to deadline queues)");
	speq_close(q);

	/* deadline order (the pool's queue): the earliest deadline first, ties in arrival order */
	{
		static uint32_t tick[32] __attribute__((aligned(128)));
		static uint32_t got[SPEQ_SCAN + 1] __attribute__((aligned(16)));
		static uint64_t dls[SPEQ_SCAN];
		struct speq_done d2 __attribute__((aligned(128)));
		unsigned k, rank, seed = 7, ok = 1;
		uint64_t base = spq_deadline(0);
		double t0;

		check(spq_open_ex(&q, "bench.spu", 1, SPQ_OPEN_DEADLINE) == 0 && q,
		      "deadline queue, 1 worker");
		memset(tick, 0, sizeof(tick));
		memset(&d, 0, sizeof(d));
		memset(&d2, 0, sizeof(d2));
		jobs[0] = (struct speq_job){ .op = BENCH_TICKET, .arg = { (uintptr_t)tick,
				(uintptr_t)&got[SPEQ_SCAN], 30000 } };
		spq_submit_dl(q, jobs, 1, &d, base + 1000000);	/* holds the worker 30 ms */
		t0 = spq_now_us();
		while (!__atomic_load_n(&tick[0], __ATOMIC_ACQUIRE) && spq_now_us() - t0 < 5e6)
			usleep(100);
		for (i = 0; i < SPEQ_SCAN; i++) {	/* queued behind it, deadlines with ties */
			dls[i] = base + 1000 + rand_r(&seed) % 6;
			jobs[0] = (struct speq_job){ .op = BENCH_TICKET, .arg = { (uintptr_t)tick,
					(uintptr_t)&got[i], 0 } };
			spq_submit_dl(q, jobs, 1, &d2, dls[i]);
		}
		check(wait10(&d) == 0 && wait10(&d2) == 0, "... every job ran");
		for (i = 0; i < SPEQ_SCAN; i++) {
			for (rank = 1, k = 0; k < SPEQ_SCAN; k++)
				rank += dls[k] < dls[i] || (dls[k] == dls[i] && k < i);
			ok &= got[i] == rank;
		}
		check(ok, "16 jobs queued behind a running one start earliest deadline first, ties in "
		      "arrival order");
		memset(&d, 0, sizeof(d));
		memset(&d2, 0, sizeof(d2));
		memset(tick, 0, sizeof(tick));
		jobs[0] = (struct speq_job){ .op = BENCH_TICKET, .arg = { (uintptr_t)tick,
				(uintptr_t)&got[SPEQ_SCAN], 30000 } };
		spq_submit_dl(q, jobs, 1, &d, 0);
		while (!__atomic_load_n(&tick[0], __ATOMIC_ACQUIRE) && spq_now_us() - t0 < 10e6)
			usleep(100);
		jobs[0].arg[1] = (uintptr_t)&got[0];
		jobs[0].arg[2] = 0;
		speq_submit(q, jobs, 1, &d2);	/* no deadline: after the deadlines */
		jobs[0].arg[1] = (uintptr_t)&got[1];
		spq_submit_dl(q, jobs, 1, &d2, spq_deadline(1e6));
		check(wait10(&d) == 0 && wait10(&d2) == 0 && got[1] == 1 && got[0] == 2,
		      "... speq_submit's jobs (no deadline) after those with one");
		ret = speq_run(q, &(struct speq_job){ .op = BENCH_TOUCH, .arg = { 0x1000, 16, 16 } });
		check(ret == -EFAULT && speq_run(q, &(struct speq_job){ .op = BENCH_NOP }) == 0,
		      "... a fault fails its job, the queue goes on");
		memset(&d, 0, sizeof(d));
		for (i = 0; i < 64; i++)
			jobs[i] = job(BENCH_SPIN, 2000, 0);
		spq_submit_dl(q, jobs, 64, &d, spq_deadline(5000));
		speq_close(q);
		check(!d.pending && d.error == -ECANCELED, "... close: queued jobs fail with -ECANCELED");

		/* many submitters, random deadlines, three workers claiming by deadline */
		check(spq_open_ex(&q, "bench.spu", 3, SPQ_OPEN_DEADLINE) == 0, "deadline queue, 3 workers");
		spq_tune(q, -1, 2000, 5000);
		memset(counter, 0, sizeof(counter));
		expected = 0;
		use_dl = 1;
		for (i = 0; i < THREADS; i++)
			pthread_create(&th[i], NULL, submitter, (void *)(uintptr_t)(i + 1));
		bad = 0;
		for (i = 0; i < THREADS; i++) {
			pthread_join(th[i], &rv);
			bad += (uintptr_t)rv;
		}
		use_dl = 0;
		spq_stats(q, &s);
		check(!bad && counter[0] == expected,
		      "8 submitters x 300 batches, random deadlines: every batch right, every add once");
		printf("stats: claims %lu lost %lu spurious %lu\n", s.claims, s.lost, s.spurious);
		speq_close(q);

		/* SPQ_OPEN_FREE: no more workers than the SPEs other queues leave */
		check(speq_open(&q, "bench.spu", 4, 0) == 0 &&
		      spq_open_ex(&q2, "bench.spu", 3, SPQ_OPEN_FREE) == 0 && speq_nworkers(q2) == 2,
		      "SPQ_OPEN_FREE next to 4 of 6: 3 asked, 2 given");
		speq_close(q2);
		speq_close(q);
	}

	/* one worker; the queue must not lose a job when the only worker parks and frees */
	check(speq_open(&q, "bench.spu", 1, 0) == 0, "open 1 worker");
	spq_tune(q, -1, 200, 2);
	bad = 0;
	for (i = 0; i < 100; i++) {
		if (speq_run(q, &(struct speq_job){ .op = BENCH_NOP }))
			bad++;
		usleep(i % 3 ? 100 : 5000);
	}
	spq_stats(q, &s);
	check(!bad, "100 jobs across park and free cycles");
	printf("stats: spawns %lu parks %lu unparks %lu frees %lu\n", s.spawns, s.parks,
	       s.unparks, s.frees);
	speq_close(q);

	if (argc > 1) {	/* bench.spu: the real ELF, as speq_open checks it */
		FILE *f = fopen(argv[1], "rb");
		static uint8_t img[1 << 20];
		size_t n = f ? fread(img, 1, sizeof(img), f) : 0;
		uint32_t entry = 0;

		if (f)
			fclose(f);
		check(n && spq_elf_check(img, n, &entry) == 0 && entry, "bench.spu: SPU ELF, ABI");
		if (n) {
			uint8_t save = img[19];

			img[19] = 0;	/* e_machine, big-endian */
			check(spq_elf_check(img, n, NULL) == -EPROTO, "not an SPU ELF: -EPROTO");
			img[19] = save;
		}
	}
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
