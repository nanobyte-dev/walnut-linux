/* LD_PRELOAD in front of Mesa's nouveau noop drm-shim: answer like ps3rsx (chipset 0x47,
 * PCI vendor 0x104d, PS3_ABI 2, PS3_RECOVER, no VMA tilemode, a 64 KiB channel notifier, the
 * fence label and query slice of channel 1), place BOs like ps3rsx (its three heaps, first fit,
 * page aligned, never moving, ENOSPC when full; PS3SHIM_VRAM_CPU_KB, PS3SHIM_VRAM_GPU_KB and
 * PS3SHIM_GART_KB make a heap smaller, as X and the VRAM swap do on the PS3), and write every push of every submit to $PUSHDUMP
 * (default /tmp/push.bin: per push the big-endian words 'PUSH', submit, push index, word count,
 * then the words; $PUSHDUMP.bos lists the BOs). DUMPBO_SUBMIT=N also writes the BOs submit N
 * lists to /tmp/bo-N-<offset>.bin (programs, staging data). PUSHDUMP_PERPID=1 appends .<pid> to
 * the names, for programs with several GL processes (a browser's UI and web processes); the
 * files are made at a process's first nouveau ioctl.
 *
 * Every submit also goes through ps3rsx's validator (ps3val.c) and gets ps3rsx's answer
 * (submit.c): a rejected push cancels the submit (-ECANCELED) for a client that asked for
 * PS3_RECOVER, else fails it (-EINVAL) and refuses the channel from then on (-EIO). What would
 * reach the GPU (the accepted pushes, and the END ps3rsx sends for a primitive the channel left
 * open, push index 0xffff) goes to $PUSHDUMP.gpu in the same format, the verdicts to
 * $PUSHDUMP.log. PS3SHIM_RECOVER=0: answer PS3_RECOVER like an older ps3rsx (unknown);
 * PS3SHIM_REJECT=N[,N...]: reject those submits as if a push of each were bad (a one-off
 * rejection, to watch Mesa recover); PS3SHIM_VALIDATE=0: no validation (accept all). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm.h>
#include <nouveau_drm.h>

#define MAXBO 65536
struct bo { uint32_t handle, domain; uint64_t offset, size, map_handle; void *ptr; int heap; };
static struct bo bos[MAXBO];
static int nbo;

/* ps3rsx's three heaps (mem.c): the CPU-visible VRAM from above ps3fb's 9 MiB to 224 MiB, the
 * GPU-only VRAM from 224 to 254 MiB, the GART's IO window from 2 MiB (the ring) to 64 MiB, each
 * first fit at 4 KiB. PS3SHIM_VRAM_CPU_KB, PS3SHIM_VRAM_GPU_KB and PS3SHIM_GART_KB shrink
 * a heap from its top (what X and the VRAM swap hold on the PS3); a request that fits no heap
 * it may use fails with ENOSPC as ps3rsx's does, counted in the summary. */
enum { H_CPU, H_GPU, H_GART, H_N };
static const char *heap_name[H_N] = { "VRAM cpu", "VRAM gpu", "GART" };
static uint64_t heap_lo[H_N] = { 0x900000, 0xe000000, 0x200000 };
static uint64_t heap_hi[H_N] = { 0xe000000, 0xfe00000, 0x4000000 };
static uint64_t heap_used[H_N], heap_peak[H_N];
static unsigned nospc[H_N + 1];
static uint64_t nospc_bytes;
static FILE *dump, *bolog, *gpudump, *vlog;
static unsigned submit_seq;

/* ps3val.c: ps3rsx's validator on the channel's state */
void ps3val_begin(void);
int ps3val_push(const uint32_t *w, uint32_t n, const char **why);
void ps3val_commit(void);
bool ps3val_cancel(uint32_t *subch3d);
static bool recover, refused;
static unsigned cancelled, rejected;

static int (*real_ioctl)(int, unsigned long, ...);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);

