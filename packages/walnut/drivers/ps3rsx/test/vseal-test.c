/* vseal-test: spu/vseal.c, the SPE client's SPU program, run on the PC against vseal-sim.h's MFC model
 * (commands interleaved unit by unit, unfenced ones in any order, old puts starved). Checks:
 *  - the reference (the scalar ChaCha20-Poly1305 of spu/v2_aead.c) against RFC 8439 2.8.2 and A.5;
 *  - the mailbox word: a job runs only on its own word (generation, number, index); a stale or foreign
 *    word is dropped with nothing written and no answer; the answer is the same word;
 *  - the descriptor's inputs are never written, only its result part;
 *  - the done word is the job's last write (the MFC model records one written while anything is queued);
 *  - ECHO: the done word and the interrupt mailbox value;
 *  - SEAL: every page's ciphertext and tag equal the reference's, under many schedules;
 *  - OPEN: every page's plaintext back; a changed tag or ciphertext byte refuses that page and leaves its
 *    destination untouched; with the source rewritten while the gets read it (a torn or changed copy), every page
 *    is either refused with its destination untouched or opened to exactly its plaintext;
 *  - refused descriptors (unknown op, 0 or 33 pages, unaligned or null addresses): status INVALID, no
 *    destination touched, the done word still written; a bad magic: dropped, no answer;
 *  - GOLDEN: the program's answer equals src/vseal_golden.h and the reference's;
 *  - TEST_SLOW: the status line's progress counter rises by one a page (test image);
 *  - the release image (no VSEAL_TEST_OPS) refuses every test op as INVALID.
 * Built with a fix-off it must fail: VSEAL_FIXOFF_PLAIN_GET (a plain get instead of the fenced one: an LS
 * buffer refilled before its put has read it), VSEAL_FIXOFF_SPLIT_TAGS (the puts on other tag groups than
 * their buffers' gets: the fence no longer orders them), VSEAL_FIXOFF_PUT_REFUSED (a refused page's buffer
 * put anyway), VSEAL_FIXOFF_DONE_EARLY (the results and the done word before the data puts finished). */
#define VSEAL_HOST
#include "../spu/vseal.c"
#include "../src/vseal_golden.h"
#include "vseal-rfc8439.h"

static int fails, checks;

static void check(int ok, const char *what, int i)
{
	checks++;
	if (!ok) {
		if (fails < 20)
			printf("FAIL: %s (%d)\n", what, i);
		fails++;
	}
}

static uint32_t trs = 0x2468ace1;
static uint32_t trnd(void)
{
	trs ^= trs << 13;
	trs ^= trs >> 17;
	trs ^= trs << 5;
	return trs;
}
static void tfill(uint8_t *p, size_t n)
{
	while (n--)
		*p++ = trnd();
}

#define NP 34
static uint8_t ring[VSEAL_JOBS][VSEAL_PAGE] __attribute__((aligned(4096)));
static struct vseal_status status_line __attribute__((aligned(128)));
static uint8_t pt[NP][VSEAL_PAGE] __attribute__((aligned(4096)));
static uint8_t ct[NP][VSEAL_PAGE] __attribute__((aligned(4096)));
static uint8_t out[NP][VSEAL_PAGE] __attribute__((aligned(4096)));
static uint8_t key[32];
static uint32_t gen = 5, jobno;

static struct vseal_job *J(int idx)
{
	return (struct vseal_job *)ring[idx];
}

static void load(void)
{
	struct vseal_param *p = PARAM;

	memset(vs_ls, 0, sizeof(vs_ls));
	memset(p, 0, sizeof(*p));
	p->magic = VSEAL_MAGIC;
	p->abi = VSEAL_ABI;
	p->ring_ea = (uintptr_t)ring;
	p->status_ea = (uintptr_t)&status_line;
	tfill(key, 32);
	memcpy(p->key, key, 32);
	progress = 0;
	DEC_START();
}

static struct vseal_job *job(int idx, uint32_t op, uint32_t npages)
{
	struct vseal_job *j = J(idx);

	memset(j, 0, sizeof(*j));
	j->magic = VSEAL_MAGIC;
	j->op = op;
	j->npages = npages;
	j->seq = ((uint64_t)gen << 32) | ++jobno;
	return j;
}

static uint8_t in_copy[VSEAL_IN_BYTES];
static int inputs_changed;

/* the job's word into the mailbox: its inputs must come back unchanged, its done word last */
static void run_word(int idx, uint32_t word)
{
	vs_irq_count = 0;
	vs_done_watch = (uint8_t *)&J(idx)->done;
	memcpy(in_copy, J(idx), VSEAL_IN_BYTES);
	vseal_job(word);
	vs_drain();
	vs_done_watch = NULL;
	inputs_changed |= memcmp(in_copy, J(idx), VSEAL_IN_BYTES) != 0;
}

