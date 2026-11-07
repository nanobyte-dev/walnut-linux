/* bo-domain-test (also 0066's default of NV30_TEX_INVALIDATE_ALWAYS): where nv30 references a texture's or render target's buffer (nv30_bo_domain, 0055; gated
 * on the PS3 by 0064). On the PS3: VRAM when the buffer's flags say VRAM, else GART, as before. Anywhere
 * else: VRAM, whatever the flags say (a buffer imported into GART, or one whose flags are not filled in yet,
 * is still addressed as the stock driver does). The PS3 half is the golden check: every combination of
 * the placement flags gives what the unconditional rule of 0055 gave.
 * (host-tests.sh bo-domain; the fix-off run uses nv30_winsys.h as it was before 0064) */
#include <stdio.h>
#include "nv30/nv30_winsys.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rule_0055(const struct nouveau_bo *bo)
{
	return (bo->flags & NOUVEAU_BO_VRAM) ? NOUVEAU_BO_VRAM : NOUVEAU_BO_GART;
}

int main(void)
{
	struct nouveau_device ps3 = { .ps3 = true }, other = { .ps3 = false };
	/* every subset of the flag bits the domain looks at, with the rest of the word set and clear */
	for (uint32_t extra = 0; extra < 2; extra++) {
		for (uint32_t f = 0; f < 4; f++) {
			struct nouveau_bo bo = { .flags = (f & 1 ? NOUVEAU_BO_VRAM : 0) | (f & 2 ? NOUVEAU_BO_GART : 0) |
			                                  (extra ? NOUVEAU_BO_MAP : 0) };
			bo.device = &ps3;
			CHECK(nv30_bo_domain(&bo) == rule_0055(&bo), "PS3, flags %#x: %#x, want %#x (the rule as it was)", bo.flags,
			      nv30_bo_domain(&bo), rule_0055(&bo));
			bo.device = &other;
			CHECK(nv30_bo_domain(&bo) == NOUVEAU_BO_VRAM, "not the PS3, flags %#x: %#x, want VRAM", bo.flags,
			      nv30_bo_domain(&bo));
		}
	}
	/* 0066: the texture cache invalidate before every draw is the default off the PS3 only */
#ifdef NV30_HAVE_TEX_DEFAULT
	CHECK(!nv30_tex_invalidate_always_default(true) && nv30_tex_invalidate_always_default(false),
	      "NV30_TEX_INVALIDATE_ALWAYS default: PS3 %d, other %d, want 0 and 1", nv30_tex_invalidate_always_default(true),
	      nv30_tex_invalidate_always_default(false));
#endif
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
