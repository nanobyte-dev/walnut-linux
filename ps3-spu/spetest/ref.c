#include <math.h>
#include <string.h>
#include "gen.h"
#include "ref.h"

const struct spt_opinfo spt_ops[OPX_N] = {
#define X(n, in, cmp, expr) { #n, in, CMP_##cmp },
	OPLIST(X)
#undef X
};

typedef unsigned __int128 u128;
typedef __int128 s128;

static float f_of(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }

/* ---- SPU single precision ---- */
static void sp_unpack(uint32_t w, int *sign, int *e, uint32_t *m)
{
	*sign = w >> 31;
	*e = (w >> 23) & 0xFF;
	*m = *e ? ((w & 0x7FFFFF) | 0x800000) : 0;	/* denormals read as zero */
}

int spt_sp_fma(uint32_t a, uint32_t b, uint32_t c, uint32_t *r)
{
	int sa, ea, sb, eb, sc, ec, ul, sp, scs, pe;
	uint32_t ma, mb, mc;
	uint64_t P;
	s128 n;
	u128 mag;
	int p, e, sign;
	uint32_t m;

	sp_unpack(a, &sa, &ea, &ma);
	sp_unpack(b, &sb, &eb, &mb);
	sp_unpack(c, &sc, &ec, &mc);
	P = (uint64_t)ma * mb;
	if (!P) {	/* the product is zero: the addend, flushed */
		*r = mc ? c : 0;
		return 1;
	}
	pe = ea + eb - 300;	/* exponent of the product's unit bit */
	if (!mc) {
		ul = pe;
		sp = 0;
		scs = 0;
	} else {
		ul = pe < ec - 150 ? pe : ec - 150;
		sp = pe - ul;
		scs = ec - 150 - ul;
		if (sp > 70 || scs > 70)
			return 0;
	}
	n = (s128)((u128)P << sp);
	if (sa ^ sb)
		n = -n;
	if (mc) {
		s128 cc = (s128)((u128)mc << scs);

		n += sc ? -cc : cc;
	}
	if (n == 0) {
		*r = 0;
		return 1;
	}
	sign = n < 0;
	mag = sign ? (u128)(-n) : (u128)n;
	p = 127;
	while (!(mag >> p))
		p--;
	m = p >= 23 ? (uint32_t)(mag >> (p - 23)) : (uint32_t)(mag << (23 - p));
	e = ul + p - 23 + 150;
	if (e <= 0) {
		*r = 0;
		return 1;
	}
	if (e >= 256) {
		*r = ((uint32_t)sign << 31) | 0x7FFFFFFFu;
		return 1;
	}
	*r = ((uint32_t)sign << 31) | ((uint32_t)e << 23) | (m & 0x7FFFFF);
	return 1;
}

static uint32_t flip(uint32_t w) { return w ^ 0x80000000u; }

/* ---- byte and bit helpers ---- */
static void tob(const uint32_t w[4], uint8_t b[16])
{
	int i;

	for (i = 0; i < 16; i++)
		b[i] = (uint8_t)(w[i / 4] >> (24 - 8 * (i % 4)));
}

static void fromb(const uint8_t b[16], uint32_t w[4])
{
	int i;

	for (i = 0; i < 4; i++)
		w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
}

static u128 toq(const uint32_t w[4])
{
	return (u128)w[0] << 96 | (u128)w[1] << 64 | (u128)w[2] << 32 | w[3];
}

static void fromq(u128 q, uint32_t w[4])
{
	w[0] = (uint32_t)(q >> 96);
	w[1] = (uint32_t)(q >> 64);
	w[2] = (uint32_t)(q >> 32);
	w[3] = (uint32_t)q;
}

