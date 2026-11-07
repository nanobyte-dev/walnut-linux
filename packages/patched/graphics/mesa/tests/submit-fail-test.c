/* submit-fail-test PART: the winsys's push buffer submission (nouveau.c's pushbuf_submit, compiled in
 * whole, the kernel's answer faked) and what it counts in nouveau_device::cancelled.
 *   ecanceled  the kernel cancels a submission (ps3rsx): cancelled goes up by one
 *   failed     the kernel fails it another way (-EINVAL, -ENOMEM): cancelled goes up by one too, so the
 *              reports it held count as lost (nv30_query.c) and the driver sends its state again
 *   notify     the fence of the submission could not be made (kick_notify fails): it counts too, so the words that
 *              stay in the push buffer are dropped at the next kick and not sent without their relocations
 *   ratelimit  a channel that fails every submission: the message and the dump come at the 1st, 2nd, 4th... failure
 *   ok         an accepted submission leaves it alone
 *   threads    four threads, each with a push buffer, cancelled submissions at once: every one that reached the
 *              kernel is counted
 * A tree before the patch (no counting of other failures, a plain ++) fails failed and threads.
 * (host-tests.sh submit-fail; the drm calls of xf86drm.h are declared by a stub header and answered here) */
#include "nouveau.c"
#include <pthread.h>

#ifndef NOUVEAU_CANCELLED	/* a tree before the patch */
#define NOUVEAU_CANCELLED(dev) ((dev)->cancelled)
#endif

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static int answer;	/* what the kernel's pushbuf ioctl returns */
static unsigned ioctls;	/* how many reached it */

int drmCommandWriteRead(int fd, unsigned long idx, void *data, unsigned long size)
{
	(void)fd; (void)idx; (void)data; (void)size;
	__atomic_fetch_add(&ioctls, 1, __ATOMIC_RELAXED);
	return answer;
}
int drmCommandWrite(int fd, unsigned long idx, void *data, unsigned long size)
{ (void)fd; (void)idx; (void)data; (void)size; return 0; }
int drmIoctl(int fd, unsigned long req, void *arg) { (void)fd; (void)req; (void)arg; return 0; }
int drmCloseBufferHandle(int fd, uint32_t handle) { (void)fd; (void)handle; return 0; }
int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *d) { (void)fd; (void)flags; (void)d; return -1; }
void drmFreeDevice(drmDevicePtr *d) { (void)d; }
drmVersionPtr drmGetVersion(int fd) { (void)fd; return NULL; }
void drmFreeVersion(drmVersionPtr v) { (void)v; }
int drmPrimeFDToHandle(int fd, int pfd, uint32_t *h) { (void)fd; (void)pfd; (void)h; return -1; }
int drmPrimeHandleToFD(int fd, uint32_t h, uint32_t flags, int *pfd) { (void)fd; (void)h; (void)flags; (void)pfd; return -1; }

static struct nouveau_device_priv dev;
static struct nouveau_drm drm;
static struct nouveau_object chan_obj;
static struct nouveau_client client;
static uint32_t words[64];

static struct nouveau_pushbuf_priv *make_push(void)
{
	struct nouveau_pushbuf_priv *p = calloc(1, sizeof(*p));
	struct nouveau_pushbuf_krec *k = calloc(1, sizeof(*k));
	struct nouveau_bo *bo = calloc(1, sizeof(*bo));

	bo->map = words;
	bo->size = sizeof(words);
	k->nr_buffer = 1;
	k->buffer[0].user_priv = (uintptr_t)bo;
	k->nr_push = 1;
	k->push[0].length = 16;
	p->list = p->krec = k;
	p->base.client = &client;
	p->base.cur = p->bgn = words;
	p->base.cancel_seen = NOUVEAU_CANCELLED(&dev.base);
	return p;
}

static int submit(struct nouveau_pushbuf_priv *p)
{
	return pushbuf_submit(&p->base, &chan_obj);
}

static void setup(void)
{
	memset(&dev, 0, sizeof(dev));
	ioctls = 0;
	drm.fd = 3;
	dev.base.object.parent = &drm.client;
	client.device = &dev.base;
	chan_obj.oclass = NOUVEAU_FIFO_CHANNEL_CLASS;
	chan_obj.handle = 7;
}

