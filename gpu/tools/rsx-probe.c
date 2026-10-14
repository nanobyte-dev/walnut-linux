/*
 * rsx-probe: hardware exploration for the nouveau shim design. Takes over the ps3fb FIFO and
 * checks, one by one, what the RSX context lv1 gave ps3fb can do:
 *
 *   info     driver info block (GPU version, clocks, label/report areas)
 *   ref      SET_REFERENCE updates the ref word in the FIFO control block
 *   sema     FIFO and 3D semaphore releases land in the reports area (fence candidates)
 *   report   3D GET_REPORT writes a report into the reports area
 *   m2mf     memory-to-memory copies XDR<->VRAM and VRAM->VRAM (object 0x313371c3, subch 3)
 *   cpu      CPU write/read bandwidth to VRAM through the mmap
 *   fault    (only if asked) an invalid method: does a graph exception fire, and does EXIT recover
 *
 *   rsx-probe [test ...]     default: all except fault
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* From arch/powerpc/include/uapi/asm/ps3fb.h (ps3fb GPU patch) */
struct ps3gpu_ioctl_info {
	uint32_t ioif_base, reset_start, reset_size, fifo_start, fifo_size, fifo_size_max;
	uint32_t gart_size, report_size[2], driver_size, ctrl_size, video_size;
};
#define PS3GPU_IOCTL_INFO  _IOR('r', 7, struct ps3gpu_ioctl_info)
#define PS3GPU_IOCTL_EXIT  _IOW('r', 8, int)
#define PS3GPU_IOCTL_ENTER _IOW('r', 15, int)

/* From ps3fb.c */
struct fifo_ctrl {
	uint8_t res[64];
	volatile uint32_t put, get, ref;
};
struct driver_info {
	uint32_t version_driver, version_gpu, memory_size, hardware_channel;
	uint32_t nvcore_frequency, memory_frequency, reserved[6];
	uint32_t label_area, report_data_area;
};

#define GPU_USER_DEBUG 1023
#define CMD_JMP(addr)  (0x20000000u | (addr))
#define MTH(count, subch, method) (((count) << 18) | ((subch) << 13) | (method))

/* Subchannels as bound by ps3fb's reset program */
#define SC_3D   0
#define SC_M2MF 1	/* 0x31337303; subch 2 (0x3137c0de) is another M2MF, 3 is surface 2D */

#define DMA_VRAM 0xfeed0000u
#define DMA_XDR  0xfeed0001u

/* Scratch in the XDR FIFO mapping: after the 4 KB reset area and the 60 KB ring */
#define XDR_SCRATCH 0x10000u
#define XDR_SCRATCH_SIZE (GPU_FB_START - XDR_SCRATCH)
#define GPU_FB_START 0x80000u
/* VRAM scratch well above the visible frame(s) */
#define VRAM_SCRATCH 0x4000000u

static struct ps3gpu_ioctl_info info;
static int fb;
static uint32_t *fifo;			/* XDR, IOIF 0 .. 512 KB */
static struct fifo_ctrl *ctrl;
static volatile uint8_t *vram;
static volatile uint8_t *reports;
static struct driver_info *dinfo;
static uint32_t *cmd, *p;		/* ring start, write pointer */
static int failed;			/* exit status: a test or the hand-back failed */

static void *map(const char *path, size_t len)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); exit(1); }
	void *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) { fprintf(stderr, "mmap %s %#zx: ", path, len); perror(""); exit(1); }
	return m;
}

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int wait_idle(const char *what, int ms)
{
	for (int i = 0; i < ms * 10; i++) {
		if (ctrl->put == ctrl->get)
			return 0;
		usleep(100);
	}
	fprintf(stderr, "  timeout waiting for %s: put=%08x get=%08x exc=%u\n",
		what, ctrl->put, ctrl->get, fifo[GPU_USER_DEBUG]);
	return -1;
}

