/* staging-pool-test: NV30_STAGING_POOL_MB's decisions (Mesa patch "big texture uploads reuse
 * staging buffers too", src/gallium/drivers/nouveau/nv30/nv30_staging_policy.h) on the host: the
 * MiB classes, which idle buffer a request takes, what goes back to the kernel and when, and a
 * model of the pool as nv30_miptree.c drives it (get at the map, put from the fence work, age at
 * every flush): OpenTTD's two full-screen uploads a frame, a load-time burst followed by silence,
 * and random traffic against the invariants (bytes = sum of the classes, at most M MiB and 16
 * entries, nothing older than the idle time after an age).
 *   cc -I build/work/mesa/src/gallium/drivers/nouveau mesa/tests/staging-pool-test.c
 * (tools/run-tests.sh mesa-staging-pool; skipped without build/work/mesa) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv30/nv30_staging_policy.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MiB (1u << 20)

/* the pool as nv30_miptree.c keeps it */
struct pool {
	struct nv30_staging_entry e[NV30_STAGING_BIG_ENTRIES];
	unsigned n;
	uint64_t bytes, max;
	unsigned hits, misses, allocs, frees;
};

static void take(struct pool *p, unsigned k)
{
	for (unsigned i = 0; i < k; i++) {
		p->bytes -= p->e[i].size;
		p->frees++;
	}
	memmove(&p->e[0], &p->e[k], (p->n - k) * sizeof(p->e[0]));
	p->n -= k;
}

/* returns the class of the buffer the upload got (a pooled one or a new one) */
static unsigned get(struct pool *p, unsigned size, int *reused)
{
	unsigned cls = nv30_staging_big_class(size);
	int i = nv30_staging_big_pick(p->e, p->n, cls);

	*reused = i >= 0;
	if (i < 0) {
		p->misses++;
		p->allocs++;
		return cls;
	}
	cls = p->e[i].size;
	p->bytes -= cls;
	memmove(&p->e[i], &p->e[i + 1], (p->n - i - 1) * sizeof(p->e[0]));
	p->n--;
	p->hits++;
	return cls;
}

static void put(struct pool *p, unsigned cls, int64_t now)
{
	if (!nv30_staging_big_fits(cls, p->max)) {
		p->frees++;
		return;
	}
	take(p, nv30_staging_big_evict(p->e, p->n, p->bytes, cls, p->max, now, NV30_STAGING_BIG_IDLE_US));
	p->e[p->n].bo = NULL;
	p->e[p->n].size = cls;
	p->e[p->n++].put_us = now;
	p->bytes += cls;
}

static void age(struct pool *p, int64_t now)
{
	take(p, nv30_staging_big_evict(p->e, p->n, p->bytes, 0, p->max, now, NV30_STAGING_BIG_IDLE_US));
}

static void invariants(const struct pool *p, int64_t now, int aged, const char *what)
{
	uint64_t sum = 0;

	for (unsigned i = 0; i < p->n; i++) {
		sum += p->e[i].size;
		if (i)
			CHECK(p->e[i - 1].put_us <= p->e[i].put_us, "%s: entries out of order", what);
		if (aged)
			CHECK(now - p->e[i].put_us <= NV30_STAGING_BIG_IDLE_US,
			      "%s: entry %u idle %lld us after an age", what, i, (long long)(now - p->e[i].put_us));
	}
	CHECK(sum == p->bytes, "%s: bytes %llu, entries sum to %llu", what,
	      (unsigned long long)p->bytes, (unsigned long long)sum);
	CHECK(p->bytes <= p->max, "%s: %llu bytes over the budget %llu", what,
	      (unsigned long long)p->bytes, (unsigned long long)p->max);
	CHECK(p->n <= NV30_STAGING_BIG_ENTRIES, "%s: %u entries", what, p->n);
}