static void quiet(void) { nouveau_out = fopen("/dev/null", "w"); }

static void part_one(int ans, uint32_t want, const char *what)
{
	setup();
	struct nouveau_pushbuf_priv *p = make_push();
	answer = ans;
	int ret = submit(p);
	CHECK(ret == ans, "%s: the submit returned %d, want %d", what, ret, ans);
	CHECK(dev.base.cancelled == want, "%s: cancelled is %u, want %u", what, dev.base.cancelled, want);
}

static bool notify_fails;
static bool fail_notify(struct nouveau_pushbuf *p) { (void)p; return !notify_fails; }

static void part_notify(void)
{
	setup();
	struct nouveau_pushbuf_priv *p = make_push();
	p->base.kick_notify = fail_notify;
	notify_fails = true;
	answer = 0;
	int ret = submit(p);
	CHECK(ret == -EINVAL, "notify: the submit returned %d, want -EINVAL", ret);
	CHECK(dev.base.cancelled == 1 && ioctls == 0, "notify: cancelled is %u after a failed fence, ioctls %u, want 1 and 0",
	      dev.base.cancelled, ioctls);
	CHECK(p->base.cancel_seen != NOUVEAU_CANCELLED(&dev.base), "notify: the push buffer is not stale, its words would go out again");
}

static void part_ratelimit(void)
{
	char *buf = NULL;
	size_t len = 0;

	setup();
	nouveau_out = open_memstream(&buf, &len);
	struct nouveau_pushbuf_priv *p = make_push();
	answer = -EINVAL;
	for (int i = 0; i < 100; i++) {
		p->base.cancel_seen = NOUVEAU_CANCELLED(&dev.base);
		p->list->nr_push = 1;
		submit(p);
	}
	fflush(nouveau_out);
	unsigned lines = 0;
	for (size_t i = 0; i < len; i++)
		lines += buf[i] == '\n';
	CHECK(dev.base.cancelled == 100, "ratelimit: cancelled %u, want 100", dev.base.cancelled);
	CHECK(lines > 0 && lines < 60, "ratelimit: %u lines of messages for 100 failures, want a handful", lines);
	fclose(nouveau_out);
	nouveau_out = fopen("/dev/null", "w");
	free(buf);
}

#define NT 4
#define NS 20000
static void *worker(void *arg)
{
	struct nouveau_pushbuf_priv *p = make_push();
	(void)arg;
	for (int i = 0; i < NS; i++) {
		p->base.cancel_seen = NOUVEAU_CANCELLED(&dev.base);	/* it goes to the kernel unless another thread's bump comes first */
		p->list->nr_push = 1;
		submit(p);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	const char *part = argc > 1 ? argv[1] : "";

	quiet();

	if (!strcmp(part, "ecanceled")) {
		part_one(-ECANCELED, 1, "ECANCELED");
	} else if (!strcmp(part, "failed")) {
		part_one(-EINVAL, 1, "EINVAL");
		part_one(-ENOMEM, 1, "ENOMEM");
	} else if (!strcmp(part, "notify")) {
		part_notify();
	} else if (!strcmp(part, "ratelimit")) {
		part_ratelimit();
	} else if (!strcmp(part, "ok")) {
		part_one(0, 0, "accepted");
	} else if (!strcmp(part, "threads")) {
		pthread_t t[NT];

		setup();
		answer = -ECANCELED;
		for (int i = 0; i < NT; i++)
			pthread_create(&t[i], NULL, worker, NULL);
		for (int i = 0; i < NT; i++)
			pthread_join(t[i], NULL);
		CHECK(ioctls > NS && dev.base.cancelled == ioctls, "%u cancelled submissions reached the kernel (from %d threads) "
		      "and %u were counted", ioctls, NT, dev.base.cancelled);
	} else {
		printf("usage: %s ecanceled|failed|notify|ratelimit|ok|threads\n", argv[0]);
		return 2;
	}
	printf("%s %s: %d checks, %d failed\n", fails ? "FAIL" : "ok", part, checks, fails);
	return fails != 0;
}
