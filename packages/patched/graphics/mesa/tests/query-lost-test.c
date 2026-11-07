/* query-lost-test PART: nv30's queries whose report never lands (Mesa patch "nv30: a query
 * report that never comes is not waited for forever on the PS3", mesa-query-wait-deadline).
 * After ps3rsx cancels a submission (0053, PS3_RECOVER: -ECANCELED, nouveau_device::cancelled),
 * the QUERY_GETs that were in it never reach the GPU and their notifier slots stay pending.
 * nv30_query_read() took such a query as lost, then freed its report objects through
 * nv30_query_object_del(), which spun on the pending slot forever: the program hung at 100% of a
 * PPE thread. nv30_query.c and nouveau_heap.c are compiled in whole; the push buffer, the
 * winsys's cancellation and a GPU that writes the reports at each submission are faked here.
 * A hang is caught by alarm(): "FAIL PART: hangs".
 *   end       an occlusion query whose end went out in a cancelled submission, read with a wait
 *             (spinning, and with NV30_QUERY_WAIT=0): the read returns, "samples passed"
 *   steal     the same query left unread while 200 more fill the 124 report slots: taking its
 *             slot back returns, its result is "passed", the others' are theirs
 *   destroy   the same query destroyed unread, then 200 more: they all get their results
 *   elapsed   a TIME_ELAPSED query whose begin was in a cancelled submission and whose end
 *             landed: the read returns, no time went by (before: the end's raw timestamp)
 *   deadline  a report sent but never written, nothing cancelled (the GPU stopped): the read
 *             returns after about a second, "passed"
 *   failed    not the PS3 (the deadline is the PS3's only), an end whose submission the kernel failed (0061: the
 *             winsys counts it as cancelled): the read returns, "samples passed", where it spun for ever
 *   deadline-edge  a report that lands in the very call that finds the deadline passed (a fake clock): read again
 *             before it is given up (0062: it was taken as lost)
 *   begin-sent  a TIME_ELAPSED query destroyed while its reports are on their way, after a cancellation of some
 *             other submission: its slots are not handed out yet (0062: the begin counted as lost, freed)
 *   two-ctx   a second context takes slots back while the first has a TIME_ELAPSED begin not kicked yet:
 *             it passes over that begin instead of waiting for it, the first query still gets its time
 *             (0062: on a machine without a deadline it waited for ever, on the PS3 for a second)
 *   own-begin the same with the one context's own unkicked begin: kicked, then waited for (0062)
 *   stale     a query begun or ended on a push buffer a cancellation made stale: the context recovers first,
 *             so its reset and its report reach the GPU (0062: both dropped); a query whose begin was in a
 *             cancelled submission reads "lost" (1, or 0 for time), not the counter nobody reset
 *   no-slot   an end that finds every slot another context's unkicked begin reads "samples passed" (0062)
 *   disowned  the same begin destroyed before its end (disowned, still unkicked): passed over too (0062)
 *   wait-option  NV30_QUERY_WAIT's value is clamped on both sides (0063)
 *   late      a report that lands 100 ms late on the PS3: its real value (no loss inside the
 *             deadline)
 *   x86       not the PS3, a report that lands 1.2 s late: its real value (the deadline is the
 *             PS3's only)
 * late and x86 pass on any tree; the others need 0053's nouveau_device::cancelled (built with
 * -DHAVE_DEVICE_CANCELLED) and the fix. (tools/run-tests.sh mesa-query-lost; host-tests.sh query-lost) */