static void test_classes(void)
{
	CHECK(nv30_staging_big_class(1) == MiB, "1 byte -> %u", nv30_staging_big_class(1));
	CHECK(nv30_staging_big_class(MiB) == MiB, "1 MiB -> %u", nv30_staging_big_class(MiB));
	CHECK(nv30_staging_big_class(MiB + 1) == 2 * MiB, "1 MiB + 1 -> %u", nv30_staging_big_class(MiB + 1));
	/* OpenTTD at 1080p: 1920 x 4 bytes (64-aligned pitch) x 1080 rows, and the 8-bit animation buffer */
	CHECK(nv30_staging_big_class(7680 * 1080) == 8 * MiB, "video buffer -> %u", nv30_staging_big_class(7680 * 1080));
	CHECK(nv30_staging_big_class(1920 * 1080) == 2 * MiB, "animation buffer -> %u", nv30_staging_big_class(1920 * 1080));
	CHECK(!nv30_staging_big_fits(0, 24 * MiB), "class 0 fits");
	CHECK(nv30_staging_big_fits(24 * MiB, 24 * MiB), "24 MiB doesn't fit 24");
	CHECK(!nv30_staging_big_fits(25 * MiB, 24 * MiB), "25 MiB fits 24");
	CHECK(!nv30_staging_big_fits(MiB, 0), "anything fits a budget of 0");
}

static void test_pick(void)
{
	struct nv30_staging_entry e[4] = {
		{ NULL, 8 * MiB, 0 }, { NULL, 2 * MiB, 1 }, { NULL, 3 * MiB, 2 }, { NULL, 2 * MiB, 3 },
	};

	CHECK(nv30_staging_big_pick(e, 4, 2 * MiB) == 1, "2 MiB takes %d", nv30_staging_big_pick(e, 4, 2 * MiB));
	CHECK(nv30_staging_big_pick(e, 4, 3 * MiB) == 2, "3 MiB takes %d", nv30_staging_big_pick(e, 4, 3 * MiB));
	CHECK(nv30_staging_big_pick(e, 4, 4 * MiB) == 0, "4 MiB takes %d", nv30_staging_big_pick(e, 4, 4 * MiB));
	CHECK(nv30_staging_big_pick(e, 4, 5 * MiB) == 0, "5 MiB takes %d", nv30_staging_big_pick(e, 4, 5 * MiB));
	CHECK(nv30_staging_big_pick(e, 4, 9 * MiB) == -1, "9 MiB takes %d", nv30_staging_big_pick(e, 4, 9 * MiB));
	/* a 1 MiB request may take a 2 MiB buffer, never the 8 MiB one */
	CHECK(nv30_staging_big_pick(e, 1, MiB) == -1, "1 MiB takes the 8 MiB buffer");
	CHECK(nv30_staging_big_pick(e + 1, 1, MiB) == 0, "1 MiB doesn't take a 2 MiB buffer");
	CHECK(nv30_staging_big_pick(e, 0, MiB) == -1, "an empty pool gives %d", nv30_staging_big_pick(e, 0, MiB));
}

static void test_evict(void)
{
	struct nv30_staging_entry e[NV30_STAGING_BIG_ENTRIES];
	unsigned i;

	for (i = 0; i < 4; i++)
		e[i] = (struct nv30_staging_entry){ NULL, 4 * MiB, (int64_t)i * 400000 };
	/* 16 MiB in 4 entries, budget 24: a 4 MiB put fits, an 8 MiB one needs the oldest out, 16 two */
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 4 * MiB, 24 * MiB, 900000, 1000000) == 0, "fits");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 8 * MiB, 24 * MiB, 900000, 1000000) == 0, "8 fits 24");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 12 * MiB, 24 * MiB, 900000, 1000000) == 1, "12 needs one out");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 16 * MiB, 24 * MiB, 900000, 1000000) == 2, "16 needs two out");
	/* at 1.3 s the oldest is idle anyway: it goes, and then 12 more fit */
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 4 * MiB, 24 * MiB, 1300000, 1000000) == 1, "aged at a put");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 12 * MiB, 24 * MiB, 1300000, 1000000) == 1, "aged, 12 fits");
	/* ageing: at 1.5 s the entries put at 0 and 0.4 s are idle for more than a second */
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 0, 24 * MiB, 1500000, 1000000) == 2, "ageing at 1.5 s");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 0, 24 * MiB, 3000000, 1000000) == 4, "ageing at 3 s");
	CHECK(nv30_staging_big_evict(e, 4, 16 * MiB, 0, 24 * MiB, 900000, 1000000) == 0, "ageing at 0.9 s");
	/* the entry limit: 16 small entries, one more makes the oldest go */
	for (i = 0; i < NV30_STAGING_BIG_ENTRIES; i++)
		e[i] = (struct nv30_staging_entry){ NULL, MiB, (int64_t)i };
	CHECK(nv30_staging_big_evict(e, NV30_STAGING_BIG_ENTRIES, 16 * MiB, MiB, 64 * MiB, 100, 1000000) == 1,
	      "a 17th entry");
	CHECK(nv30_staging_big_evict(e, NV30_STAGING_BIG_ENTRIES, 16 * MiB, 0, 64 * MiB, 100, 1000000) == 0,
	      "ageing alone at the limit");
}

