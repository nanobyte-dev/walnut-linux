/* speq-skin: tests and benchmarks libspeq's vertex skinning (speq_skin_*_v1).
 *   speq-skin [--path spu|ppe|auto] --check
 *   speq-skin [--path spu|ppe|auto] --bench
 * --check: random meshes through the API on the chosen path (as SPEQ_SKIN, which it overrides;
 * default spu) against a scalar reference: Sauerbraten's own skinning loop (skelmodel.h
 * interpverts with geom.h's dualquat, quat and squat, written out here in C) for its two vertex
 * formats (normals; tangent frames into shorts) with its palette of bones and blended combos,
 * and a general reference for the rest (3x4 matrices, 4 weighted bones per vertex, uint8/uint16
 * indices, float tangents, no template, a second stream). Several meshes share each batch's
 * buffer at every 4-byte phase (neighbouring jobs share 16-byte lines). Floats within 2e-5
 * relative, shorts within 1 (or the same rotation, -q, when w is 0), every other byte equal to
 * the template's. On a fake or host build the SPU path must equal the PPE path byte for byte
 * (same code, same compiler). Then: refusals (formats, alignment, sizes), an index past the
 * palette, a job forced to fault, timeouts (late batches left to their jobs, the buffers
 * reused), the back-off, the auto mode's minimum, two threads at once.
 * --bench: Sauerbraten-sized batches (its hud gun: hands 1497 + grenade launcher 1683
 * vertices with tangent frames; a player: 2451 vertices with normals): us per frame for
 * Sauerbraten's loop (the engine's own C path, compiled here), libspeq's PPE path and the SPEs
 * (hot, and after the workers parked); without --path both.
 * libspeq's code is compiled in (its internals: the mode, the statistics, the fault). */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "speq-int.h"
#include "skin-job.h"

#define CHECK_TIMEOUT_US	20000000

static int failures, verbose;
static unsigned rng = 20261017;

