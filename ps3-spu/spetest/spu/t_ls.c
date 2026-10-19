/* Local store test: march C- with six background patterns, an address-in-data march and two
 * random fills over the free local store (above the program's image, below the stack
 * reserve). The image and the stack (about 10 KB) are not covered. */
#include "common.h"

typedef vec_uint4 V;

static V *lo, *hi;

static inline int same(V a, V b)
{
	return spu_extract(spu_gather(spu_cmpeq(a, b)), 0) == 0xF;
}

/* a mismatch: record every bad word of the quadword */
static void bad(unsigned sub, V *p, V got, V want)
{
	int i;

	for (i = 0; i < 4; i++)
		if (spu_extract(got, i) != spu_extract(want, i))
			t_fail(sub, (uint32_t)(uintptr_t)p + 4 * i, spu_extract(got, i), spu_extract(want, i));
}

#define R(p, want) do { V g_ = *(volatile V *)(p); if (!same(g_, (want))) bad(sub, (p), g_, (want)); } while (0)
#define W(p, v) (*(volatile V *)(p) = (v))

static void march(V pat)
{
	const unsigned sub = LS_MARCH;
	V inv = spu_nor(pat, pat), *p;

	for (p = lo; p < hi; p++) W(p, pat);
	for (p = lo; p < hi; p++) { R(p, pat); W(p, inv); }
	for (p = lo; p < hi; p++) { R(p, inv); W(p, pat); }
	for (p = hi - 1; p >= lo; p--) { R(p, pat); W(p, inv); }
	for (p = hi - 1; p >= lo; p--) { R(p, inv); W(p, pat); }
	for (p = lo; p < hi; p++) R(p, pat);
}

/* each word holds a hash of its own address (so aliased addresses differ), then the inverse */
static inline V addr_word(V *p, uint32_t salt)
{
	uint32_t a = (uint32_t)(uintptr_t)p;
	V w = { (a + 0) * 0x9E3779B1u ^ salt, (a + 4) * 0x9E3779B1u ^ salt,
		(a + 8) * 0x9E3779B1u ^ salt, (a + 12) * 0x9E3779B1u ^ salt };

	return w;
}

static void addr_march(uint32_t salt)
{
	const unsigned sub = LS_ADDR;
	V *p;

	for (p = lo; p < hi; p++) W(p, addr_word(p, salt));
	for (p = lo; p < hi; p++) { V w = addr_word(p, salt); R(p, w); W(p, spu_nor(w, w)); }
	for (p = hi - 1; p >= lo; p--) { V w = addr_word(p, salt); R(p, spu_nor(w, w)); W(p, w); }
	for (p = lo; p < hi; p++) { V w = addr_word(p, salt); R(p, w); }
}

static void rand_fill(uint32_t seed)
{
	const unsigned sub = LS_RAND;
	uint32_t s = spt_seed(seed, 5);
	V *p;

	for (p = lo; p < hi; p++) {
		V w = { spt_rnd(&s), spt_rnd(&s), spt_rnd(&s), spt_rnd(&s) };

		W(p, w);
	}
	s = spt_seed(seed, 5);
	for (p = lo; p < hi; p++) {
		V w = { spt_rnd(&s), spt_rnd(&s), spt_rnd(&s), spt_rnd(&s) };

		R(p, w);
	}
}

int main(unsigned long long slot, unsigned long long ea, unsigned long long seed)
{
	static const uint32_t pat[] = { 0x00000000u, 0x55555555u, 0x33333333u, 0x0F0F0F0Fu,
					0x00FF00FFu, 0x0000FFFFu };
	uint32_t sp = (uint32_t)(uintptr_t)__builtin_frame_address(0);
	unsigned i, n = 0;

	t_begin(slot, ea, T_LS);
	lo = (V *)(uintptr_t)t_free_start();
	hi = (V *)(uintptr_t)t_free_end();
	if (sp < t_free_end() - 512 || lo >= hi) {	/* the stack is not where we think */
		t_fail(LS_SETUP, sp, (uint32_t)(uintptr_t)lo, (uint32_t)(uintptr_t)hi);
		return t_end();
	}
	res.aux[0] = (uint32_t)(uintptr_t)lo;
	res.aux[1] = (uint32_t)(uintptr_t)hi;
	do {
		for (i = 0; i < sizeof(pat) / sizeof(pat[0]); i++)
			march(spu_splats(pat[i]));
		addr_march((uint32_t)seed + n);
		rand_fill((uint32_t)seed + n);
		n++;
	} while (t_more());
	return t_end();
}