#include "nv30/nv30_query.c"
#include "nouveau_heap.c"
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static int fails, checks;
static const char *part = "?";

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- the fake GPU: a submission's QUERY_GETs get their reports written ------------------------ */
#define NSLOT ((4096 - 128) / 32)
#define NTFY_OFFSET 0x1400	/* the PS3's: the query DMA object starts 0x1400 into the notifier */
static uint32_t notifier_mem[(NTFY_OFFSET + 4096) / 4] __attribute__((aligned(16)));
static uint32_t *const notifier = notifier_mem + NTFY_OFFSET / 4;
static uint32_t pushmem[4096], pushmem2[4096];
static uint32_t *submitted, *submitted2;	/* the push buffers up to here went to the kernel */
static uint64_t gpu_clock = 1000;
static uint32_t next_value;		/* what the next occlusion report counts */
static struct nouveau_pushbuf push, push2;	/* push2: a second context's */
static unsigned resets;			/* QUERY_RESETs that reached the GPU */
static struct nouveau_device dev;
static int cancel_next;			/* ps3rsx rejects the next submission */
static int gpu_stopped;			/* reports of submissions accepted from now on are never written */
static unsigned late_ms;		/* they are written this long after the submission, by a thread */

#ifdef HAVE_DEVICE_CANCELLED
#define CANCELLED() (dev.cancelled)
#define CANCEL_BUMP() (dev.cancelled++)
#define STALE() (push.cancel_seen != dev.cancelled)
#define PB_STALE(pb) ((pb)->cancel_seen != dev.cancelled)
#define RECOVER() (push.cancel_seen = dev.cancelled)	/* nv30_state_lost() before the next draw */
#else
#define CANCELLED() 0u
#define CANCEL_BUMP() ((void)0)
#define STALE() 0
#define PB_STALE(pb) 0
#define RECOVER() ((void)0)
#endif

static void gpu_write(unsigned s, uint32_t value)
{
	volatile uint32_t *n = &notifier[s * 8];
	uint64_t ts = gpu_clock++;

	memcpy((void *)&n[0], &ts, 8);
	n[2] = value;
	__atomic_store_n(&n[3], 0, __ATOMIC_RELEASE);	/* done */
}

struct late { unsigned slot[NSLOT]; uint32_t value[NSLOT]; unsigned n; };

