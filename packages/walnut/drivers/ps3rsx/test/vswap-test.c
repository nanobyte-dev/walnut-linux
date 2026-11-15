/* vswap-test: the sealed VRAM swap's page logic (src/vswap_core.c) on a model: a pool of 4 chunks of VRAM
 * slots, the GART bounce, the disk, and an SPE that seals and opens with the same core (spu/v2_*.c) and
 * refuses a page whose tag doesn't match without writing it, as spu/vseal.c does. The slots and the
 * bounce may change at any time; the model tracks what each device page must read.
 *
 * Checked throughout: no plaintext ever in a VRAM slot or the bounce (every plaintext page carries a marker);
 * no nonce counter used twice; no slot a map entry points at is written; a read returns exactly the page's
 * last write, or fails (never another page's bytes, never changed bytes); the map's counters match it.
 * Cases: writes and reads by R1 and R2, overwrites into fresh slots, partial writes and reads, discards,
 * BAD entries (discard, full write, partial write, read, write-back), changed slots and a changed bounce
 * (the retry from VRAM), a map entry pointing at another page's slot, the lockout, a restart (-ETIMEDOUT: a
 * resubmission with fresh counters), the SPE gone (-ENODEV: the PPE opens, the drain), kept pages
 * (-EHWPOISON), no SPE at all (nothing in VRAM), the test builds' unsealed slots, write-back into private
 * pages, random sequences under several seeds. The disk completes writes out of order (a write-back's write
 * and a request's write of one page must still end with the newer one); a kept job's SPE keeps writing its
 * destinations after the failure; a request's failure stays with its own piece; replays into the same slot and
 * into another; a changed bounce under the PPE's opens; batches of requests (one page written twice, the second
 * write going to the disk); an open from the bounce failing as a whole, retried from VRAM. Frees by the swap
 * layer (swap_slot_free_notify's marks): the slot and the counts freed, disk, BAD and empty entries, a discard
 * taking the marks (none left waiting), a mark during an open, a seal and a write-back (no job ever names a free
 * slot; one during the write-back's own open never reaches the disk), a freed page rewritten (whole, partly,
 * in the same batch, into the same slot with a fresh counter) reads the new bytes, marks under random runs (a
 * marked page reads its old bytes or zeros, never another's), and the mark and apply racing on two threads (no
 * mark lost, the count exact at rest). The marks applied lazily: a batch with room applies none; a write that
 * finds the pool short applies them before its page would go to the disk (mid-batch, with writes queued; a
 * merge too), counted only when they gave it a slot, and asks for a demotion only when they gave no room; a
 * burst of marks during a batch's job or with the lock held never waits. A write-back with its own pages, the
 * lock dropped between its batches: pages opened before a gap rewritten to the disk there (whole, partly, after a free; the pool
 * full or the SPE gone), freed or discarded there, also under the random runs: the newer bytes stay on the disk,
 * a page freed before its write starts never reaches it. Slot reuse off (vswap_reuse): no mark, no short-pool
 * apply; marks left from a time it was on taken by a rewrite and a discard and applied by the write-back, the
 * count exact; the switch flipped at random in the random runs (the count equal to the bits at the end). The age
 * order (vsx_chunk_used, the demotion's choice as vswap_demote_work's): with slot reuse, a short pool's apply frees
 * holes in the oldest chunk and new pages fill them; the demotion takes the chunk holding only older pages and
 * the new ones stay in VRAM; off, the age at join as before.
 * Built with VSX_FIXOFF_IGNORE_TAG, VSX_FIXOFF_SAME_CTR, VSX_FIXOFF_AD_NO_PAGE, VSX_FIXOFF_WB_INTO_BOUNCE,
 * VSX_FIXOFF_NO_RETRY, VSX_FIXOFF_DISCARD_BAD, VSX_FIXOFF_NO_DEFER, VSX_FIXOFF_PPE_NO_RETRY,
 * VSX_FIXOFF_PARTIAL_EMPTY, VSX_FIXOFF_MODE_BY_SWITCH, VSX_FIXOFF_BATCH_REORDER, VSX_FIXOFF_R1_FINAL,
 * VSX_FIXOFF_NO_FREE, VSX_FIXOFF_FREE_KEEPS_MARK, VSX_FIXOFF_FREE_NOW, VSX_FIXOFF_WB_KEEP_ALL,
 * VSX_FIXOFF_SHORT_NO_APPLY, VSX_FIXOFF_EAGER_APPLY, VSX_FIXOFF_PEND_UNCOUNTED, VSX_FIXOFF_DISCARD_KEEPS_MARK,
 * VSX_FIXOFF_SHORT_COUNT_ANY, VSX_FIXOFF_KEEP_NO_APPLY, VSX_FIXOFF_OFF_MARKS, VSX_FIXOFF_OFF_SHORT_APPLY,
 * VSX_FIXOFF_AGE_AT_JOIN or VSX_FIXOFF_AGE_ALWAYS it must fail. */
#define VSX_PLAIN
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/vswap_core.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 30) { printf("FAIL: "); printf(__VA_ARGS__); \
	printf("\n"); } fails++; } } while (0)
/* the run's invariants at the end: printed past the first 30 failures too */
#define CHECK_END(c, ...) do { checks++; if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); \
	fails++; } } while (0)

#define NPG	600
#define NCH	4
#define NSL	((int)(NCH * VSX_SLOTS))
#define MARK	0x504c41494e545854ull	/* "PLAINTXT": the first word of every plaintext page */