static uint16_t hw(const uint32_t w[4], int i) { return (uint16_t)(w[i / 2] >> (i % 2 ? 0 : 16)); }
static void sethw(uint32_t w[4], int i, uint16_t v)
{
	int sh = i % 2 ? 0 : 16;

	w[i / 2] = (w[i / 2] & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
}

static double d_of(const uint32_t w[4], int i)
{
	uint64_t u = (uint64_t)w[2 * i] << 32 | w[2 * i + 1];
	double d;

	memcpy(&d, &u, 8);
	return d;
}

static void setd(uint32_t w[4], int i, double d)
{
	uint64_t u;

	memcpy(&u, &d, 8);
	w[2 * i] = (uint32_t)(u >> 32);
	w[2 * i + 1] = (uint32_t)u;
}

static void setmask64(uint32_t w[4], int i, int yes)
{
	w[2 * i] = w[2 * i + 1] = yes ? ~0u : 0;
}

static uint32_t shl32(uint32_t x, uint32_t n) { return n > 31 ? 0 : x << n; }
static uint32_t shr32(uint32_t x, uint32_t n) { return n > 31 ? 0 : x >> n; }

void spt_ref(int op, const uint32_t a[4], const uint32_t b[4], const uint32_t c[4],
	     uint32_t r[4], unsigned *skip)
{
	int i;
	uint8_t ab[16], bb[16], cb[16], rb[16];

	*skip = 0;
	memset(r, 0, 16);
	tob(a, ab);
	tob(b, bb);
	tob(c, cb);
	switch (op) {
#define W4(expr) for (i = 0; i < 4; i++) { uint32_t x = a[i], y = b[i], z = c[i]; (void)x; (void)y; (void)z; r[i] = (expr); } break
	case OPX_add: W4(x + y);
	case OPX_sub: W4(x - y);
	case OPX_and: W4(x & y);
	case OPX_or: W4(x | y);
	case OPX_xor: W4(x ^ y);
	case OPX_nor: W4(~(x | y));
	case OPX_nand: W4(~(x & y));
	case OPX_andc: W4(x & ~y);
	case OPX_orc: W4(x | ~y);
	case OPX_eqv: W4(~(x ^ y));
	case OPX_selb: W4((x & ~z) | (y & z));
	case OPX_ceq: W4(x == y ? ~0u : 0);
	case OPX_cgt: W4((int32_t)x > (int32_t)y ? ~0u : 0);
	case OPX_clgt: W4(x > y ? ~0u : 0);
	case OPX_ceqh:
		for (i = 0; i < 8; i++) sethw(r, i, hw(a, i) == hw(b, i) ? 0xFFFF : 0);
		break;
	case OPX_cgth:
		for (i = 0; i < 8; i++) sethw(r, i, (int16_t)hw(a, i) > (int16_t)hw(b, i) ? 0xFFFF : 0);
		break;
	case OPX_clgth:
		for (i = 0; i < 8; i++) sethw(r, i, hw(a, i) > hw(b, i) ? 0xFFFF : 0);
		break;
	case OPX_ceqb:
		for (i = 0; i < 16; i++) rb[i] = ab[i] == bb[i] ? 0xFF : 0;
		fromb(rb, r);
		break;
	case OPX_cgtb:
		for (i = 0; i < 16; i++) rb[i] = (int8_t)ab[i] > (int8_t)bb[i] ? 0xFF : 0;
		fromb(rb, r);
		break;
	case OPX_clgtb:
		for (i = 0; i < 16; i++) rb[i] = ab[i] > bb[i] ? 0xFF : 0;
		fromb(rb, r);
		break;
	case OPX_shl: W4(shl32(x, y & 63));
	case OPX_rot: W4((x << (y & 31)) | (y & 31 ? x >> (32 - (y & 31)) : 0));
	case OPX_rotm: W4(shr32(x, (0u - y) & 63));
	case OPX_rotma: W4((uint32_t)((int32_t)x >> (((0u - y) & 63) > 31 ? 31 : ((0u - y) & 63))));
	case OPX_shli: W4(x << 5);
	case OPX_roti: W4((x << 11) | (x >> 21));
	case OPX_rotmi: W4(x >> 9);
	case OPX_rotmai: W4((uint32_t)((int32_t)x >> 9));
	case OPX_ah:
		for (i = 0; i < 8; i++) sethw(r, i, (uint16_t)(hw(a, i) + hw(b, i)));
		break;
	case OPX_sfh:
		for (i = 0; i < 8; i++) sethw(r, i, (uint16_t)(hw(a, i) - hw(b, i)));
		break;
	case OPX_shlh:
		for (i = 0; i < 8; i++) {
			unsigned n = hw(b, i) & 31;

			sethw(r, i, n > 15 ? 0 : (uint16_t)(hw(a, i) << n));
		}
		break;
	case OPX_roth:
		for (i = 0; i < 8; i++) {
			unsigned n = hw(b, i) & 15;
			uint16_t v = hw(a, i);

			sethw(r, i, (uint16_t)((v << n) | (n ? v >> (16 - n) : 0)));
		}
		break;
	case OPX_mpye: W4((uint32_t)((int32_t)(int16_t)(x >> 16) * (int32_t)(int16_t)(y >> 16)));
	case OPX_mpyo: W4((uint32_t)((int32_t)(int16_t)x * (int32_t)(int16_t)y));
	case OPX_mpyu: W4((x & 0xFFFF) * (y & 0xFFFF));
	case OPX_mpyh: W4((uint32_t)((int32_t)(int16_t)(x >> 16) * (int32_t)(int16_t)y) << 16);
	case OPX_mpya: W4((uint32_t)((int32_t)(int16_t)x * (int32_t)(int16_t)y) + z);
	case OPX_mul32: W4(x * y);
	case OPX_clz: W4(x ? (uint32_t)__builtin_clz(x) : 32);
	case OPX_cntb:
		for (i = 0; i < 16; i++) rb[i] = (uint8_t)__builtin_popcount(ab[i]);
		fromb(rb, r);
		break;
	case OPX_avgb:
		for (i = 0; i < 16; i++) rb[i] = (uint8_t)((ab[i] + bb[i] + 1) >> 1);
		fromb(rb, r);
		break;
	case OPX_absdb:
		for (i = 0; i < 16; i++) rb[i] = ab[i] > bb[i] ? ab[i] - bb[i] : bb[i] - ab[i];
		fromb(rb, r);
		break;
	case OPX_sumb:
		break;
	case OPX_gb:
		r[0] = (a[0] & 1) << 3 | (a[1] & 1) << 2 | (a[2] & 1) << 1 | (a[3] & 1);
		break;
	case OPX_gbh:
		for (i = 0; i < 8; i++) r[0] |= (uint32_t)(hw(a, i) & 1) << (7 - i);
		break;
	case OPX_gbb:
		for (i = 0; i < 16; i++) r[0] |= (uint32_t)(ab[i] & 1) << (15 - i);
		break;
	case OPX_orx:
		r[0] = a[0] | a[1] | a[2] | a[3];
		break;
	case OPX_xsbh:
		for (i = 0; i < 8; i++) sethw(r, i, (uint16_t)(int16_t)(int8_t)(hw(a, i) & 0xFF));
		break;
	case OPX_xshw: W4((uint32_t)(int32_t)(int16_t)x);
	case OPX_xswd:
		r[0] = (int32_t)a[1] < 0 ? ~0u : 0;
		r[1] = a[1];
		r[2] = (int32_t)a[3] < 0 ? ~0u : 0;
		r[3] = a[3];
		break;
	case OPX_rotqby: {
		unsigned n = b[0] & 15;

		for (i = 0; i < 16; i++) rb[i] = ab[(i + n) & 15];
		fromb(rb, r);
		break;
	}
	case OPX_shlqby: {
		unsigned n = b[0] & 31;

		for (i = 0; i < 16; i++) rb[i] = i + n < 16 ? ab[i + n] : 0;
		fromb(rb, r);
		break;
	}
	case OPX_rotqmby: {
		unsigned n = (0u - b[0]) & 31;

		for (i = 0; i < 16; i++) rb[i] = (n < 16 && (unsigned)i >= n) ? ab[i - n] : 0;
		fromb(rb, r);
		break;
	}
	case OPX_rotqbi: {
		unsigned n = b[0] & 7;
		u128 v = toq(a);

		fromq(n ? (v << n) | (v >> (128 - n)) : v, r);
		break;
	}
	case OPX_shlqbi:
		fromq(toq(a) << (b[0] & 7), r);
		break;
	case OPX_rotqmbi:
		fromq(toq(a) >> ((0u - b[0]) & 7), r);
		break;
	case OPX_shufb:
		for (i = 0; i < 16; i++) {
			uint8_t k = cb[i];

			if ((k & 0xC0) == 0x80) rb[i] = 0x00;
			else if ((k & 0xE0) == 0xC0) rb[i] = 0xFF;
			else if ((k & 0xE0) == 0xE0) rb[i] = 0x80;
			else rb[i] = (k & 0x1F) < 16 ? ab[k & 0x1F] : bb[(k & 0x1F) - 16];
		}
		fromb(rb, r);
		break;
	/* single precision */
	case OPX_fa: case OPX_fa_e:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(a[i], 0x3F800000u, b[i], &r[i])) *skip |= 1u << i;
		break;
	case OPX_fs: case OPX_fs_e:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(a[i], 0x3F800000u, flip(b[i]), &r[i])) *skip |= 1u << i;
		break;
	case OPX_fm: case OPX_fm_e:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(a[i], b[i], 0, &r[i])) *skip |= 1u << i;
		break;
	case OPX_fma:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(a[i], b[i], c[i], &r[i])) *skip |= 1u << i;
		break;
	case OPX_fms:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(a[i], b[i], flip(c[i]), &r[i])) *skip |= 1u << i;
		break;
	case OPX_fnms:
		for (i = 0; i < 4; i++) if (!spt_sp_fma(flip(a[i]), b[i], c[i], &r[i])) *skip |= 1u << i;
		break;
	case OPX_fceq: for (i = 0; i < 4; i++) r[i] = f_of(a[i]) == f_of(b[i]) ? ~0u : 0; break;
	case OPX_fcgt: for (i = 0; i < 4; i++) r[i] = f_of(a[i]) > f_of(b[i]) ? ~0u : 0; break;
	case OPX_fcmeq: for (i = 0; i < 4; i++) r[i] = fabsf(f_of(a[i])) == fabsf(f_of(b[i])) ? ~0u : 0; break;
	case OPX_fcmgt: for (i = 0; i < 4; i++) r[i] = fabsf(f_of(a[i])) > fabsf(f_of(b[i])) ? ~0u : 0; break;
	case OPX_cflts0: case OPX_cflts8: {
		double k = op == OPX_cflts0 ? 1.0 : 256.0;

		for (i = 0; i < 4; i++) {
			double v = trunc((double)f_of(a[i]) * k);

			r[i] = v >= 2147483647.0 ? 0x7FFFFFFFu : v <= -2147483648.0 ? 0x80000000u : (uint32_t)(int32_t)v;
		}
		break;
	}
	case OPX_csflt: case OPX_frest: case OPX_frsqest: case OPX_fdiv:
		break;
	/* double precision */