static void *late_writer(void *arg)
{
	struct late *l = arg;
	struct timespec ts = { late_ms / 1000, (late_ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
	for (unsigned i = 0; i < l->n; i++)
		gpu_write(l->slot[i], l->value[i]);
	free(l);
	return NULL;
}

/* the words since the last submission: the GPU sees them, unless the submission was cancelled
 * (none of it is sent) or the push buffer is stale (the winsys drops its words) */
static void submit_push(struct nouveau_pushbuf *pb, uint32_t **submittedp)
{
	uint32_t hdr = NV04_FIFO_PKHDR(7, NV30_3D_QUERY_GET, 1);
	uint32_t rhdr = NV04_FIFO_PKHDR(7, NV30_3D_QUERY_RESET, 1);
	uint32_t *end = pb->cur;
	struct late *l = NULL;
	uint32_t *submitted = *submittedp;
	bool stale = PB_STALE(pb);

	if (stale || cancel_next) {
		if (!stale)
			CANCEL_BUMP();
		cancel_next = 0;
		*submittedp = end;
		return;
	}
	for (uint32_t *p = submitted; p + 1 < end; p++) {
		if (*p == rhdr) {
			resets++;
			continue;
		}
		if (*p != hdr)
			continue;
		unsigned s = (p[1] & 0xffffff) / 32;	/* the query DMA object's base is 0 here */
		p++;
		if (gpu_stopped)
			continue;
		if (late_ms) {
			if (!l)
				l = calloc(1, sizeof(*l));
			l->slot[l->n] = s;
			l->value[l->n++] = next_value;
		} else {
			gpu_write(s, next_value);
		}
	}
	*submittedp = end;
	if (l) {
		pthread_t t;
		pthread_create(&t, NULL, late_writer, l);
		pthread_detach(t);
	}
}

static void submit(struct nouveau_pushbuf *pb)
{
	submit_push(pb, pb == &push2 ? &submitted2 : &submitted);
}

/* --- what nv30_query.c calls ------------------------------------------------------------------ */
int nouveau_pushbuf_space(struct nouveau_pushbuf *p, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)dwords; (void)relocs; (void)pushes;
	submit(p);		/* out of space: a kick */
	if (p == &push2)
		p->cur = submitted2 = pushmem2;
	else
		p->cur = submitted = pushmem;
	return 0;
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p)
{
	submit(p);
	return 0;
}
void nouveau_fence_ref(struct nouveau_fence *fence, struct nouveau_fence **ref, struct nouveau_screen *s)
{
	(void)s;
	*ref = fence;
}
bool nouveau_fence_wait(struct nouveau_fence *f, struct util_debug_callback *d)
{
	(void)f; (void)d; return true;	/* the fence follows the FIFO: it says nothing of a lost report */
}
static int clock_step;			/* fake clock: every call moves it this many ms (0: the real one) */
static int64_t fake_now;
static unsigned clock_calls, write_at_call;	/* write_at_call: the call that also lands slot_to_write's report */
static unsigned slot_to_write;
static void gpu_write(unsigned s, uint32_t value);
int64_t os_time_get_nano(void)
{
	struct timespec ts;

	if (clock_step) {
			fake_now += clock_step * 1000000LL;
		if (write_at_call && ++clock_calls == write_at_call)
			gpu_write(slot_to_write, 555);
		return fake_now;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
/* nv30_state_validate.c's recovery of a context after a cancelled submission, as far as queries see it */
void nv30_state_lost(struct nv30_context *nv30)
{
	struct nouveau_pushbuf *pb = nv30->base.pushbuf;

	PUSH_KICK(pb);		/* the stale words go nowhere */
#ifdef HAVE_DEVICE_CANCELLED
	pb->cancel_seen = dev.cancelled;
#endif
}
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{
	(void)once; func(val);
}

/* --- the context ------------------------------------------------------------------------------ */
static struct nv30_screen screen;
static struct nv30_context ctx, ctx2;
static struct pipe_context *pctx, *pctx2;
static struct nouveau_bo notify_bo;
static struct nouveau_object query_obj, eng3d;
static struct nv04_notify query_ntfy;
static struct nouveau_pushbuf_priv ppriv;
static struct nouveau_pushbuf_priv ppriv2;

static void setup(bool ps3, int query_wait_us)
{
	memset(notifier_mem, 0, sizeof(notifier_mem));
	memset(&dev, 0, sizeof(dev));
	cancel_next = gpu_stopped = 0;
	late_ms = 0;
	clock_step = 0;
	write_at_call = clock_calls = 0;
	resets = 0;
	notify_bo.map = notifier_mem;
	query_ntfy.offset = NTFY_OFFSET;
	query_obj.data = &query_ntfy;
	eng3d.oclass = NV40_3D_CLASS;
	screen.base.is_ps3 = ps3;
	screen.base.device = &dev;
	screen.notify = &notify_bo;
	screen.query = &query_obj;
	screen.eng3d = &eng3d;
	screen.query_wait_us = query_wait_us;
	if (screen.query_heap)
		nouveau_heap_destroy(&screen.query_heap);
	nouveau_heap_init(&screen.query_heap, 0, 4096 - 128);
	list_inithead(&screen.queries);
	ppriv.screen = &screen.base;
	push.user_priv = &ppriv;
	push.cur = submitted = pushmem;
	push.end = pushmem + 4096;
	ppriv2.screen = &screen.base;
	push2.user_priv = &ppriv2;
	push2.cur = submitted2 = pushmem2;
	push2.end = pushmem2 + 4096;
#ifdef HAVE_DEVICE_CANCELLED
	push.cancel_seen = push2.cancel_seen = 0;
#endif
	ctx.screen = &screen;
	ctx.base.pushbuf = &push;
	ctx.base.fence = NULL;
	ctx.base.pipe.screen = &screen.base.base;
	pctx = &ctx.base.pipe;
	nv30_query_init(pctx);
	ctx2.screen = &screen;
	ctx2.base.pushbuf = &push2;
	ctx2.base.fence = NULL;
	ctx2.base.pipe.screen = &screen.base.base;
	pctx2 = &ctx2.base.pipe;
	nv30_query_init(pctx2);
}

static void teardown(void)
{
	/* what the queries left in the list; a lost one must not hang here either */
	list_for_each_entry_safe(struct nv30_query_object, qo, &screen.queries, list) {
		struct nv30_query_object *p = qo;
		nv30_query_object_del(&screen, &p);
	}
}

static void hang(int sig)
{
	char msg[96];
	(void)sig;
	int n = snprintf(msg, sizeof(msg), "FAIL %s: hangs (a wait for a report that never comes)\n", part);
	if (write(1, msg, n) < 0) { }
	_exit(1);
}

static struct pipe_query *occlusion(void) { return pctx->create_query(pctx, PIPE_QUERY_OCCLUSION_COUNTER, 0); }

/* an occlusion query whose end goes out in a submission ps3rsx cancels; the context recovers */
static struct pipe_query *lost_occlusion(void)
{
	struct pipe_query *q = occlusion();
	pctx->begin_query(pctx, q);
	next_value = 77;
	cancel_next = 1;
	pctx->end_query(pctx, q);	/* kicks: cancelled */
	CHECK(CANCELLED() == 1, "the end's submission was not cancelled");
	RECOVER();
	return q;
}

/* n queries begun, ended and read, each with its own value */
static void traffic(unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		struct pipe_query *q = occlusion();
		union pipe_query_result r = { 0 };
		pctx->begin_query(pctx, q);
		next_value = 1000 + i;
		pctx->end_query(pctx, q);
		bool ok = pctx->get_query_result(pctx, q, true, &r);
		CHECK(ok && r.u64 == 1000 + i, "query %u after the lost one: %d %llu", i, ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, q);
	}
}

static void part_end(void)
{
	for (int qw = -1; qw <= 0; qw++) {
		setup(true, qw);
		struct pipe_query *q = lost_occlusion();
		union pipe_query_result r = { 0 };
		bool ok = pctx->get_query_result(pctx, q, false, &r);
		if (!ok)
			ok = pctx->get_query_result(pctx, q, true, &r);
		CHECK(ok && r.u64 == 1, "NV30_QUERY_WAIT=%d: the lost query gave %d %llu, want samples passed (1)",
		      qw, ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, q);
		traffic(4);
		teardown();
	}
}

static void part_steal(void)
{
	setup(true, -1);
	struct pipe_query *lq = lost_occlusion();
	struct pipe_query *q[200];
	for (unsigned i = 0; i < 200; i++) {	/* ended, read later: the slots run out */
		q[i] = occlusion();
		pctx->begin_query(pctx, q[i]);
		next_value = 2000 + i;
		pctx->end_query(pctx, q[i]);
	}
	for (unsigned i = 0; i < 200; i++) {
		union pipe_query_result r = { 0 };
		bool ok = pctx->get_query_result(pctx, q[i], true, &r);
		CHECK(ok && r.u64 == 2000 + i, "query %u: %d %llu", i, ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, q[i]);
	}
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, lq, true, &r);
	CHECK(ok && r.u64 == 1, "the lost query, its slot taken back: %d %llu, want 1", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, lq);
	teardown();
}

static void part_failed(void)
{
	for (int ps3 = 0; ps3 <= 1; ps3++) {
		setup(ps3, -1);
		struct pipe_query *q = lost_occlusion();	/* the fake GPU bumps cancelled, as the winsys does for a failed ioctl */
		union pipe_query_result r = { 0 };
		bool ok = pctx->get_query_result(pctx, q, true, &r);
		CHECK(ok && r.u64 == 1, "%s, a failed submission: %d %llu, want samples passed (1)", ps3 ? "PS3" : "x86",
		      ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, q);
		traffic(4);
		teardown();
	}
}

static void part_destroy(void)
{
	setup(true, -1);
	pctx->destroy_query(pctx, lost_occlusion());	/* unread */
	struct pipe_query *q[200];
	for (unsigned i = 0; i < 200; i++) {
		q[i] = occlusion();
		pctx->begin_query(pctx, q[i]);
		next_value = 3000 + i;
		pctx->end_query(pctx, q[i]);
	}
	for (unsigned i = 0; i < 200; i++) {
		union pipe_query_result r = { 0 };
		bool ok = pctx->get_query_result(pctx, q[i], true, &r);
		CHECK(ok && r.u64 == 3000 + i, "query %u: %d %llu", i, ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, q[i]);
	}
	teardown();
}

static void part_elapsed(void)
{
	setup(true, -1);
	struct pipe_query *q = pctx->create_query(pctx, PIPE_QUERY_TIME_ELAPSED, 0);
	pctx->begin_query(pctx, q);	/* its QUERY_GET waits in the push buffer */
	cancel_next = 1;
	PUSH_KICK(&push);		/* a draw's submission, with the begin's report: cancelled */
	CHECK(CANCELLED() == 1, "the begin's submission was not cancelled");
	RECOVER();
	gpu_clock = 5000000;
	pctx->end_query(pctx, q);	/* lands */
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	CHECK(ok && r.u64 == 0, "TIME_ELAPSED with its begin lost: %d %llu, want 0 (no time went by)",
	      ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q);
	traffic(4);
	teardown();
}

static void part_deadline(void)
{
	setup(true, -1);
	struct pipe_query *q = occlusion();
	pctx->begin_query(pctx, q);
	gpu_stopped = 1;
	pctx->end_query(pctx, q);	/* accepted, never written */
	int64_t t0 = os_time_get_nano();
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	int64_t ms = (os_time_get_nano() - t0) / 1000000;
	CHECK(ok && r.u64 == 1, "a report that never comes: %d %llu, want 1", ok, (unsigned long long)r.u64);
	CHECK(ms >= 900 && ms < 3000, "given up after %lld ms, want about 1000", (long long)ms);
	pctx->destroy_query(pctx, q);	/* lost for good: no second wait */
	CHECK((os_time_get_nano() - t0) / 1000000 < ms + 200, "the destroy waited again");
	gpu_stopped = 0;
	traffic(4);
	teardown();
}

/* the report lands in the very clock call that finds the deadline passed */
static void part_deadline_edge(void)
{
	setup(true, -1);
	struct pipe_query *q = occlusion();
	pctx->begin_query(pctx, q);
	gpu_stopped = 1;
	pctx->end_query(pctx, q);	/* accepted, never written... */
	clock_step = 400;		/* ... until the clock call that expires the wait: set end (call 1), 400, 800, 1200 ms */
	write_at_call = 4;
	slot_to_write = (unsigned)(((struct nv30_query *)q)->qo[1]->hw->start / 32);
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	CHECK(ok && r.u64 == 555, "a report landing as the deadline passed: %d %llu, want 555", ok, (unsigned long long)r.u64);
	clock_step = 0;
	pctx->destroy_query(pctx, q);
	gpu_stopped = 0;
	teardown();
}

/* a TIME_ELAPSED query that went through is destroyed while its reports are on their way, after
 * a cancellation of some other submission: its slots stay out of reach of the next query until the
 * reports are in (the begin counted as lost, and its slot was handed out while the report was still
 * to be written into it) */
static void part_begin_sent(void)
{
	setup(true, -1);
	struct pipe_query *fill[122];
	for (unsigned i = 0; i < 122; i++) {	/* all but two of the 124 slots taken, so a freed one is the next to be handed out */
		fill[i] = occlusion();
		pctx->begin_query(pctx, fill[i]);
		pctx->end_query(pctx, fill[i]);
	}
	struct pipe_query *q = pctx->create_query(pctx, PIPE_QUERY_TIME_ELAPSED, 0);
	pctx->begin_query(pctx, q);
	late_ms = 150;
	next_value = 111;
	pctx->end_query(pctx, q);	/* both reports on their way, 150 ms late */
	late_ms = 0;
	CANCEL_BUMP();			/* some other submission of the channel was cancelled meanwhile */
	RECOVER();
	pctx->destroy_query(pctx, q);
	struct pipe_query *q2 = occlusion();
	pctx->begin_query(pctx, q2);
	late_ms = 400;
	next_value = 777;
	pctx->end_query(pctx, q2);
	late_ms = 0;
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q2, true, &r);
	CHECK(ok && r.u64 == 777, "a query after a destroyed TIME_ELAPSED: %d %llu, want 777 (not the destroyed one's 111, "
	      "written into the slot it was handed)", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q2);
	usleep(600000);			/* the late writers */
	for (unsigned i = 0; i < 122; i++)
		pctx->destroy_query(pctx, fill[i]);
	teardown();
}