static void init(void)
{
	if (real_ioctl)
		return;
	real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	real_mmap = dlsym(RTLD_NEXT, "mmap");
}

static void summary(void);

static void open_dump(void)
{
	if (dump)
		return;
	const char *p = getenv("PUSHDUMP");
	char n[512], b[520];
	if (getenv("PUSHDUMP_PERPID"))
		snprintf(n, sizeof(n), "%s.%d", p ? p : "/tmp/push.bin", (int)getpid());
	else
		snprintf(n, sizeof(n), "%s", p ? p : "/tmp/push.bin");
	dump = fopen(n, "wb");
	snprintf(b, sizeof(b), "%s.bos", n);
	bolog = fopen(b, "w");
	snprintf(b, sizeof(b), "%s.gpu", n);
	gpudump = fopen(b, "wb");
	snprintf(b, sizeof(b), "%s.log", n);
	vlog = fopen(b, "w");
	atexit(summary);
}

static struct bo *bo_by_handle(uint32_t h)
{
	for (int i = nbo - 1; i >= 0; i--)
		if (bos[i].handle == h)
			return &bos[i];
	return NULL;
}

static void heaps_init(void)
{
	static bool done;
	const char *env[H_N] = { "PS3SHIM_VRAM_CPU_KB", "PS3SHIM_VRAM_GPU_KB", "PS3SHIM_GART_KB" };

	if (done)
		return;
	done = true;
	for (int h = 0; h < H_N; h++) {
		const char *v = getenv(env[h]);
		if (v && *v) {
			uint64_t lim = heap_lo[h] + (strtoull(v, NULL, 10) << 10);
			if (lim < heap_hi[h])
				heap_hi[h] = lim;
		}
	}
}

/* first fit in heap h among the live BOs; ~0 when nothing fits */
static uint64_t fit(int h, uint64_t size, uint64_t align)
{
	uint64_t o = heap_lo[h];

	for (;;) {
		bool moved = false;
		o = (o + align - 1) & ~(align - 1);
		if (o + size > heap_hi[h])
			return ~0ull;
		for (int i = 0; i < nbo; i++) {
			struct bo *b = &bos[i];
			if (b->handle && b->heap == h && b->offset < o + size && o < b->offset + b->size) {
				o = b->offset + b->size;
				moved = true;
			}
		}
		if (!moved)
			return o;
	}
}

/* ps3rsx_bo_new()'s placement: *heap and the offset, or ~0 (ENOSPC) */
static uint64_t place(uint32_t domain, uint64_t size, uint32_t align, int *heap)
{
	bool vram = !domain || (domain & NOUVEAU_GEM_DOMAIN_VRAM);
	bool gart = !domain || (domain & NOUVEAU_GEM_DOMAIN_GART);
	bool mappable = domain & NOUVEAU_GEM_DOMAIN_MAPPABLE;
	int order[3], n = 0;
	uint64_t o;

	heaps_init();
	if (align < 4096)
		align = 4096;
	size = (size + 4095) & ~4095ull;
	if (vram && !mappable)
		order[n++] = H_GPU;
	if (vram)
		order[n++] = H_CPU;
	if (gart)
		order[n++] = H_GART;
	for (int i = 0; i < n; i++) {
		o = fit(order[i], size, align);
		if (o != ~0ull) {
			*heap = order[i];
			heap_used[*heap] += size;
			if (heap_used[*heap] > heap_peak[*heap])
				heap_peak[*heap] = heap_used[*heap];
			return o;
		}
	}
	nospc[gart ? H_GART : H_CPU]++;
	nospc_bytes += size;
	return ~0ull;
}

static void bo_close(uint32_t handle)
{
	struct bo *b = bo_by_handle(handle);

	if (!b)
		return;
	heap_used[b->heap] -= (b->size + 4095) & ~4095ull;
	b->handle = 0;
	if (nbo == MAXBO) {	/* make room: drop the closed ones, keeping the order */
		int j = 0;
		for (int i = 0; i < nbo; i++)
			if (bos[i].handle)
				bos[j++] = bos[i];
		nbo = j;
	}
}