/* OpenTTD: a 8.3 MB and a 2.1 MB upload a frame, each buffer back one frame later (its fence) */
static void test_openttd(void)
{
	struct pool p = { .max = 24 * MiB };
	unsigned held[2] = { 0, 0 }, f;
	int64_t now = 0;
	int reused;

	for (f = 0; f < 1000; f++, now += 50000) {	/* 20 fps */
		unsigned a = get(&p, 7680 * 1080, &reused);
		unsigned b = get(&p, 1920 * 1080, &reused);
		if (f > 1)
			CHECK(reused, "frame %u: the animation upload missed", f);
		if (held[0])
			put(&p, held[0], now);
		if (held[1])
			put(&p, held[1], now);
		held[0] = a;
		held[1] = b;
		age(&p, now);
		invariants(&p, now, 1, "openttd");
	}
	CHECK(p.allocs <= 4, "openttd: %u buffers made in 1000 frames", p.allocs);
	CHECK(p.hits >= 1996, "openttd: %u hits", p.hits);
	/* the game quits drawing: a second later the flush hands everything back */
	put(&p, held[0], now);
	put(&p, held[1], now);
	age(&p, now + NV30_STAGING_BIG_IDLE_US + 1);
	CHECK(p.n == 0 && p.bytes == 0, "openttd: %u entries, %llu bytes left after a second", p.n,
	      (unsigned long long)p.bytes);
}

/* a load: 40 big uploads of mixed sizes in one go, then nothing big for minutes */
static void test_burst(void)
{
	struct pool p = { .max = 16 * MiB };
	int64_t now = 0;
	int reused;

	for (unsigned i = 0; i < 40; i++, now += 1000) {
		unsigned c = get(&p, (1 + i % 5) * MiB - 4096, &reused);
		put(&p, c, now);
		invariants(&p, now, 0, "burst");
	}
	CHECK(p.bytes <= 16 * MiB, "burst: %llu bytes", (unsigned long long)p.bytes);
	age(&p, now + 500000);
	CHECK(p.n > 0, "burst: emptied before a second");
	age(&p, now + NV30_STAGING_BIG_IDLE_US + 1);
	CHECK(p.n == 0, "burst: %u entries left a second later", p.n);
	CHECK(p.allocs == p.frees, "burst: %u made, %u freed", p.allocs, p.frees);
}

static void test_random(void)
{
	struct pool p;
	unsigned seed, held[32], nheld, k;
	int64_t now;
	int reused;

	for (seed = 1; seed <= 200; seed++) {
		srand(seed);
		memset(&p, 0, sizeof(p));
		p.max = (uint64_t)(rand() % 33) * MiB;
		nheld = 0;
		now = 0;
		for (k = 0; k < 2000; k++) {
			now += rand() % 300000;
			switch (rand() % 4) {
			case 0: case 1:		/* an upload: the buffer is in flight */
				if (nheld < 32)
					held[nheld++] = get(&p, 256 * 1024 + rand() % (12 * MiB), &reused);
				break;
			case 2:			/* a fence signals: its buffer comes back */
				if (nheld) {
					unsigned i = rand() % nheld;
					put(&p, held[i], now);
					held[i] = held[--nheld];
				}
				break;
			default:
				age(&p, now);
				invariants(&p, now, 1, "random age");
				continue;
			}
			invariants(&p, now, 0, "random");
		}
		while (nheld)
			put(&p, held[--nheld], now);
		age(&p, now + NV30_STAGING_BIG_IDLE_US + 1);
		CHECK(p.n == 0, "random %u: %u entries left", seed, p.n);
		CHECK(p.allocs == p.frees, "random %u: %u made, %u freed", seed, p.allocs, p.frees);
	}
}

int main(void)
{
	test_classes();
	test_pick();
	test_evict();
	test_openttd();
	test_burst();
	test_random();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