/* Every submission starts at the ring start: jump there from wherever put is. */
static int begin(void)
{
	uint32_t start = info.ioif_base + info.fifo_start;
	if (wait_idle("idle", 2000))
		return -1;
	if (ctrl->put != start) {
		fifo[(ctrl->put - info.ioif_base) / 4] = CMD_JMP(start);
		__sync_synchronize();
		ctrl->put = start;
		if (wait_idle("jump to ring start", 2000))
			return -1;
	}
	p = cmd = fifo + (start - info.ioif_base) / 4;
	return 0;
}

#define OUT(sc, m, v) do { *p++ = MTH(1, (sc), (m)); *p++ = (v); } while (0)

/* GET only says the FIFO fetched the commands, not that the engine ran them: end every
 * submission with SET_REFERENCE and wait for the ref word to reach that token. */
static uint32_t token = 0x7e000000;

static int kick(const char *what, int ms)
{
	OUT(SC_3D, 0x0050, ++token);
	__sync_synchronize();
	ctrl->put = info.ioif_base + info.fifo_start + (uint32_t)((p - cmd) * 4);
	for (int i = 0; i < ms * 10; i++) {
		if (ctrl->ref == token)
			return 0;
		usleep(100);
	}
	fprintf(stderr, "  timeout waiting for %s: put=%08x get=%08x ref=%08x (want %08x) exc=%u\n",
		what, ctrl->put, ctrl->get, ctrl->ref, token, fifo[GPU_USER_DEBUG]);
	return -1;
}

static void dump_nonzero(const char *name, volatile uint8_t *base, size_t len, size_t max)
{
	size_t shown = 0;
	for (size_t o = 0; o < len && shown < max; o += 4) {
		uint32_t v = *(volatile uint32_t *)(base + o);
		if (v) {
			printf("  %s+%#06zx: %08x\n", name, o, v);
			shown++;
		}
	}
	if (!shown)
		printf("  %s: all zero\n", name);
}

static void t_info(void)
{
	printf("[info] ioif=%#x reset=%#x+%#x fifo=%#x+%#x max=%#x gart=%#x (%u MB)\n",
	       info.ioif_base, info.reset_start, info.reset_size, info.fifo_start, info.fifo_size,
	       info.fifo_size_max, info.gart_size, info.gart_size >> 20);
	printf("  reports=%#x driver=%#x ctrl=%#x vram=%#x (%u MB)\n", info.report_size[0],
	       info.driver_size, info.ctrl_size, info.video_size, info.video_size >> 20);
	printf("  driver %#x gpu %#x mem_size %#x hw_channel %u core %u MHz mem %u MHz\n",
	       dinfo->version_driver, dinfo->version_gpu, dinfo->memory_size,
	       dinfo->hardware_channel, dinfo->nvcore_frequency / 1000000,
	       dinfo->memory_frequency / 1000000);
	printf("  label_area %#x report_data_area %#x\n", dinfo->label_area, dinfo->report_data_area);
	printf("  reports area, non-zero words before tests:\n");
	dump_nonzero("reports", reports, info.report_size[0], 16);
}

static void t_ref(void)
{
	uint32_t before = ctrl->ref;
	if (begin()) { failed = 1; return; }
	int rc = kick("ref", 100);		/* kick ends with NV406E SET_REFERENCE */
	printf("[ref] ref %08x -> %08x: %s\n", before, ctrl->ref, rc ? "FAIL" : "OK");
	failed |= rc != 0;
}

/* Look for a value anywhere in the reports area */
static long find_in_reports(uint32_t v)
{
	for (size_t o = 0; o < info.report_size[0]; o += 4)
		if (*(volatile uint32_t *)(reports + o) == v)
			return (long)o;
	return -1;
}

#define LABEL(o) (*(volatile uint32_t *)(reports + (o)))

