/* query-steal-test: nv30's queries when the hardware runs out of report slots (Mesa patch "nv30:
 * a query whose report slot is taken back keeps its result"). nv30_query.c and nouveau_heap.c
 * are compiled in whole; the pushbuffer, the fences and a GPU that writes the reports are faked
 * here. The query heap has 124 slots; Sauerbraten ends up to 2048 occlusion queries a frame and
 * reads their results a frame later, so the oldest slots are taken back while their queries
 * still point at them. Before the patch that was a use after free (AddressSanitizer stops at the
 * first result read); after it, every result must be the one the GPU wrote for that query.
 *   cases: Sauerbraten's pattern (300 queries, results read a frame later), the same with
 *   NV30_QUERY_KICK=0 (reports written only when their batch is submitted), and a random mix
 *   of occlusion, TIME_ELAPSED and TIMESTAMP queries: begun again unread, destroyed unread,
 *   results asked with and without waiting.
 * (tools/run-tests.sh mesa-query-steal; skipped without build/work/mesa) */
#include "nv30/nv30_query.c"
#include "nouveau_heap.c"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- the fake GPU: it reads QUERY_GET from the pushbuffer and writes the report --------------- */
#define NSLOT (4096 / 32)
static uint32_t notifier[4096 / 4] __attribute__((aligned(16)));
static uint32_t pushmem[4096];
static unsigned scanned;		/* pushbuffer words looked at */
static struct { int pending; uint64_t ts; uint32_t value; } slot[NSLOT];
static uint64_t gpu_clock = 1000;
static uint32_t next_value;		/* what the next occlusion report counts */
static int lazy;			/* NV30_QUERY_KICK=0: reports land when their batch is submitted */
static struct nouveau_pushbuf push;

static void gpu_write(unsigned s)
{
	volatile uint32_t *n = &notifier[s * 8];
	uint64_t ts = slot[s].ts;

	memcpy((void *)&n[0], &ts, 8);
	n[2] = slot[s].value;
	n[3] = 0;		/* done */
	slot[s].pending = 0;
}

static void gpu_complete_all(void)
{
	for (unsigned s = 0; s < NSLOT; s++)
		if (slot[s].pending)
			gpu_write(s);
}

/* the commands since the last look: a QUERY_GET queues a report in its slot */
static void gpu_scan(void)
{
	uint32_t hdr = NV04_FIFO_PKHDR(7, NV30_3D_QUERY_GET, 1);
	unsigned end = push.cur - pushmem;

	for (; scanned < end; scanned++) {
		if (pushmem[scanned] != hdr || scanned + 1 >= end)
			continue;
		uint32_t data = pushmem[++scanned];
		unsigned s = (data & 0xffffff) / 32;
		slot[s].pending = 1;
		slot[s].ts = gpu_clock++;
		slot[s].value = next_value;
		if (!lazy)
			gpu_write(s);
	}
	if (end > 3000) {	/* reuse the pushbuffer (after what was queued is seen) */
		push.cur = pushmem;
		scanned = 0;
	}
}

