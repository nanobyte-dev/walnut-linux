/* Execution units: every operation of oplist.h on EX_QW pseudo-random quadwords (integer,
 * logic, shifts, rotates, quadword shifts, shuffles, single and double precision, compares,
 * conversions) plus a branch kernel and an unaligned access kernel. The results go to the PPE
 * (out buffer, EX_OP_BYTES per op), which checks them against its own computation. In stress
 * mode the first pass is sent and every later pass is compared with a digest of the first. */
#include "common.h"
#include "../oplist.h"

typedef vec_uint4 V;
#define U4(x)	((vec_uint4)(x))
#define S4(x)	((vec_int4)(x))
#define F4(x)	((vec_float4)(x))
#define D2(x)	((vec_double2)(x))
#define H8(x)	((vec_ushort8)(x))
#define SH8(x)	((vec_short8)(x))
#define B16(x)	((vec_uchar16)(x))
#define SB16(x)	((vec_char16)(x))

static inline V mul32(V a, V b)
{
	V r = a;
	r = spu_insert(spu_extract(a, 0) * spu_extract(b, 0), r, 0);
	r = spu_insert(spu_extract(a, 1) * spu_extract(b, 1), r, 1);
	r = spu_insert(spu_extract(a, 2) * spu_extract(b, 2), r, 2);
	r = spu_insert(spu_extract(a, 3) * spu_extract(b, 3), r, 3);
	return r;
}

/* a/b by estimate and one Newton step (no reference: digest only) */
static inline vec_float4 fdiv(vec_float4 a, vec_float4 b)
{
	vec_float4 one = spu_splats(1.0f), y = spu_re(b);

	y = spu_madd(spu_nmsub(y, b, one), y, y);
	return spu_mul(a, y);
}

static V buf[EX_QW] __attribute__((aligned(128)));
static uint32_t dig0[OPX_N], dig[OPX_N];
static uint8_t mem[1024] __attribute__((aligned(16)));

#define X(n, in, cmp, expr) \
static void op_##n(uint32_t seed) \
{ \
	uint32_t s = spt_seed(seed, OPX_##n), wa[4], wb[4], wc[4]; \
	int i; \
	for (i = 0; i < EX_QW; i++) { \
		V a, b, c; \
		spt_gen(in, &s, wa, wb, wc); \
		a = (V){ wa[0], wa[1], wa[2], wa[3] }; \
		b = (V){ wb[0], wb[1], wb[2], wb[3] }; \
		c = (V){ wc[0], wc[1], wc[2], wc[3] }; \
		(void)b; (void)c; \
		buf[i] = (expr); \
	} \
}
OPLIST(X)
#undef X

static void (*const ops[OPX_N])(uint32_t) = {
#define X(n, in, cmp, expr) op_##n,
	OPLIST(X)
#undef X
};

static uint32_t digest(void)
{
	const uint32_t *w = (const uint32_t *)buf;
	uint32_t h = 2166136261u;
	int i;

	for (i = 0; i < EX_QW * 4; i++)
		h = (h ^ w[i]) * 16777619u + (h >> 13);
	return h;
}

static uint32_t kernels[2];

static void put_block(uint64_t ea, const void *p, unsigned size)
{
	while (size) {
		unsigned n = size > 16384 ? 16384 : size;

		mfc_put((volatile void *)p, ea, n, 1, 0, 0);
		p = (const char *)p + n;
		ea += n;
		size -= n;
	}
	mfc_write_tag_mask(1u << 1);
	mfc_read_tag_status_all();
}

int main(unsigned long long slot, unsigned long long ea, unsigned long long seed0)
{
	uint64_t out;
	uint32_t seed, pass = 0, k;
	unsigned op;

	t_begin(slot, ea, T_EXEC);
	out = EA64(ctl.out_hi, ctl.out_lo);
	seed = (uint32_t)seed0;
	do {
		for (op = 0; op < OPX_N; op++) {
			ops[op](seed);
			dig[op] = digest();
			if (pass == 0) {
				dig0[op] = dig[op];
				put_block(out + (uint64_t)op * EX_OP_BYTES, buf, EX_OP_BYTES);
			} else if (dig[op] != dig0[op]) {
				t_fail(EX_SELF, op, dig[op], dig0[op]);
			}
		}
		k = spt_branch_kernel(seed, 2000);
		if (pass == 0)
			kernels[0] = k;
		else if (k != kernels[0])
			t_fail(EX_BRANCH, pass, k, kernels[0]);
		k = spt_mem_kernel(mem, seed);
		if (pass == 0)
			kernels[1] = k;
		else if (k != kernels[1])
			t_fail(EX_MEM, pass, k, kernels[1]);
		pass++;
	} while (t_more());
	res.aux[0] = kernels[0];	/* the PPE computes the same */
	res.aux[1] = kernels[1];
	res.aux[2] = OPX_N;
	return t_end();
}