#define D2(expr) for (i = 0; i < 2; i++) { double x = d_of(a, i), y = d_of(b, i), z = d_of(c, i); (void)x; (void)y; (void)z; setd(r, i, (expr)); } break
	case OPX_dfa: case OPX_dfa_d: D2(x + y);
	case OPX_dfs: case OPX_dfs_d: D2(x - y);
	case OPX_dfm: case OPX_dfm_d: D2(x * y);
	case OPX_dfma: case OPX_dfma_d: D2(fma(x, y, z));
	case OPX_dfms: D2(fma(x, y, -z));
	case OPX_dfnms: D2(fma(-x, y, z));
	case OPX_dfnma: D2(-fma(x, y, z));
	case OPX_dfceq: for (i = 0; i < 2; i++) setmask64(r, i, d_of(a, i) == d_of(b, i)); break;
	case OPX_dfcgt: for (i = 0; i < 2; i++) setmask64(r, i, d_of(a, i) > d_of(b, i)); break;
	case OPX_dfcmeq: for (i = 0; i < 2; i++) setmask64(r, i, fabs(d_of(a, i)) == fabs(d_of(b, i))); break;
	case OPX_dfcmgt: for (i = 0; i < 2; i++) setmask64(r, i, fabs(d_of(a, i)) > fabs(d_of(b, i))); break;
	case OPX_fesd:
		setd(r, 0, (double)f_of(a[0]));
		setd(r, 1, (double)f_of(a[2]));
		break;
	case OPX_frds: {
		float f0 = (float)d_of(a, 0), f1 = (float)d_of(a, 1);

		memcpy(&r[0], &f0, 4);
		memcpy(&r[2], &f1, 4);
		break;
	}
	default:
		break;
	}
}

