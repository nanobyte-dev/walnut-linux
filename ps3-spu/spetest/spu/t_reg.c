/* Register file test: 128 registers x 128 bits loaded from a table, stored back, compared.
 * Patterns: zeros, ones, 0x55, 0xAA, the register number, a distinct value per register and
 * word, a walking one, a walking zero and random tables. */
#include "common.h"

typedef vec_uint4 V;

V regsrc[128] __attribute__((aligned(16)));
V regdst[128] __attribute__((aligned(16)));
V regsave[128] __attribute__((aligned(16)));

void reg_roundtrip(void);

static void fill(unsigned pat, uint32_t seed)
{
	uint32_t s = spt_seed(seed, pat);
	unsigned i, w;

	for (i = 0; i < 128; i++)
		for (w = 0; w < 4; w++) {
			uint32_t v;

			switch (pat) {
			case REG_ZERO: v = 0; break;
			case REG_ONES: v = ~0u; break;
			case REG_55: v = 0x55555555u; break;
			case REG_AA: v = 0xAAAAAAAAu; break;
			case REG_INDEX: v = i; break;
			case REG_UNIQUE: v = (i * 4 + w + 1) * 0x01010101u; break;
			case REG_WALK1: v = 1u << ((i + w * 7) & 31); break;
			case REG_WALK0: v = ~(1u << ((i + w * 7) & 31)); break;
			default: v = spt_rnd(&s); break;
			}
			regsrc[i] = spu_insert(v, regsrc[i], w);
		}
}

int main(unsigned long long slot, unsigned long long ea, unsigned long long seed)
{
	unsigned pat, i, w, n = 0;

	t_begin(slot, ea, T_REG);
	do {
		for (pat = REG_ZERO; pat < REG_SETUP; pat++) {
			fill(pat, (uint32_t)seed + n);
			for (i = 0; i < 128; i++)
				regdst[i] = spu_splats(0xDEADBEEFu);
			reg_roundtrip();
			for (i = 0; i < 128; i++)
				for (w = 0; w < 4; w++) {
					uint32_t g = spu_extract(regdst[i], w), e = spu_extract(regsrc[i], w);

					if (g != e)
						t_fail(pat, i * 4 + w, g, e);
				}
		}
		n++;
	} while (t_more());
	return t_end();
}
