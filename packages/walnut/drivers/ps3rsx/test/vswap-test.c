/* vswap-test: the sealed VRAM swap's page logic (src/vswap_core.c) on a model: a pool of 4 chunks of VRAM
 * slots, the GART bounce, the disk, and an SPE that seals and opens with the same core (spu/v2_*.c) and
 * refuses a page whose tag doesn't match without writing it, as spu/vseal.c does. Someone else may write
 * VRAM and the bounce at any time (the GPU's clients); the model tracks what each device page must read.
 *
 * Checked throughout: no plaintext ever in a VRAM slot or the bounce (every plaintext page carries a marker);
 * no nonce counter used twice; no slot a map entry points at is written; a read returns exactly the page's
 * last write, or fails (never another page's bytes, never changed bytes); the map's counters match it.
 * Cases: writes and reads by R1 and R2, overwrites into fresh slots, partial writes and reads, discards,
 * BAD entries (discard, full write, partial write, read, write-back), changed slots and a changed bounce
 * (the retry from VRAM), a map entry pointing at another page's slot, the lockout, a restart (-ETIMEDOUT: a
 * resubmission with fresh counters), the SPE gone (-ENODEV: the PPE opens, the drain), kept pages
 * (-EHWPOISON), no SPE at all (nothing in VRAM), the test builds' unsealed slots, write-back into private
 * pages, random sequences.
 * Built with VSX_FIXOFF_IGNORE_TAG, VSX_FIXOFF_SAME_CTR, VSX_FIXOFF_AD_NO_PAGE, VSX_FIXOFF_WB_INTO_BOUNCE,
 * VSX_FIXOFF_NO_RETRY or VSX_FIXOFF_DISCARD_BAD it must fail. */
#define VSX_PLAIN
#include <stdio.h>
#include <stdlib.h>
#include "../src/vswap_core.c"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails < 30) { printf("FAIL: "); printf(__VA_ARGS__); \
	printf("\n"); } fails++; } } while (0)

#define NPG	600
#define NCH	4
#define NSL	((int)(NCH * VSX_SLOTS))
#define MARK	0x504c41494e545854ull	/* "PLAINTXT": the first word of every plaintext page */