static vs_u8 vram[NSL][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 bounce[VSX_SLOTS][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 disk[NPG][VSX_PAGE];
static vs_u8 truth[NPG][VSX_PAGE];
static int truth_lost[NPG];		/* allowed to fail: its slot was changed by someone else */
static vs_u8 ghost[NPG][VSX_PAGE];	/* a freed page's bytes before the free (dead[p]) */
static int dead[NPG];			/* freed, not rewritten: it reads truth (zeros) or its ghost */
static vs_u8 bio[64][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 priv[VSEAL_PAGES_MAX][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 scratch[VSX_PAGE] __attribute__((aligned(16)));
static struct vsx_meta meta[NSL];
static int used[NSL], slot_page_of[NSL];
static vs_u32 map[NPG];
static unsigned long freed_bits[(NPG + 63) / 64], freed_sumw[1];
static vs_u8 key[32];
static struct vsx X;

/* the model's knobs */
static int job_fail_skip, copies;
static int gpu_ok = 1, job_fail, job_fail_once, corrupt_bounce, have_key = 1, pinned, drains, slot_free_bad;
static vs_u64 ctr_seen[200000];
static unsigned int nctr;
static int live_written, plain_seen, next_free_hint, bounce_plain[VSX_SLOTS];
static int job_names_free;		/* a job or a GPU copy named a slot that wasn't allocated */
static int slot_cap = NSL;		/* the pool's room: slots it hands out at most (a pool that can't grow) */
static int fails_last, fails_first;	/* failed allocations: the last word (a demotion asked), or not yet */
static void (*job_hook)(void);		/* runs when a job or a GPU copy starts (a notify meanwhile) */
static int evicting = -1;		/* the chunk being written back: none of its slots handed out (slot_alloc) */
static void (*gap_hook)(const vs_u32 *pages, int n);	/* own pages: between two batches, the lock dropped */
static bool reuse_on = true;		/* vswap_reuse (X.reuse points here) */
static int pool_chunks = NCH;		/* the chunks in the pool: slots are handed out from these only */
static vs_u64 born[NCH], age_seq;	/* each chunk's age (vswap.c's vchunk.born, vs->seq) */
static bool joined[NCH];		/* the chunk has joined the pool (vswap.c: slot_alloc's grow) */

struct req { int failed; };

static vs_u32 rnd_s = 0x13579bdf;
static vs_u32 rnd(void)
{
	rnd_s ^= rnd_s << 13;
	rnd_s ^= rnd_s >> 17;
	rnd_s ^= rnd_s << 5;
	return rnd_s;
}

/* --- the hardware calls ------------------------------------------------------------------------- */
int vsx_hw_slot_alloc(void *hw, int last)
{
	int nused = 0, lim = pool_chunks * (int)VSX_SLOTS;

	for (int n = 0; n < NSL; n++)
		nused += used[n];
	for (int n = 0; n < lim && nused < slot_cap; n++) {
		int s = (next_free_hint + n) % lim, c = s / (int)VSX_SLOTS;

		if (!used[s] && c != evicting) {
			if (!joined[c]) {		/* the chunk joins the pool (vswap.c: the grow) */
				joined[c] = true;
				born[c] = ++age_seq;
			}
			vsx_chunk_used(&X, &born[c], &age_seq);
			used[s] = 1;
			next_free_hint = s + 1;
			return s;
		}
	}
	if (last)
		fails_last++;
	else
		fails_first++;
	return -1;
}
void vsx_hw_slot_free(void *hw, vs_u32 slot)
{
	if (slot >= NSL || !used[slot]) {
		slot_free_bad++;
		return;
	}
	used[slot] = 0;
}
void vsx_hw_slot_page(void *hw, vs_u32 slot, vs_u32 p) { slot_page_of[slot] = p; }
struct vsx_meta *vsx_hw_meta(void *hw, vs_u32 slot) { return &meta[slot]; }
vs_u64 vsx_hw_slot_ea(void *hw, vs_u32 slot) { return EA(vram[slot]); }
void vsx_hw_touch(void *hw, vs_u32 slot) { memset(vram[slot], 0, 8); }
void vsx_hw_read_slot(void *hw, vs_u32 slot, void *dst) { memcpy(dst, vram[slot], VSX_PAGE); }
void vsx_hw_write_slot(void *hw, vs_u32 slot, const void *src) { memcpy(vram[slot], src, VSX_PAGE); }
int vsx_hw_gpu_to_bounce(void *hw, const vs_u32 *slots, unsigned int n)
{
	if (!gpu_ok)
		return -1;
	if (job_hook)
		job_hook();
	for (unsigned int i = 0; i < n; i++)
		job_names_free += slots[i] >= NSL || !used[slots[i]];
	for (unsigned int i = 0; i < n; i++) {
		memcpy(bounce[i], vram[slots[i]], VSX_PAGE);
		bounce_plain[i] = !!(meta[slots[i]].flags & (VSX_M_PLAIN | VSX_M_COPY));	/* (the test builds' off-switches) */
		if (corrupt_bounce)
			bounce[i][rnd() % VSX_PAGE] ^= 0x20;	/* the bounce changes */
	}
	return 0;
}
vs_u64 vsx_hw_bounce_ea(void *hw, unsigned int i) { return EA(bounce[i]); }
const void *vsx_hw_bounce_ptr(void *hw, unsigned int i) { return bounce[i]; }
const vs_u8 *vsx_hw_key(void *hw) { return have_key ? key : NULL; }
/* a kept job's SPE: its destinations written once more, at the next hardware call (a late put) */
static struct { vs_u8 *dst; vs_u8 data[VSX_PAGE]; } late[VSEAL_PAGES_MAX];
static int nlate, late_mode;
static void *pins[4096];
static int npins;
static vs_u8 scratch2[4][VSX_PAGE] __attribute__((aligned(16)));
static int nscratch;
static void late_writes(void)
{
	for (int i = 0; i < nlate; i++)
		memcpy(late[i].dst, late[i].data, VSX_PAGE);
	nlate = 0;
}
void vsx_hw_pin(void *hw, void *ref)
{
	pinned++;
	if (npins < 4096)
		pins[npins++] = ref;
	if (ref == X.scratch_ref && nscratch < 4) {	/* as vswap.c: a new private page */
		X.scratch = X.scratch_ref = scratch2[nscratch++];
	}
}
static int is_pinned(const void *p)
{
	for (int i = 0; i < npins; i++)
		if (pins[i] == p)
			return 1;
	return 0;
}

/* the disk: writes queue up and complete in any order; a deferred one only after the write-back's */
struct dw { vs_u32 p; vs_u16 in, len; vs_u8 data[VSX_PAGE]; int deferred; };
static struct dw dq[256];
static int ndq, async_disk;
static void disk_complete(int deferred_too)
{
	while (ndq) {
		int k = async_disk ? ndq - 1 : 0;	/* async: the newest first, the worst order */

		if (dq[k].deferred && !deferred_too) {	/* find a non-deferred one */
			int j;

			for (j = 0; j < ndq && dq[j].deferred; j++)
				;
			if (j == ndq)
				return;
			k = j;
		}
		memcpy(disk[dq[k].p] + dq[k].in, dq[k].data, dq[k].len);
		dq[k] = dq[--ndq];
	}
}
static void disk_write(vs_u32 p, int in, int len, const void *data, int deferred)
{
	if (ndq == 256)
		disk_complete(1);
	dq[ndq].p = p;
	dq[ndq].in = in;
	dq[ndq].len = len;
	dq[ndq].deferred = deferred;
	memcpy(dq[ndq].data, data, len);
	ndq++;
	if (!async_disk)
		disk_complete(1);
}
void vsx_hw_disk(void *hw, struct vsx_piece *pc, int write, int defer)
{
	if (write) {
		disk_write(pc->p, pc->in_page, pc->len, pc->buf, defer);
	} else {
		disk_complete(1);	/* (a read after every write before it) */
		memcpy(pc->buf, disk[pc->p] + pc->in_page, pc->len);
	}
}
int vsx_hw_disk_copy(void *hw, void *ctx, vs_u32 p, const void *page, int defer)
{
	disk_write(p, 0, VSX_PAGE, page, defer);
	return 0;
}
void vsx_hw_fail(void *hw, void *ctx) { ((struct req *)ctx)->failed = 1; }
void vsx_hw_zero(void *hw, struct vsx_piece *pc) { memset(pc->buf, 0, pc->len); }
void vsx_hw_event(void *hw, int what, vs_u32 p, vs_u32 slot) {}
void vsx_hw_drain(void *hw) { drains++; }

static int is_slot(vs_u64 ea, vs_u32 *slot)
{
	if (ea < EA(vram[0]) || ea >= EA(vram[NSL]))
		return 0;
	*slot = (ea - EA(vram[0])) / VSX_PAGE;
	return 1;
}

/* the SPE: spu/vseal.c's semantics, with the same core */
int vsx_hw_job(void *hw, unsigned int op, unsigned int n, const struct vseal_page *pg, struct vseal_result *res,
	       enum spe_path path)
{
	int bad = 0;

	late_writes();
	if (job_hook)
		job_hook();
	for (unsigned int i = 0; i < n; i++) {	/* every slot the job names is allocated */
		vs_u32 s;

		if ((op == VSEAL_OP_SEAL || op == VSEAL_OP_TEST_COPY) && is_slot(pg[i].dst, &s))
			job_names_free += !used[s];
		if (is_slot(pg[i].src, &s))
			job_names_free += !used[s];
	}
	if (job_fail && job_fail_skip > 0) {
		job_fail_skip--;
	} else if (job_fail) {
		int r = job_fail;

		if (job_fail_once)
			job_fail = 0;
		if (r == SPE_RET_KEEP && late_mode)	/* the buried SPE still opens its job, later */
			for (unsigned int i = 0; i < n && nlate < VSEAL_PAGES_MAX; i++, nlate++) {
				late[nlate].dst = (vs_u8 *)(unsigned long)pg[i].dst;
				memset(late[nlate].data, 0x66, VSX_PAGE);
			}
		return r;
	}
	for (unsigned int i = 0; i < n; i++) {
		vs_u8 nonce[12], buf[VSX_PAGE] __attribute__((aligned(16)));
		vs_u8 *src = (vs_u8 *)(unsigned long)pg[i].src, *dst = (vs_u8 *)(unsigned long)pg[i].dst;
		vs_u32 s;

		v2_nonce_from_counter(pg[i].ctr, nonce);
		memcpy(buf, src, VSX_PAGE);		/* one copy */
		if (op == VSEAL_OP_TEST_COPY) {	/* the test image's copy: no cipher */
			memcpy(dst, buf, VSX_PAGE);
			res[i].status = VSEAL_ST_OK;
			copies++;
			continue;
		}
		if (op == VSEAL_OP_SEAL) {
			int dup = 0;

			for (unsigned int k = 0; k < nctr; k++)
				dup |= ctr_seen[k] == pg[i].ctr;
			CHECK(!dup, "the counter %llu used twice under one key", (unsigned long long)pg[i].ctr);
			if (nctr < sizeof(ctr_seen) / sizeof(ctr_seen[0]))
				ctr_seen[nctr++] = pg[i].ctr;
			if (is_slot(pg[i].dst, &s))
				for (int p = 0; p < NPG; p++)
					if (map[p] == s + 1)
						live_written++;
			v2_aead_seal(key, nonce, pg[i].ad, 16, buf, VSX_PAGE, buf, res[i].tag);
			memcpy(dst, buf, VSX_PAGE);
			res[i].status = VSEAL_ST_OK;
		} else {
			if (v2_aead_open(key, nonce, pg[i].ad, 16, buf, VSX_PAGE, buf, pg[i].tag)) {
				res[i].status = VSEAL_ST_BADTAG;	/* nothing written */
				bad = 1;
				continue;
			}
			memcpy(dst, buf, VSX_PAGE);
			memcpy(res[i].tag, pg[i].tag, 16);
			res[i].status = VSEAL_ST_OK;
		}
	}
	return bad ? SPE_RET_BADMSG : 0;
}

/* --- the model's requests ---------------------------------------------------------------------------- */
static void fill(int p)
{
	for (int i = 0; i < VSX_PAGE; i++)
		truth[p][i] = rnd();
	memcpy(truth[p], &(vs_u64){ MARK }, 8);
	memcpy(truth[p] + 8, &p, 4);
	truth_lost[p] = 0;
	dead[p] = 0;
}

/* the swap layer frees page p: marked; until an apply or a rewrite it may still read its old bytes. With slot reuse
 * off nothing is marked, and the device keeps the page as it is (as before the notify) */
static void free_page(int p)
{
	if (!vsx_free_mark(&X, p) && !(freed_bits[p / 64] >> (p % 64) & 1))
		return;
	if (!dead[p])
		memcpy(ghost[p], truth[p], VSX_PAGE);
	dead[p] = 1;
	memset(truth[p], 0, VSX_PAGE);
}

static void scan(const char *when)
{
	for (int s = 0; s < NSL; s++)
		if (!(meta[s].flags & (VSX_M_PLAIN | VSX_M_COPY)) && !memcmp(vram[s], &(vs_u64){ MARK }, 8))
			plain_seen++;
	for (int i = 0; i < (int)VSX_SLOTS; i++)
		if (!bounce_plain[i] && !memcmp(bounce[i], &(vs_u64){ MARK }, 8))
			plain_seen++;
	(void)when;
}

static void counts(const char *when)
{
	vs_u32 v = 0, d = 0, b = 0, live[NSL] = { 0 };
	int twice = 0, unalloc = -1;

	for (int p = 0; p < NPG; p++) {
		if (map[p] == VSX_ON_DISK)
			d++;
		else if (map[p] == VSX_BAD)
			b++;
		else if (map[p]) {
			v++;
			if (map[p] - 1 < NSL) {
				twice |= live[map[p] - 1]++ > 0;
				if (!used[map[p] - 1])
					unalloc = p;
			}
		}
	}
	CHECK(unalloc < 0, "%s: page %d's slot not allocated", when, unalloc);
	CHECK(!twice, "%s: two pages in one slot", when);
	CHECK(v == X.in_vram && d == X.on_disk && b == X.bad, "%s: counts: vram %u/%u disk %u/%u bad %u/%u", when,
	      v, X.in_vram, d, X.on_disk, b, X.bad);
}

/* write pages p[0..n) whole (or one partial piece) as one request */
static int wr(const int *p, int n, int in_page, int len)
{
	struct req r = { 0 };

	vsx_begin(&X);
	for (int i = 0; i < n; i++) {
		struct vsx_piece pc = { bio[i], bio[i], p[i], 0, 0, in_page, len, 0, -1, &r };

		if (len == VSX_PAGE)
			fill(p[i]);
		else
			for (int k = in_page; k < in_page + len; k++)
				truth[p[i]][k] = rnd();
		dead[p[i]] = 0;			/* (the write takes the mark: zeros around a piece) */
		memcpy(bio[i], truth[p[i]] + in_page, len);
		pc.buf = bio[i];
		vsx_write(&X, &pc);
	}
	vsx_finish(&X);
	scan("write");
	counts("write");
	return r.failed;
}

static int rd_check(const int *p, int n, int in_page, int len, const char *what)
{
	struct req r = { 0 };
	int bad = 0;

	memset(bio, 0xee, sizeof(bio));
	vsx_begin(&X);
	for (int i = 0; i < n; i++) {
		struct vsx_piece pc = { bio[i], bio[i], p[i], 0, 0, in_page, len, 0, -1, &r };

		vsx_read(&X, &pc);
	}
	vsx_finish(&X);
	scan("read");
	counts("read");
	if (r.failed)
		return -1;
	for (int i = 0; i < n; i++)
		if (memcmp(bio[i], truth[p[i]] + in_page, len) &&
		    !(dead[p[i]] && !memcmp(bio[i], ghost[p[i]] + in_page, len)))
			bad++;
	CHECK(!bad, "%s: %d pages read back wrong", what, bad);
	return bad ? -2 : 0;
}

static void reset_model(void)
{
	memset(vram, 0x5a, sizeof(vram));
	memset(bounce, 0, sizeof(bounce));
	memset(disk, 0, sizeof(disk));
	memset(meta, 0, sizeof(meta));
	memset(used, 0, sizeof(used));
	memset(map, 0, sizeof(map));
	memset(truth, 0, sizeof(truth));
	memset(&X, 0, sizeof(X));
	memset(freed_bits, 0, sizeof(freed_bits));
	memset(freed_sumw, 0, sizeof(freed_sumw));
	memset(dead, 0, sizeof(dead));
	slot_cap = NSL;
	fails_last = fails_first = 0;
	X.map = map;
	X.freed = freed_bits;
	X.freed_sum = freed_sumw;
	reuse_on = true;
	X.reuse = &reuse_on;
	pool_chunks = NCH;
	memset(born, 0, sizeof(born));
	memset(joined, 0, sizeof(joined));
	age_seq = 0;
	X.npages = NPG;
	X.next_ctr = 1;
	X.mode = VSX_SEAL;
	X.lockout = VSX_LOCKOUT;
	X.scratch = scratch;
	X.scratch_ref = scratch;
	gpu_ok = 1;
	nctr = 0;
	job_fail = job_fail_once = corrupt_bounce = 0;
	have_key = 1;
	pinned = drains = 0;
	npins = nscratch = nlate = late_mode = job_fail_skip = 0;
	ndq = 0;
	async_disk = 0;
	job_hook = NULL;
	gap_hook = NULL;
	evicting = -1;
	for (int i = 0; i < 32; i++)
		key[i] = rnd();
}

static void (*wb_hook)(const vs_u32 *pages, int n);
static vs_u8 own[VSX_SLOTS][VSX_PAGE] __attribute__((aligned(16)));

/* write-back of chunk c, as vswap.c's chunk_evict: the GPU copy (or not), then batch by batch the marks applied
 * and the slots opened into private pages. With the reserve (own 0) each batch is written at once, under the
 * same lock hold; with own pages (a page each slot) the whole chunk is opened first, the lock dropped between
 * the batches (gap_hook), and written at the end. Before the writes, what changed since its open is dropped
 * (vsx_writeback_keep); in the unlocked window a request may write these pages (deferred, wb_hook); after it,
 * the map switched for what didn't change. */
static void evict_m(int c, int gpu, int own_pages)
{
	vs_u32 slots[VSEAL_PAGES_MAX], pages[VSEAL_PAGES_MAX], wp[VSX_SLOTS], ws[VSX_SLOTS];
	void *pp[VSEAL_PAGES_MAX], *w[VSX_SLOTS];
	int s = 0, n, nw = 0, batch = own_pages && !gpu ? 8 : VSEAL_PAGES_MAX;

	evicting = c;
	if (gpu)
		for (int k = 0; k < (int)VSX_SLOTS; k++) {
			memcpy(bounce[k], vram[c * VSX_SLOTS + k], VSX_PAGE);
			bounce_plain[k] = !!(meta[c * VSX_SLOTS + k].flags & (VSX_M_PLAIN | VSX_M_COPY));
		}
	while (s < (int)VSX_SLOTS) {
		vsx_apply_frees(&X);		/* (chunk_evict: before it collects a batch) */
		for (n = 0; s < (int)VSX_SLOTS && n < batch; s++) {
			int slot = c * VSX_SLOTS + s;

			if (!used[slot] || map[slot_page_of[slot]] != (vs_u32)slot + 1)
				continue;
			slots[n] = slot;
			pages[n] = slot_page_of[slot];
			pp[n] = own_pages ? own[s] : priv[n];
			n++;
		}
		if (n) {
			vsx_writeback_open(&X, slots, pages, n, pp, pp, gpu);
			scan("write-back");
			for (int i = 0; i < n; i++)
				if (X.op[i].status == VSX_O_OK) {
					CHECK(!is_pinned(pp[i]), "write-back: a pinned page reused");
					w[nw] = pp[i];
					ws[nw] = slots[i];
					wp[nw++] = pages[i];
				}
		}
		if (own_pages && s < (int)VSX_SLOTS) {	/* the lock dropped: anything may happen */
			if (gap_hook)
				gap_hook(wp, nw);
			continue;
		}
		nw = vsx_writeback_keep(&X, wp, ws, w, nw);
		if (!nw)
			continue;
		for (int i = 0; i < nw; i++)
			disk_write(wp[i], 0, VSX_PAGE, w[i], 0);
		X.wb_pages = wp;		/* the unlocked window: a request may write these pages */
		X.wb_n = nw;
		if (wb_hook)
			wb_hook(wp, nw);
		disk_complete(0);		/* the write-back's writes (and others), in any order */
		X.wb_n = 0;
		vsx_apply_frees(&X);		/* (chunk_evict: after the window, before the switch) */
		for (int i = 0; i < nw; i++)
			if (map[wp[i]] == ws[i] + 1)
				vsx_set_disk(&X, wp[i]);
		disk_complete(1);		/* then the held-back ones */
		nw = 0;
	}
	{
		int cu = 0;

		for (int k = 0; k < (int)VSX_SLOTS; k++)
			cu += used[c * VSX_SLOTS + k];
		if (!cu)
			born[c] = ++age_seq;	/* emptied by its write-back (chunk_evict); frees keep the age */
	}
	evicting = -1;
	counts("write-back");
}

static void evict(int c, int gpu)
{
	evict_m(c, gpu, 0);
}

/* the demotion's choice (vswap_demote_work): the chunk in the pool, not evicting, with pages, of the lowest age */
static int demote_victim(void)
{
	int v = -1;

	for (int c = 0; c < pool_chunks; c++) {
		int n = 0;

		for (int k = 0; k < (int)VSX_SLOTS; k++)
			n += used[c * VSX_SLOTS + k];
		if (n && c != evicting && (v < 0 || born[c] < born[v]))
			v = c;
	}
	return v;
}

/* someone else changes page p's slot */
static void corrupt(int p, int at)
{
	if (vsx_in_vram(map[p]))
		vram[map[p] - 1][at] ^= 1;
}

/* during a write-back's unlocked window, a request rewrites one of its pages to the disk */
static void wb_rewrite(const vs_u32 *pages, int n)
{
	int p = pages[0];

	if (!n)
		return;
	wr(&p, 1, 0, VSX_PAGE);
}

/* in the first gap of a write-back with its own pages, six pages opened before it: rewritten to the disk (no
 * fresh slot: the pool full, or the SPE gone) whole, partly (a merge's disk copy), freed and the offset reused;
 * freed (applied there, as by a write that found the pool short, or left for the write-back); discarded */
static int gapp[6] = { -1, -1, -1, -1, -1, -1 }, gap_drain;
static void gap_change(const vs_u32 *pages, int n)
{
	int q;

	if (gapp[0] >= 0 || n < 6)
		return;
	for (int i = 0; i < 6; i++)
		gapp[i] = pages[i];
	if (gap_drain)
		X.mode = VSX_DRAIN;
	else
		X.locked = 1;
	q = gapp[0];
	wr(&q, 1, 0, VSX_PAGE);
	q = gapp[1];
	wr(&q, 1, 1024, 512);
	vsx_free_mark(&X, gapp[2]);
	q = gapp[2];
	wr(&q, 1, 0, VSX_PAGE);
	vsx_free_mark(&X, gapp[3]);
	vsx_free_mark(&X, gapp[4]);
	vsx_apply_frees(&X);
	vsx_free_mark(&X, gapp[3]);		/* (again: left for the write-back) */
	vsx_discard(&X, gapp[5]);
	for (int i = 3; i < 6; i++)
		memset(truth[gapp[i]], 0, VSX_PAGE);
	X.locked = 0;
}

/* the random runs' gaps: now and then one opened page rewritten (whole or partly, to VRAM or the disk), freed,
 * discarded, or freed and rewritten */
static void gap_random(const vs_u32 *pages, int n)
{
	int q, k, locked = X.locked;

	if (!n || rnd() % 2)
		return;
	q = pages[rnd() % n];
	k = rnd() % 5;
	X.locked = rnd() & 1;
	if (k == 0) {
		wr(&q, 1, 0, VSX_PAGE);
	} else if (k == 1) {
		if (map[q] != VSX_BAD)
			wr(&q, 1, (rnd() % 8) * 512, 512);
	} else if (k == 2 || k == 4) {
		free_page(q);
		if (k == 4)
			wr(&q, 1, 0, VSX_PAGE);
	} else {
		vsx_discard(&X, q);
		memset(truth[q], 0, VSX_PAGE);
		dead[q] = 0;
	}
	X.locked = locked;
}

/* --- frees by the swap layer ------------------------------------------------------------------------ */
static int hook_page = -1, hook_slot_used = -1;
static void hook_mark(void)		/* a notify while a job or a GPU copy runs */
{
	if (hook_page < 0)
		return;
	if (vsx_in_vram(map[hook_page]))
		hook_slot_used = map[hook_page] - 1;	/* the slot the job names */
	vsx_free_mark(&X, hook_page);
	hook_page = -1;
}

/* during a write-back's unlocked window: page 0 of it freed and applied (a short pool's write), page 1 freed only
 * (applied by the write-back itself), page 2 freed and rewritten by a request */
static int wbf_pages[3] = { -1, -1, -1 };
static void wb_free(const vs_u32 *pages, int n)
{
	if (n < 3)
		return;
	for (int i = 0; i < 3; i++)
		wbf_pages[i] = pages[i];
	vsx_free_mark(&X, pages[0]);
	vsx_apply_frees(&X);
	vsx_free_mark(&X, pages[1]);
	vsx_free_mark(&X, pages[2]);
	{
		int p = pages[2];

		wr(&p, 1, 0, VSX_PAGE);		/* (page 1's mark waits: room enough) */
	}
	vsx_free_mark(&X, pages[1]);		/* again, left for the write-back */
}

/* the mark and the apply on two threads: the notify marks with no lock and wakes nothing, the applier applies
 * whenever it likes (room needed) and flips vswap_reuse at random times (a flip at run time, possibly in the middle
 * of a mark); no mark lost, a free made while it was off leaves its page as it is, and at rest the count is exactly
 * the marks waiting */
static int race_done;
static unsigned char race_marked[NPG];
static long race_on_total, race_off_total;	/* frees marked, frees made while off, over all rounds */
static void *race_marker(void *arg)
{
	vs_u32 *order = arg;

	for (int i = 0; i < NPG - 100; i++) {
		race_marked[order[i]] = vsx_free_mark(&X, order[i]) != 0;
		if (i % 37 == 0)
			sched_yield();
	}
	__atomic_store_n(&race_done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static int race_round(void)
{
	static vs_u32 order[NPG];
	pthread_t t;
	int lost = 0, bits = 0, nmarked = 0, off_kept = 0;
	long rest;

	reset_model();
	memset(race_marked, 0, sizeof(race_marked));
	for (int p = 100; p < NPG; p++) {
		map[p] = VSX_ON_DISK;
		order[p - 100] = p;
	}
	X.on_disk = NPG - 100;
	for (int i = NPG - 101; i > 0; i--) {
		int j = rnd() % (i + 1);
		vs_u32 t2 = order[i];

		order[i] = order[j];
		order[j] = t2;
	}
	race_done = 0;
	pthread_create(&t, NULL, race_marker, order);
	while (!__atomic_load_n(&race_done, __ATOMIC_SEQ_CST)) {
		if (rnd() % 3 == 0)
			vsx_apply_frees(&X);	/* (vs->lock held: a short pool, the write-back, the demotion) */
		if (rnd() % 7 == 0)		/* (vswap_reuse written through sysfs) */
			__atomic_store_n(&reuse_on, !__atomic_load_n(&reuse_on, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
	}
	pthread_join(t, NULL);
	reuse_on = true;
	for (unsigned int k = 0; k < sizeof(freed_bits) / sizeof(freed_bits[0]); k++)
		bits += __builtin_popcountl(freed_bits[k]);
	rest = vsx_frees_count(&X);
	vsx_apply_frees(&X);			/* the next time room is needed */
	for (int p = 100; p < NPG; p++) {
		nmarked += race_marked[p];
		if (race_marked[p])
			lost += map[p] != 0;
		else
			off_kept += map[p] == VSX_ON_DISK;
	}
	CHECK(rest == bits && X.pending == 0, "marks racing the apply: %ld counted at rest for %d marks, %ld after",
	      rest, bits, X.pending);
	CHECK(!lost && X.on_disk == (vs_u32)(NPG - 100 - nmarked) && X.st.freed_disk == (vs_u64)nmarked &&
	      !vsx_frees_pending(&X), "marks racing the apply: %d lost, %u on disk, %llu applied of %d marked", lost,
	      X.on_disk, (unsigned long long)X.st.freed_disk, nmarked);
	CHECK(off_kept == NPG - 100 - nmarked, "marks racing the apply, the switch flipped: %d frees made while off "
	      "left their page (%d marked)", off_kept, nmarked);
	race_on_total += nmarked;
	race_off_total += off_kept;
	return lost;
}

/* a burst of frees while the swap's lock is held (a batch, a write-back): every mark goes through without it */
static pthread_mutex_t burst_lock = PTHREAD_MUTEX_INITIALIZER;
static int burst_done;
static void *burst_marker(void *arg)
{
	for (int r = 0; r < 50; r++)
		for (int p = 0; p < NPG; p++)
			vsx_free_mark(&X, (p * 7 + r) % NPG);
	__atomic_store_n(&burst_done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

/* a job's hook: a burst of marks on pages the batch doesn't name (the batch's own are under their write) */
static int burst_from = -1;
static void burst_hook(void)
{
	if (burst_from < 0)
		return;
	for (int p = burst_from; p < burst_from + 200; p++)
		free_page(p);
	burst_from = -1;
}

static int seq(int a, int n, int *p)
{
	for (int i = 0; i < n; i++)
		p[i] = a + i;
	return n;
}

int main(void)
{
	int p[32], i, r;

	/* --- writes and reads, R1 and R2 --- */
	reset_model();
	r = wr(p, seq(0, 32, p), 0, VSX_PAGE);
	CHECK(!r && X.in_vram == 32 && X.st.seals == 32 && X.st.seal_jobs == 1, "32 pages sealed in one job");
	CHECK(rd_check(p, 32, 0, VSX_PAGE, "R1") == 0 && X.st.opens_bounce == 32, "read back through the bounce");
	gpu_ok = 0;
	CHECK(rd_check(p, 8, 0, VSX_PAGE, "R2") == 0 && X.st.opens_vram == 8, "read back from VRAM");
	gpu_ok = 1;
	{
		vs_u64 c0 = X.next_ctr;

		r = wr(p, seq(0, 4, p), 0, VSX_PAGE);	/* overwrite: fresh slots, counters rise */
		CHECK(!r && X.in_vram == 32 && X.next_ctr == c0 + 4, "overwrites: fresh slots, fresh counters");
		CHECK(rd_check(p, 4, 0, VSX_PAGE, "overwrite") == 0, "overwrites read back");
	}
	/* partial pieces on pages in VRAM: merged, sealed into fresh slots */
	p[0] = 5;
	{
		vs_u32 old = map[5];

		r = wr(p, 1, 512, 1024);
		CHECK(!r && vsx_in_vram(map[5]) && map[5] != old && X.st.merges == 1, "a partial write merged into a fresh slot");
	}
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "merged") == 0, "the merged page reads back whole");
	CHECK(rd_check(p, 1, 1024, 512, "partial read") == 0, "a partial read");
	p[0] = 100;
	r = wr(p, 1, 0, 512);			/* partial write on an empty page: the disk, as before */
	CHECK(!r && map[100] == VSX_ON_DISK, "a partial write on an empty page goes to the disk");
	/* discard */
	vsx_discard(&X, 6);
	CHECK(map[6] == 0 && X.in_vram == 31, "a discard frees the slot");
	p[0] = 6;
	memset(truth[6], 0, VSX_PAGE);
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "discarded reads zeros") == 0, "a discarded page reads zeros");

	/* --- a changed bounce: retried from VRAM, nothing lost --- */
	corrupt_bounce = 1;
	seq(16, 16, p);
	CHECK(rd_check(p, 16, 0, VSX_PAGE, "changed bounce") == 0 && X.st.retries >= 16 && X.bad == 0,
	      "a changed bounce: every page retried from VRAM and read");
	corrupt_bounce = 0;

	/* --- a changed slot: BAD, and BAD through every branch --- */
	corrupt(10, 77);
	p[0] = 10;
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "changed slot") == -1 && map[10] == VSX_BAD && X.bad == 1,
	      "a changed slot: the read fails, the entry is BAD");
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "BAD read") == -1, "a BAD page's read fails");
	CHECK(wr(p, 1, 0, 100) == 1 && map[10] == VSX_BAD, "a partial write on a BAD page fails");
	vsx_discard(&X, 10);
	CHECK(map[10] == 0 && X.bad == 0 && !slot_free_bad, "a discard clears BAD (no slot freed)");
	counts("discard of BAD");
	corrupt(11, 5);
	p[0] = 11;
	rd_check(p, 1, 0, VSX_PAGE, "x");
	CHECK(map[11] == VSX_BAD, "BAD again");
	CHECK(!wr(p, 1, 0, VSX_PAGE) && vsx_in_vram(map[11]) && X.bad == 0, "a full write replaces BAD");
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "rewritten BAD") == 0, "and reads back");

	/* --- a map entry pointing at another page's slot: fails, never the other page's bytes --- */
	{
		vs_u32 a = map[20], b = map[21];

		struct req rq = { 0 };
		struct vsx_piece pc = { bio[0], bio[0], 20, 0, 0, 0, VSX_PAGE, 0, -1, &rq };

		CHECK(vsx_in_vram(a) && vsx_in_vram(b), "pages 20 and 21 in VRAM for the swapped entry");
		if (vsx_in_vram(a) && vsx_in_vram(b)) {
		map[20] = b;			/* (a kernel bug's shape) */
		memset(bio[0], 0, VSX_PAGE);
		vsx_begin(&X);
		vsx_read(&X, &pc);
		vsx_finish(&X);
		CHECK(rq.failed && memcmp(bio[0], truth[21], VSX_PAGE), "page 20 never reads page 21's bytes");
		if (map[20] == VSX_BAD) {	/* (undo the model's damage: page 21's slot freed) */
			X.bad--;
			X.in_vram++;
			used[b - 1] = 1;
		}
		map[20] = a;
		}
		counts("swapped entry");
	}

	/* --- write-back into private pages, from the bounce and from VRAM, with a changed slot --- */
	corrupt(3, 9);
	evict(0, 1);
	CHECK(map[3] == VSX_BAD, "write-back: a changed slot becomes BAD, not written");
	for (i = 0; i < 32; i++)
		if (i != 3 && i != 6 && i != 10 && i != 20 && i != 21 && map[i] == VSX_ON_DISK)
			CHECK(!memcmp(disk[i], truth[i], VSX_PAGE), "write-back: page %d on the disk", i);
	p[0] = 12;
	CHECK(map[12] == VSX_ON_DISK && rd_check(p, 1, 0, VSX_PAGE, "written back") == 0, "written-back pages read from the disk");
	r = wr(p, seq(40, 20, p), 0, VSX_PAGE);
	corrupt_bounce = 0;
	evict(0, 0);
	evict(1, 1);
	CHECK(rd_check(p, 20, 0, VSX_PAGE, "written back") == 0, "written back from VRAM and the bounce: read from the disk");

	/* --- the lockout --- */
	reset_model();
	wr(p, seq(0, 20, p), 0, VSX_PAGE);
	for (i = 0; i < VSX_LOCKOUT; i++) {
		corrupt(i, 1);
		p[0] = i;
		rd_check(p, 1, 0, VSX_PAGE, "x");
	}
	CHECK(X.locked && X.fails == VSX_LOCKOUT, "8 final failures: locked out");
	CHECK(!wr(p, seq(100, 4, p), 0, VSX_PAGE) && map[100] == VSX_ON_DISK, "locked out: new pages go to the disk");
	CHECK(rd_check(p, 4, 0, VSX_PAGE, "after lockout") == 0, "and read back");
	CHECK(rd_check((seq(10, 4, p), p), 4, 0, VSX_PAGE, "old pages") == 0, "pages in VRAM still read");

	/* --- a restart: resubmitted with fresh counters --- */
	reset_model();
	job_fail = SPE_RET_TIMEDOUT;
	job_fail_once = 1;
	{
		vs_u64 c0 = X.next_ctr;

		r = wr(p, seq(0, 8, p), 0, VSX_PAGE);
		CHECK(!r && X.in_vram == 8 && X.st.resubmits == 1 && X.next_ctr == c0 + 16, "a restart: resubmitted, fresh counters");
	}
	job_fail = SPE_RET_TIMEDOUT;
	job_fail_once = 1;
	CHECK(rd_check(p, 8, 0, VSX_PAGE, "restart open") == 0, "an open resubmitted after a restart");

	/* --- an open from the bounce that fails as a whole (-EIO): the slots are opened from VRAM --- */
	reset_model();
	CHECK(!wr(p, seq(0, 8, p), 0, VSX_PAGE), "8 pages into VRAM");
	job_fail = SPE_RET_IO;
	job_fail_once = 1;
	{
		vs_u64 v0 = X.st.opens_vram;

		CHECK(rd_check(p, 8, 0, VSX_PAGE, "R1 failed whole") == 0 && X.st.opens_vram == v0 + 8 && X.bad == 0,
		      "an R1 job failed as a whole: the reads come from VRAM");
	}

	/* --- the SPE gone: the PPE opens, the drain moves everything to the disk --- */
	job_fail = SPE_RET_NODEV;
	CHECK(rd_check(p, 8, 0, VSX_PAGE, "gone") == 0 && X.mode == VSX_DRAIN && drains == 1 && X.st.opens_ppe == 8,
	      "the SPE gone: the PPE opens the reads, the drain started");
	corrupt(2, 3);
	p[0] = 2;
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "PPE tag") == -1 && map[2] == VSX_BAD, "the PPE's open checks the tag too");
	CHECK(!wr(p, seq(50, 4, p), 0, VSX_PAGE) && map[50] == VSX_ON_DISK, "after the SPE: new pages to the disk");
	for (i = 0; i < NCH; i++)
		evict(i, i & 1);
	CHECK(X.in_vram == 0, "the drain: nothing left in VRAM (%u)", X.in_vram);
	CHECK(rd_check((seq(0, 8, p), p + 3), 5, 0, VSX_PAGE, "drained") == 0, "drained pages read from the disk");

	/* --- kept pages --- */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	job_fail = SPE_RET_KEEP;
	CHECK(rd_check(p, 4, 0, VSX_PAGE, "kept") == -1 && pinned == 4 && X.mode == VSX_DRAIN,
	      "-EHWPOISON on an open: the pieces fail, their pages pinned, the drain");
	r = wr(p, seq(20, 2, p), 0, VSX_PAGE);
	CHECK(!r && map[20] == VSX_ON_DISK, "then writes go to the disk");
	reset_model();
	job_fail = SPE_RET_KEEP;
	{
		int u0 = 0, u1 = 0;

		for (i = 0; i < NSL; i++)
			u0 += used[i];
		r = wr(p, seq(0, 4, p), 0, VSX_PAGE);
		for (i = 0; i < NSL; i++)
			u1 += used[i];
		CHECK(!r && map[0] == VSX_ON_DISK && pinned == 4 && u1 == u0 + 4,
		      "-EHWPOISON on a seal: written to the disk, pages pinned, the slots kept out of use");
	}

	/* --- no SPE at all: nothing in VRAM --- */
	reset_model();
	X.mode = VSX_OFF;
	CHECK(!wr(p, seq(0, 16, p), 0, VSX_PAGE) && X.in_vram == 0 && X.on_disk == 16, "no SPE: every page to the disk");
	CHECK(rd_check(p, 16, 0, VSX_PAGE, "no SPE") == 0, "and back");

	/* --- the test builds' unsealed slots, mixed with sealed ones --- */
	reset_model();
	X.plain = 1;
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	X.plain = 0;
	wr(p, seq(8, 8, p), 0, VSX_PAGE);
	CHECK(rd_check((seq(0, 16, p), p), 16, 0, VSX_PAGE, "mixed") == 0 && X.st.plain_reads == 8,
	      "unsealed and sealed slots read back");
	evict(0, 1);
	for (i = 0; i < 16; i++)
		CHECK(map[i] == VSX_ON_DISK && !memcmp(disk[i], truth[i], VSX_PAGE), "mixed write-back: page %d", i);

	/* --- a request's failure stays with its piece --- */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	corrupt(3, 50);
	{
		struct req rq[8] = { { 0 } };

		memset(bio, 0xee, sizeof(bio));
		vsx_begin(&X);
		for (i = 0; i < 8; i++) {
			struct vsx_piece pc = { bio[i], bio[i], i, 0, 0, 0, VSX_PAGE, 0, -1, &rq[i] };

			vsx_read(&X, &pc);
		}
		vsx_finish(&X);
		for (i = 0; i < 8; i++)
			CHECK(rq[i].failed == (i == 3) && (i == 3 || !memcmp(bio[i], truth[i], VSX_PAGE)),
			      "one changed slot fails only its own piece (%d)", i);
	}

	/* --- a batch of requests (io_work's): one job; the same page written twice keeps the later write --- */
	reset_model();
	{
		struct req rq[3] = { { 0 } };
		vs_u8 first[VSX_PAGE];
		vs_u64 j0 = X.st.seal_jobs;

		vsx_begin(&X);
		for (i = 0; i < 3; i++) {
			struct vsx_piece pc = { bio[i], bio[i], 60, 0, 0, 0, VSX_PAGE, 0, -1, &rq[i] };

			fill(60);
			if (!i)
				memcpy(first, truth[60], VSX_PAGE);
			memcpy(bio[i], truth[60], VSX_PAGE);
			if (i == 2) {
				pc.p = 61;
				fill(61);
				memcpy(bio[i], truth[61], VSX_PAGE);
				fill(60);	/* (truth[60] back to request 1's bytes) */
				memcpy(truth[60], bio[1], VSX_PAGE);
			}
			vsx_write(&X, &pc);
		}
		vsx_finish(&X);
		/* (the second write of page 60 seals the queued first one before it is queued itself) */
		CHECK(X.st.seal_jobs == j0 + 2 && X.in_vram == 2, "three requests, one page twice: two jobs");
		p[0] = 60;
		p[1] = 61;
		CHECK(rd_check(p, 2, 0, VSX_PAGE, "batched writes") == 0 && memcmp(first, truth[60], VSX_PAGE),
		      "the same page twice in a batch: the later write reads back");
		counts("batch");
	}
	reset_model();
	{
		struct req rq[3] = { { 0 } };
		vs_u64 j0 = X.st.seal_jobs;

		vsx_begin(&X);
		for (i = 0; i < 3; i++) {
			struct vsx_piece pc = { bio[i], bio[i], 70 + i, 0, 0, 0, VSX_PAGE, 0, -1, &rq[i] };

			fill(70 + i);
			memcpy(bio[i], truth[70 + i], VSX_PAGE);
			vsx_write(&X, &pc);
		}
		vsx_finish(&X);
		CHECK(X.st.seal_jobs == j0 + 1 && X.in_vram == 3, "three requests sealed in one job");
		CHECK(rd_check((seq(70, 3, p), p), 3, 0, VSX_PAGE, "batch of three") == 0, "and read back");
	}
	/* the same page twice in a batch, the second to the disk (the lockout trips between them): the disk's
	 * newer write stays, the queued older one is never installed over it */
	reset_model();
	{
		struct req rq[2] = { { 0 } };

		vsx_begin(&X);
		for (i = 0; i < 2; i++) {
			struct vsx_piece pc = { bio[i], bio[i], 80, 0, 0, 0, VSX_PAGE, 0, -1, &rq[i] };

			fill(80);
			memcpy(bio[i], truth[80], VSX_PAGE);
			if (i == 1)
				X.locked = 1;
			vsx_write(&X, &pc);
		}
		vsx_finish(&X);
		disk_complete(1);
		p[0] = 80;
		CHECK(map[80] == VSX_ON_DISK && X.in_vram == 0, "the newer write on the disk, no slot left installed (map %#x)",
		      map[80]);
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "batch to the disk") == 0, "the same page twice, the second to the disk: it reads back");
		counts("batch to the disk");
	}

	/* --- replays: an older ciphertext into another slot and into the same slot --- */
	reset_model();
	p[0] = 30;
	wr(p, 1, 0, VSX_PAGE);
	{
		vs_u8 old[VSX_PAGE];
		vs_u32 s0 = map[30] - 1;

		memcpy(old, vram[s0], VSX_PAGE);
		wr(p, 1, 0, VSX_PAGE);			/* a new seal, a fresh slot */
		CHECK(map[30] - 1 != s0, "the rewrite went to a fresh slot");
		memcpy(vram[map[30] - 1], old, VSX_PAGE);	/* the old ciphertext into the new slot */
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "cross-slot replay") == -1 && map[30] == VSX_BAD,
		      "an older ciphertext in another slot: refused");
		vsx_discard(&X, 30);
		next_free_hint = s0;			/* the same slot again */
		wr(p, 1, 0, VSX_PAGE);
		CHECK(map[30] - 1 == s0, "the same slot reused");
		memcpy(vram[s0], old, VSX_PAGE);
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "same-slot replay") == -1, "an older ciphertext in the same slot: refused");
	}

	/* --- a changed bounce under the PPE's opens: retried from VRAM --- */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	job_fail = SPE_RET_NODEV;
	rd_check(p, 1, 0, VSX_PAGE, "x");
	CHECK(X.mode == VSX_DRAIN, "drain mode");
	corrupt_bounce = 1;
	CHECK(rd_check((seq(1, 7, p), p), 7, 0, VSX_PAGE, "PPE bounce") == 0 && X.bad == 0,
	      "the PPE: a changed bounce retried from VRAM");
	corrupt_bounce = 0;

	/* --- a kept job's late writes never reach a page in use --- */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	late_mode = 1;
	job_fail = SPE_RET_KEEP;
	job_fail_once = 1;
	p[0] = 2;
	wr(p, 1, 512, 1024);			/* a merge whose open is kept */
	CHECK(pinned >= 1 && X.scratch != scratch, "the private page replaced after a pin");
	CHECK(!wr(p, seq(40, 4, p), 0, VSX_PAGE), "writes after the pin");
	CHECK(rd_check(p, 4, 0, VSX_PAGE, "after late writes") == 0, "pages after the late writes read back");
	for (i = 0; i < NCH; i++)
		evict(i, 0);
	CHECK(rd_check(p, 4, 0, VSX_PAGE, "drained after late") == 0, "drained pages after the late writes");
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	late_mode = 1;
	job_fail = SPE_RET_KEEP;
	job_fail_once = 1;
	job_fail_skip = 1;			/* the open goes through, the seal is kept */
	p[0] = 3;
	wr(p, 1, 0, 512);			/* a merge whose seal is kept: the disk gets the merged page */
	disk_complete(1);
	CHECK(map[3] == VSX_ON_DISK ? !memcmp(disk[3], truth[3], VSX_PAGE) : 1, "a kept merge's disk copy is the merged page");

	/* --- a partial write on an empty page: zeros around the piece --- */
	reset_model();
	p[0] = 77;
	memset(disk[77], 0x99, VSX_PAGE);		/* an earlier page's bytes at that place */
	wr(p, 1, 2560, 512);
	disk_complete(1);
	CHECK(rd_check(p, 1, 0, VSX_PAGE, "partial on empty") == 0, "a partial write on an empty page: zeros around it");

	/* --- a write-back and a request writing one page: the newer one stays --- */
	reset_model();
	async_disk = 1;
	wr(p, seq(0, 16, p), 0, VSX_PAGE);
	X.mode = VSX_OFF;				/* (the request's write goes to the disk) */
	wb_hook = wb_rewrite;
	evict(0, 1);
	wb_hook = NULL;
	disk_complete(1);
	CHECK(rd_check((seq(0, 16, p), p), 16, 0, VSX_PAGE, "write-back race") == 0, "the request's newer write stays on the disk");
	async_disk = 0;

	/* --- the cipher's cost measured: the SPE copies unsealed (2), mixed with sealed (0) and PPE (1) slots, the
	 * switch changed at run time: each slot read back as it was written --- */
	reset_model();
	X.plain = 2;
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	CHECK(copies == 8 && X.in_vram == 8, "seal 2: the SPE copied the pages into their slots");
	X.plain = 0;
	wr(p, seq(8, 8, p), 0, VSX_PAGE);
	X.plain = 1;
	wr(p, seq(16, 8, p), 0, VSX_PAGE);
	for (int sw = 0; sw < 3; sw++) {
		X.plain = sw;
		gpu_ok = sw != 1;
		CHECK(rd_check((seq(0, 24, p), p), 24, 0, VSX_PAGE, "mixed modes") == 0 && X.bad == 0,
		      "switch %d: copied, sealed and PPE slots each read back as written", sw);
	}
	gpu_ok = 1;
	X.plain = 2;
	evict(0, 1);
	for (i = 0; i < 24; i++)
		CHECK(map[i] == VSX_ON_DISK && !memcmp(disk[i], truth[i], VSX_PAGE), "mixed-mode write-back: page %d", i);
	X.plain = 0;

	/* --- frees by the swap layer (swap_slot_free_notify) --- */
	reset_model();
	wr(p, seq(0, 32, p), 0, VSX_PAGE);
	{
		vs_u32 s5 = map[5] - 1;

		CHECK(vsx_free_mark(&X, 5) == 1 && vsx_free_mark(&X, 7) == 1 && vsx_free_mark(&X, 5) == 0 &&
		      vsx_frees_count(&X) == 2 && vsx_frees_pending(&X), "two marks counted, the second of one page not");
		CHECK(vsx_in_vram(map[5]) && used[s5] && X.in_vram == 32, "a mark alone frees nothing");
		CHECK(vsx_apply_frees(&X) == 2, "two marks applied");
		CHECK(map[5] == 0 && !used[s5] && map[7] == 0 && X.in_vram == 30 && X.st.freed_vram == 2 &&
		      !vsx_frees_pending(&X), "a freed page's slot and count go");
		counts("freed");
		memset(truth[5], 0, VSX_PAGE);
		memset(truth[7], 0, VSX_PAGE);
		CHECK(rd_check((p[0] = 5, p[1] = 7, p), 2, 0, VSX_PAGE, "freed") == 0, "a freed page reads zeros");
		CHECK(vsx_free_mark(&X, 9) == 1 && vsx_frees_count(&X) == 1, "after the apply, a new mark counts again");
		{
			vs_u32 e9 = map[9];
			vs_u64 v0 = X.st.freed_vram;

			CHECK(!wr((p[0] = 40, p), 1, 0, VSX_PAGE) && vsx_in_vram(e9) && map[9] == e9 && used[e9 - 1] && X.in_vram == 31 &&
			      X.st.freed_vram == v0 && vsx_frees_count(&X) == 1 && !fails_first && !fails_last,
			      "lazy: a batch with room applies no mark (page 9 still held, its mark waiting)");
			CHECK(rd_check((p[0] = 8, p), 1, 0, VSX_PAGE, "next to a waiting mark") == 0, "its neighbour reads");
			vsx_apply_frees(&X);
			CHECK(vsx_in_vram(e9) && map[9] == 0 && !used[e9 - 1] && X.in_vram == 30 && !vsx_frees_pending(&X),
			      "and applied by the next apply");
		}
		/* the slot goes back into use */
		next_free_hint = s5;
		CHECK(!wr((p[0] = 41, p), 1, 0, VSX_PAGE) && map[41] - 1 == s5, "a freed slot is reused");
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "reused slot") == 0, "and reads back");
	}
	/* a disk entry, a BAD one, an empty one, one past the end */
	p[0] = 100;
	wr(p, 1, 0, 512);
	corrupt(10, 3);
	rd_check((p[0] = 10, p), 1, 0, VSX_PAGE, "x");
	CHECK(map[100] == VSX_ON_DISK && map[10] == VSX_BAD, "a disk and a BAD entry");
	vsx_free_mark(&X, 100);
	vsx_free_mark(&X, 10);
	vsx_free_mark(&X, 300);
	CHECK(vsx_free_mark(&X, NPG) == 0 && vsx_free_mark(&X, (unsigned long)-1) == 0, "marks past the end ignored");
	vsx_apply_frees(&X);
	CHECK(map[100] == 0 && map[10] == 0 && X.on_disk == 0 && X.bad == 0 && X.st.freed_disk == 1 &&
	      X.st.freed_other == 2 && !slot_free_bad, "a freed disk, BAD and empty entry: empty, counted, no slot freed");
	counts("freed disk and BAD");
	memset(truth[100], 0, VSX_PAGE);
	memset(truth[10], 0, VSX_PAGE);
	CHECK(rd_check((p[0] = 100, p[1] = 10, p), 2, 0, VSX_PAGE, "freed disk") == 0, "and read zeros");

	/* a discard takes the marks with the entries (a swapoff's frees, then swapon --discard): none left waiting,
	 * none for a later apply to count */
	reset_model();
	wr(p, seq(0, 32, p), 0, VSX_PAGE);
	p[0] = 40;
	wr(p, 1, 0, 512);
	for (i = 0; i < 32; i++)
		free_page(i);
	free_page(40);
	free_page(41);
	CHECK(vsx_frees_count(&X) == 34, "34 marks waiting before the discard");
	for (i = 0; i < NPG; i++) {
		vsx_discard(&X, i);
		dead[i] = 0;
	}
	CHECK(vsx_frees_count(&X) == 0 && !vsx_frees_pending(&X) && X.in_vram == 0 && X.on_disk == 0,
	      "a discard of marked pages: none waiting (%ld), the area empty", vsx_frees_count(&X));
	CHECK(vsx_apply_frees(&X) == 0 && X.st.freed_vram == 0 && X.st.freed_disk == 0 && X.st.freed_other == 0,
	      "and no later apply finds one (freed %llu/%llu/%llu)", (unsigned long long)X.st.freed_vram,
	      (unsigned long long)X.st.freed_disk, (unsigned long long)X.st.freed_other);
	counts("discard of marked pages");
	CHECK(vsx_free_mark(&X, 3) == 1 && vsx_frees_count(&X) == 1, "a new mark after the discard counts");
	vsx_discard(&X, 3);
	CHECK(!vsx_frees_pending(&X), "and a discard takes it too");

	/* a mark while an open from VRAM runs: the slot stays the job's; the read gets the old bytes */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	gpu_ok = 0;
	hook_page = 3;
	hook_slot_used = -1;
	job_hook = hook_mark;
	CHECK(rd_check(p, 8, 0, VSX_PAGE, "mark during an open") == 0, "a mark during an open: every page read whole");
	job_hook = NULL;
	gpu_ok = 1;
	CHECK(hook_slot_used >= 0 && used[hook_slot_used] && vsx_in_vram(map[3]), "the job's slot still allocated after it");
	vsx_apply_frees(&X);
	CHECK(map[3] == 0 && !used[hook_slot_used], "applied after the batch");
	counts("mark during an open");
	/* the same through the bounce (a mark while the GPU copies) */
	hook_page = 4;
	hook_slot_used = -1;
	job_hook = hook_mark;
	CHECK(rd_check((seq(4, 4, p), p), 4, 0, VSX_PAGE, "mark during a copy") == 0, "a mark during the GPU copy");
	job_hook = NULL;
	CHECK(vsx_in_vram(map[4]) && used[hook_slot_used], "the copied slot still allocated");
	vsx_apply_frees(&X);
	CHECK(map[4] == 0, "applied after");

	/* a mark while a seal of the page runs: installed, then freed by the next apply (the swap layer never
	 * frees an entry under its write; a free that comes late frees the page as it is then) */
	reset_model();
	wr(p, seq(0, 4, p), 0, VSX_PAGE);
	{
		vs_u32 old = map[2] - 1;

		hook_page = 2;
		hook_slot_used = -1;
		job_hook = hook_mark;
		CHECK(!wr((p[0] = 2, p), 1, 0, VSX_PAGE) && vsx_in_vram(map[2]) && map[2] - 1 != old && !used[old],
		      "a mark during the page's own seal: the new slot in, the old one freed by the install");
		job_hook = NULL;
		vsx_apply_frees(&X);
		CHECK(map[2] == 0 && X.in_vram == 3 && !slot_free_bad, "and the mark applied after");
		counts("mark during a seal");
	}

	/* free, then rewrite: the new bytes; in the same batch too (the write takes the mark) */
	reset_model();
	wr(p, seq(0, 8, p), 0, VSX_PAGE);
	vsx_free_mark(&X, 1);
	CHECK(!wr((p[0] = 1, p), 1, 0, VSX_PAGE) && rd_check(p, 1, 0, VSX_PAGE, "free then rewrite") == 0,
	      "freed then rewritten: the new bytes");
	{
		struct req rq = { 0 };
		struct vsx_piece pc = { bio[0], bio[0], 2, 0, 0, 0, VSX_PAGE, 0, -1, &rq };

		vsx_begin(&X);
		vsx_free_mark(&X, 2);		/* after the batch's apply, before the write */
		fill(2);
		memcpy(bio[0], truth[2], VSX_PAGE);
		vsx_write(&X, &pc);
		vsx_finish(&X);
		vsx_apply_frees(&X);		/* (io_work's next run) */
		CHECK(!rq.failed && vsx_in_vram(map[2]) && rd_check((p[0] = 2, p), 1, 0, VSX_PAGE, "mark in the batch") == 0,
		      "a mark taken by the write in the same batch: the later apply leaves the new bytes");
	}
	{
		struct req rq = { 0 };
		struct vsx_piece pc = { bio[0], bio[0], 3, 0, 0, 1024, 512, 0, -1, &rq };

		vsx_begin(&X);
		vsx_free_mark(&X, 3);		/* a partial write after the free: zeros around it, not the old page */
		memset(truth[3], 0, VSX_PAGE);
		for (i = 1024; i < 1536; i++)
			truth[3][i] = rnd();
		memcpy(bio[0], truth[3] + 1024, 512);
		vsx_write(&X, &pc);
		vsx_finish(&X);
		disk_complete(1);
		vsx_apply_frees(&X);
		CHECK(!rq.failed && rd_check((p[0] = 3, p), 1, 0, VSX_PAGE, "partial after free") == 0,
		      "a partial write of a freed page: zeros around the piece");
	}
	/* freed, then the same slot sealed again (the pool short: the mark applied for the write): a fresh counter */
	{
		vs_u32 s4 = map[4] - 1;
		vs_u64 c4 = meta[s4].ctr;
		int u = 0;

		for (i = 0; i < NSL; i++)
			u += used[i];
		slot_cap = u;			/* no free slot, no chunk to grow by */
		free_page(4);
		next_free_hint = s4;
		CHECK(!wr((p[0] = 5, p), 1, 0, VSX_PAGE) && map[5] - 1 == s4 && meta[s4].ctr > c4 && X.next_ctr > meta[s4].ctr,
		      "the freed slot sealed again under a fresh counter (%llu after %llu)",
		      (unsigned long long)meta[s4].ctr, (unsigned long long)c4);
		CHECK(map[4] == 0 && X.st.short_applies == 1 && fails_first == 1 && !fails_last,
		      "the pool short: the mark applied before the slot was handed out, no demotion asked");
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "same slot fresh counter") == 0, "and reads back");
		counts("same slot");
		slot_cap = NSL;
	}

	/* a write-back with frees: before it collects (never written), during its window (empty, not on the disk), a
	 * freed page rewritten meanwhile (the new bytes) */
	reset_model();
	next_free_hint = 0;
	wr(p, seq(0, 16, p), 0, VSX_PAGE);
	vsx_free_mark(&X, 15);
	memset(truth[15], 0, VSX_PAGE);
	wb_hook = wb_free;
	evict(0, 1);
	wb_hook = NULL;
	disk_complete(1);
	CHECK(map[15] == 0 && !memcmp(disk[15], (vs_u8[VSX_PAGE]){ 0 }, VSX_PAGE), "freed before the write-back: never written");
	if (wbf_pages[0] >= 0) {
		memset(truth[wbf_pages[0]], 0, VSX_PAGE);
		memset(truth[wbf_pages[1]], 0, VSX_PAGE);
		CHECK(map[wbf_pages[0]] == 0 && map[wbf_pages[1]] == 0, "freed during the write-back: empty, not on the disk");
		CHECK(map[wbf_pages[2]] != 0, "freed and rewritten during the write-back: kept");
		for (i = 0; i < 3; i++)
			p[i] = wbf_pages[i];
		CHECK(rd_check(p, 3, 0, VSX_PAGE, "write-back frees") == 0, "and each reads as it should");
	}
	CHECK(wbf_pages[0] >= 0, "the write-back's window ran");
	counts("write-back frees");
	CHECK(rd_check((seq(0, 15, p), p), 15, 0, VSX_PAGE, "write-back with frees") == 0, "the other pages read back");

	/* a mark during the open of a write-back's batch (its job's hook): after the batch's top apply, so only
	 * vsx_writeback_keep's apply keeps the dead page off the disk; from VRAM and from the bounce */
	for (int g = 0; g < 2; g++) {
		reset_model();
		next_free_hint = 0;
		wr(p, seq(0, 8, p), 0, VSX_PAGE);
		hook_page = 5;
		hook_slot_used = -1;
		job_hook = hook_mark;
		evict(0, g);
		job_hook = NULL;
		disk_complete(1);
		CHECK(hook_page < 0 && hook_slot_used >= 0, "mark in the write-back's open %d: the hook ran", g);
		CHECK(map[5] == 0 && !memcmp(disk[5], (vs_u8[VSX_PAGE]){ 0 }, VSX_PAGE) && X.st.freed_vram == 1 &&
		      !vsx_frees_pending(&X), "a mark during the write-back's open (%s): applied by the keep, never "
		      "written to the disk", g ? "bounce" : "VRAM");
		memset(truth[5], 0, VSX_PAGE);
		CHECK(rd_check((seq(0, 8, p), p), 8, 0, VSX_PAGE, "mark in the write-back's open") == 0 && X.on_disk == 7,
		      "the other seven from the disk, the freed one zeros");
		counts("mark in the write-back's open");
	}

	/* --- a write-back with its own pages: the lock dropped between its batches, and pages opened before a gap
	 * changed in it (gap_change); written back from VRAM with the pool full, and from the bounce with the SPE
	 * gone in the gap (the drain); each with the disk in order and out of order. A request's newer bytes on the
	 * disk stay, a freed or discarded page never reaches the disk --- */
	for (int g = 0; g < 4; g++) {
		int ok = 1, zero = 1;

		reset_model();
		next_free_hint = 0;
		async_disk = g & 1;
		gap_drain = g >> 1;
		wr(p, seq(0, 32, p), 0, VSX_PAGE);
		wr(p, seq(32, 32, p), 0, VSX_PAGE);	/* 64 pages in chunk 0: three batches from the bounce */
		CHECK(map[63] - 1 < VSX_SLOTS, "own pages: all in chunk 0");
		for (i = 0; i < 6; i++)
			gapp[i] = -1;
		gap_hook = gap_change;
		evict_m(0, gap_drain, 1);
		gap_hook = NULL;
		disk_complete(1);
		CHECK(gapp[0] >= 0, "own pages %d: a gap ran", g);
		if (gapp[0] < 0)
			continue;
		for (i = 0; i < 3; i++) {
			p[0] = gapp[i];
			ok &= map[gapp[i]] == VSX_ON_DISK && rd_check(p, 1, 0, VSX_PAGE, "rewritten in a gap") == 0;
		}
		CHECK(ok, "own pages %d (%s, %s disk): pages rewritten to the disk in a gap read the newer bytes", g,
		      gap_drain ? "drain" : "pool full", async_disk ? "out-of-order" : "in-order");
		for (i = 3; i < 6; i++)
			zero &= map[gapp[i]] == 0 && !memcmp(disk[gapp[i]], (vs_u8[VSX_PAGE]){ 0 }, VSX_PAGE);
		CHECK(zero, "own pages %d: pages freed or discarded in a gap: empty, never written to the disk", g);
		CHECK(rd_check((seq(0, 32, p), p), 32, 0, VSX_PAGE, "own pages") == 0 &&
		      rd_check((seq(32, 32, p), p), 32, 0, VSX_PAGE, "own pages") == 0, "own pages %d: every page reads back", g);
		CHECK(X.in_vram == 0, "own pages %d: the chunk written back (%u left)", g, X.in_vram);
		counts("own pages");
	}
	async_disk = 0;

	/* the mark and the apply on two threads */
	{
		int lost = 0;

		race_on_total = race_off_total = 0;
		for (i = 0; i < 200; i++)
			lost += race_round();
		CHECK(!lost, "200 rounds of marks racing the apply: %d lost", lost);
		CHECK(race_on_total > 1000 && race_off_total > 1000, "200 rounds racing the switch: %ld frees marked, %ld "
		      "made while off (both ways covered)", race_on_total, race_off_total);
	}

	/* --- the marks applied lazily (vswap_core.h "Frees") --- */
	/* the pool short in the middle of a batch, four writes queued: the marks applied, every page into VRAM */
	reset_model();
	wr(p, seq(0, 28, p), 0, VSX_PAGE);
	slot_cap = 32;				/* a pool that can't grow: 4 slots left */
	for (i = 0; i < 8; i++)
		free_page(i);
	CHECK(vsx_frees_count(&X) == 8 && X.in_vram == 28, "eight marks waiting, nothing freed yet");
	{
		vs_u64 j0 = X.st.seal_jobs;

		r = wr(p, seq(100, 12, p), 0, VSX_PAGE);
		CHECK(!r && X.in_vram == 32 && X.on_disk == 0 && X.st.freed_vram == 8 && X.st.short_applies == 1 &&
		      fails_first == 1 && !fails_last && !vsx_frees_pending(&X) && X.st.seal_jobs == j0 + 1,
		      "the pool short mid-batch: the marks applied, all 12 pages into VRAM in one job (vram %u, disk %u, "
		      "freed %llu, short %llu, fails %d/%d)", X.in_vram, X.on_disk, (unsigned long long)X.st.freed_vram,
		      (unsigned long long)X.st.short_applies, fails_first, fails_last);
	}
	CHECK(rd_check(p, 12, 0, VSX_PAGE, "short pool") == 0, "and read back");
	CHECK(rd_check((seq(0, 28, p), p), 28, 0, VSX_PAGE, "short pool, the rest") == 0, "freed pages zeros, the rest theirs");
	counts("short pool");
	/* still short, no mark: the disk, and now the demotion is asked */
	CHECK(!wr((p[0] = 200, p), 1, 0, VSX_PAGE) && map[200] == VSX_ON_DISK && fails_last == 1 && fails_first == 1,
	      "short with no mark: the disk, the demotion asked");
	/* short, only a disk entry marked: applied, no slot from it: the disk, the demotion asked */
	free_page(200);
	CHECK(!wr((p[0] = 201, p), 1, 0, VSX_PAGE) && map[201] == VSX_ON_DISK && map[200] == 0 && X.st.freed_disk == 1 &&
	      fails_first == 2 && fails_last == 2, "short, only a disk mark: applied, the page to the disk, demotion asked");
	CHECK(X.st.short_applies == 1, "and that apply, which gave no slot, isn't counted (%llu)",
	      (unsigned long long)X.st.short_applies);
	/* a merge that finds the pool short: the mark applied, the merged page sealed into the freed slot */
	{
		vs_u32 s10 = map[10] - 1;

		free_page(10);
		next_free_hint = s10;
		p[0] = 11;
		CHECK(!wr(p, 1, 512, 1024) && vsx_in_vram(map[11]) && map[11] - 1 == s10 && map[10] == 0 &&
		      X.st.short_applies == 2, "a merge on a short pool: the mark applied, sealed into the freed slot");
		CHECK(rd_check(p, 1, 0, VSX_PAGE, "short merge") == 0, "and reads back whole");
	}
	/* a write takes its own mark: the count follows */
	free_page(12);
	CHECK(vsx_frees_count(&X) == 1, "one mark waiting");
	CHECK(!wr((p[0] = 12, p), 1, 0, VSX_PAGE) && vsx_frees_count(&X) == 0 && !vsx_frees_pending(&X) &&
	      vsx_in_vram(map[12]), "a rewrite takes its mark: none waiting");
	counts("short pool end");
	slot_cap = NSL;

	/* --- slot reuse off (vswap_reuse 0, the off switch): the notify marks nothing, a short pool applies nothing --- */
	reset_model();
	wr(p, seq(0, 28, p), 0, VSX_PAGE);
	reuse_on = false;
	{
		int got = 0;

		for (i = 0; i < 8; i++)
			got += vsx_free_mark(&X, i);
		CHECK(!got && !vsx_frees_pending(&X) && !freed_bits[0] && !freed_sumw[0],
		      "reuse off: no mark set (%d new, count %ld)", got, vsx_frees_count(&X));
	}
	slot_cap = 28;				/* short: no free slot, no chunk to grow by */
	CHECK(!wr((p[0] = 100, p), 1, 0, VSX_PAGE) && map[100] == VSX_ON_DISK && fails_first == 0 && fails_last == 1 &&
	      X.in_vram == 28 && X.st.freed_vram == 0, "reuse off, the pool short: the disk, the demotion asked at once, "
	      "nothing applied (fails %d/%d)", fails_first, fails_last);
	slot_cap = NSL;
	CHECK(rd_check((seq(0, 28, p), p), 28, 0, VSX_PAGE, "reuse off") == 0, "reuse off: the unmarked pages read back");
	counts("reuse off");
	/* marks left from a time it was on: taken by a rewrite and a discard, kept by a short pool, applied by the
	 * write-back; the count follows */
	reset_model();
	wr(p, seq(0, 28, p), 0, VSX_PAGE);
	for (i = 0; i < 8; i++)
		free_page(i);
	reuse_on = false;
	slot_cap = 28;
	CHECK(!wr((p[0] = 100, p), 1, 0, VSX_PAGE) && map[100] == VSX_ON_DISK && vsx_frees_count(&X) == 8 &&
	      X.st.freed_vram == 0 && fails_first == 0 && fails_last == 1,
	      "reuse off with 8 marks left, the pool short: none applied, the page to the disk (count %ld, fails %d/%d)",
	      vsx_frees_count(&X), fails_first, fails_last);
	CHECK(!wr((p[0] = 0, p), 1, 0, VSX_PAGE) && vsx_frees_count(&X) == 7 && vsx_in_vram(map[0]),
	      "reuse off: a rewrite still takes its mark, its slot freed for the new bytes (count %ld)", vsx_frees_count(&X));
	slot_cap = NSL;
	vsx_discard(&X, 1);
	memset(truth[1], 0, VSX_PAGE);
	dead[1] = 0;
	CHECK(vsx_frees_count(&X) == 6, "reuse off: a discard still takes its mark (count %ld)", vsx_frees_count(&X));
	evict(0, 1);
	disk_complete(1);
	{
		int written = 0;

		for (i = 2; i < 8; i++)
			written += map[i] != 0;
		CHECK(!vsx_frees_pending(&X) && vsx_frees_count(&X) == 0 && !freed_bits[0] && !written &&
		      X.st.freed_vram == 7, "reuse off: the write-back applies the 6 left, none written (count %ld, "
		      "freed %llu, %d written)", vsx_frees_count(&X), (unsigned long long)X.st.freed_vram, written);
	}
	CHECK(rd_check((seq(0, 28, p), p), 28, 0, VSX_PAGE, "reuse off, marks left") == 0 &&
	      rd_check((p[0] = 100, p), 1, 0, VSX_PAGE, "reuse off, marks left") == 0, "and every page reads as it should");
	reuse_on = true;
	CHECK(vsx_free_mark(&X, 20) == 1 && vsx_frees_count(&X) == 1, "reuse on again: a free marks");
	counts("reuse off, marks left");

	/* --- the age order (vsx_chunk_used): a pool of two chunks that can't grow, 100 holes in chunk 0 (the oldest),
	 * 88 new pages into them, then the demotion's choice. Reuse on: the holes from marks, applied by the short
	 * pool; chunk 0's age is now its newest page's, so the demotion takes chunk 1 (older pages only) and the new
	 * pages stay in VRAM. Off: the holes from discards (as before slot reuse), the age at join: chunk 0 --- */
	for (int off = 0; off < 2; off++) {
		int v, in0 = 0, kept = 0, ok = 1;

		reset_model();
		reuse_on = !off;
		pool_chunks = 2;
		next_free_hint = 0;
		for (i = 0; i < 512; i += 32)
			wr(p, seq(i, 32, p), 0, VSX_PAGE);
		CHECK(map[255] - 1 < VSX_SLOTS && map[256] - 1 >= VSX_SLOTS && map[511] - 1 < 2 * VSX_SLOTS && X.in_vram == 512,
		      "age order %d: pages 0-255 in chunk 0, 256-511 in chunk 1", off);
		for (i = 0; i < 100; i++) {
			if (!off) {
				free_page(i);
			} else {
				vsx_discard(&X, i);
				memset(truth[i], 0, VSX_PAGE);
			}
		}
		for (i = 512; i < 600; i += 32)
			wr(p, seq(i, i + 32 <= 600 ? 32 : 600 - i, p), 0, VSX_PAGE);
		for (i = 512; i < 600; i++)
			in0 += vsx_in_vram(map[i]) && map[i] - 1 < VSX_SLOTS;
		CHECK(in0 == 88 && X.on_disk == 0, "age order %d: the 88 new pages in chunk 0's holes (%d, disk %u)", off, in0,
		      X.on_disk);
		v = demote_victim();
		if (!off) {
			CHECK(X.st.short_applies == 1 && v == 1, "reuse on: the demotion takes chunk 1 (older pages only), not "
			      "chunk 0 refilled (victim %d, ages %llu %llu)", v, (unsigned long long)born[0],
			      (unsigned long long)born[1]);
			if (v >= 0)
				evict(v, 1);
			disk_complete(1);
			for (i = 512; i < 600; i++)
				kept += vsx_in_vram(map[i]);
			CHECK(kept == 88, "reuse on: after the demotion every new page is still in VRAM (%d of 88)", kept);
		} else {
			CHECK(v == 0 && born[0] < born[1], "reuse off: the age at join, as before slot reuse: the demotion takes "
			      "chunk 0 (victim %d, ages %llu %llu)", v, (unsigned long long)born[0],
			      (unsigned long long)born[1]);
		}
		for (i = 0; i < 600; i += 32)
			ok &= rd_check((seq(i, i + 32 <= 600 ? 32 : 600 - i, p), p), i + 32 <= 600 ? 32 : 600 - i, 0, VSX_PAGE,
				       "age order") == 0;
		CHECK(ok, "age order %d: every page reads as it should", off);
		counts("age order");
	}
	/* reuse off, chunk 0 emptied whole by discards, 88 new pages into it: its age stays the one at join (only
	 * the grow and the write-back's empty set it, as vswap.c), so the demotion takes it, new pages and all, as on
	 * 0.1534 */
	{
		int v, in0 = 0, ok = 1;

		reset_model();
		reuse_on = false;
		pool_chunks = 2;
		next_free_hint = 0;
		for (i = 0; i < 512; i += 32)
			wr(p, seq(i, 32, p), 0, VSX_PAGE);
		for (i = 0; i < 256; i++) {
			vsx_discard(&X, i);
			memset(truth[i], 0, VSX_PAGE);
		}
		for (i = 512; i < 600; i += 32)
			wr(p, seq(i, i + 32 <= 600 ? 32 : 600 - i, p), 0, VSX_PAGE);
		for (i = 512; i < 600; i++)
			in0 += vsx_in_vram(map[i]) && map[i] - 1 < VSX_SLOTS;
		v = demote_victim();
		CHECK(in0 == 88 && v == 0 && born[0] < born[1], "reuse off: chunk 0 emptied by discards and refilled (%d "
		      "new pages) keeps its age at join, the demotion takes it (victim %d, ages %llu %llu)", in0, v,
		      (unsigned long long)born[0], (unsigned long long)born[1]);
		for (i = 0; i < 600; i += 32)
			ok &= rd_check((seq(i, i + 32 <= 600 ? 32 : 600 - i, p), p), i + 32 <= 600 ? 32 : 600 - i, 0, VSX_PAGE,
				       "age at join") == 0;
		CHECK(ok, "reuse off, chunk 0 refilled: every page reads as it should");
		counts("age at join");
		reuse_on = true;
	}
	/* reuse off, chunk 0 emptied by its write-back (chunk_evict) and refilled: a new age (vswap.c: the empty
	 * branch), so the demotion now takes chunk 1 */
	{
		int v, in0 = 0;

		reset_model();
		reuse_on = false;
		pool_chunks = 2;
		next_free_hint = 0;
		for (i = 0; i < 512; i += 32)
			wr(p, seq(i, 32, p), 0, VSX_PAGE);
		evict(0, 1);
		disk_complete(1);
		for (i = 512; i < 600; i += 32)
			wr(p, seq(i, i + 32 <= 600 ? 32 : 600 - i, p), 0, VSX_PAGE);
		for (i = 512; i < 600; i++)
			in0 += vsx_in_vram(map[i]) && map[i] - 1 < VSX_SLOTS;
		v = demote_victim();
		CHECK(in0 == 88 && v == 1 && born[0] > born[1], "reuse off: chunk 0 written back whole and refilled (%d new "
		      "pages) gets a new age, the demotion takes chunk 1 (victim %d, ages %llu %llu)", in0, v,
		      (unsigned long long)born[0], (unsigned long long)born[1]);
		CHECK(rd_check((seq(0, 32, p), p), 32, 0, VSX_PAGE, "age after write-back") == 0 &&
		      rd_check((seq(512, 32, p), p), 32, 0, VSX_PAGE, "age after write-back") == 0,
		      "reuse off, chunk 0 written back: its old pages read from the disk, the new ones from VRAM");
		counts("age after write-back");
		reuse_on = true;
	}
	pool_chunks = NCH;

	/* a burst of marks during a batch's seal: the batch goes on, nothing applied while there is room */
	reset_model();
	wr(p, seq(300, 32, p), 0, VSX_PAGE);
	for (i = 332; i < 500; i += 32)
		wr(p, seq(i, i + 32 <= 500 ? 32 : 500 - i, p), 0, VSX_PAGE);
	{
		vs_u32 v0 = X.in_vram;

		burst_from = 300;
		job_hook = burst_hook;
		r = wr(p, seq(0, 8, p), 0, VSX_PAGE);
		job_hook = NULL;
		CHECK(!r && burst_from < 0 && X.in_vram == v0 + 8 && X.st.freed_vram == 0 && vsx_frees_count(&X) == 200,
		      "200 marks during a seal: the batch done, all 200 waiting (vram %u, count %ld)", X.in_vram,
		      vsx_frees_count(&X));
		CHECK(rd_check(p, 8, 0, VSX_PAGE, "burst batch") == 0, "the batch's pages read back");
		CHECK(rd_check((seq(300, 32, p), p), 32, 0, VSX_PAGE, "burst freed") == 0, "a marked page: old bytes or zeros");
		next_free_hint = 0;
		evict(1, 1);				/* the write-back applies them before it collects */
		evict(0, 1);
		{
			int k, written = 0;

			for (k = 300; k < 500; k++)
				written += map[k] != 0;
			CHECK(X.st.freed_vram == 200 && !vsx_frees_pending(&X) && !written && X.on_disk == 8,
			      "the write-backs applied the 200 first: none written (freed %llu, %d written, disk %u)",
			      (unsigned long long)X.st.freed_vram, written, X.on_disk);
		}
		CHECK(rd_check((seq(0, 8, p), p), 8, 0, VSX_PAGE, "burst written back") == 0, "the batch's pages from the disk");
		counts("burst");
	}
	/* a burst of frees with the swap's lock held: no mark waits for it */
	reset_model();
	wr(p, seq(0, 32, p), 0, VSX_PAGE);
	{
		pthread_t t;
		struct timespec t0, t1;
		int done = 0;

		pthread_mutex_lock(&burst_lock);	/* (vs->lock: a batch, a write-back) */
		burst_done = 0;
		pthread_create(&t, NULL, burst_marker, NULL);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		do {
			done = __atomic_load_n(&burst_done, __ATOMIC_SEQ_CST);
			clock_gettime(CLOCK_MONOTONIC, &t1);
			sched_yield();
		} while (!done && t1.tv_sec - t0.tv_sec < 20);
		CHECK(done, "30000 marks while the lock is held: all through without it");
		pthread_mutex_unlock(&burst_lock);
		pthread_join(t, NULL);
		CHECK(vsx_frees_count(&X) == NPG, "each page counted once (%ld)", vsx_frees_count(&X));
		vsx_apply_frees(&X);
		CHECK(X.in_vram == 0 && !vsx_frees_pending(&X) && X.st.freed_vram == 32 && X.st.freed_other == NPG - 32,
		      "and all applied at the next apply");
		counts("burst with the lock held");
	}

	/* --- random sequences, several seeds --- */
	static const vs_u32 seeds[] = { 0x13579bdf, 0x31415926, 0x27182818, 0x9e3779b9, 0x0badf00d };
	for (unsigned int sd = 0; sd < sizeof(seeds) / sizeof(seeds[0]); sd++) {
	rnd_s = seeds[sd];
	async_disk = sd & 1;
	for (int round = 0; round < 40; round++) {
		reset_model();
		gap_hook = gap_random;
		for (int step = 0; step < 300; step++) {
			int k = rnd() % 10, n = 1 + rnd() % 8, a = rnd() % (NPG - 8), c;

			gpu_ok = rnd() % 4 != 0;
			if (step % 50 == 0)
				X.plain = round % 4 == 0 ? (int)(rnd() % 3) : 0;	/* the switch, at run time */
			if (step % 50 == 25 && round % 2)
				reuse_on = rnd() % 3 != 0;	/* vswap_reuse flipped at run time */
			if (k < 4) {
				wr(p, seq(a, n, p), 0, VSX_PAGE);
			} else if (k < 7) {
				for (i = 0; i < n; i++)
					if (vsx_in_vram(map[a + i]) || !map[a + i] || map[a + i] == VSX_ON_DISK)
						p[i] = a + i;
				r = rd_check((seq(a, n, p), p), n, 0, VSX_PAGE, "random");
				CHECK(r != -2, "round %d: a random read came back wrong", round);
			} else if (k == 7) {
				if (rnd() & 1) {
					vsx_discard(&X, a);
					memset(truth[a], 0, VSX_PAGE);
					dead[a] = 0;
				} else {	/* a free: applied when room is needed (a short pool, the write-back) */
					for (i = 0; i < n; i++)
						free_page(a + i);
				}
				c = rnd() % 4;
				if (c == 0) {		/* the pool can't grow: a few slots left, short soon */
					int u = 0;

					for (i = 0; i < NSL; i++)
						u += used[i];
					slot_cap = u + (int)(rnd() % 16);
				} else if (c == 1) {
					slot_cap = NSL;
				}
			} else if (k == 8) {
				int in = (rnd() % 8) * 512;

				p[0] = a;
				if (map[a] != VSX_BAD)
					wr(p, 1, in, 512);
			} else {
				corrupt_bounce = round % 4 != 0;	/* (unsealed slots have no check to catch it) */
				r = rd_check((seq(a, n, p), p), n, 0, VSX_PAGE, "random bounce");
				corrupt_bounce = 0;
				CHECK(r != -2, "round %d: a read through a changed bounce came back wrong", round);
				if (rnd() % 3 == 0) {
					int c = rnd() % NCH, gpu = rnd() & 1;

					evict_m(c, gpu, rnd() & 1);
				}
			}
		}
		counts("random");
		{
			long bits = 0;

			for (i = 0; i < (int)(sizeof(freed_bits) / sizeof(freed_bits[0])); i++)
				bits += __builtin_popcountl(freed_bits[i]);
			CHECK(bits == vsx_frees_count(&X), "round %d: %ld marks, the count %ld", round, bits,
			      vsx_frees_count(&X));
		}
	}
	disk_complete(1);
	async_disk = 0;
	gap_hook = NULL;
	}

	CHECK_END(!plain_seen, "plaintext seen in VRAM or the bounce %d times", plain_seen);
	CHECK_END(!live_written, "a live slot was written %d times", live_written);
	CHECK_END(!slot_free_bad, "a slot freed that wasn't allocated (%d)", slot_free_bad);
	CHECK_END(!job_names_free, "a job or a GPU copy named a free slot %d times", job_names_free);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
