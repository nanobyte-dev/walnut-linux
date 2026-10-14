/*
 * rsxdrm-smoke: ps3rsx driver tests through raw ioctls, one subtest per run.
 *
 *   rsxdrm-smoke bo      buffer objects: VRAM (mappable / GPU-only) and GART, info, mmap r/w
 *   rsxdrm-smoke nop     channel + a push that only binds the 3D object (needs the GPU)
 *   rsxdrm-smoke m2mf    copy GART -> VRAM -> GART with the M2MF engine, compare
 *   rsxdrm-smoke clear   3D clear of a VRAM BO, read back with M2MF
 *   rsxdrm-smoke fence   10k back-end semaphore releases, label checked after each SYNC submit
 *   rsxdrm-smoke reject  pushes the validator must refuse (JMP, unknown object/DMA/method, host)
 *   rsxdrm-smoke tri     a magenta triangle into a 64x64 VRAM target, read back (center, corner)
 *   rsxdrm-smoke twochan two channels (two fds): A sets its surface once, B sets another and
 *                        clears, A clears again: with the kernel's context switch A's clear
 *                        must land in A's surface (docs/RSX.md E5)
 *   rsxdrm-smoke quirks  what Mesa does that libgcm doesn't: M2MF NOP on the M2MF subchannel,
 *                        RT_ENABLE 0x12 (both fine), vertex format type 0 (WEDGES the RSX: the
 *                        validator now rejects it, so this expects -EINVAL)
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

/* --- submission helpers ---------------------------------------------------------------- */

#define MTH(count, subch, m) (((count) << 18) | ((subch) << 13) | (m))

static struct drm_nouveau_channel_alloc chan;
static struct drm_nouveau_gem_info pushbo;
static uint32_t *pushmap, *p;

static struct drm_nouveau_gem_info newbo(uint32_t domain, uint64_t size)
{
	struct drm_nouveau_gem_info bo;
	int rc = gem_new(fd, domain | NOUVEAU_GEM_DOMAIN_MAPPABLE, size, 0, &bo);
	if (rc) { fprintf(stderr, "GEM_NEW: %s\n", strerror(-rc)); exit(1); }
	return bo;
}

static void *map(struct drm_nouveau_gem_info *bo)
{
	void *m = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bo->map_handle);
	if (m == MAP_FAILED) { perror("mmap"); exit(1); }
	return m;
}

static void setup(void)
{
	struct drm_nouveau_getparam gp = { .param = 0x1000 };
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GETPARAM, &gp)) { perror("PS3_ABI"); exit(1); }
	chan.fb_ctxdma_handle = 0xfeed0000;
	chan.tt_ctxdma_handle = 0xfeed0001;
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC, &chan)) { perror("CHANNEL_ALLOC"); exit(1); }
	printf("  channel %d, pushbuf domains %#x, notifier handle %u\n", chan.channel,
	       chan.pushbuf_domains, chan.notifier_handle);
	pushbo = newbo(NOUVEAU_GEM_DOMAIN_GART, 65536);
	pushmap = map(&pushbo);
	p = pushmap;
}

#define OUT(sc, m, v) do { *p++ = MTH(1, (sc), (m)); *p++ = (v); } while (0)

/* submit what's in the push BO since the last submit; extra BOs are referenced too */
static int submit(struct drm_nouveau_gem_info **extra, int nextra)
{
	struct drm_nouveau_gem_pushbuf_bo bos[8] = { { .handle = pushbo.handle,
		.read_domains = NOUVEAU_GEM_DOMAIN_GART, .valid_domains = NOUVEAU_GEM_DOMAIN_GART } };
	for (int i = 0; i < nextra; i++) {
		bos[1 + i].handle = extra[i]->handle;
		bos[1 + i].read_domains = bos[1 + i].write_domains = extra[i]->domain;
		bos[1 + i].valid_domains = extra[i]->domain;
	}
	struct drm_nouveau_gem_pushbuf_push push = { .bo_index = 0, .offset = 0,
						     .length = (uint64_t)(p - pushmap) * 4 };
	struct drm_nouveau_gem_pushbuf req = {
		.channel = chan.channel, .nr_buffers = 1 + nextra,
		.buffers = (uintptr_t)bos, .nr_push = 1, .push = (uintptr_t)&push,
		.vram_available = NOUVEAU_GEM_PUSHBUF_SYNC,
	};
	int rc = ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_PUSHBUF, &req) ? -errno : 0;
	p = pushmap;
	return rc;
}

