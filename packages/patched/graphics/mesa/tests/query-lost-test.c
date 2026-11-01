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
static uint32_t pushmem[4096];
static uint32_t *submitted;		/* the push buffer up to here went to the kernel */
static uint64_t gpu_clock = 1000;
static uint32_t next_value;		/* what the next occlusion report counts */
static struct nouveau_pushbuf push;
static struct nouveau_device dev;
static int cancel_next;			/* ps3rsx rejects the next submission */
static int gpu_stopped;			/* reports of submissions accepted from now on are never written */
static unsigned late_ms;		/* they are written this long after the submission, by a thread */

#ifdef HAVE_DEVICE_CANCELLED
#define CANCELLED() (dev.cancelled)
#define CANCEL_BUMP() (dev.cancelled++)
#define STALE() (push.cancel_seen != dev.cancelled)
#define RECOVER() (push.cancel_seen = dev.cancelled)	/* nv30_state_lost() before the next draw */
#else
#define CANCELLED() 0u
#define CANCEL_BUMP() ((void)0)
#define STALE() 0
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
static void submit(void)
{
	uint32_t hdr = NV04_FIFO_PKHDR(7, NV30_3D_QUERY_GET, 1);
	uint32_t *end = push.cur;
	struct late *l = NULL;

	if (STALE() || cancel_next) {
		if (!STALE())
			CANCEL_BUMP();
		cancel_next = 0;
		submitted = end;
		return;
	}
	for (uint32_t *p = submitted; p + 1 < end; p++) {
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
	submitted = end;
	if (l) {
		pthread_t t;
		pthread_create(&t, NULL, late_writer, l);
		pthread_detach(t);
	}
}

/* --- what nv30_query.c calls ------------------------------------------------------------------ */
int nouveau_pushbuf_space(struct nouveau_pushbuf *p, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)dwords; (void)relocs; (void)pushes;
	submit();		/* out of space: a kick */
	p->cur = submitted = pushmem;
	return 0;
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p)
{
	(void)p;
	submit();
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
int64_t os_time_get_nano(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
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
static struct nv30_context ctx;
static struct pipe_context *pctx;
static struct nouveau_bo notify_bo;
static struct nouveau_object query_obj, eng3d;
static struct nv04_notify query_ntfy;
static struct nouveau_pushbuf_priv ppriv;

static void setup(bool ps3, int query_wait_us)
{
	memset(notifier_mem, 0, sizeof(notifier_mem));
	memset(&dev, 0, sizeof(dev));
	cancel_next = gpu_stopped = 0;
	late_ms = 0;
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
#ifdef HAVE_DEVICE_CANCELLED
	push.cancel_seen = 0;
#endif
	ctx.screen = &screen;
	ctx.base.pushbuf = &push;
	ctx.base.fence = NULL;
	ctx.base.pipe.screen = &screen.base.base;
	pctx = &ctx.base.pipe;
	nv30_query_init(pctx);
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
	bool cancel_part = strcmp(part, "late") && strcmp(part, "x86");
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
	else if (!strcmp(part, "deadline"))
		part_deadline();
	else if (!strcmp(part, "late"))
		late_report(true, 100);
	else if (!strcmp(part, "x86"))
		late_report(false, 1200);
	else {
		printf("usage: %s end|steal|destroy|elapsed|deadline|late|x86\n", argv[0]);
		return 2;
	}
	nouveau_heap_destroy(&screen.query_heap);
	printf("%s %s: %d checks, %d failed\n", fails ? "FAIL" : "ok", part, checks, fails);
	return fails != 0;
}
