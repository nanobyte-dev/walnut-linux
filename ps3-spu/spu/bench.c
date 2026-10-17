/* bench.spu: the operations speq-bench measures the queue, the decrementer, the 0x2104 path
 * and DMA with (spu/bench.h). Links libspeq-spu.a, which gives main. */
#include "bench.h"
#include "speq-spu.h"

#define CHUNK	16384	/* the largest single DMA */
#define NBUF	4

static SPEQ_LOCAL uint8_t buf[NBUF][CHUNK] __attribute__((aligned(128)));
static SPEQ_LOCAL struct bench_result res;
static SPEQ_LOCAL uint8_t line[128] __attribute__((aligned(128)));
static SPEQ_LOCAL uint32_t word[4] __attribute__((aligned(16)));

static void result(const struct speq_job *j, uint64_t v0, uint64_t v1, uint64_t v2, uint64_t v3)
{
	if (!j->arg[BENCH_RESULT])
		return;
	res.v[0] = v0;
	res.v[1] = v1;
	res.v[2] = v2;
	res.v[3] = v3;
	speq_put(&res, j->arg[BENCH_RESULT], sizeof(res), 0);
	speq_wait_tags(1);	/* res is reused by the next job */
}

static uint32_t us_ticks(uint64_t us)
{
	return us * speq_tb_hz() / 1000000;
}

static int op_nop(const struct speq_job *j)
{
	return 0;
}

static int op_spin(const struct speq_job *j)
{
	uint32_t t0 = speq_dec_read(), n = us_ticks(j->arg[0]);

	while (t0 - speq_dec_read() < n)
		;
	if (j->arg[1] & 1)
		speq_ibox_write(1);
	return 0;
}

/* Bandwidth: 16 KB commands round-robin over 4 buffers, never waiting in between (the MFC
 * queue's 16 entries throttle us). The data is meaningless. */
static int op_dma(const struct speq_job *j)
{
	uint64_t ea = j->arg[0], bytes = j->arg[1] & ~(uint64_t)(CHUNK - 1), off;
	uint32_t t0, t1, pass, k = 0;

	if (!bytes || (ea & 127))
		return -22;
	t0 = speq_dec_read();
	for (pass = 0; pass < j->arg[3]; pass++)
		for (off = 0; off < bytes; off += CHUNK, k++) {
			if (j->arg[2])
				speq_put(buf[k % NBUF], ea + off, CHUNK, k % NBUF);
			else
				speq_get(buf[k % NBUF], ea + off, CHUNK, k % NBUF);
		}
	speq_wait_tags((1u << NBUF) - 1);
	t1 = speq_dec_read();
	result(j, t0 - t1, bytes * j->arg[3], 0, 0);
	return 0;
}

static int op_touch(const struct speq_job *j)
{
	uint64_t off, stride = j->arg[2] ? j->arg[2] : 4096;
	uint32_t t0, t1, n = 0;

	t0 = speq_dec_read();
	for (off = 0; off < j->arg[1]; off += stride, n++)
		speq_put(buf[0], j->arg[0] + off, 16, 1);
	speq_wait_tags(2);
	t1 = speq_dec_read();
	result(j, t0 - t1, n, 0, 0);
	return 0;
}

static int op_getpid(const struct speq_job *j)
{
	uint32_t t0, t1, i;
	long long pid = 0;

	t0 = speq_dec_read();
	for (i = 0; i < j->arg[0]; i++)
		pid = speq_syscall(SPEQ_NR_getpid, 0, 0, 0, 0, 0, 0);
	t1 = speq_dec_read();
	result(j, t0 - t1, pid, 0, 0);
	return 0;
}

/* P1: how a store to the reserved line (or another word of it, or the next line) wakes the
 * event wait. Uses the runtime's event mask (LLR_LOST | DECREMENTER) and the decrementer as
 * the timeout; the runtime rearms it after the job. */
static int op_watch(const struct speq_job *j)
{
	uint32_t t0, t1, ev = 0, wakes = 0, limit = us_ticks(j->arg[2]);

	speq_event_drain();
	speq_getllar(line, j->arg[0]);
	speq_dec_write(limit ? limit : 0x7fffffff);
	word[0] = 1;
	speq_put(word, j->arg[1], 4, 2);
	speq_wait_tags(4);
	t0 = speq_dec_read();
	do {	/* a decrementer event left from before the rearm would end it early */
		ev = speq_event_wait();
		wakes++;
	} while (!(ev & SPEQ_LLR_LOST_EVENT) && (int32_t)speq_dec_read() >= 0);
	t1 = speq_dec_read();
	word[0] = 2;
	speq_put(word, j->arg[1], 4, 2);
	speq_wait_tags(4);
	result(j, t0 - t1, ev, wakes, 0);
	return 0;
}

/* P4: the decrementer against the PPE's clock: the PPE sets the flag a known time after the
 * submit */
static int op_dec(const struct speq_job *j)
{
	uint32_t d0 = speq_dec_read(), d1, polls = 0;

	do {
		speq_get(word, j->arg[0], 4, 3);
		speq_wait_tags(8);
		polls++;
	} while (!word[0]);
	d1 = speq_dec_read();
	result(j, d0 - d1, polls, d0, d1);
	return 0;
}

static int op_fail(const struct speq_job *j)
{
	return (int)j->arg[0];
}

static int op_add(const struct speq_job *j)
{
	do {
		speq_getllar(line, j->arg[0]);
		(*(uint32_t *)line)++;
	} while (speq_putllc(line, j->arg[0]));
	return 0;
}

static int op_ticket(const struct speq_job *j)
{
	uint32_t t, t0, n = us_ticks(j->arg[2]);

	if (j->arg[1] & 3)
		return -22;
	do {
		speq_getllar(line, j->arg[0]);
		t = (*(uint32_t *)line)++;
	} while (speq_putllc(line, j->arg[0]));
	word[(j->arg[1] & 15) >> 2] = t;
	speq_put(&word[(j->arg[1] & 15) >> 2], j->arg[1], 4, 0);
	speq_wait_tags(1);	/* word is reused by the next job */
	t0 = speq_dec_read();
	while (t0 - speq_dec_read() < n)
		;
	return 0;
}

SPEQ_PROGRAM(bench, "bench.spu", BENCH_NOPS) = {
	[BENCH_NOP] = op_nop,
	[BENCH_SPIN] = op_spin,
	[BENCH_DMA] = op_dma,
	[BENCH_TOUCH] = op_touch,
	[BENCH_GETPID] = op_getpid,
	[BENCH_WATCH] = op_watch,
	[BENCH_DEC] = op_dec,
	[BENCH_FAIL] = op_fail,
	[BENCH_ADD] = op_add,
	[BENCH_TICKET] = op_ticket,
};