static int t_nop(void)
{
	setup();
	OUT(0, 0, 0x31337000);
	int rc = submit(NULL, 0);
	printf("  bind 3D + fence: %s\n", rc ? strerror(-rc) : "ok");
	return rc != 0;
}

/* M2MF (subch 1, 0x31337303) linear copy of len bytes, 4 KiB lines */
static void m2mf(uint32_t dma_in, uint32_t in, uint32_t dma_out, uint32_t out, uint32_t len)
{
	uint32_t line = len < 4096 ? len : 4096;
	OUT(1, 0x184, dma_in);
	OUT(1, 0x188, dma_out);
	*p++ = MTH(8, 1, 0x30c);
	*p++ = in; *p++ = out; *p++ = line; *p++ = line; *p++ = line; *p++ = len / line;
	*p++ = 0x101; *p++ = 0;
}

static void bind_m2mf(void)
{
	OUT(1, 0, 0x31337303);
	OUT(1, 0x180, 0x66604200);	/* notifier DMA: unset, a fresh context faults (RSX.md) */
}

static int t_m2mf(void)
{
	setup();
	struct drm_nouveau_gem_info src = newbo(NOUVEAU_GEM_DOMAIN_GART, 65536);
	struct drm_nouveau_gem_info vram = newbo(NOUVEAU_GEM_DOMAIN_VRAM, 65536);
	struct drm_nouveau_gem_info back = newbo(NOUVEAU_GEM_DOMAIN_GART, 65536);
	uint32_t *s = map(&src), *b = map(&back);
	for (int i = 0; i < 16384; i++) { s[i] = 0x5a5a0000u ^ (i * 2654435761u); b[i] = 0; }
	bind_m2mf();
	m2mf(0xfeed0001, src.offset, 0xfeed0000, vram.offset, 65536);
	m2mf(0xfeed0000, vram.offset, 0xfeed0001, back.offset, 65536);
	struct drm_nouveau_gem_info *x[] = { &src, &vram, &back };
	int rc = submit(x, 3), bad = 0;
	for (int i = 0; i < 16384; i++)
		bad += b[i] != (0x5a5a0000u ^ (i * 2654435761u));
	printf("  GART %#llx -> VRAM %#llx -> GART %#llx: %s, %d of 16384 words differ\n",
	       (unsigned long long)src.offset, (unsigned long long)vram.offset,
	       (unsigned long long)back.offset, rc ? strerror(-rc) : "submitted", bad);
	return rc || bad;
}

static int t_clear(void)
{
	setup();
	const uint32_t w = 64, h = 64;
	struct drm_nouveau_gem_info rt = newbo(NOUVEAU_GEM_DOMAIN_VRAM, w * h * 4);
	struct drm_nouveau_gem_info back = newbo(NOUVEAU_GEM_DOMAIN_GART, w * h * 4);
	uint32_t *b = map(&back);
	memset(b, 0, w * h * 4);
	OUT(0, 0, 0x31337000);
	OUT(0, 0x194, 0xfeed0000);
	OUT(0, 0x200, w << 16); OUT(0, 0x204, h << 16);
	OUT(0, 0x208, 0x148); OUT(0, 0x20c, w * 4); OUT(0, 0x210, rt.offset);
	OUT(0, 0x220, 1); OUT(0, 0x2b8, 0);
	OUT(0, 0x8c0, w << 16); OUT(0, 0x8c4, h << 16);
	OUT(0, 0x324, 0x01010101);
	OUT(0, 0x1d90, 0xff12ab34);
	OUT(0, 0x1d94, 0xf0);
	bind_m2mf();
	m2mf(0xfeed0000, rt.offset, 0xfeed0001, back.offset, w * h * 4);
	struct drm_nouveau_gem_info *x[] = { &rt, &back };
	int rc = submit(x, 2), bad = 0;
	for (uint32_t i = 0; i < w * h; i++)
		bad += b[i] != 0xff12ab34;
	printf("  clear %ux%u at VRAM %#llx to ff12ab34, readback: %s, %d of %u pixels wrong (first %08x)\n",
	       w, h, (unsigned long long)rt.offset, rc ? strerror(-rc) : "submitted", bad, w * h, b[0]);
	return rc || bad;
}