static void check(int ok, const char *what)
{
	if (!ok || verbose)
		printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

static void *xalign(size_t align, size_t n)
{
	void *p = NULL;

	if (posix_memalign(&p, align, n ? n : 1)) {
		fprintf(stderr, "speq-skin: out of memory\n");
		exit(1);
	}
	memset(p, 0, n);
	return p;
}

static unsigned rnd(unsigned n)
{
	rng = rng * 1103515245 + 12345;
	return n ? (rng >> 8) % n : 0;
}

static float frnd(float a, float b)
{
	return a + (b - a) * (rnd(1u << 20) / (float)(1u << 20));
}

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* --- Sauerbraten's math (shared/geom.h, engine/animmodel.h), scalar --- */

typedef struct { float x, y, z; } rvec;
typedef struct { float x, y, z, w; } rquat;
typedef struct { rquat real, dual; } rdq;

static rvec vadd(rvec a, rvec b) { return (rvec){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static rvec vsub(rvec a, rvec b) { return (rvec){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static rvec vmul(rvec a, float k) { return (rvec){ a.x * k, a.y * k, a.z * k }; }
static rvec qv(rquat q) { return (rvec){ q.x, q.y, q.z }; }
static rvec vcross(rvec a, rvec b)
{
	return (rvec){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

/* dualquat::transform(vec) */
static rvec ref_dq_pos(const rdq *d, rvec v)
{
	rvec r = qv(d->real), du = qv(d->dual);
	rvec c1 = vadd(vadd(vcross(r, v), vmul(v, d->real.w)), du);

	return vadd(vmul(vsub(vadd(vcross(r, c1), vmul(du, d->real.w)), vmul(r, d->dual.w)), 2), v);
}

/* quat::rotate */
static rvec ref_rot(rquat q, rvec v)
{
	rvec r = qv(q);

	return vadd(vmul(vcross(r, vadd(vcross(r, v), vmul(v, q.w))), 2), v);
}

/* quat::mul(p, o) */
static rquat ref_qmul(rquat p, rquat o)
{
	return (rquat){ p.w * o.x + p.x * o.w + p.y * o.z - p.z * o.y,
			p.w * o.y - p.x * o.z + p.y * o.w + p.z * o.x,
			p.w * o.z + p.x * o.y - p.y * o.x + p.z * o.w,
			p.w * o.w - p.x * o.x - p.y * o.y - p.z * o.z };
}

/* animmodel's fixqtangent */
static rquat ref_fixq(rquat q, float bt)
{
	static const float bias = -1.5f / 65535;
	float biasscale = sqrtf(1 - bias * bias);

	if (bt < 0) {
		if (q.w >= 0)
			q = (rquat){ -q.x, -q.y, -q.z, -q.w };
		if (q.w > bias) {
			q.x *= biasscale;
			q.y *= biasscale;
			q.z *= biasscale;
			q.w = bias;
		}
	} else if (q.w < 0) {
		q = (rquat){ -q.x, -q.y, -q.z, -q.w };
	}
	return q;
}

/* squat::convert */
static void ref_squat(int16_t *s, rquat q)
{
	s[0] = (int16_t)(q.x * 32767.5f - 0.5f);
	s[1] = (int16_t)(q.y * 32767.5f - 0.5f);
	s[2] = (int16_t)(q.z * 32767.5f - 0.5f);
	s[3] = (int16_t)(q.w * 32767.5f - 0.5f);
}

static float qdot(rquat a, rquat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static rquat qscale(rquat a, float k) { return (rquat){ a.x * k, a.y * k, a.z * k, a.w * k }; }
static rquat qadd(rquat a, rquat b) { return (rquat){ a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }

/* skelmeshgroup::blendbones (dualquat::mul, accumulate, normalize): n bones, weights > 0 */
static rdq ref_blend(const rdq *const *b, const float *w, int n)
{
	rdq d = { qscale(b[0]->real, w[0]), qscale(b[0]->dual, w[0]) };
	float invlen;
	int k;

	for (k = 1; k < n; k++) {
		float kk = w[k];

		if (kk == 0)
			continue;
		if (qdot(d.real, b[k]->real) < 0)
			kk = -kk;
		d.real = qadd(d.real, qscale(b[k]->real, kk));
		d.dual = qadd(d.dual, qscale(b[k]->dual, kk));
	}
	invlen = 1 / sqrtf(qdot(d.real, d.real));
	d.real = qscale(d.real, invlen);
	d.dual = qscale(d.dual, invlen);
	return d;
}

/* dualquat(quat, vec) */
static rdq ref_dq_make(rquat q, rvec p)
{
	return (rdq){ q, { 0.5f * (p.x * q.w + p.y * q.z - p.z * q.y),
			   0.5f * (-p.x * q.z + p.y * q.w + p.z * q.x),
			   0.5f * (p.x * q.y - p.y * q.x + p.z * q.w),
			   -0.5f * (p.x * q.x + p.y * q.y + p.z * q.z) } };
}

static rquat rand_quat(void)
{
	rquat q;
	float l;

	do {
		q = (rquat){ frnd(-1, 1), frnd(-1, 1), frnd(-1, 1), frnd(-1, 1) };
		l = qdot(q, q);
	} while (l < 0.01f || l > 1);
	return qscale(q, 1 / sqrtf(l));
}

static rvec rand_unit(void)
{
	rquat q = rand_quat();
	float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z);

	return l > 1e-3f ? vmul(qv(q), 1 / l) : (rvec){ 0, 0, 1 };
}

/* --- Sauerbraten's vertex formats (skelmodel.h) and its interpverts --- */

struct c2vert { float pos[3], norm[3], tc[2]; int32_t blend, interpindex; };
struct c2bump { float tangent[4]; };
struct c2vvertn { float pos[3], tc[2], norm[3]; };
struct c2vvertbump { float pos[3], tc[2]; int16_t tangent[4]; };

static rvec v3(const float *f) { return (rvec){ f[0], f[1], f[2] }; }
static void put3(float *f, rvec v) { f[0] = v.x; f[1] = v.y; f[2] = v.z; }

/* skelmesh::interpverts: bdata1's bones below blendoffset, bdata2's blends from it on */
static void c2_interp_n(const struct c2vert *src, int n, const rdq *bdata1, const rdq *bdata2,
			int blendoffset, struct c2vvertn *dst)
{
	int i;

	for (i = 0; i < n; i++) {
		int x = src[i].interpindex;
		const rdq *b = x < blendoffset ? &bdata1[x] : &bdata2[x - blendoffset];

		put3(dst[i].pos, ref_dq_pos(b, v3(src[i].pos)));
		put3(dst[i].norm, ref_rot(b->real, v3(src[i].norm)));
	}
}

static void c2_interp_bump(const struct c2vert *src, const struct c2bump *bsrc, int n,
			   const rdq *bdata1, const rdq *bdata2, int blendoffset,
			   struct c2vvertbump *dst)
{
	int i;

	for (i = 0; i < n; i++) {
		int x = src[i].interpindex;
		const rdq *b = x < blendoffset ? &bdata1[x] : &bdata2[x - blendoffset];
		rquat t = { bsrc[i].tangent[0], bsrc[i].tangent[1], bsrc[i].tangent[2], bsrc[i].tangent[3] };

		put3(dst[i].pos, ref_dq_pos(b, v3(src[i].pos)));
		ref_squat(dst[i].tangent, ref_fixq(ref_qmul(b->real, t), t.w));
	}
}

/* A Sauerbraten skeleton: nb bones, nblend blended combos of 2-4 of them (cube2 normalizes
 * them, in its CPU path), each palette 16-aligned */
struct c2skel {
	rdq *bones, *blends;
	int nb, nblend;
};

static void c2_skel(struct c2skel *k, int nb, int nblend)
{
	int i, j;

	k->nb = nb;
	k->nblend = nblend;
	k->bones = xalign(16, nb * sizeof(rdq));
	k->blends = xalign(16, (nblend ? nblend : 1) * sizeof(rdq));
	for (i = 0; i < nb; i++)
		k->bones[i] = ref_dq_make(rand_quat(), (rvec){ frnd(-50, 50), frnd(-50, 50), frnd(-50, 50) });
	for (i = 0; i < nblend; i++) {
		const rdq *b[4];
		float w[4], t = 0;
		int nw = 2 + rnd(3);

		for (j = 0; j < nw; j++) {
			b[j] = &k->bones[rnd(nb)];
			w[j] = frnd(0.05f, 1);
			t += w[j];
		}
		for (j = 0; j < nw; j++)
			w[j] /= t;
		k->blends[i] = ref_blend(b, w, nw);
	}
}

static void c2_skel_free(struct c2skel *k)
{
	free(k->bones);
	free(k->blends);
}

/* nverts vertices of a mesh: random positions, normals, texture coordinates, palette indices;
 * tangent frames as calctangent leaves them (fixqtangent'd unit quaternions, either sign) */
static void c2_mesh(struct c2vert *v, struct c2bump *bv, int n, int npal)
{
	int i;

	for (i = 0; i < n; i++) {
		rvec p = { frnd(-40, 40), frnd(-40, 40), frnd(-40, 40) }, nn = rand_unit();

		put3(v[i].pos, p);
		put3(v[i].norm, nn);
		v[i].tc[0] = frnd(0, 1);
		v[i].tc[1] = frnd(0, 1);
		v[i].blend = rnd(1000);
		v[i].interpindex = rnd(npal);
		if (bv) {
			rquat q = ref_fixq(rand_quat(), rnd(2) ? 1.0f : -1.0f);

			if (!rnd(50))	/* a frame right at w = 0 */
				q = ref_fixq((rquat){ q.x, q.y, q.z, 0 }, -1.0f);
			bv[i].tangent[0] = q.x;
			bv[i].tangent[1] = q.y;
			bv[i].tangent[2] = q.z;
			bv[i].tangent[3] = q.w;
		}
	}
}

static void c2_meshdesc(struct speq_skin_mesh *m, const struct c2skel *k, const struct c2vert *v,
			const struct c2bump *bv, int n, const void *tmpl, uint32_t dst_offset)
{
	memset(m, 0, sizeof(*m));
	m->nverts = n;
	m->palette_fmt = SPEQ_SKIN_DQ;
	m->influence = SPEQ_SKIN_IDX_S32;
	m->palette[0] = k->bones;
	m->npal[0] = k->nb;
	m->palette[1] = k->nblend ? k->blends : NULL;
	m->npal[1] = k->nblend;
	m->src[0] = v;
	m->src_stride[0] = sizeof(struct c2vert);
	m->pos = (struct speq_skin_field){ offsetof(struct c2vert, pos), 0, 0 };
	m->index = (struct speq_skin_field){ offsetof(struct c2vert, interpindex), 0, 0 };
	m->weights.off = SPEQ_SKIN_NONE;
	m->tmpl = tmpl;
	m->dst_offset = dst_offset;
	m->dst_pos = 0;
	if (bv) {
		m->src[1] = bv;
		m->src_stride[1] = sizeof(struct c2bump);
		m->norm.off = SPEQ_SKIN_NONE;
		m->tan = (struct speq_skin_field){ 0, 1, 0 };
		m->tan_in = SPEQ_SKIN_TAN_QUAT;
		m->tan_out = SPEQ_SKIN_OUT_S16;
		m->dst_stride = sizeof(struct c2vvertbump);
		m->dst_norm = SPEQ_SKIN_NONE;
		m->dst_tan = offsetof(struct c2vvertbump, tangent);
	} else {
		m->norm = (struct speq_skin_field){ offsetof(struct c2vert, norm), 0, 0 };
		m->tan.off = SPEQ_SKIN_NONE;
		m->dst_stride = sizeof(struct c2vvertn);
		m->dst_norm = offsetof(struct c2vvertn, norm);
		m->dst_tan = SPEQ_SKIN_NONE;
	}
}

/* --- the general reference, from the mesh description --- */

static float ldf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static rvec ldv(const uint8_t *p) { return (rvec){ ldf(p), ldf(p + 4), ldf(p + 8) }; }
static rquat ldq(const uint8_t *p) { return (rquat){ ldf(p), ldf(p + 4), ldf(p + 8), ldf(p + 12) }; }
static void stf(uint8_t *p, float f) { memcpy(p, &f, 4); }
static void stv(uint8_t *p, rvec v) { stf(p, v.x); stf(p + 4, v.y); stf(p + 8, v.z); }

static const float *pal_entry(const struct speq_skin_mesh *m, uint32_t i)
{
	unsigned e = m->palette_fmt == SPEQ_SKIN_DQ ? 8 : 12;

	return i < m->npal[0] ? (const float *)m->palette[0] + i * e :
	       (const float *)m->palette[1] + (i - m->npal[0]) * e;
}

static rvec mat_mul(const float *r, rvec v, float w)
{
	return (rvec){ r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3] * w,
		       r[4] * v.x + r[5] * v.y + r[6] * v.z + r[7] * w,
		       r[8] * v.x + r[9] * v.y + r[10] * v.z + r[11] * w };
}

/* The mesh into out (its vertex 0), the template copied first; -1 for an index past the palette */
static int ref_mesh(const struct speq_skin_mesh *m, uint8_t *out)
{
	uint32_t npal = m->npal[0] + m->npal[1], i;
	const uint8_t *s[2] = { m->src[0], m->src[1] };

	if (m->tmpl)
		memcpy(out, m->tmpl, (size_t)m->nverts * m->dst_stride);
	else
		memset(out, 0, (size_t)m->nverts * m->dst_stride);
	for (i = 0; i < m->nverts; i++) {
#define FP(f)	(s[(f).stream] + (size_t)i * m->src_stride[(f).stream] + (f).off)
		uint8_t *o = out + (size_t)i * m->dst_stride;
		rdq dq = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
		float mat[12] = { 0 };
		int k;

		if (m->influence == SPEQ_SKIN_W4) {
			const uint8_t *bb = FP(m->index), *ww = FP(m->weights);

			for (k = 0; k < 4; k++)
				if (bb[k] >= npal)
					return -1;
			if (m->palette_fmt == SPEQ_SKIN_DQ) {
				const rdq *b[4];
				float w[4];

				for (k = 0; k < 4; k++) {
					b[k] = (const rdq *)pal_entry(m, bb[k]);
					w[k] = ww[k] * (1 / 255.0f);
				}
				dq = ref_blend(b, w, 4);
			} else {
				for (k = 0; k < 12 * 4; k++)
					mat[k % 12] += pal_entry(m, bb[k / 12])[k % 12] * (ww[k / 12] * (1 / 255.0f));
			}
		} else {
			const uint8_t *p = FP(m->index);
			uint32_t x;

			if (m->influence == SPEQ_SKIN_IDX_S32)
				memcpy(&x, p, 4);
			else if (m->influence == SPEQ_SKIN_IDX_U16) {
				uint16_t h;

				memcpy(&h, p, 2);
				x = h;
			} else {
				x = *p;
			}
			if (x >= npal)
				return -1;
			if (m->palette_fmt == SPEQ_SKIN_DQ)
				memcpy(&dq, pal_entry(m, x), sizeof(dq));
			else
				memcpy(mat, pal_entry(m, x), sizeof(mat));
		}
		if (m->palette_fmt == SPEQ_SKIN_DQ)
			stv(o + m->dst_pos, ref_dq_pos(&dq, ldv(FP(m->pos))));
		else
			stv(o + m->dst_pos, mat_mul(mat, ldv(FP(m->pos)), 1));
		if (m->norm.off != SPEQ_SKIN_NONE && m->dst_norm != SPEQ_SKIN_NONE) {
			rvec n = ldv(FP(m->norm));

			stv(o + m->dst_norm, m->palette_fmt == SPEQ_SKIN_DQ ? ref_rot(dq.real, n) :
					     mat_mul(mat, n, 0));
		}
		if (m->tan_in) {
			rquat t = ldq(FP(m->tan)), q;

			if (m->tan_in == SPEQ_SKIN_TAN_QUAT) {
				q = ref_fixq(ref_qmul(dq.real, t), t.w);
			} else {
				rvec r = m->palette_fmt == SPEQ_SKIN_DQ ? ref_rot(dq.real, qv(t)) :
					 mat_mul(mat, qv(t), 0);

				q = (rquat){ r.x, r.y, r.z, t.w };
			}
			if (m->tan_out == SPEQ_SKIN_OUT_S16) {
				int16_t sq[4];

				ref_squat(sq, q);
				memcpy(o + m->dst_tan, sq, 8);
			} else {
				stv(o + m->dst_tan, qv(q));
				stf(o + m->dst_tan + 12, q.w);
			}
		}
#undef FP
	}
	return 0;
}

/* --- comparing a mesh's output with the reference --- */

struct cmpstat {
	double maxrel;
	int maxs16;
	unsigned long bytes_bad, nan;
};

static void cmp_floats(const uint8_t *a, const uint8_t *r, int n, struct cmpstat *c)
{
	int k;

	for (k = 0; k < n; k++) {
		float x = ldf(a + 4 * k), y = ldf(r + 4 * k);
		double e;

		if (isnan(x) || isinf(x)) {
			if (!(isnan(y) || isinf(y)))
				c->nan++;
			continue;
		}
		e = fabs((double)x - y) / (1 + fabs((double)y));
		if (e > c->maxrel)
			c->maxrel = e;
	}
}

static void cmp_s16(const uint8_t *a, const uint8_t *r, struct cmpstat *c)
{
	int16_t x[4], y[4];
	int k, d = 0, dn = 0;

	memcpy(x, a, 8);
	memcpy(y, r, 8);
	for (k = 0; k < 4; k++) {
		if (abs(x[k] - y[k]) > d)
			d = abs(x[k] - y[k]);
		if (abs(x[k] + y[k] + 1) > dn)	/* -q: short(-v*k - .5) = -short(v*k - .5) - 1 */
			dn = abs(x[k] + y[k] + 1);
	}
	if (abs(y[3]) <= 2 && dn < d)	/* w at 0: q and -q are the same frame */
		d = dn;
	if (d > c->maxs16)
		c->maxs16 = d;
}

/* got and ref: the mesh's n vertices; the fields within tolerance, the other bytes equal */
static void cmp_mesh(const struct speq_skin_mesh *m, const uint8_t *got, const uint8_t *ref,
		     struct cmpstat *c)
{
	uint8_t map[SKIN_STRIDE_MAX] = { 0 };
	uint32_t i, b;
	int hasn = m->norm.off != SPEQ_SKIN_NONE && m->dst_norm != SPEQ_SKIN_NONE;

	memset(map + m->dst_pos, 1, 12);
	if (hasn)
		memset(map + m->dst_norm, 1, 12);
	if (m->tan_in)
		memset(map + m->dst_tan, 1, m->tan_out == SPEQ_SKIN_OUT_S16 ? 8 : 16);
	for (i = 0; i < m->nverts; i++) {
		const uint8_t *a = got + (size_t)i * m->dst_stride, *r = ref + (size_t)i * m->dst_stride;

		for (b = 0; b < m->dst_stride; b++)
			if (!map[b] && a[b] != r[b])
				c->bytes_bad++;
		cmp_floats(a + m->dst_pos, r + m->dst_pos, 3, c);
		if (hasn)
			cmp_floats(a + m->dst_norm, r + m->dst_norm, 3, c);
		if (m->tan_in) {
			if (m->tan_out == SPEQ_SKIN_OUT_S16)
				cmp_s16(a + m->dst_tan, r + m->dst_tan, c);
			else
				cmp_floats(a + m->dst_tan, r + m->dst_tan, 4, c);
		}
	}
}

static int cmp_ok(const struct cmpstat *c)
{
	return c->maxrel <= 2e-5 && c->maxs16 <= 1 && !c->bytes_bad && !c->nan;
}

/* --- the cases --- */

static const char *path_name[] = { "off", "ppe", "spu", "auto" };
static int path = SPQ_SKIN_SPU, path_given;
static struct speq_skin *sk, *sk_ppe;

static int open_skin(struct speq_skin **s, int m)
{
	int ret;

	spq_skin_tune(m, -1, -1, -1);
	ret = speq_skin_open_v1(s, SPEQ_SKIN_ABI, sizeof(struct speq_skin_mesh));
	spq_skin_tune(path, -1, -1, -1);
	return ret;
}

/* Runs a batch of meshes on s; out_bytes from the meshes. Returns end's result, *out */
static int run_batch(struct speq_skin *s, const struct speq_skin_mesh *m, int nm, uint32_t bytes,
		     void **out)
{
	uint32_t nv = 0;
	int i, ret;

	for (i = 0; i < nm; i++)
		nv += m[i].nverts;
	ret = speq_skin_begin_v1(s, bytes, nv);
	if (ret)
		return ret;
	for (i = 0; i < nm; i++)
		if ((ret = speq_skin_add_v1(s, &m[i])))
			break;
	return speq_skin_end_v1(s, out) ?: ret;
}

/* Sauerbraten mesh groups: nm meshes of either format, packed in one buffer (vdata) as genvbo
 * lays them out; the template is vdata with the texture coordinates fillverts wrote */
static void case_c2(int bump, int nm, const int *nverts, int nb, int nblend, const char *what)
{
	struct c2skel k;
	struct speq_skin_mesh m[8];
	struct c2vert *v[8];
	struct c2bump *bv[8];
	uint32_t stride = bump ? sizeof(struct c2vvertbump) : sizeof(struct c2vvertn), off = 0;
	uint8_t *vdata, *ref, *got2;
	struct cmpstat c = { 0 };
	char line[200];
	void *out = NULL, *out2 = NULL;
	int i, ret, total = 0;

	for (i = 0; i < nm; i++)
		total += nverts[i];
	c2_skel(&k, nb, nblend);
	vdata = xalign(16, (size_t)total * stride + 16);
	ref = xalign(16, (size_t)total * stride + 16);
	for (i = 0; i < nm; i++) {
		int j;

		v[i] = xalign(16, nverts[i] * sizeof(struct c2vert));
		bv[i] = bump ? xalign(16, nverts[i] * sizeof(struct c2bump)) : NULL;
		c2_mesh(v[i], bv[i], nverts[i], nb + nblend);
		for (j = 0; j < nverts[i]; j++)	/* fillverts: the texture coordinates */
			memcpy(vdata + (size_t)(off + j) * stride + 12, v[i][j].tc, 8);
		c2_meshdesc(&m[i], &k, v[i], bv[i], nverts[i], vdata + (size_t)off * stride, off * stride);
		off += nverts[i];
	}
	memcpy(ref, vdata, (size_t)total * stride);
	for (i = 0, off = 0; i < nm; i++) {
		if (bump)
			c2_interp_bump(v[i], bv[i], nverts[i], k.bones, k.blends, nb,
				       (struct c2vvertbump *)ref + off);
		else
			c2_interp_n(v[i], nverts[i], k.bones, k.blends, nb, (struct c2vvertn *)ref + off);
		off += nverts[i];
	}
	ret = run_batch(sk, m, nm, total * stride, &out);
	if (path == SPQ_SKIN_AUTO && total < 256) {
		snprintf(line, sizeof(line), "auto %s: %s (%d vertices): declined", bump ? "c2 bump" :
			 "c2 norm", what, total);
		check(ret == -ENOTSUP, line);
		goto done;
	}
	if (!ret) {
		for (i = 0, off = 0; i < nm; i++) {
			cmp_mesh(&m[i], (uint8_t *)out + (size_t)off * stride, ref + (size_t)off * stride, &c);
			off += nverts[i];
		}
	}
	snprintf(line, sizeof(line), "%s %s: %s (%d vertices, %d bones + %d blends): max rel %.2g, "
		 "short %d, bad bytes %lu (%d)", path_name[path], bump ? "c2 bump" : "c2 norm", what, total,
		 nb, nblend, c.maxrel, c.maxs16, c.bytes_bad, ret);
	check(!ret && cmp_ok(&c), line);
	if (!ret && sk_ppe && path != SPQ_SKIN_PPE) {
		got2 = xalign(16, (size_t)total * stride);
		memcpy(got2, out, (size_t)total * stride);
		ret = run_batch(sk_ppe, m, nm, total * stride, &out2);
#ifdef SPEQ_FAKE
		snprintf(line, sizeof(line), "%s: the SPU path = the PPE path, byte for byte", what);
		check(!ret && !memcmp(got2, out2, (size_t)total * stride), line);
#else
		(void)ret;
#endif
		free(got2);
	}
done:
	for (i = 0; i < nm; i++) {
		free(v[i]);
		free(bv[i]);
	}
	free(vdata);
	free(ref);
	c2_skel_free(&k);
}

/* General meshes: a stream of stride s0 with pos, normal, tangent (vec4) and the influence, a
 * second stream with the weights (W4); the output at stride ds with fields at random 4-byte
 * offsets; template or zeros; meshes packed at every 4-byte phase */
static void case_general(int pf, int inf, int tin, int tout, int hasn, int tmpl, const char *what)
{
	enum { NM = 5 };
	struct speq_skin_mesh m[NM];
	uint8_t *src[NM], *src1[NM], *tm, *ref;
	float *pal;
	uint32_t npal = pf == SPEQ_SKIN_DQ ? 300 : 200, e = pf == SPEQ_SKIN_DQ ? 8 : 12;
	uint32_t s0 = 64 + 4 * rnd(8), ds = 52 + 4 * rnd(6), off = 0, bytes, i, j;
	uint32_t n[NM];
	struct cmpstat c = { 0 };
	char line[200];
	void *out = NULL, *out2 = NULL;
	int ret, k;

	if (inf == SPEQ_SKIN_IDX_U8)
		npal = 200;
	pal = xalign(16, npal * e * sizeof(float));
	for (i = 0; i < npal; i++) {
		if (pf == SPEQ_SKIN_DQ) {
			rdq d = ref_dq_make(rand_quat(), (rvec){ frnd(-50, 50), frnd(-50, 50), frnd(-50, 50) });

			memcpy(pal + i * e, &d, sizeof(d));
		} else {
			rquat q = rand_quat();
			float sc = frnd(0.5f, 2), *r = pal + i * e;
			/* rotation (quat to matrix) times a scale, plus a translation */
			float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z;
			float yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			r[0] = sc * (1 - 2 * (yy + zz)); r[1] = sc * 2 * (xy - wz); r[2] = sc * 2 * (xz + wy);
			r[4] = sc * 2 * (xy + wz); r[5] = sc * (1 - 2 * (xx + zz)); r[6] = sc * 2 * (yz - wx);
			r[8] = sc * 2 * (xz - wy); r[9] = sc * 2 * (yz + wx); r[10] = sc * (1 - 2 * (xx + yy));
			r[3] = frnd(-50, 50); r[7] = frnd(-50, 50); r[11] = frnd(-50, 50);
		}
	}
	for (k = 0; k < NM; k++)
		n[k] = k == 1 ? 1 + rnd(5) : 30 + rnd(900);
	for (k = 0; k < NM; k++)
		off += n[k] * ds + 4 * (1 + rnd(3));
	bytes = off;
	tm = xalign(16, bytes + 16);
	ref = xalign(16, bytes + 16);
	for (i = 0; i < bytes; i++)
		tm[i] = rnd(256);
	off = 4 * rnd(4);
	for (k = 0; k < NM; k++) {
		struct speq_skin_mesh *mm = &m[k];

		src[k] = xalign(16, n[k] * s0 + 16);
		src1[k] = xalign(16, n[k] * 8 + 16);
		memset(mm, 0, sizeof(*mm));
		mm->nverts = n[k];
		mm->palette_fmt = pf;
		mm->influence = inf;
		mm->tan_in = tin;
		mm->tan_out = tout;
		/* the palette in two parts, split anywhere */
		j = rnd(npal);
		mm->palette[0] = pal;
		mm->npal[0] = j;
		mm->palette[1] = pal + j * e;
		mm->npal[1] = npal - j;
		mm->src[0] = src[k];
		mm->src_stride[0] = s0;
		mm->src[1] = src1[k];
		mm->src_stride[1] = 8;
		mm->pos = (struct speq_skin_field){ 0, 0, 0 };
		mm->norm = (struct speq_skin_field){ hasn ? 12 : SPEQ_SKIN_NONE, 0, 0 };
		mm->tan = (struct speq_skin_field){ tin ? 24 : SPEQ_SKIN_NONE, 0, 0 };
		mm->index = (struct speq_skin_field){ inf == SPEQ_SKIN_W4 ? 0 : 40, inf == SPEQ_SKIN_W4, 0 };
		mm->weights = (struct speq_skin_field){ inf == SPEQ_SKIN_W4 ? 4 : SPEQ_SKIN_NONE, 1, 0 };
		mm->dst_offset = off;
		mm->dst_stride = ds;
		mm->dst_pos = 4 * rnd(2);
		mm->dst_norm = hasn ? 16 : SPEQ_SKIN_NONE;
		mm->dst_tan = tin ? 32 : SPEQ_SKIN_NONE;
		mm->tmpl = tmpl ? tm + off : NULL;
		for (i = 0; i < n[k]; i++) {
			uint8_t *p = src[k] + i * s0, *q = src1[k] + i * 8;
			rvec pos = { frnd(-40, 40), frnd(-40, 40), frnd(-40, 40) };
			rquat t = rand_quat();

			for (j = 0; j < s0; j++)
				p[j] = rnd(256);
			stv(p, pos);
			stv(p + 12, rand_unit());
			stv(p + 24, rand_unit());
			stf(p + 36, rnd(2) ? 1.0f : -1.0f);
			if (tin == SPEQ_SKIN_TAN_QUAT) {
				stv(p + 24, qv(t));
				stf(p + 36, t.w);
			}
			if (inf == SPEQ_SKIN_IDX_S32) {
				uint32_t x = rnd(npal);

				memcpy(p + 40, &x, 4);
			} else if (inf == SPEQ_SKIN_IDX_U16) {
				uint16_t x = rnd(npal);

				memcpy(p + 40, &x, 2);
			} else if (inf == SPEQ_SKIN_IDX_U8) {
				p[40] = rnd(npal);
			} else {
				int w[4] = { 1 + rnd(255), rnd(3) ? rnd(200) : 0, rnd(2) ? rnd(100) : 0, 0 };

				w[3] = rnd(3) ? 0 : rnd(60);
				for (j = 0; j < 4; j++) {
					q[j] = rnd(npal);
					q[4 + j] = w[j];
				}
			}
		}
		ref_mesh(mm, ref + off);
		off += n[k] * ds + 4 * (k == 2 ? 0 : 1 + rnd(3));	/* mesh 3 right after mesh 2 */
	}
	bytes = off;
	ret = run_batch(sk, m, NM, bytes, &out);
	if (!ret)
		for (k = 0; k < NM; k++)
			cmp_mesh(&m[k], (uint8_t *)out + m[k].dst_offset, ref + m[k].dst_offset, &c);
	snprintf(line, sizeof(line), "%s general: %s: max rel %.2g, short %d, bad bytes %lu (%d)",
		 path_name[path], what, c.maxrel, c.maxs16, c.bytes_bad, ret);
	check(!ret && cmp_ok(&c), line);
	if (!ret && sk_ppe && path != SPQ_SKIN_PPE) {
		uint8_t *got2 = xalign(16, bytes);
		int same = 1;

		memcpy(got2, out, bytes);
		ret = run_batch(sk_ppe, m, NM, bytes, &out2);
		for (k = 0; k < NM && !ret; k++)
			same &= !memcmp(got2 + m[k].dst_offset, (uint8_t *)out2 + m[k].dst_offset,
					m[k].nverts * ds);
#ifdef SPEQ_FAKE
		snprintf(line, sizeof(line), "%s: the SPU path = the PPE path, byte for byte", what);
		check(!ret && same, line);
#else
		(void)same;
#endif
		free(got2);
	}
	for (k = 0; k < NM; k++) {
		free(src[k]);
		free(src1[k]);
	}
	free(tm);
	free(ref);
	free(pal);
}

/* A valid Sauerbraten-like mesh to break in the refusal cases */
struct fixture {
	struct c2skel k;
	struct c2vert *v;
	struct c2bump *bv;
	uint8_t *vdata;
	struct speq_skin_mesh m;
	int n;
};

static void fixture(struct fixture *f, int n)
{
	f->n = n;
	c2_skel(&f->k, 40, 60);
	f->v = xalign(16, n * sizeof(struct c2vert));
	f->bv = xalign(16, n * sizeof(struct c2bump));
	f->vdata = xalign(16, n * sizeof(struct c2vvertn) + 64);
	c2_mesh(f->v, f->bv, n, 100);
	c2_meshdesc(&f->m, &f->k, f->v, NULL, n, f->vdata, 0);
}

static void fixture_free(struct fixture *f)
{
	free(f->v);
	free(f->bv);
	free(f->vdata);
	c2_skel_free(&f->k);
}

static void refusal(struct fixture *f, void (*breakit)(struct speq_skin_mesh *), int want,
		    const char *what)
{
	struct speq_skin_mesh m = f->m;
	void *out;
	int ret;

	breakit(&m);
	ret = run_batch(sk, &m, 1, f->n * sizeof(struct c2vvertn) + 64, &out);
	check(ret == want, what);
}

static void b_fmt(struct speq_skin_mesh *m) { m->palette_fmt = 7; }
static void b_quatmat(struct speq_skin_mesh *m) { m->tan_in = SPEQ_SKIN_TAN_QUAT; m->palette_fmt = SPEQ_SKIN_MAT; }
static void b_palign(struct speq_skin_mesh *m) { m->palette[0] = (const uint8_t *)m->palette[0] + 4; }
static void b_pbig(struct speq_skin_mesh *m) { m->npal[0] = SKIN_DQ_MAX; }
static void b_stride(struct speq_skin_mesh *m) { m->src_stride[0] = 42; }
static void b_offset(struct speq_skin_mesh *m) { m->pos.off = 30; }
static void b_tphase(struct speq_skin_mesh *m) { m->tmpl = (const uint8_t *)m->tmpl + 4; }
static void b_dst(struct speq_skin_mesh *m) { m->dst_offset = 1000000; }
static void b_nopos(struct speq_skin_mesh *m) { m->dst_pos = SPEQ_SKIN_NONE; }
static void b_index(struct speq_skin_mesh *m) { ((struct c2vert *)m->src[0])[m->nverts - 1].interpindex = 100; }

static void case_errors(void)
{
	struct fixture f;
	struct speq_skin_mesh m[40];
	struct spq_skin_stats st0, st1;
	void *out;
	int ret, i;

	fixture(&f, 700);
	refusal(&f, b_fmt, -ENOTSUP, "unknown palette format: -ENOTSUP");
	refusal(&f, b_quatmat, -ENOTSUP, "tangent frames with matrices: -ENOTSUP");
	refusal(&f, b_palign, -EINVAL, "palette not 16-aligned: -EINVAL");
	refusal(&f, b_pbig, -ENOTSUP, "palette past local store: -ENOTSUP");
	refusal(&f, b_stride, -EINVAL, "stride not a multiple of 4: -EINVAL");
	refusal(&f, b_offset, -EINVAL, "a field past its stride: -EINVAL");
	refusal(&f, b_tphase, -EINVAL, "template at another 16-byte phase: -EINVAL");
	refusal(&f, b_dst, -EINVAL, "mesh past the batch's buffer: -EINVAL");
	refusal(&f, b_nopos, -EINVAL, "no position output: -EINVAL");
	refusal(&f, b_index, -EIO, "an index past the palette: -EIO");
	f.v[f.n - 1].interpindex = 0;
	for (i = 0; i < 40; i++)
		m[i] = f.m;
	ret = run_batch(sk, m, 40, f.n * sizeof(struct c2vvertn) + 64, &out);
	check(ret == -ENOSPC, "40 meshes in a batch: -ENOSPC");
	ret = speq_skin_begin_v1(sk, 0, 10);
	check(ret == -EINVAL, "an empty buffer: -EINVAL");
	ret = run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
	check(!ret, "a good batch after the refusals");

	if (path == SPQ_SKIN_SPU) {
		spq_skin_fault = 1;
		ret = run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
		check(ret == -EIO, "a faulting job: -EIO");
		ret = run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
		check(!ret, "a good batch after the fault (the context recreated)");

		/* timeouts: end polls; the batches stay their jobs' until done, others are used */
		spq_skin_stats(sk, &st0);
		spq_skin_tune(-1, 0, 0, -1);
		for (i = 0; i < 2; i++) {
			ret = run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
			check(ret == -ETIMEDOUT || ret == 0, "timeout 0: late (or already done)");
		}
		spq_skin_tune(-1, CHECK_TIMEOUT_US, 0, -1);
		usleep(20000);
		for (i = 0; i < 6; i++) {
			ret = run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
			if (ret)
				break;
		}
		check(!ret, "after the late batches: six good ones (their buffers came back)");
		spq_skin_stats(sk, &st1);
		check(st1.nofree == st0.nofree, "no batch went without a buffer");

		/* three failures in a row: the SPEs set aside, begin declines */
		for (i = 0; i < 3; i++) {
			spq_skin_fault = 1;
			run_batch(sk, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
		}
		ret = speq_skin_begin_v1(sk, 1000, 1000);
		check(ret == -ENOTSUP, "three failed batches: begin declines (back-off)");
		speq_skin_close_v1(sk);
		ret = open_skin(&sk, path);
		check(!ret, "a new speq_skin after the back-off");
	}
	if (path == SPQ_SKIN_SPU || path == SPQ_SKIN_AUTO) {
		struct speq_skin *a;

		ret = open_skin(&a, SPQ_SKIN_AUTO);
		check(!ret, "auto: opens");
		spq_skin_tune(SPQ_SKIN_AUTO, -1, -1, -1);	/* the mode is the process's */
		if (!ret) {
			ret = speq_skin_begin_v1(a, 1000, 100);
			check(ret == -ENOTSUP, "auto: 100 vertices declined (SPEQ_SKIN_MIN 256)");
			ret = run_batch(a, &f.m, 1, f.n * sizeof(struct c2vvertn) + 64, &out);
			check(!ret, "auto: 700 vertices on the SPEs");
			speq_skin_close_v1(a);
		}
		spq_skin_tune(path, -1, -1, -1);
	}
	fixture_free(&f);
}

struct thr {
	pthread_t th;
	int ok;
	unsigned seed;
};

static void *thread_main(void *arg)
{
	struct thr *t = arg;
	struct speq_skin *s;
	struct fixture f;
	uint8_t *ref;
	int i;

	t->ok = 0;
	if (speq_skin_open_v1(&s, SPEQ_SKIN_ABI, sizeof(struct speq_skin_mesh)))
		return NULL;
	fixture(&f, 1500);	/* rnd() isn't thread-safe: same data either way is fine */
	ref = xalign(16, f.n * sizeof(struct c2vvertn));
	memcpy(ref, f.vdata, f.n * sizeof(struct c2vvertn));
	c2_interp_n(f.v, f.n, f.k.bones, f.k.blends, f.k.nb, (struct c2vvertn *)ref);
	t->ok = 1;
	for (i = 0; i < 200 && t->ok; i++) {
		struct cmpstat c = { 0 };
		void *out;

		if (run_batch(s, &f.m, 1, f.n * sizeof(struct c2vvertn), &out)) {
			t->ok = 0;
			break;
		}
		cmp_mesh(&f.m, out, ref, &c);
		t->ok = cmp_ok(&c);
	}
	free(ref);
	fixture_free(&f);
	speq_skin_close_v1(s);
	return NULL;
}

static int run_check(void)
{
	static const int one[] = { 1683 }, two[] = { 1497, 951 }, small[] = { 1, 3, 2, 17 };
	static const int big[] = { 9000 };
	struct thr th[2];
	int ret, i;

	/* correctness first: a slow host (qemu-user in the package build) must not time out */
	spq_skin_tune(-1, CHECK_TIMEOUT_US, 0, -1);
	ret = open_skin(&sk, path);
	if (ret) {
		printf("FAIL speq_skin_open_v1 (%s): %s\n", path_name[path], strerror(-ret));
		return 1;
	}
	if (path != SPQ_SKIN_PPE && open_skin(&sk_ppe, SPQ_SKIN_PPE))
		sk_ppe = NULL;
	for (i = 0; i < 3; i++) {
		case_c2(0, 1, one, 33, 120, "one mesh");
		case_c2(1, 1, one, 4, 6, "one mesh");
		case_c2(0, 2, two, 75, 400, "two meshes");
		case_c2(1, 2, two, 75, 400, "two meshes");
		case_c2(1, 4, small, 3, 0, "tiny meshes, no blends");
		case_c2(0, 1, big, 200, 1300, "9000 vertices, a full palette");
	}
	for (i = 0; i < 2; i++) {
		case_general(SPEQ_SKIN_DQ, SPEQ_SKIN_IDX_S32, SPEQ_SKIN_TAN_VEC4, SPEQ_SKIN_OUT_F32, 1, 1,
			     "DQ, int32 index, normal, float tangent, template");
		case_general(SPEQ_SKIN_DQ, SPEQ_SKIN_IDX_U16, SPEQ_SKIN_TAN_QUAT, SPEQ_SKIN_OUT_F32, 0, 0,
			     "DQ, uint16 index, tangent frame as floats, zeros");
		case_general(SPEQ_SKIN_DQ, SPEQ_SKIN_W4, SPEQ_SKIN_TAN_NONE, SPEQ_SKIN_OUT_F32, 1, 1,
			     "DQ, 4 weighted bones, normal");
		case_general(SPEQ_SKIN_DQ, SPEQ_SKIN_IDX_U8, SPEQ_SKIN_TAN_QUAT, SPEQ_SKIN_OUT_S16, 1, 0,
			     "DQ, uint8 index, normal and tangent frame");
		case_general(SPEQ_SKIN_MAT, SPEQ_SKIN_IDX_S32, SPEQ_SKIN_TAN_NONE, SPEQ_SKIN_OUT_F32, 1, 1,
			     "matrices, int32 index, normal");
		case_general(SPEQ_SKIN_MAT, SPEQ_SKIN_W4, SPEQ_SKIN_TAN_VEC4, SPEQ_SKIN_OUT_F32, 1, 0,
			     "matrices, 4 weighted bones, normal, float tangent, zeros");
		case_general(SPEQ_SKIN_MAT, SPEQ_SKIN_IDX_U8, SPEQ_SKIN_TAN_VEC4, SPEQ_SKIN_OUT_S16, 0, 1,
			     "matrices, uint8 index, tangent into shorts");
	}
	case_errors();
	if (path != SPQ_SKIN_PPE) {
		for (i = 0; i < 2; i++)
			pthread_create(&th[i].th, NULL, thread_main, &th[i]);
		for (i = 0; i < 2; i++)
			pthread_join(th[i].th, NULL);
		check(th[0].ok && th[1].ok, "two threads, 200 batches each, each its own speq_skin");
	}
	speq_skin_close_v1(sk);
	speq_skin_close_v1(sk_ppe);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}

/* --- --bench --- */

static double median(double *v, unsigned n)
{
	unsigned i, j;
	double t;

	for (i = 1; i < n; i++)
		for (j = i; j > 0 && v[j - 1] > v[j]; j--)
			t = v[j], v[j] = v[j - 1], v[j - 1] = t;
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

struct benchset {
	const char *name;
	int bump, nm, nverts[2], nb, nblend;
};

static void bench_one(const struct benchset *b, struct speq_skin *spu, struct speq_skin *ppe)
{
	struct c2skel k[2];
	struct c2vert *v[2];
	struct c2bump *bv[2];
	uint8_t *vdata[2];
	struct speq_skin_mesh m[2];
	uint32_t stride = b->bump ? sizeof(struct c2vvertbump) : sizeof(struct c2vvertn);
	double t0, t, vv[9];
	int i, n, total = 0, iter;
	void *out;

	/* each mesh its own group (Sauerbraten's hud gun: the hands and the gun, two skeletons) */
	for (i = 0; i < b->nm; i++) {
		total += b->nverts[i];
		c2_skel(&k[i], b->nb, b->nblend);
		v[i] = xalign(16, b->nverts[i] * sizeof(struct c2vert));
		bv[i] = b->bump ? xalign(16, b->nverts[i] * sizeof(struct c2bump)) : NULL;
		vdata[i] = xalign(16, b->nverts[i] * stride + 16);
		c2_mesh(v[i], bv[i], b->nverts[i], b->nb + b->nblend);
		c2_meshdesc(&m[i], &k[i], v[i], bv[i], b->nverts[i], vdata[i], 0);
	}
	/* Sauerbraten's interpverts, as the engine runs it */
	iter = 0;
	t0 = now();
	do {
		for (i = 0; i < b->nm; i++) {
			if (b->bump)
				c2_interp_bump(v[i], bv[i], b->nverts[i], k[i].bones, k[i].blends, b->nb,
					       (struct c2vvertbump *)vdata[i]);
			else
				c2_interp_n(v[i], b->nverts[i], k[i].bones, k[i].blends, b->nb,
					    (struct c2vvertn *)vdata[i]);
		}
		iter++;
		t = now() - t0;
	} while (t < 0.3 && iter < 5000);
	printf("bench %-22s %5d vertices  engine C   %8.1f us/frame  %6.1f ns/vertex\n", b->name, total,
	       t / iter * 1e6, t / iter / total * 1e9);
	for (n = 0; n < 2; n++) {
		struct speq_skin *s = n ? spu : ppe;
		const char *pn = n ? "spu" : "ppe";
		int fail = 0;

		if (!s)
			continue;
		iter = 0;
		t0 = now();
		do {
			for (i = 0; i < b->nm; i++)
				fail |= run_batch(s, &m[i], 1, b->nverts[i] * stride, &out);
			iter++;
			t = now() - t0;
		} while (t < 0.3 && iter < 5000);
		printf("bench %-22s %5d vertices  libspeq %s %8.1f us/frame  %6.1f ns/vertex%s\n",
		       b->name, total, pn, t / iter * 1e6, t / iter / total * 1e9,
		       fail ? "  (some batches declined or failed)" : "");
		if (n) {
			for (iter = 0; iter < 9; iter++) {
				usleep(250000);	/* > 200 ms: the workers park */
				t0 = now();
				for (i = 0; i < b->nm; i++)
					run_batch(s, &m[i], 1, b->nverts[i] * stride, &out);
				vv[iter] = (now() - t0) * 1e6;
			}
			printf("bench %-22s %5d vertices  libspeq spu %8.1f us/frame after the workers "
			       "parked (median of 9)\n", b->name, total, median(vv, 9));
		}
	}
	for (i = 0; i < b->nm; i++) {
		free(v[i]);
		free(bv[i]);
		free(vdata[i]);
		c2_skel_free(&k[i]);
	}
}

static int run_bench(void)
{
	static const struct benchset sets[] = {
		{ "hud gun (2 x bump)", 1, 2, { 1497, 1683 }, 33, 150 },
		{ "player (normals)", 0, 1, { 2451, 0 }, 75, 300 },
	};
	struct speq_skin *spu = NULL, *ppe = NULL;
	struct spq_skin_stats st;
	unsigned i;
	int ret;

	if ((!path_given || path == SPQ_SKIN_PPE) && open_skin(&ppe, SPQ_SKIN_PPE))
		ppe = NULL;
	if (!path_given || path != SPQ_SKIN_PPE) {
		ret = open_skin(&spu, SPQ_SKIN_SPU);
		if (ret) {
			printf("bench spu: no queue (%s): is deadline.spu installed, spufs mounted?\n",
			       strerror(-ret));
			spu = NULL;
		}
	}
	for (i = 0; i < sizeof(sets) / sizeof(sets[0]); i++)
		bench_one(&sets[i], spu, ppe);
	if (spu) {
		spq_skin_stats(spu, &st);
		printf("bench spu: %lu batches, %lu jobs, %.1f us mean wait, %.1f max, %lu timeouts, "
		       "%lu errors\n", st.spu, st.jobs, st.spu ? st.wait_us / st.spu : 0, st.wait_max,
		       st.timeouts, st.errors);
	}
	speq_skin_close_v1(spu);
	speq_skin_close_v1(ppe);
	return 0;
}

int main(int argc, char **argv)
{
	int do_check = 0, do_bench = 0, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--check"))
			do_check = 1;
		else if (!strcmp(argv[i], "--bench"))
			do_bench = 1;
		else if (!strcmp(argv[i], "-v"))
			verbose = 1;
		else if (!strcmp(argv[i], "--path") && i + 1 < argc) {
			const char *p = argv[++i];

			path_given = 1;
			if (!strcmp(p, "spu"))
				path = SPQ_SKIN_SPU;
			else if (!strcmp(p, "ppe"))
				path = SPQ_SKIN_PPE;
			else if (!strcmp(p, "auto"))
				path = SPQ_SKIN_AUTO;
			else
				do_check = do_bench = -1;
		} else {
			do_check = do_bench = -1;
		}
	}
	if (do_check < 0 || (!do_check && !do_bench)) {
		fprintf(stderr, "usage: speq-skin [--path spu|ppe|auto] [-v] --check | --bench\n");
		return 2;
	}
	spq_skin_tune(path, -1, -1, -1);
	if (do_check && run_check())
		return 1;
	if (do_bench)
		return run_bench();
	return 0;
}