static void put32(uint32_t v) { fwrite(&v, 4, 1, dump); }

static void gpu_put(uint32_t sub, uint32_t idx, const uint32_t *w, uint32_t n)
{
	uint32_t h[4] = { 0x50555348, sub, idx, n };

	fwrite(h, 4, 4, gpudump);
	fwrite(w, 4, n, gpudump);
	fflush(gpudump);
}

static bool forced_reject(unsigned sub)
{
	const char *r = getenv("PS3SHIM_REJECT");

	while (r && *r) {
		char *end;
		unsigned long v = strtoul(r, &end, 10);
		if (end == r)
			break;
		if (v == sub)
			return true;
		r = *end ? end + 1 : end;
	}
	return false;
}

static void summary(void)
{
	for (int h = 0; h < H_N; h++)
		if (vlog)
			fprintf(vlog, "heap %s: peak %llu KiB of %llu, %llu KiB at the end\n", heap_name[h],
				(unsigned long long)heap_peak[h] >> 10,
				(unsigned long long)(heap_hi[h] - heap_lo[h]) >> 10,
				(unsigned long long)heap_used[h] >> 10);
	if (vlog)
		fprintf(vlog, "out of space: %u VRAM-only requests, %u that allowed GART, %llu KiB in all\n",
			nospc[H_CPU], nospc[H_GART], (unsigned long long)nospc_bytes >> 10);
	fprintf(stderr, "ps3shim: out of space: %u VRAM-only, %u with GART (%llu KiB); peak VRAM cpu %llu KiB, GART %llu KiB\n",
		nospc[H_CPU], nospc[H_GART], (unsigned long long)nospc_bytes >> 10,
		(unsigned long long)heap_peak[H_CPU] >> 10, (unsigned long long)heap_peak[H_GART] >> 10);
	if (vlog)
		fprintf(vlog, "end: %u submits, %u rejected, %u cancelled, channel %s\n", submit_seq,
			rejected, cancelled, refused ? "refused" : "open");
	fprintf(stderr, "ps3shim: %u submits, %u rejected, %u cancelled, channel %s\n", submit_seq,
		rejected, cancelled, refused ? "refused" : "open");
}

/* ps3rsx's answer to the submit (submit.c): 0, or the errno it fails with */
static int ps3_submit(struct drm_nouveau_gem_pushbuf *s, struct drm_nouveau_gem_pushbuf_bo *bl,
		      struct drm_nouveau_gem_pushbuf_push *pl)
{
	const char *v = getenv("PS3SHIM_VALIDATE"), *why = "";
	uint32_t sc;

	if (!s->nr_push || (v && !strcmp(v, "0")))
		return 0;
	if (refused)
		return EIO;
	ps3val_begin();
	for (unsigned i = 0; i < s->nr_push; i++) {
		struct bo *b = bo_by_handle(bl[pl[i].bo_index].handle);
		uint32_t len = pl[i].length & 0x7fffff;
		int bad = forced_reject(submit_seq) && i == 0;

		if (bad)
			why = "PS3SHIM_REJECT\n";
		else if (!b || !b->ptr)
			bad = 1, why = "push buffer not mapped\n";
		else
			bad = ps3val_push((const uint32_t *)((char *)b->ptr + pl[i].offset), len / 4,
					  &why) != 0;
		if (!bad)
			continue;
		rejected++;
		fprintf(vlog, "submit %u push %u rejected: %s", submit_seq, i, why);
		if (!recover) {
			refused = true;
			fprintf(vlog, "submit %u: channel refused from now on\n", submit_seq);
			fflush(vlog);
			return EINVAL;
		}
		if (ps3val_cancel(&sc)) {
			uint32_t end[2] = { (1u << 18) | (sc << 13) | 0x1808, 0 };

			gpu_put(submit_seq, 0xffff, end, 2);
			fprintf(vlog, "submit %u: ended the open primitive\n", submit_seq);
		}
		cancelled++;
		fprintf(vlog, "submit %u cancelled\n", submit_seq);
		fflush(vlog);
		return ECANCELED;
	}
	ps3val_commit();
	for (unsigned i = 0; i < s->nr_push; i++) {
		struct bo *b = bo_by_handle(bl[pl[i].bo_index].handle);

		gpu_put(submit_seq, i, (const uint32_t *)((char *)b->ptr + pl[i].offset),
			(pl[i].length & 0x7fffff) / 4);
	}
	return 0;
}