static uint32_t swz02(uint32_t v) { return (v & 0xff00ff00) | ((v & 0xff) << 16) | ((v >> 16) & 0xff); }

static int t_fence(void)
{
	setup();
	struct drm_nouveau_notifierobj_alloc n = { .channel = chan.channel, .handle = 0x66606660, .size = 16 };
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC, &n)) { perror("NOTIFIEROBJ_ALLOC"); return 1; }
	struct drm_nouveau_gem_info ntfy = { .handle = chan.notifier_handle };
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_INFO, &ntfy)) { perror("GEM_INFO notifier"); return 1; }
	volatile uint32_t *labels = map(&ntfy);
	printf("  fence label at reports+%#x\n", n.offset);
	int stale = 0;
	OUT(0, 0, 0x31337000);
	for (uint32_t i = 1; i <= 10000; i++) {
		OUT(0, 0x1d6c, n.offset);
		OUT(0, 0x1d70, swz02(0x70000000 + i));
		int rc = submit(NULL, 0);
		if (rc) { printf("  submit %u: %s\n", i, strerror(-rc)); return 1; }
		stale += labels[n.offset / 4] != 0x70000000 + i;
	}
	printf("  10000 SYNC submits: %d with the label not yet written when the fence signalled\n", stale);
	return stale != 0;
}