/* context 2 runs the slots out while context 1 has a TIME_ELAPSED begin that was not kicked */
static void part_two_ctx(void)
{
	for (int ps3 = 0; ps3 <= 1; ps3++) {
		setup(ps3, -1);
		struct pipe_query *a = pctx->create_query(pctx, PIPE_QUERY_TIME_ELAPSED, 0);
		pctx->begin_query(pctx, a);	/* in context 1's push buffer; nothing kicks it for now */
		struct pipe_query *q[200];
		int64_t t0 = os_time_get_nano();
		for (unsigned i = 0; i < 200; i++) {
			q[i] = pctx2->create_query(pctx2, PIPE_QUERY_OCCLUSION_COUNTER, 0);
			pctx2->begin_query(pctx2, q[i]);
			next_value = 6000 + i;
			pctx2->end_query(pctx2, q[i]);
		}
		for (unsigned i = 0; i < 200; i++) {
			union pipe_query_result r = { 0 };
			bool ok = pctx2->get_query_result(pctx2, q[i], true, &r);
			CHECK(ok && r.u64 == 6000 + i, "context 2, query %u: %d %llu", i, ok, (unsigned long long)r.u64);
			pctx2->destroy_query(pctx2, q[i]);
		}
		int64_t ms = (os_time_get_nano() - t0) / 1000000;
		CHECK(ms < 500, "%s: context 2 took %lld ms with context 1's begin pending", ps3 ? "PS3" : "x86", (long long)ms);
		pctx->end_query(pctx, a);
		union pipe_query_result r = { 0 };
		bool ok = pctx->get_query_result(pctx, a, true, &r);
		CHECK(ok && r.u64 > 0 && r.u64 < 1000000, "%s: context 1's TIME_ELAPSED: %d %llu, want a real difference",
		      ps3 ? "PS3" : "x86", ok, (unsigned long long)r.u64);
		pctx->destroy_query(pctx, a);
		teardown();
	}
}

