/*
 * rsxdrm-smoke: ps3rsx driver tests through raw ioctls, one subtest per run.
 *
 *   rsxdrm-smoke bo     buffer objects: VRAM (mappable / GPU-only) and GART, info, mmap r/w
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rsxdrm.h"

static int fd;

static const char *dom(uint32_t d)
{
	return d == NOUVEAU_GEM_DOMAIN_VRAM ? "VRAM" : d == NOUVEAU_GEM_DOMAIN_GART ? "GART" : "?";
}

/* allocate, report, and if mappable write+read a pattern through mmap */
static int one_bo(const char *what, uint32_t domain, uint64_t size, uint32_t align)
{
	struct drm_nouveau_gem_info bo, again = { 0 };
	int rc = gem_new(fd, domain, size, align, &bo);
	if (rc) {
		printf("  %-22s GEM_NEW: %s\n", what, strerror(-rc));
		return 1;
	}
	again.handle = bo.handle;
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_INFO, &again) || again.offset != bo.offset) {
		printf("  %-22s GEM_INFO mismatch\n", what);
		return 1;
	}
	printf("  %-22s handle %u %s offset %#llx size %#llx map %s", what, bo.handle,
	       dom(bo.domain), (unsigned long long)bo.offset, (unsigned long long)bo.size,
	       bo.map_handle ? "yes" : "no");
	int bad = 0;
	if (bo.map_handle) {
		volatile uint32_t *p = mmap(NULL, bo.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
					    bo.map_handle);
		if (p == MAP_FAILED) {
			printf(", mmap failed: %s\n", strerror(errno));
			return 1;
		}
		size_t words = bo.size / 4 > 4096 ? 4096 : bo.size / 4;
		for (size_t i = 0; i < words; i++)
			p[i] = 0xb0000000u ^ (uint32_t)(i * 2654435761u) ^ bo.handle;
		for (size_t i = 0; i < words; i++)
			bad += p[i] != (0xb0000000u ^ (uint32_t)(i * 2654435761u) ^ bo.handle);
		munmap((void *)p, bo.size);
		printf(", %zu words r/w, %d bad", words, bad);
	}
	printf("\n");
	gem_close(fd, bo.handle);
	return bad != 0;
}

static int t_bo(void)
{
	int fail = 0;
	fail |= one_bo("VRAM mappable 64K", NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_MAPPABLE, 65536, 0);
	fail |= one_bo("VRAM gpu-only 1M", NOUVEAU_GEM_DOMAIN_VRAM, 1 << 20, 0);
	fail |= one_bo("GART 512K (pushbuf)", NOUVEAU_GEM_DOMAIN_GART | NOUVEAU_GEM_DOMAIN_MAPPABLE, 512 << 10, 0);
	fail |= one_bo("domain 0, align 256", 0, 4096, 256);
	fail |= one_bo("VRAM|GART mappable", NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART | NOUVEAU_GEM_DOMAIN_MAPPABLE, 8192, 0x1000);
	/* exhaustion: GART is small, it must fail cleanly */
	struct drm_nouveau_gem_info big;
	int rc = gem_new(fd, NOUVEAU_GEM_DOMAIN_GART, 512ull << 20, 0, &big);
	printf("  %-22s %s\n", "GART 512M (too big)", rc ? strerror(-rc) : "UNEXPECTEDLY OK");
	if (!rc) { gem_close(fd, big.handle); fail = 1; }
	return fail;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: %s bo\n", argv[0]);
		return 2;
	}
	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) { perror("/dev/dri/renderD128"); return 1; }
	int fail;
	if (!strcmp(argv[1], "bo"))
		fail = t_bo();
	else {
		fprintf(stderr, "unknown subtest %s\n", argv[1]);
		return 2;
	}
	printf("%s: %s\n", argv[1], fail ? "FAIL" : "ok");
	return fail;
}
