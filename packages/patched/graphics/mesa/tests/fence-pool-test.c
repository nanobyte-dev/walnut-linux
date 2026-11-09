/* fence-pool-test (Mesa 0077): NV30_FENCE_BO_POOL's default (nv30_screen.h), 16 on the PS3 and 0 elsewhere, and
 * within the room of the fence list's pool (nouveau_fence.h). (host-tests.sh fence-pool) */
#include <stdio.h>
#include "nv30/nv30_screen.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	struct nouveau_fence_list fl;

	CHECK(nv30_fence_bo_pool_default(true) == 16 && nv30_fence_bo_pool_default(false) == 0,
	      "NV30_FENCE_BO_POOL default: PS3 %d, other %d, want 16 and 0", nv30_fence_bo_pool_default(true),
	      nv30_fence_bo_pool_default(false));
	CHECK(nv30_fence_bo_pool_default(true) <= (int)ARRAY_SIZE(fl.bo_pool),
	      "NV30_FENCE_BO_POOL default: %d past the pool's %d buffers", nv30_fence_bo_pool_default(true),
	      (int)ARRAY_SIZE(fl.bo_pool));
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