/* one context, 130 TIME_ELAPSED begins and no end: the slots run out on its own unkicked begins */
static void part_own_begin(void)
{
	for (int ps3 = 0; ps3 <= 1; ps3++) {
		setup(ps3, -1);
		struct pipe_query *q[130];
		int64_t t0 = os_time_get_nano();
		for (unsigned i = 0; i < 130; i++) {
			q[i] = pctx->create_query(pctx, PIPE_QUERY_TIME_ELAPSED, 0);
			pctx->begin_query(pctx, q[i]);
		}
		for (unsigned i = 0; i < 130; i++) {
			pctx->end_query(pctx, q[i]);
			union pipe_query_result r = { 0 };
			bool ok = pctx->get_query_result(pctx, q[i], true, &r);
			CHECK(ok, "%s: query %u did not return", ps3 ? "PS3" : "x86", i);
			pctx->destroy_query(pctx, q[i]);
		}
		int64_t ms = (os_time_get_nano() - t0) / 1000000;
		CHECK(ms < 500, "%s: 130 begins took %lld ms", ps3 ? "PS3" : "x86", (long long)ms);
		teardown();
	}
}

/* every slot is another context's unkicked begin: an end that finds none reads "samples passed", not the
 * previous value of the query */
static void part_no_slot(void)
{
	setup(true, -1);
	struct pipe_query *b[124];
	for (unsigned i = 0; i < 124; i++) {
		b[i] = pctx2->create_query(pctx2, PIPE_QUERY_TIME_ELAPSED, 0);
		pctx2->begin_query(pctx2, b[i]);
	}
	struct pipe_query *q = occlusion();
	union pipe_query_result r = { .u64 = 0 };
	pctx->begin_query(pctx, q);
	pctx->end_query(pctx, q);
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	CHECK(ok && r.u64 == 1, "an end that found no slot: %d %llu, want 1 (samples passed)", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q);
	PUSH_KICK(&push2);		/* the begins' reports */
	for (unsigned i = 0; i < 124; i++) {
		pctx2->end_query(pctx2, b[i]);
		pctx2->get_query_result(pctx2, b[i], true, &r);
		pctx2->destroy_query(pctx2, b[i]);
	}
	teardown();
}