static int is_zero_sp(uint32_t w) { return (w & 0x7FFFFFFFu) == 0; }
static int is_zero_dp(uint32_t hi, uint32_t lo) { return ((hi & 0x7FFFFFFFu) | lo) == 0; }

int spt_cmp(int cmp, const uint32_t got[4], const uint32_t want[4], unsigned skip)
{
	int i;

	switch (cmp) {
	case CMP_NONE:
		return 1;
	case CMP_SPF:
		for (i = 0; i < 4; i++)
			if (!(skip >> i & 1) && got[i] != want[i] && !(is_zero_sp(got[i]) && is_zero_sp(want[i])))
				return 0;
		return 1;
	case CMP_SPF02:
		for (i = 0; i < 4; i += 2)
			if (got[i] != want[i] && !(is_zero_sp(got[i]) && is_zero_sp(want[i])))
				return 0;
		return 1;
	case CMP_DPF:
		for (i = 0; i < 4; i += 2)
			if ((got[i] != want[i] || got[i + 1] != want[i + 1]) &&
			    !(is_zero_dp(got[i], got[i + 1]) && is_zero_dp(want[i], want[i + 1])))
				return 0;
		return 1;
	default:
		return got[0] == want[0] && got[1] == want[1] && got[2] == want[2] && got[3] == want[3];
	}
}

uint32_t spt_ref_branch(uint32_t seed) { return spt_branch_kernel(seed, 2000); }

uint32_t spt_ref_mem(uint32_t seed)
{
	uint8_t buf[1024] __attribute__((aligned(16)));	/* (the jobs verify in parallel: no static) */

	return spt_mem_kernel(buf, seed);
}