static int t_reject(void)
{
	setup();
	int fail = 0, rc;
	*p++ = 0x20000000 | 0x1000;		/* JMP */
	rc = submit(NULL, 0);
	printf("  JMP: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	OUT(0, 0, 0xdeadbeef);			/* unknown object */
	rc = submit(NULL, 0);
	printf("  bind unknown object: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	OUT(0, 0, 0x31337000);
	OUT(0, 0x194, 0x12345678);		/* unknown context DMA */
	rc = submit(NULL, 0);
	printf("  unknown context DMA: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	*p++ = MTH(4, 0, 0x100); *p++ = 0;	/* burst longer than the push */
	rc = submit(NULL, 0);
	printf("  truncated burst: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	OUT(0, 0, 0x31337000);
	OUT(0, 0x1ffc, 0);			/* not in the 3D allowlist */
	rc = submit(NULL, 0);
	printf("  3D method 0x1ffc: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	OUT(0, 0x68, 0);			/* FIFO semaphore acquire */
	rc = submit(NULL, 0);
	printf("  semaphore acquire: %s\n", rc == -EINVAL ? "rejected" : "NOT rejected"); fail |= rc != -EINVAL;
	return fail;
}

/* Render target: a VRAM BO, w x h, A8R8G8B8 linear */
static void surface(uint32_t off, uint32_t w, uint32_t h)
{
	OUT(0, 0x194, 0xfeed0000);
	OUT(0, 0x200, w << 16); OUT(0, 0x204, h << 16);
	OUT(0, 0x208, 0x148); OUT(0, 0x20c, w * 4); OUT(0, 0x210, off);
	OUT(0, 0x220, 1); OUT(0, 0x2b8, 0);
	OUT(0, 0x8c0, w << 16); OUT(0, 0x8c4, h << 16);
	OUT(0, 0x324, 0x01010101);
}

static void words(const uint32_t *w, size_t n) { memcpy(p, w, n * 4); p += n; }
#define WORDS(...) do { static const uint32_t w_[] = { __VA_ARGS__ }; words(w_, sizeof(w_) / 4); } while (0)

/* docs/RSX-PROGRAMS.md A.1, scaled to a 64x64 target: state, viewport, VP, FP_CONST, triangle */
static int t_tri(void)
{
	setup();
	const uint32_t w = 64, h = 64;
	struct drm_nouveau_gem_info rt = newbo(NOUVEAU_GEM_DOMAIN_VRAM, w * h * 4);
	struct drm_nouveau_gem_info fp = newbo(NOUVEAU_GEM_DOMAIN_VRAM, 4096);
	struct drm_nouveau_gem_info back = newbo(NOUVEAU_GEM_DOMAIN_GART, w * h * 4);
	static const uint32_t fp_const[] = {	/* MOV R0, {1,0,1,1}, memory (halfword-swapped) form */
		0x1e010100, 0xc8021c9d, 0xc8000001, 0xc8000001,
		0x00003f80, 0x00000000, 0x00003f80, 0x00003f80,
		0x00010001, 0x00000000, 0x00000000, 0x00000000,
	};
	volatile uint32_t *f = map(&fp);
	for (int i = 0; i < 12; i++)
		f[i] = fp_const[i];
	uint32_t *b = map(&back);
	memset(b, 0, w * h * 4);

	OUT(0, 0, 0x31337000);
	surface(rt.offset, w, h);
	OUT(0, 0x1d90, 0xff000000);
	OUT(0, 0x1d94, 0xf0);
	WORDS(0x00040a74, 0, 0x00040310, 0, 0x0004183c, 0, 0x00040304, 0, 0x00040348, 0,
	      0x00040374, 0, 0x00040368, 0x00001d01, 0x00081828, 0x00001b02, 0x00001b02,
	      0x00041d88, 0x00001040,		/* SHADER_WINDOW: (1<<12) | 64 */
	      0x00080a00, 0x00400000, 0x00400000,	/* viewport 64x64 */
	      0x00080394, 0x00000000, 0x3f800000,
	      0x00200a20, 0x42000000, 0x42000000, 0x3f000000, 0, 0x42000000, 0xc2000000, 0x3f000000, 0,
	      0x00081e9c, 0, 0,
	      0x00200b80, 0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,
	                  0x401f9c6c, 0x0040030d, 0x8106c083, 0x6041ff85,
	      0x00041ff0, 0x00000009, 0x00041ff4, 0x00000025, 0x00041ef8, 0x0020ffff);
	OUT(0, 0x8e4, (uint32_t)fp.offset | 1);
	OUT(0, 0x1d60, 0x02000440);
	WORDS(0x00041808, 0x00000005,
	      0x00101c30, 0x3f800000, 0, 0, 0x3f800000, 0x00101c00, 0, 0x3f000000, 0, 0x3f800000,
	      0x00101c30, 0, 0x3f800000, 0, 0x3f800000, 0x00101c00, 0xbf000000, 0xbf000000, 0, 0x3f800000,
	      0x00101c30, 0, 0, 0x3f800000, 0x3f800000, 0x00101c00, 0x3f000000, 0xbf000000, 0, 0x3f800000,
	      0x00041808, 0x00000000);
	bind_m2mf();
	m2mf(0xfeed0000, rt.offset, 0xfeed0001, back.offset, w * h * 4);
	struct drm_nouveau_gem_info *x[] = { &rt, &fp, &back };
	int rc = submit(x, 3);
	uint32_t center = b[(h / 2) * w + w / 2], corner = b[0];
	int lit = 0;
	for (uint32_t i = 0; i < w * h; i++)
		lit += b[i] == 0xffff00ff;
	printf("  triangle: %s, center %08x (want ffff00ff), corner %08x (want ff000000), %d magenta pixels\n",
	       rc ? strerror(-rc) : "submitted", center, corner, lit);
	for (uint32_t y = 0; y < h; y += 8) {
		printf("    ");
		for (uint32_t x2 = 0; x2 < w; x2 += 2)
			putchar(b[y * w + x2] == 0xffff00ff ? '#' : '.');
		putchar('\n');
	}
	return rc || center != 0xffff00ff || corner != 0xff000000;
}

/* A second open of the device with its own channel and push BO */
struct ctxt { int fd; struct drm_nouveau_channel_alloc chan; struct drm_nouveau_gem_info pushbo; uint32_t *map; };

static void ctxt_switch(struct ctxt *c)
{
	fd = c->fd; chan = c->chan; pushbo = c->pushbo; pushmap = p = c->map;
}

static void ctxt_new(struct ctxt *c)
{
	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) { perror("open"); exit(1); }
	setup();
	c->fd = fd; c->chan = chan; c->pushbo = pushbo; c->map = pushmap;
}

static int t_twochan(void)
{
	struct ctxt a, b;
	const uint32_t w = 64, h = 64;

	ctxt_new(&a);
	struct drm_nouveau_gem_info rta = newbo(NOUVEAU_GEM_DOMAIN_VRAM, w * h * 4);
	struct drm_nouveau_gem_info backa = newbo(NOUVEAU_GEM_DOMAIN_GART, w * h * 4);
	uint32_t *ba = map(&backa);
	ctxt_new(&b);
	struct drm_nouveau_gem_info rtb = newbo(NOUVEAU_GEM_DOMAIN_VRAM, w * h * 4);
	struct drm_nouveau_gem_info backb = newbo(NOUVEAU_GEM_DOMAIN_GART, w * h * 4);
	uint32_t *bb = map(&backb);

	ctxt_switch(&a);		/* A: surface + clear 1 */
	OUT(0, 0, 0x31337000);
	surface(rta.offset, w, h);
	OUT(0, 0x1d90, 0xffaa0000); OUT(0, 0x1d94, 0xf0);
	struct drm_nouveau_gem_info *xa[] = { &rta, &backa };
	int rc = submit(xa, 2);

	ctxt_switch(&b);		/* B: its own surface + clear */
	OUT(0, 0, 0x31337000);
	surface(rtb.offset, w, h);
	OUT(0, 0x1d90, 0xff00bb00); OUT(0, 0x1d94, 0xf0);
	bind_m2mf();
	m2mf(0xfeed0000, rtb.offset, 0xfeed0001, backb.offset, w * h * 4);
	struct drm_nouveau_gem_info *xb[] = { &rtb, &backb };
	rc |= submit(xb, 2);

	ctxt_switch(&a);		/* A: clear 2 without re-sending its surface */
	OUT(0, 0x1d90, 0xff0000cc); OUT(0, 0x1d94, 0xf0);
	bind_m2mf();
	m2mf(0xfeed0000, rta.offset, 0xfeed0001, backa.offset, w * h * 4);
	rc |= submit(xa, 2);

	printf("  A's surface %08x (want ff0000cc), B's surface %08x (want ff00bb00): %s\n",
	       ba[0], bb[0], rc ? "submit failed" :
	       ba[0] == 0xff0000cc && bb[0] == 0xff00bb00 ? "state switched with the channel" :
	       "STATE LEAKED between channels");
	return rc || ba[0] != 0xff0000cc || bb[0] != 0xff00bb00;
}

static int t_quirks(void)
{
	setup();
	int rc, fail = 0;
	bind_m2mf();
	OUT(1, 0x100, 0);			/* NOP on the M2MF subchannel (nv30_transfer.c) */
	rc = submit(NULL, 0);
	printf("  M2MF NOP: %s\n", rc ? strerror(-rc) : "ok"); fail |= rc != 0;
	OUT(0, 0, 0x31337000);
	OUT(0, 0x220, 0x12);			/* RT_ENABLE outside libgcm's set */
	OUT(0, 0x220, 1);
	rc = submit(NULL, 0);
	printf("  RT_ENABLE 0x12: %s\n", rc ? strerror(-rc) : "ok"); fail |= rc != 0;
	OUT(0, 0x1740, 0x00000000);		/* vertex format type 0, size 0 (array off) */
	OUT(0, 0x1740, 0x00000002);
	rc = submit(NULL, 0);
	printf("  vertex format type 0: %s\n", rc == -EINVAL ? "rejected (it wedges the RSX)" : "NOT rejected");
	fail |= rc != -EINVAL;
	return fail;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: %s bo|nop|m2mf|clear|fence|reject|tri|twochan|quirks\n", argv[0]);
		return 2;
	}
	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) { perror("/dev/dri/renderD128"); return 1; }
	int fail;
	if (!strcmp(argv[1], "bo"))
		fail = t_bo();
	else if (!strcmp(argv[1], "nop"))
		fail = t_nop();
	else if (!strcmp(argv[1], "m2mf"))
		fail = t_m2mf();
	else if (!strcmp(argv[1], "clear"))
		fail = t_clear();
	else if (!strcmp(argv[1], "fence"))
		fail = t_fence();
	else if (!strcmp(argv[1], "reject"))
		fail = t_reject();
	else if (!strcmp(argv[1], "tri"))
		fail = t_tri();
	else if (!strcmp(argv[1], "twochan"))
		fail = t_twochan();
	else if (!strcmp(argv[1], "quirks"))
		fail = t_quirks();
	else {
		fprintf(stderr, "unknown subtest %s\n", argv[1]);
		return 2;
	}
	printf("%s: %s\n", argv[1], fail ? "FAIL" : "ok");
	return fail;
}
