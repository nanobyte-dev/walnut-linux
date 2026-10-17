/* bench.spu's operations, shared with speq-bench (tools/speq-bench.c). Results go to
 * arg[BENCH_RESULT], a 32-byte struct bench_result (16-byte aligned) the op puts. Times are in
 * decrementer ticks (speq_tb_hz per second). */
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

enum {
	BENCH_NOP,	/* nothing: the queue's latency */
	BENCH_SPIN,	/* spin arg0 us; arg1 & 1: then write the interrupt mailbox (P2 ibox) */
	BENCH_DMA,	/* arg0 EA, arg1 bytes (16 KB multiple), arg2 0 get / 1 put, arg3 passes:
			 * v0 ticks, v1 bytes moved */
	BENCH_TOUCH,	/* a 16-byte put every arg2 bytes over arg1 bytes at arg0 (fresh pages:
			 * the fault path; an unmapped arg0: a fatal fault): v0 ticks, v1 puts */
	BENCH_GETPID,	/* arg0 times getpid through stop 0x2104: v0 ticks, v1 pid */
	BENCH_WATCH,	/* reserve the line at arg0 (getllar), put 1 to the u32 at arg1, wait
			 * for an event or arg2 us, put 2 there: v0 ticks waited, v1 events,
			 * v2 wakes (P1) */
	BENCH_DEC,	/* poll the u32 at arg0 until nonzero: v0 decrementer ticks taken, v1
			 * polls, v2 first value, v3 last value (P4) */
	BENCH_FAIL,	/* returns arg0 as the job's error */
	BENCH_ADD,	/* adds 1 to the u32 at arg0 (128-aligned line) with getllar/putllc */
	BENCH_TICKET,	/* takes a ticket: adds 1 to the u32 at arg0 (128-aligned line), puts the
			 * value it had to the u32 at arg1 (4-aligned), then spins arg2 us: the
			 * order jobs started in (queue-test, the deadline order) */
	BENCH_NOPS
};

#define BENCH_RESULT	13	/* arg index of the result EA (0: no result) */

struct bench_result {
	uint64_t v[4];
} __attribute__((aligned(16)));

#endif