/* --- what nv30_query.c calls ------------------------------------------------------------------ */
int nouveau_pushbuf_space(struct nouveau_pushbuf *p, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)dwords; (void)relocs; (void)pushes;
	gpu_scan();
	p->cur = pushmem; scanned = 0;
	return 0;
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p)
{
	(void)p;
	gpu_scan();
	gpu_complete_all();	/* a submission: everything queued gets written */
	return 0;
}
static struct nouveau_fence fake_fence;
void nouveau_fence_ref(struct nouveau_fence *fence, struct nouveau_fence **ref, struct nouveau_screen *s)
{
	(void)s;
	*ref = fence;
}
bool nouveau_fence_flush(struct nouveau_fence *f) { (void)f; gpu_scan(); gpu_complete_all(); return true; }
bool nouveau_fence_wait(struct nouveau_fence *f, struct util_debug_callback *d)
{
	(void)f; (void)d; gpu_scan(); gpu_complete_all(); return true;
}
int64_t os_time_get_nano(void) { return 0; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{
	(void)once; func(val);
}

/* --- the context ------------------------------------------------------------------------------ */
static struct nv30_screen screen;
static struct nv30_context ctx;
static struct pipe_context *pctx;
static struct nouveau_bo notify_bo;
static struct nouveau_object query_obj, eng3d;
static struct nv04_notify query_ntfy;
static struct nouveau_pushbuf_priv ppriv;

static void setup(int kick)
{
	memset(notifier, 0, sizeof(notifier));
	memset(slot, 0, sizeof(slot));
	notify_bo.map = notifier;
	query_ntfy.offset = 0;
	query_obj.data = &query_ntfy;
	eng3d.oclass = NV40_3D_CLASS;
	screen.base.is_ps3 = false;
	screen.notify = &notify_bo;
	screen.query = &query_obj;
	screen.eng3d = &eng3d;
	screen.query_kick = kick;
	screen.query_wait_us = -1;
	lazy = kick != 1;
	if (screen.query_heap)
		nouveau_heap_destroy(&screen.query_heap);
	nouveau_heap_init(&screen.query_heap, 0, 4096 - 128);
	list_inithead(&screen.queries);
	ppriv.screen = &screen.base;
	push.user_priv = &ppriv;
	push.cur = pushmem; scanned = 0;
	push.end = pushmem + 4096;
	ctx.screen = &screen;
	ctx.base.pushbuf = &push;
	ctx.base.fence = kick != 1 ? &fake_fence : NULL;
	ctx.base.pipe.screen = &screen.base.base;
	pctx = &ctx.base.pipe;
	nv30_query_init(pctx);
}

static void teardown(void)
{
	/* what the queries left in the list (begun again or destroyed unread) */
	gpu_complete_all();
	list_for_each_entry_safe(struct nv30_query_object, qo, &screen.queries, list) {
		struct nv30_query_object *p = qo;
		nv30_query_object_del(&screen, &p);
	}
}

static void q_end(struct pipe_query *q, uint32_t value)
{
	next_value = value;
	pctx->end_query(pctx, q);
	gpu_scan();
}

static void q_begin(struct pipe_query *q)
{
	pctx->begin_query(pctx, q);
	gpu_scan();
}

/* Sauerbraten: n occlusion queries a frame, each frame's results read in the next one */
static void test_sauerbraten(int kick, unsigned n, unsigned frames)
{
	struct pipe_query *q[2][512];
	uint32_t want[2][512];

	setup(kick);
	for (unsigned f = 0; f < 2; f++)
		for (unsigned i = 0; i < n; i++)
			q[f][i] = pctx->create_query(pctx, PIPE_QUERY_OCCLUSION_COUNTER, 0);
	for (unsigned frame = 0; frame < frames; frame++) {
		unsigned cur = frame & 1, prev = cur ^ 1;
		for (unsigned i = 0; i < n; i++) {
			q_begin(q[cur][i]);
			want[cur][i] = frame * 1000 + i;
			q_end(q[cur][i], want[cur][i]);
		}
		if (frame == 0)
			continue;
		for (unsigned i = 0; i < n; i++) {
			union pipe_query_result r = { 0 };
			bool ok = pctx->get_query_result(pctx, q[prev][i], false, &r);
			if (!ok)	/* not there yet (KICK=0 submits on asking, so it is): wait */
				ok = pctx->get_query_result(pctx, q[prev][i], true, &r);
			CHECK(ok && r.u64 == want[prev][i], "kick %d frame %u query %u: %d %llu, want %u",
			      kick, frame, i, ok, (unsigned long long)r.u64, want[prev][i]);
		}
	}
	for (unsigned f = 0; f < 2; f++)
		for (unsigned i = 0; i < n; i++)
			pctx->destroy_query(pctx, q[f][i]);
	teardown();
}

/* random traffic: every result read must be what the GPU wrote for that query's last end */
static void test_random(int kick, unsigned steps, unsigned seed)
{
	enum { NQ = 200 };
	struct pipe_query *q[NQ];
	unsigned type[NQ];
	int state[NQ];		/* 0 idle, 1 begun, 2 ended (result due) */
	uint64_t want[NQ], t0[NQ];
	static const unsigned types[] = { PIPE_QUERY_OCCLUSION_COUNTER, PIPE_QUERY_OCCLUSION_COUNTER,
		PIPE_QUERY_OCCLUSION_PREDICATE, PIPE_QUERY_TIME_ELAPSED, PIPE_QUERY_TIMESTAMP };

	setup(kick);
	srand(seed);
	for (unsigned i = 0; i < NQ; i++) {
		type[i] = types[rand() % 5];
		q[i] = pctx->create_query(pctx, type[i], 0);
		state[i] = 0;
	}
	for (unsigned s = 0; s < steps; s++) {
		unsigned i = rand() % NQ, op = rand() % 10;
		if (op < 4 && type[i] != PIPE_QUERY_TIMESTAMP && state[i] != 1) {
			/* begin (also again, its last result unread) */
			if (type[i] == PIPE_QUERY_TIME_ELAPSED)
				t0[i] = gpu_clock;
			q_begin(q[i]);
			state[i] = 1;
		} else if (op < 7 && (state[i] == 1 || type[i] == PIPE_QUERY_TIMESTAMP)) {
			uint32_t v = rand() % 5000;
			uint64_t ts = gpu_clock;
			q_end(q[i], v);
			if (type[i] == PIPE_QUERY_TIME_ELAPSED)
				want[i] = ts - t0[i];
			else if (type[i] == PIPE_QUERY_TIMESTAMP)
				want[i] = ts;
			else if (type[i] == PIPE_QUERY_OCCLUSION_PREDICATE)
				want[i] = v != 0;
			else
				want[i] = v;
			state[i] = 2;
		} else if (op < 9 && state[i] == 2) {
			union pipe_query_result r = { 0 };
			bool wait = rand() & 1;
			bool ok = pctx->get_query_result(pctx, q[i], wait, &r);
			uint64_t got = type[i] == PIPE_QUERY_OCCLUSION_PREDICATE ? r.b : r.u64;
			CHECK(!wait || ok, "kick %d step %u query %u: no result with wait", kick, s, i);
			if (ok)
				CHECK(got == want[i], "kick %d step %u query %u (type %u): %llu, want %llu", kick,
				      s, i, type[i], (unsigned long long)got, (unsigned long long)want[i]);
		} else if (op == 9 && state[i] != 1) {
			/* destroyed, maybe unread, and made again */
			pctx->destroy_query(pctx, q[i]);
			q[i] = pctx->create_query(pctx, type[i], 0);
			state[i] = 0;
		}
	}
	for (unsigned i = 0; i < NQ; i++)
		pctx->destroy_query(pctx, q[i]);
	teardown();
}

int main(void)
{
	test_sauerbraten(1, 300, 6);
	test_sauerbraten(0, 300, 6);
	test_sauerbraten(1, 100, 4);	/* fits in the heap: nothing taken back */
	for (unsigned seed = 1; seed <= 20; seed++) {
		test_random(1, 20000, seed);
		test_random(0, 20000, seed);
	}
	nouveau_heap_destroy(&screen.query_heap);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