static void t_sema(void)
{
	/* FIFO (NV406E) semaphore: ctx 0x66616661 was bound at 0x60 by the reset program */
	if (begin()) return;
	OUT(SC_3D, 0x0064, 0x10);			/* SEMAPHORE_OFFSET */
	OUT(SC_3D, 0x006c, 0x5e4a0001);		/* SEMAPHORE_RELEASE */
	int rc = kick("fifo semaphore", 100);
	long at = find_in_reports(0x5e4a0001);
	printf("[sema] fifo semaphore release (offset 0x10): %s, found at reports+%#lx\n",
	       rc ? "TIMEOUT" : "consumed", at);
	if (begin()) return;
	rc = kick("empty", 100);
	printf("  label 0x10 after an empty submission: %08x\n", LABEL(0x10));

	/* 3D back-end semaphore: ctx 0x66606660 bound at 0x1a4 */
	if (begin()) return;
	OUT(SC_3D, 0x1d6c, 0x20);			/* SET_SEMAPHORE_OFFSET */
	OUT(SC_3D, 0x1d70, 0x5e4a0002);		/* BACK_END_WRITE_SEMAPHORE_RELEASE */
	rc = kick("3d semaphore", 100);
	printf("  label 0x10 after the 3D release: %08x\n", LABEL(0x10));
	at = find_in_reports(0x5e4a0002);
	long sw = find_in_reports(0x5e02004a);	/* nv40 swaps bytes 0 and 2 on this one */
	printf("[sema] 3D back-end semaphore release (offset 0x20): %s, found at reports+%#lx, "
	       "byte-swapped at reports+%#lx (word there: %08x)\n", rc ? "TIMEOUT" : "consumed", at, sw,
	       *(volatile uint32_t *)(reports + 0x20));
	/* Texture read semaphore, the other 3D one */
	if (begin()) return;
	OUT(SC_3D, 0x1d6c, 0x30);
	OUT(SC_3D, 0x1d74, 0x5e4a0003);		/* TEXTURE_READ_SEMAPHORE_RELEASE */
	rc = kick("3d tex semaphore", 100);
	printf("  label 0x10 after the 3D texture-read release: %08x\n", LABEL(0x10));
	at = find_in_reports(0x5e4a0003);
	printf("[sema] 3D texture-read semaphore release (offset 0x30): %s, found at reports+%#lx\n",
	       rc ? "TIMEOUT" : "consumed", at);

	/* Acquire on a value already there must pass straight through */
	printf("  label 0x10 before the acquire: %08x\n", LABEL(0x10));
	if (LABEL(0x10) != 0x5e4a0001) {
		printf("  (rewriting it from the CPU so the acquire can't block)\n");
		LABEL(0x10) = 0x5e4a0001;
		__sync_synchronize();
	}
	if (begin()) return;
	OUT(SC_3D, 0x0064, 0x10);
	OUT(SC_3D, 0x0068, 0x5e4a0001);		/* SEMAPHORE_ACQUIRE */
	rc = kick("semaphore acquire", 100);
	printf("[sema] acquire of a released value: %s\n", rc ? "FAIL" : "OK");
}

static void t_report(void)
{
	uint8_t snap[0x10000];
	size_t n = info.report_size[0] < sizeof(snap) ? info.report_size[0] : sizeof(snap);
	for (size_t o = 0; o < n; o += 4)
		*(uint32_t *)(snap + o) = *(volatile uint32_t *)(reports + o);
	if (begin()) return;
	OUT(SC_3D, 0x17c8, 1);				/* CLEAR_REPORT_VALUE: ZPASS_PIXEL_CNT */
	OUT(SC_3D, 0x1800, (1u << 24) | 0x100);	/* GET_REPORT ZPASS at offset 0x100 */
	int rc = kick("report", 100);
	printf("[report] GET_REPORT ZPASS -> offset 0x100: %s; words that changed:\n",
	       rc ? "TIMEOUT" : "consumed");
	int changed = 0;
	for (size_t o = 0; o < n; o += 4) {
		uint32_t v = *(volatile uint32_t *)(reports + o);
		if (v != *(uint32_t *)(snap + o) && changed++ < 12)
			printf("  reports+%#06zx: %08x -> %08x\n", o, *(uint32_t *)(snap + o), v);
	}
	if (!changed)
		printf("  none in the first %#zx bytes of the reports area\n", n);
	printf("  VRAM+0x100: %08x %08x %08x %08x\n", ((volatile uint32_t *)(vram + 0x100))[0],
	       ((volatile uint32_t *)(vram + 0x100))[1], ((volatile uint32_t *)(vram + 0x100))[2],
	       ((volatile uint32_t *)(vram + 0x100))[3]);
}