static void run(int idx)
{
	run_word(idx, vseal_word(J(idx)->seq, idx));
}

static int answered(int idx)
{
	struct vseal_job *j = J(idx);

	return j->done == j->seq && vs_irq_count == 1 && vs_irq_value == vseal_word(j->seq, idx);
}

static void sentinel(uint8_t *p)
{
	memset(p, 0xa5, VSEAL_PAGE);
}

static int is_sentinel(const uint8_t *p)
{
	int i;

	for (i = 0; i < VSEAL_PAGE; i++)
		if (p[i] != 0xa5)
			return 0;
	return 1;
}

/* the reference seal of page i */
static void ref_seal(const struct vseal_page *pg, const uint8_t *in, uint8_t *c, uint8_t *tag)
{
	uint8_t nonce[12];

	v2_nonce_from_counter(pg->ctr, nonce);
	v2_aead_seal(key, nonce, pg->ad, 16, in, VSEAL_PAGE, c, tag);
}

/* seal n pages pt -> ct; returns 1 if all equal the reference */
static int seal_all(int idx, int n)
{
	struct vseal_job *j = job(idx, VSEAL_OP_SEAL, n);
	uint8_t c[VSEAL_PAGE] __attribute__((aligned(16))), tag[16];
	int i, ok = 1;

	for (i = 0; i < n; i++) {
		j->page[i].src = (uintptr_t)pt[i];
		j->page[i].dst = (uintptr_t)ct[i];
		j->page[i].ctr = ((uint64_t)trnd() << 32) | trnd();
		v2_ad_make(trnd(), trnd(), 0, j->page[i].ad);
	}
	run(idx);
	ok &= answered(idx) && j->status == VSEAL_ST_OK;
	for (i = 0; i < n; i++) {
		ref_seal(&j->page[i], pt[i], c, tag);
		ok &= j->res[i].status == VSEAL_ST_OK && !memcmp(c, ct[i], VSEAL_PAGE) &&
		      !memcmp(tag, j->res[i].tag, 16);
	}
	return ok;
}

/* open the pages ct -> out with the tags seal_all left in descriptor sidx */
static struct vseal_job *open_all(int idx, int sidx, int n)
{
	struct vseal_job *s = J(sidx), *j = job(idx, VSEAL_OP_OPEN, n);
	int i;

	for (i = 0; i < n; i++) {
		j->page[i] = s->page[i];
		memcpy(j->page[i].tag, s->res[i].tag, 16);
		j->page[i].src = (uintptr_t)ct[i];
		j->page[i].dst = (uintptr_t)out[i];
		sentinel(out[i]);
	}
	run(idx);
	return j;
}

/* ciphertext changing under the gets: a random bit of a random page, sometimes */
static int change_n;
static void change_under_get(void)
{
	if (change_n && trnd() % 64 == 0) {
		ct[trnd() % change_n][trnd() % VSEAL_PAGE] ^= 1u << (trnd() % 8);
	}
}