int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	va_start(ap, req);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	init();

	unsigned nr = _IOC_NR(req);
	if (_IOC_TYPE(req) == 'd' && nr >= DRM_COMMAND_BASE) {
		unsigned cmd = nr - DRM_COMMAND_BASE;
		open_dump();
		if (cmd == DRM_NOUVEAU_GETPARAM) {
			struct drm_nouveau_getparam *gp = arg;
			switch (gp->param) {
			case 0x1000: gp->value = 2; return 0;
			case 0x1001: {	/* PS3_RECOVER */
				const char *r = getenv("PS3SHIM_RECOVER");
				if (r && !strcmp(r, "0")) {
					errno = EINVAL;
					return -1;
				}
				recover = true;
				gp->value = 1;
				return 0;
			}
			case NOUVEAU_GETPARAM_PCI_VENDOR: gp->value = 0x104d; return 0;
			case NOUVEAU_GETPARAM_PCI_DEVICE: gp->value = 0; return 0;
			case NOUVEAU_GETPARAM_CHIPSET_ID: gp->value = 0x47; return 0;
			case NOUVEAU_GETPARAM_FB_SIZE: gp->value = 245ull << 20; return 0;
			case NOUVEAU_GETPARAM_AGP_SIZE: gp->value = 63ull << 20; return 0;
			case 20 /* HAS_VMA_TILEMODE */: errno = EINVAL; return -1;
			}
		}
		if (cmd == DRM_NOUVEAU_NOTIFIEROBJ_ALLOC) {
			/* as ps3rsx gives them to channel id 1 (submit.c) */
			struct drm_nouveau_notifierobj_alloc *n = arg;
			if (n->handle == 0x66606660)
				n->offset = 0x20 + 32 * 1;
			else if (n->handle == 0x66626660)
				n->offset = 0x1400 + 0x1000 * 1;
			else
				n->offset = 0x1000;
			return 0;
		}
		uint64_t off = 0;
		int heap = H_CPU;
		if (cmd == DRM_NOUVEAU_GEM_NEW) {
			struct drm_nouveau_gem_new *n = arg;
			off = place(n->info.domain, n->info.size, n->align, &heap);
			if (off == ~0ull) {
				fprintf(bolog, "ENOSPC dom %x size %#llx (submit %u)\n", n->info.domain,
					(unsigned long long)n->info.size, submit_seq);
				fflush(bolog);
				errno = ENOSPC;
				return -1;
			}
		}
		int r = real_ioctl(fd, req, arg);
		if (r) {
			if (cmd == DRM_NOUVEAU_GEM_NEW)
				heap_used[heap] -= (((struct drm_nouveau_gem_new *)arg)->info.size + 4095) & ~4095ull;
			return r;
		}
		if (cmd == DRM_NOUVEAU_CHANNEL_ALLOC) {
			/* ps3rsx's channel notifier maps the whole 64 KiB reports area */
			struct drm_nouveau_channel_alloc *ca = arg;
			struct drm_nouveau_gem_new g = { .info = { .size = 0x10000,
				.domain = NOUVEAU_GEM_DOMAIN_GART }, .align = 0x1000 };
			if (!real_ioctl(fd, DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_NEW,
						      struct drm_nouveau_gem_new), &g))
				ca->notifier_handle = g.info.handle;
		}
		if (cmd == DRM_NOUVEAU_GEM_NEW && nbo < MAXBO) {	/* bo_close() keeps room */
			struct drm_nouveau_gem_new *n = arg;
			struct bo *b = &bos[nbo++];
			b->handle = n->info.handle;
			b->domain = n->info.domain;
			b->size = n->info.size;
			b->map_handle = n->info.map_handle;
			b->offset = n->info.offset = off;
			b->heap = heap;
			n->info.domain = heap == H_GART ? NOUVEAU_GEM_DOMAIN_GART : NOUVEAU_GEM_DOMAIN_VRAM;
			b->domain = n->info.domain;
			fprintf(bolog, "new %u dom %x off %#llx size %#llx align %#x (submit %u)\n", b->handle,
				b->domain, (unsigned long long)b->offset, (unsigned long long)b->size,
				n->align, submit_seq);
			fflush(bolog);
		} else if (cmd == DRM_NOUVEAU_GEM_INFO) {
			struct drm_nouveau_gem_info *in = arg;
			struct bo *b = bo_by_handle(in->handle);
			if (b) {
				in->offset = b->offset;
				in->domain = b->domain;
			}
		} else if (cmd == DRM_NOUVEAU_GEM_PUSHBUF) {
			struct drm_nouveau_gem_pushbuf *s = arg;
			struct drm_nouveau_gem_pushbuf_bo *bl = (void *)(uintptr_t)s->buffers;
			struct drm_nouveau_gem_pushbuf_push *pl = (void *)(uintptr_t)s->push;
			for (unsigned i = 0; i < s->nr_buffers; i++) {
				struct bo *b = bo_by_handle(bl[i].handle);
				bl[i].presumed.valid = 1;
				if (b) {
					bl[i].presumed.offset = b->offset;
					bl[i].presumed.domain = b->domain;
				}
			}
			for (unsigned i = 0; i < s->nr_push; i++) {
				struct bo *b = bo_by_handle(bl[pl[i].bo_index].handle);
				uint32_t len = pl[i].length & 0x7fffff;
				put32(0x50555348);
				put32(submit_seq);
				put32(i);
				put32(len / 4);
				if (b && b->ptr) {
					fwrite((char *)b->ptr + pl[i].offset, 1, len, dump);
				} else {
					for (uint32_t k = 0; k < len / 4; k++)
						put32(0xdeadbeef);
				}
			}
			fflush(dump);
			const char *ds = getenv("DUMPBO_SUBMIT");
			if (ds && (unsigned)atoi(ds) == submit_seq)
				for (unsigned i = 0; i < s->nr_buffers; i++) {
					struct bo *b = bo_by_handle(bl[i].handle);
					if (!b || !b->ptr || b->size > (4u << 20))
						continue;
					char fn[256];
					snprintf(fn, sizeof(fn), "/tmp/bo-%u-%#llx.bin", submit_seq,
						 (unsigned long long)b->offset);
					FILE *f = fopen(fn, "wb");
					if (f) { fwrite(b->ptr, 1, b->size, f); fclose(f); }
				}
			int verdict = ps3_submit(s, bl, pl);
			submit_seq++;
			s->vram_available = 200ull << 20;
			s->gart_available = 40ull << 20;
			if (verdict) {
				errno = verdict;
				return -1;
			}
		}
		return 0;
	}
	if (_IOC_TYPE(req) == 'd' && nr == 0x09)	/* DRM_IOCTL_GEM_CLOSE */
		bo_close(((struct drm_gem_close *)arg)->handle);
	return real_ioctl(fd, req, arg);
}

void *mmap(void *a, size_t len, int prot, int flags, int fd, off_t off)
{
	init();
	void *p = real_mmap(a, len, prot, flags, fd, off);
	if (p != MAP_FAILED)
		for (int i = nbo - 1; i >= 0; i--)
			if (bos[i].map_handle == (uint64_t)off && off) {
				bos[i].ptr = p;
				break;
			}
	return p;
}
void *mmap64(void *a, size_t len, int prot, int flags, int fd, off_t off)
	__attribute__((alias("mmap")));