/* M2MF linear copy of len bytes as lines of up to 4 KB */
static int m2mf_copy(uint32_t dma_in, uint32_t off_in, uint32_t dma_out, uint32_t off_out,
		     uint32_t len, int ms)
{
	uint32_t line = len >= 4096 ? 4096 : len, lines = len / line;
	if (begin()) return -1;
	OUT(SC_M2MF, 0x0184, dma_in);
	OUT(SC_M2MF, 0x0188, dma_out);
	while (lines) {
		uint32_t l = lines > 2047 ? 2047 : lines;
		*p++ = MTH(8, SC_M2MF, 0x030c);
		*p++ = off_in;
		*p++ = off_out;
		*p++ = line;	/* pitch in */
		*p++ = line;	/* pitch out */
		*p++ = line;	/* line length */
		*p++ = l;	/* line count */
		*p++ = 0x101;	/* format: 1-byte in/out */
		*p++ = 0;	/* buffer notify */
		off_in += l * line;
		off_out += l * line;
		lines -= l;
	}
	return kick("m2mf", ms);
}

static void t_m2mf(void)
{
	const uint32_t len = 64 * 1024;
	uint32_t *src = fifo + XDR_SCRATCH / 4, *back = fifo + (XDR_SCRATCH + len) / 4;
	for (uint32_t i = 0; i < len / 4; i++) {
		src[i] = 0xa5000000u ^ (i * 2654435761u);
		back[i] = 0;
	}
	__sync_synchronize();

	int rc = m2mf_copy(DMA_XDR, info.ioif_base + XDR_SCRATCH, DMA_VRAM, VRAM_SCRATCH, len, 500);
	int bad = 0;
	for (uint32_t i = 0; i < len / 4; i++)
		bad += ((volatile uint32_t *)(vram + VRAM_SCRATCH))[i] != src[i];
	printf("[m2mf] XDR->VRAM 64 KB: %s, %d mismatched words\n", rc ? "TIMEOUT" : "done", bad);

	rc = m2mf_copy(DMA_VRAM, VRAM_SCRATCH, DMA_XDR, info.ioif_base + XDR_SCRATCH + len, len, 500);
	bad = 0;
	for (uint32_t i = 0; i < len / 4; i++)
		bad += back[i] != src[i];
	printf("[m2mf] VRAM->XDR 64 KB: %s, %d mismatched words\n", rc ? "TIMEOUT" : "done", bad);

	for (uint32_t mb = 8; mb <= 32; mb *= 4) {
		double t0 = now_ms();
		rc = m2mf_copy(DMA_VRAM, VRAM_SCRATCH, DMA_VRAM, VRAM_SCRATCH + 0x2000000, mb << 20, 2000);
		double t = now_ms() - t0;
		printf("[m2mf] VRAM->VRAM %u MB: %s in %.2f ms (%.0f MB/s incl. polling)\n", mb,
		       rc ? "TIMEOUT" : "done", t, mb / t * 1e3);
	}
	/* The whole XDR window (FIFO area + ps3fb's framebuffer), both directions; VRAM->XDR
	 * goes into the 512 KB FIFO area's scratch only, so the console's framebuffer survives */
	double t0 = now_ms();
	rc = m2mf_copy(DMA_XDR, info.ioif_base, DMA_VRAM, VRAM_SCRATCH, 8u << 20, 2000);
	double t = now_ms() - t0;
	printf("[m2mf] XDR->VRAM 8 MB: %s in %.2f ms (%.0f MB/s incl. polling)\n",
	       rc ? "TIMEOUT" : "done", t, 8 / t * 1e3);
	t0 = now_ms();
	for (int i = 0; i < 16 && !rc; i++)
		rc = m2mf_copy(DMA_VRAM, VRAM_SCRATCH, DMA_XDR, info.ioif_base + XDR_SCRATCH, 256 * 1024, 500);
	t = now_ms() - t0;
	printf("[m2mf] VRAM->XDR 16 x 256 KB: %s in %.2f ms (%.0f MB/s incl. polling)\n",
	       rc ? "TIMEOUT" : "done", t, 4 / t * 1e3);
}