int main(void)
{
	uint8_t tmp[512], tag[16];
	int i, s;

	/* the reference */
	for (i = 0; i < 2; i++) {
		const struct kat_aead *v = &kat_aead[i];

		v2_aead_seal(v->key, v->nonce, v->aad, v->alen, v->pt, v->len, tmp, tag);
		check(!memcmp(tmp, v->ct, v->len) && !memcmp(tag, v->tag, 16), "reference: RFC 8439 2.8.2/A.5", i);
	}

	for (s = 0; s < 300; s++) {
		int n = s == 0 ? VSEAL_PAGES_MAX : 1 + trnd() % 12;
		struct vseal_job *j;

		vs_rs = 0x9e3779b9u * (s + 1) | 1;
		load();
		for (i = 0; i < n; i++)
			tfill(pt[i], VSEAL_PAGE);
		if (s % 5 == 0)
			memset(pt[0], 0, VSEAL_PAGE);
		check(seal_all(s % VSEAL_JOBS, n), "seal: ciphertext and tags equal the reference", s);

		j = open_all((s + 1) % VSEAL_JOBS, s % VSEAL_JOBS, n);
		for (i = 0; i < n; i++)
			check(j->res[i].status == VSEAL_ST_OK && !memcmp(out[i], pt[i], VSEAL_PAGE),
			      "open: plaintext back", s);
		check(answered((s + 1) % VSEAL_JOBS), "open: done word and the answer", s);

		/* a changed tag and a changed ciphertext byte: those pages refused, untouched */
		J(s % VSEAL_JOBS)->res[0].tag[trnd() % 16] ^= 0x40;
		if (n > 1)
			ct[n - 1][trnd() % VSEAL_PAGE] ^= 0x08;
		j = open_all((s + 2) % VSEAL_JOBS, s % VSEAL_JOBS, n);
		check(j->res[0].status == VSEAL_ST_BADTAG && is_sentinel(out[0]), "open: changed tag refused", s);
		if (n > 1)
			check(j->res[n - 1].status == VSEAL_ST_BADTAG && is_sentinel(out[n - 1]),
			      "open: changed ciphertext refused", s);
		for (i = 1; i < n - 1; i++)
			check(j->res[i].status == VSEAL_ST_OK && !memcmp(out[i], pt[i], VSEAL_PAGE),
			      "open: the other pages still open", s);

		/* the source rewritten while it is read: refused and untouched, or exactly right */
		check(seal_all(s % VSEAL_JOBS, n), "seal again", s);
		change_n = n;
		vs_sim_hook = change_under_get;
		j = open_all((s + 3) % VSEAL_JOBS, s % VSEAL_JOBS, n);
		vs_sim_hook = NULL;
		change_n = 0;
		for (i = 0; i < n; i++)
			check((j->res[i].status == VSEAL_ST_BADTAG && is_sentinel(out[i])) ||
			      (j->res[i].status == VSEAL_ST_OK && !memcmp(out[i], pt[i], VSEAL_PAGE)),
			      "open under change: refused untouched or exactly the plaintext", s);
	}

	/* refused descriptors */
	load();
	{
		static const struct { uint32_t op, n; uint64_t src_off, dst_off; uint32_t magic; } bad[] = {
			{ VSEAL_OP_SEAL, 0, 0, 0, VSEAL_MAGIC }, { VSEAL_OP_SEAL, 33, 0, 0, VSEAL_MAGIC },
			{ VSEAL_OP_SEAL, 1, 16, 0, VSEAL_MAGIC }, { VSEAL_OP_OPEN, 1, 0, 2048, VSEAL_MAGIC },
			{ 99, 1, 0, 0, VSEAL_MAGIC }, { VSEAL_OP_TEST_BADADDR, 1, 16, 0, VSEAL_MAGIC },
		};
		for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
			struct vseal_job *j = job(0, bad[i].op, bad[i].n);

			j->magic = bad[i].magic;
			j->page[0].src = (uintptr_t)pt[0] + bad[i].src_off;
			j->page[0].dst = (uintptr_t)out[0] + bad[i].dst_off;
			sentinel(out[0]);
			run(0);
			check(j->status == VSEAL_ST_INVALID && j->done == j->seq && is_sentinel(out[0]),
			      "a refused descriptor: INVALID, nothing written, done", i);
		}
		{	/* a null destination */
			struct vseal_job *j = job(1, VSEAL_OP_SEAL, 1);

			j->page[0].src = (uintptr_t)pt[0];
			run(1);
			check(j->status == VSEAL_ST_INVALID, "a null address refused", 0);
		}
		{	/* a bad magic: no answer at all */
			struct vseal_job *j = job(2, VSEAL_OP_SEAL, 1);

			j->magic = 0x12345678;
			j->page[0].src = (uintptr_t)pt[0];
			j->page[0].dst = (uintptr_t)out[0];
			sentinel(out[0]);
			run(2);
			check(j->done == 0 && vs_irq_count == 0 && is_sentinel(out[0]), "a bad magic: dropped, no answer", 0);
		}
	}

	/* the mailbox word */
	load();
	{
		static const char *what[] = { "an older generation's word", "a newer generation's word",
					      "another job number's word", "another index's word" };
		struct vseal_job *j;
		int k;

		for (k = 0; k < 4; k++) {
			struct vseal_job *o = job(2, VSEAL_OP_SEAL, 1);	/* a valid job under another number */
			uint64_t sq;

			o->page[0].src = (uintptr_t)pt[1];
			o->page[0].dst = (uintptr_t)out[1];
			sentinel(out[1]);
			j = job(1, VSEAL_OP_SEAL, 1);
			j->page[0].src = (uintptr_t)pt[0];
			j->page[0].dst = (uintptr_t)out[0];
			sentinel(out[0]);
			sq = k == 0 ? j->seq - (1ull << 32) : k == 1 ? j->seq + (1ull << 32) : k == 2 ? j->seq + 1 : j->seq;
			run_word(1, vseal_word(sq, k == 3 ? 2 : 1));
			check(j->done == 0 && o->done == 0 && vs_irq_count == 0 && is_sentinel(out[0]) && is_sentinel(out[1]),
			      what[k], k);
		}
		run(1);
		check(answered(1) && j->res[0].status == VSEAL_ST_OK, "the job's own word then runs it", 0);
		check(vseal_word(0x1ff00000005ull, 3) == vseal_word(0xff00000005ull, 3) &&
		      vseal_word(0x100000005ull, 3) != vseal_word(0x200000005ull, 3) &&
		      vseal_word(0x100000005ull, 2) != vseal_word(0x100000005ull, 3),
		      "the word: 8 generation bits, the index", 0);
	}

	/* GOLDEN */
	load();
	{
		struct vseal_job *j = job(2, VSEAL_OP_GOLDEN, 0);
		uint8_t k2[32], nonce[12], ad[16], c[VSEAL_PAGE], t2[16];
		uint32_t x = 7, fnv = 2166136261u;

		run(2);
		check(answered(2) && !memcmp(j->res[0].tag, vseal_golden_tag, 16) && j->fnv == VSEAL_GOLDEN_FNV,
		      "golden: the program's answer equals vseal_golden.h", 0);
		for (i = 0; i < 32; i++)
			k2[i] = 0x80 + i;
		for (i = 0; i < VSEAL_PAGE; i++) {
			x = x * 1103515245u + 12345u;
			tmp[0] = x >> 16;
			pt[0][i] = tmp[0];
		}
		v2_nonce_from_counter(VSEAL_GOLDEN_CTR, nonce);
		v2_ad_make(0x1234, 77, 0, ad);
		v2_aead_seal(k2, nonce, ad, 16, pt[0], VSEAL_PAGE, c, t2);
		for (i = 0; i < VSEAL_PAGE; i++)
			fnv = (fnv ^ c[i]) * 16777619u;
		check(!memcmp(t2, vseal_golden_tag, 16) && fnv == VSEAL_GOLDEN_FNV,
		      "golden: vseal_golden.h equals the reference", 0);
	}

	/* ECHO and TEST_SLOW's progress */
	load();
	{
		struct vseal_job *j = job(3, VSEAL_OP_ECHO, 0);
		uint64_t p0 __attribute__((unused));

		run(3);
		check(answered(3), "echo", 0);
		p0 = progress;	/* the program's count since load (the line in RAM may be an older load's) */
		j = job(3, VSEAL_OP_TEST_SLOW, 5);
		j->arg = 3000;
		for (i = 0; i < 5; i++) {
			j->page[i].src = (uintptr_t)pt[i];
			j->page[i].dst = (uintptr_t)out[i];
		}
		run(3);
#ifdef VSEAL_TEST_OPS
		check(j->done == j->seq && status_line.progress == p0 + 5 && status_line.last_seq == j->seq,
		      "slow: the progress counter rises a page at a time", 0);
#else
		check(answered(3) && j->status == VSEAL_ST_INVALID, "release image: slow refused", 0);
#endif
		j = job(1, VSEAL_OP_TEST_DIFF, 6);
		for (i = 0; i < 6; i++) {
			tfill(pt[i], VSEAL_PAGE);
			if (i == 1)
				memset(pt[i], 0xff, VSEAL_PAGE);
			j->page[i].src = (uintptr_t)pt[i];
			j->page[i].dst = (uintptr_t)out[i];
			j->page[i].ctr = trnd();
			tfill(j->page[i].ad, 16);
			tfill(j->page[i].tag, 16);
			if (i >= 2)
				memset(j->page[i].ad, 0xff, 16);	/* r at its clamp maximum */
			sentinel(out[i]);
		}
		run(1);
#ifdef VSEAL_TEST_OPS
		for (i = 0; i < 6; i++)
			check(j->res[i].status == VSEAL_ST_OK && is_sentinel(out[i]), "diff: vector = scalar, nothing put", i);
		j = job(0, VSEAL_OP_TEST_NOIRQ, 0);
		run(0);
		check(j->done == j->seq && vs_irq_count == 0, "noirq: done, no interrupt", 0);
#else
		check(answered(1) && j->status == VSEAL_ST_INVALID && is_sentinel(out[0]), "release image: diff refused", 0);
		{
			static const uint32_t tops[] = { VSEAL_OP_TEST_HANG, VSEAL_OP_TEST_NOIRQ, VSEAL_OP_TEST_BADADDR };

			for (i = 0; i < 3; i++) {
				j = job(0, tops[i], tops[i] == VSEAL_OP_TEST_BADADDR);
				j->page[0].src = (uintptr_t)pt[0];
				run(0);
				check(answered(0) && j->status == VSEAL_ST_INVALID, "release image: test op refused", i);
			}
		}
#endif
	}
	check(!inputs_changed, "no job wrote its descriptor's inputs", 0);
	check(!vs_done_early, "every done word was its job's last write", 0);
	check(!vs_overflow, "the MFC model's queue never overflowed", 0);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
