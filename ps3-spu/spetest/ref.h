/* The PPE's reference for the execution test: what each operation of oplist.h should give.
 * Words are big-endian quadwords in host variables (word 0 = the SPU's preferred slot). */
#ifndef SPT_REF_H
#define SPT_REF_H

#include <stdint.h>
#include "oplist.h"

enum { CMP_EXACT, CMP_SPF, CMP_SPF02, CMP_DPF, CMP_NONE };

struct spt_opinfo {
	const char *name;
	int in;		/* IN_* of gen.h */
	int cmp;	/* CMP_* */
};

extern const struct spt_opinfo spt_ops[OPX_N];

/* r = op(a, b, c). *skip gets a bit per word the model does not cover (an operand pair too far
 * apart in exponent for the exact SPU model); those words are not compared. */
void spt_ref(int op, const uint32_t a[4], const uint32_t b[4], const uint32_t c[4],
	     uint32_t r[4], unsigned *skip);
/* 1 if got matches want under the compare mode */
int spt_cmp(int cmp, const uint32_t got[4], const uint32_t want[4], unsigned skip);

/* the SPU's single precision: no infinities or NaNs (exponent 255 is a normal number), denormal
 * operands read as zero, results that would be denormal are zero, overflow saturates, and the
 * result is truncated (rounded toward zero). fma is fused. Returns 0 if out of the model's
 * range. */
int spt_sp_fma(uint32_t a, uint32_t b, uint32_t c, uint32_t *r);

/* host side of the scalar kernels (gen.h) for the checks */
uint32_t spt_ref_branch(uint32_t seed);
uint32_t spt_ref_mem(uint32_t seed);

#endif