/* context 2 begins a TIME_ELAPSED query and destroys it before the end: its begin is disowned but still unkicked, and
 * context 1 running out of slots must pass it over as well */
static void part_disowned(void)
{
	for (int ps3 = 0; ps3 <= 1; ps3++) {
		setup(ps3, -1);
		struct pipe_query *a = pctx2->create_query(pctx2, PIPE_QUERY_TIME_ELAPSED, 0);
		pctx2->begin_query(pctx2, a);
		pctx2->destroy_query(pctx2, a);
		struct pipe_query *q[200];
		int64_t t0 = os_time_get_nano();
		for (unsigned i = 0; i < 200; i++) {
			q[i] = occlusion();
			pctx->begin_query(pctx, q[i]);
			next_value = 8000 + i;
			pctx->end_query(pctx, q[i]);
		}
		for (unsigned i = 0; i < 200; i++) {
			union pipe_query_result r = { 0 };
			bool ok = pctx->get_query_result(pctx, q[i], true, &r);
			CHECK(ok && r.u64 == 8000 + i, "%s: query %u: %d %llu", ps3 ? "PS3" : "x86", i, ok, (unsigned long long)r.u64);
			pctx->destroy_query(pctx, q[i]);
		}
		int64_t ms = (os_time_get_nano() - t0) / 1000000;
		CHECK(ms < 500, "%s: context 1 took %lld ms with a disowned begin of context 2 pending", ps3 ? "PS3" : "x86", (long long)ms);
		PUSH_KICK(&push2);
		teardown();
	}
}

