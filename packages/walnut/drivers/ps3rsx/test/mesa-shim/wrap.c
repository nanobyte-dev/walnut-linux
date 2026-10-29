/* LD_PRELOAD in front of Mesa's nouveau noop drm-shim: answer like ps3rsx (chipset 0x47,
 * PCI vendor 0x104d, PS3_ABI 2, PS3_RECOVER, no VMA tilemode, a 64 KiB channel notifier, the
 * fence label and query slice of channel 1), place BOs like ps3rsx (VRAM from 0x900000, GART
 * from 2 MiB, page aligned, never moving), and write every push of every submit to $PUSHDUMP
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
struct bo { uint32_t handle, domain; uint64_t offset, size, map_handle; void *ptr; };
static struct bo bos[MAXBO];
static int nbo;
static uint64_t vram_next = 0x900000, gart_next = 0x200000;
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

static uint64_t place(uint32_t domain, uint64_t size, uint32_t align)
{
	uint64_t *next = (domain & NOUVEAU_GEM_DOMAIN_VRAM) || !domain ? &vram_next : &gart_next;
	if (align < 4096)
		align = 4096;
	size = (size + 4095) & ~4095ull;
	*next = (*next + align - 1) & ~(uint64_t)(align - 1);
	uint64_t o = *next;
	*next += size;
	return o;
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
		int r = real_ioctl(fd, req, arg);
		if (r)
			return r;
		if (cmd == DRM_NOUVEAU_CHANNEL_ALLOC) {
			/* ps3rsx's channel notifier maps the whole 64 KiB reports area */
			struct drm_nouveau_channel_alloc *ca = arg;
			struct drm_nouveau_gem_new g = { .info = { .size = 0x10000,
				.domain = NOUVEAU_GEM_DOMAIN_GART }, .align = 0x1000 };
			if (!real_ioctl(fd, DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GEM_NEW,
						      struct drm_nouveau_gem_new), &g))
				ca->notifier_handle = g.info.handle;
		}
		if (cmd == DRM_NOUVEAU_GEM_NEW && nbo < MAXBO) {
			struct drm_nouveau_gem_new *n = arg;
			struct bo *b = &bos[nbo++];
			b->handle = n->info.handle;
			b->domain = n->info.domain;
			b->size = n->info.size;
			b->map_handle = n->info.map_handle;
			b->offset = n->info.offset = place(n->info.domain, n->info.size, n->align);
			if (!(n->info.domain & NOUVEAU_GEM_DOMAIN_VRAM) && n->info.domain)
				n->info.domain = NOUVEAU_GEM_DOMAIN_GART;
			else
				n->info.domain = NOUVEAU_GEM_DOMAIN_VRAM;
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