static void t_cpu(void)
{
	const size_t len = 8u << 20;
	volatile uint64_t *v = (volatile uint64_t *)(vram + VRAM_SCRATCH);
	double t0 = now_ms();
	for (size_t i = 0; i < len / 8; i++)
		v[i] = i;
	double tw = now_ms() - t0;
	const size_t rlen = 1u << 20;
	uint64_t sum = 0;
	t0 = now_ms();
	for (size_t i = 0; i < rlen / 8; i++)
		sum += v[i];
	double tr = now_ms() - t0;
	uint8_t *xdr = malloc(len);
	t0 = now_ms();
	memcpy((void *)(vram + VRAM_SCRATCH), xdr, len);
	double tm = now_ms() - t0;
	free(xdr);
	printf("[cpu] VRAM write (8-byte stores) %.0f MB/s, memcpy %.0f MB/s, read %.1f MB/s (sum %llx)\n",
	       8 / tw * 1e3, 8 / tm * 1e3, 1 / tr * 1e3, (unsigned long long)sum);
}

static void t_fault(void)
{
	printf("[fault] sending method 0x1ffc (unused) on the 3D object...\n");
	if (begin()) return;
	OUT(SC_3D, 0x1ffc, 0xdeadbeef);
	int rc = kick("fault", 500);
	printf("  %s, exception flag=%u, ref=%08x (check dmesg)\n", rc ? "STALLED" : "ran through",
	       fifo[GPU_USER_DEBUG], ctrl->ref);
	int arg = 0;
	printf("  EXIT (ps3fb restore_state): %s\n", ioctl(fb, PS3GPU_IOCTL_EXIT, &arg) ? "FAILED" : "ok");
	printf("  ENTER again: %s\n", ioctl(fb, PS3GPU_IOCTL_ENTER, &arg) ? "FAILED" : "ok");
	fifo[GPU_USER_DEBUG] = 0;
	if (begin()) return;
	rc = kick("after recovery", 100);
	printf("  FIFO after recovery: %s\n", rc ? "FAIL" : "OK");
}

static const struct { const char *name; void (*fn)(void); int dflt; } tests[] = {
	{ "info", t_info, 1 }, { "ref", t_ref, 1 }, { "sema", t_sema, 1 },
	{ "report", t_report, 1 }, { "m2mf", t_m2mf, 1 }, { "cpu", t_cpu, 1 },
	{ "fault", t_fault, 0 },
};

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	fb = open("/dev/fb0", O_RDWR);
	if (fb < 0) { perror("/dev/fb0"); return 1; }
	if (ioctl(fb, PS3GPU_IOCTL_INFO, &info)) { perror("PS3GPU_IOCTL_INFO"); return 1; }
	fifo = map("/dev/ps3gpu_fifo", info.fifo_size_max);
	ctrl = map("/dev/ps3gpu_ctrl", info.ctrl_size);
	vram = map("/dev/ps3gpu_vram", info.video_size);
	dinfo = map("/dev/ps3gpu_driver", info.driver_size);
	reports = map("/dev/ps3gpu_reports", info.report_size[0]);

	int arg = 0;
	if (ioctl(fb, PS3GPU_IOCTL_ENTER, &arg)) { perror("PS3GPU_IOCTL_ENTER"); return 1; }
	for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
		int run = argc == 1 && tests[t].dflt;
		for (int a = 1; a < argc; a++)
			run |= !strcmp(argv[a], tests[t].name);
		if (run)
			tests[t].fn();
	}
	if (ioctl(fb, PS3GPU_IOCTL_EXIT, &arg)) {
		perror("PS3GPU_IOCTL_EXIT");
		failed = 1;
	}
	return failed;
}