/* a cancelled submission leaves the push buffer stale; the next begin and end come before any draw */
static void part_stale(void)
{
	setup(true, -1);
	struct pipe_query *q = occlusion();
	pctx->begin_query(pctx, q);
	cancel_next = 1;
	PUSH_KICK(&push);		/* the cancelled submission: this push buffer is stale now */
	CHECK(STALE(), "the push buffer is not stale");
	resets = 0;
	pctx->begin_query(pctx, q);	/* before any draw: nothing has recovered the context yet */
	next_value = 99;
	pctx->end_query(pctx, q);
	CHECK(resets == 1, "the begin's reset reached the GPU %u times, want 1", resets);
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	CHECK(ok && r.u64 == 99, "a query begun and ended on a stale push buffer: %d %llu, want 99", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q);

	/* only the end is on the stale buffer */
	struct pipe_query *q2 = occlusion();
	pctx->begin_query(pctx, q2);
	cancel_next = 1;
	PUSH_KICK(&push);		/* a draw's submission is cancelled: stale again, the end comes first */
	CHECK(STALE(), "the push buffer is not stale (2)");
	next_value = 98;
	pctx->end_query(pctx, q2);
	ok = pctx->get_query_result(pctx, q2, true, &r);
	CHECK(ok && r.u64 == 1, "a query whose begin went out in a cancelled submission: %d %llu, want 1 (samples passed): "
	      "the counter was not reset, the GPU's 98 is not this query's", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q2);

	/* the same with a draw in between that recovered the context: the end is on a good buffer, the begin is still lost */
	struct pipe_query *q3 = occlusion();
	pctx->begin_query(pctx, q3);
	cancel_next = 1;
	PUSH_KICK(&push);
	RECOVER();
	next_value = 97;
	pctx->end_query(pctx, q3);
	ok = pctx->get_query_result(pctx, q3, true, &r);
	CHECK(ok && r.u64 == 1, "a query whose begin was lost, ended after a draw recovered: %d %llu, want 1", ok,
	      (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q3);

	/* and a TIME_ELAPSED query whose begin went with the cancelled submission reads no time */
	struct pipe_query *q4 = pctx->create_query(pctx, PIPE_QUERY_TIME_ELAPSED, 0);
	pctx->begin_query(pctx, q4);
	cancel_next = 1;
	PUSH_KICK(&push);
	pctx->end_query(pctx, q4);
	ok = pctx->get_query_result(pctx, q4, true, &r);
	CHECK(ok && r.u64 == 0, "TIME_ELAPSED with a lost begin, ended on a stale buffer: %d %llu, want 0", ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q4);
	teardown();
}

/* NV30_QUERY_WAIT's value: both ends clamped (0063; a tree before it has no nv30_query_wait_option) */
static void part_wait_option(void)
{
	CHECK(nv30_query_wait_option(-1) == -1 && nv30_query_wait_option(0) == 0 && nv30_query_wait_option(250) == 250 &&
	      nv30_query_wait_option(1000000) == 1000000, "values in range changed");
	CHECK(nv30_query_wait_option(-2) == -1 && nv30_query_wait_option(-4294967297L) == -1 &&
	      nv30_query_wait_option(1000001) == 1000000 && nv30_query_wait_option(4294967296L) == 1000000,
	      "out of range: %d %d %d %d", nv30_query_wait_option(-2), nv30_query_wait_option(-4294967297L),
	      nv30_query_wait_option(1000001), nv30_query_wait_option(4294967296L));
}

static void late_report(bool ps3, unsigned ms)
{
	setup(ps3, -1);
	struct pipe_query *q = occlusion();
	pctx->begin_query(pctx, q);
	late_ms = ms;
	next_value = 4242;
	pctx->end_query(pctx, q);
	union pipe_query_result r = { 0 };
	bool ok = pctx->get_query_result(pctx, q, true, &r);
	CHECK(ok && r.u64 == 4242, "%s, a report %u ms late: %d %llu, want 4242", ps3 ? "PS3" : "x86", ms,
	      ok, (unsigned long long)r.u64);
	pctx->destroy_query(pctx, q);
	late_ms = 0;
	traffic(4);
	teardown();
}

int main(int argc, char **argv)
{
	part = argc > 1 ? argv[1] : "";
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGALRM, hang);
	alarm(5);
	bool cancel_part = strcmp(part, "late") && strcmp(part, "x86") && strcmp(part, "wait-option");
#ifndef HAVE_DEVICE_CANCELLED
	if (cancel_part) {
		printf("FAIL %s: this tree has no nouveau_device::cancelled (0053): a lost report can't be told\n", part);
		return 1;
	}
#endif
	(void)cancel_part;
	if (!strcmp(part, "end"))
		part_end();
	else if (!strcmp(part, "steal"))
		part_steal();
	else if (!strcmp(part, "destroy"))
		part_destroy();
	else if (!strcmp(part, "elapsed"))
		part_elapsed();
	else if (!strcmp(part, "failed"))
		part_failed();
	else if (!strcmp(part, "deadline"))
		part_deadline();
	else if (!strcmp(part, "deadline-edge"))
		part_deadline_edge();
	else if (!strcmp(part, "begin-sent"))
		part_begin_sent();
	else if (!strcmp(part, "two-ctx"))
		part_two_ctx();
	else if (!strcmp(part, "own-begin"))
		part_own_begin();
	else if (!strcmp(part, "stale"))
		part_stale();
	else if (!strcmp(part, "no-slot"))
		part_no_slot();
	else if (!strcmp(part, "disowned"))
		part_disowned();
	else if (!strcmp(part, "wait-option"))
		part_wait_option();
	else if (!strcmp(part, "late"))
		late_report(true, 100);
	else if (!strcmp(part, "x86"))
		late_report(false, 1200);
	else {
		printf("usage: %s end|steal|destroy|elapsed|failed|deadline|deadline-edge|begin-sent|two-ctx|own-begin|stale|no-slot|disowned|wait-option|late|x86\n", argv[0]);
		return 2;
	}
	nouveau_heap_destroy(&screen.query_heap);
	printf("%s %s: %d checks, %d failed\n", fails ? "FAIL" : "ok", part, checks, fails);
	return fails != 0;
}