static vs_u8 vram[NSL][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 bounce[VSX_SLOTS][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 disk[NPG][VSX_PAGE];
static vs_u8 truth[NPG][VSX_PAGE];
static int truth_lost[NPG];		/* allowed to fail: its slot was changed by someone else */
static vs_u8 bio[64][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 priv[VSEAL_PAGES_MAX][VSX_PAGE] __attribute__((aligned(16)));
static vs_u8 scratch[VSX_PAGE] __attribute__((aligned(16)));
static struct vsx_meta meta[NSL];
static int used[NSL], slot_page_of[NSL];
static vs_u32 map[NPG];
static vs_u8 key[32];
static struct vsx X;

/* the model's knobs */
static int gpu_ok = 1, job_fail, job_fail_once, corrupt_bounce, have_key = 1, pinned, drains, slot_free_bad;
static vs_u64 ctr_seen[200000];
static unsigned int nctr;
static int live_written, plain_seen, next_free_hint, bounce_plain[VSX_SLOTS];

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
int vsx_hw_slot_alloc(void *hw)
{
	for (int n = 0; n < NSL; n++) {
		int s = (next_free_hint + n) % NSL;

		if (!used[s]) {
			used[s] = 1;
			next_free_hint = s + 1;
			return s;
		}
	}
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
	for (unsigned int i = 0; i < n; i++) {
		memcpy(bounce[i], vram[slots[i]], VSX_PAGE);
		bounce_plain[i] = !!(meta[slots[i]].flags & VSX_M_PLAIN);	/* (the test builds' off-switch) */
		if (corrupt_bounce)
			bounce[i][rnd() % VSX_PAGE] ^= 0x20;	/* a client changes the bounce */
	}
	return 0;
}
vs_u64 vsx_hw_bounce_ea(void *hw, unsigned int i) { return EA(bounce[i]); }
const void *vsx_hw_bounce_ptr(void *hw, unsigned int i) { return bounce[i]; }
const vs_u8 *vsx_hw_key(void *hw) { return have_key ? key : NULL; }
void vsx_hw_pin(void *hw, void *ref) { pinned++; }
void vsx_hw_disk(void *hw, struct vsx_piece *pc, int write)
{
	if (write)
		memcpy(disk[pc->p] + pc->in_page, pc->buf, pc->len);
	else
		memcpy(pc->buf, disk[pc->p] + pc->in_page, pc->len);
}
int vsx_hw_disk_copy(void *hw, void *ctx, vs_u32 p, const void *page) { memcpy(disk[p], page, VSX_PAGE); return 0; }
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

	if (job_fail) {
		int r = job_fail;

		if (job_fail_once)
			job_fail = 0;
		return r;
	}
	for (unsigned int i = 0; i < n; i++) {
		vs_u8 nonce[12], buf[VSX_PAGE] __attribute__((aligned(16)));
		vs_u8 *src = (vs_u8 *)(unsigned long)pg[i].src, *dst = (vs_u8 *)(unsigned long)pg[i].dst;
		vs_u32 s;

		v2_nonce_from_counter(pg[i].ctr, nonce);
		memcpy(buf, src, VSX_PAGE);		/* one copy */
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
}

static void scan(const char *when)
{
	for (int s = 0; s < NSL; s++)
		if (!(meta[s].flags & VSX_M_PLAIN) && !memcmp(vram[s], &(vs_u64){ MARK }, 8))
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
		if (memcmp(bio[i], truth[p[i]] + in_page, len))
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
	X.map = map;
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
	for (int i = 0; i < 32; i++)
		key[i] = rnd();
}

/* write-back of chunk c, as vswap.c's chunk_evict: the GPU copy (or not), opens into private pages,
 * the disk written from those, the map switched for what didn't change */
static void evict(int c, int gpu)
{
	vs_u32 slots[VSEAL_PAGES_MAX], pages[VSEAL_PAGES_MAX];
	void *pp[VSEAL_PAGES_MAX], *pr[VSEAL_PAGES_MAX];
	int n = 0;

	if (gpu)
		for (int s = 0; s < (int)VSX_SLOTS; s++) {
			memcpy(bounce[s], vram[c * VSX_SLOTS + s], VSX_PAGE);
			bounce_plain[s] = !!(meta[c * VSX_SLOTS + s].flags & VSX_M_PLAIN);
		}
	for (int s = 0; s <= (int)VSX_SLOTS; s++) {
		int slot = c * VSX_SLOTS + s;

		if (s < (int)VSX_SLOTS && used[slot] && map[slot_page_of[slot]] == (vs_u32)slot + 1) {
			slots[n] = slot;
			pages[n] = slot_page_of[slot];
			pp[n] = priv[n];
			pr[n] = priv[n];
			n++;
			if (n < VSEAL_PAGES_MAX)
				continue;
		}
		if (!n)
			continue;
		vsx_writeback_open(&X, slots, pages, n, pp, pr, gpu);
		scan("write-back");
		for (int i = 0; i < n; i++) {
			if (X.op[i].status != VSX_O_OK)
				continue;
			memcpy(disk[pages[i]], priv[i], VSX_PAGE);
			if (map[pages[i]] == slots[i] + 1)
				vsx_set_disk(&X, pages[i]);
		}
		n = 0;
	}
	counts("write-back");
}

/* someone else changes page p's slot */
static void corrupt(int p, int at)
{
	if (vsx_in_vram(map[p]))
		vram[map[p] - 1][at] ^= 1;
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

	/* --- random sequences --- */
	for (int round = 0; round < 40; round++) {
		reset_model();
		for (int step = 0; step < 300; step++) {
			int k = rnd() % 10, n = 1 + rnd() % 8, a = rnd() % (NPG - 8);

			gpu_ok = rnd() % 4 != 0;
			if (k < 4) {
				wr(p, seq(a, n, p), 0, VSX_PAGE);
			} else if (k < 7) {
				for (i = 0; i < n; i++)
					if (vsx_in_vram(map[a + i]) || !map[a + i] || map[a + i] == VSX_ON_DISK)
						p[i] = a + i;
				r = rd_check((seq(a, n, p), p), n, 0, VSX_PAGE, "random");
				CHECK(r != -2, "round %d: a random read came back wrong", round);
			} else if (k == 7) {
				vsx_discard(&X, a);
				memset(truth[a], 0, VSX_PAGE);
			} else if (k == 8) {
				int in = (rnd() % 8) * 512;

				p[0] = a;
				if (map[a] != VSX_BAD)
					wr(p, 1, in, 512);
			} else {
				corrupt_bounce = 1;
				r = rd_check((seq(a, n, p), p), n, 0, VSX_PAGE, "random bounce");
				corrupt_bounce = 0;
				CHECK(r != -2, "round %d: a read through a changed bounce came back wrong", round);
				if (rnd() % 3 == 0)
					evict(rnd() % NCH, rnd() & 1);
			}
		}
		counts("random");
	}

	CHECK(!plain_seen, "plaintext seen in VRAM or the bounce %d times", plain_seen);
	CHECK(!live_written, "a live slot was written %d times", live_written);
	CHECK(!slot_free_bad, "a slot freed that wasn't allocated (%d)", slot_free_bad);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
