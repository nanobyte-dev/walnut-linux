/*
 * rsx-tri: E12/E13, draws on the visible frame with raw 3D commands through ps3fb's FIFO.
 * Command and program words come from docs/RSX-PROGRAMS.md (section numbers below).
 *
 *   rsx-tri <stage> [seconds to hold, default 8]
 *
 * Stages, each adding one new thing (run them in this order; one may wedge the GPU):
 *   state     render state + viewport writes only (A.1.1, A.1.2)
 *   vp        + vertex program upload and fragment program select, FP_CONST (A.1.3, A.1.4)
 *   empty     + BEGIN_END triangles / stop with no vertices
 *   const     + immediate-mode triangle, solid magenta (FP_CONST)
 *   col       + colour-interpolated triangle (FP_COL)
 *   varray    triangle from a vertex array instead of immediate mode, FP_CONST (B.4)
 *   texconst  textured-quad setup with FP_CONST (texture unit set up, not sampled)
 *   tex       4x4 textured quad (FP_TEX) (B.2)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct ps3gpu_ioctl_info {
	uint32_t ioif_base, reset_start, reset_size, fifo_start, fifo_size, fifo_size_max;
	uint32_t gart_size, report_size[2], driver_size, ctrl_size, video_size;
};
#define PS3GPU_IOCTL_INFO  _IOR('r', 7, struct ps3gpu_ioctl_info)
#define PS3GPU_IOCTL_EXIT  _IOW('r', 8, int)
#define PS3GPU_IOCTL_ENTER _IOW('r', 15, int)

struct fifo_ctrl {
	uint8_t res[64];
	volatile uint32_t put, get, ref;
};

#define GPU_USER_DEBUG 1023
#define CMD_JMP(a) (0x20000000u | (a))
#define MTH(count, subch, method) (((count) << 18) | ((subch) << 13) | (method))

/* VRAM layout: frame at 0 (ps3fb's visible frame), our data far above it */
#define FP_OFF  0x9000000u		/* fragment program images, 64-byte aligned */
#define TEX_OFF 0x9010000u		/* 4x4 texture, 128-byte aligned */
#define VB_OFF  0x9020000u		/* vertex array */

static struct ps3gpu_ioctl_info info;
static uint32_t *fifo, *cmd, *p, token = 0x7e000000;
static struct fifo_ctrl *ctrl;
static volatile uint32_t *vram;

static void *map(const char *path, size_t len)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); exit(1); }
	void *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) { perror(path); exit(1); }
	return m;
}

static int wait_get(void)
{
	for (int i = 0; i < 20000; i++) {
		if (ctrl->put == ctrl->get)
			return 0;
		usleep(100);
	}
	fprintf(stderr, "timeout: put=%08x get=%08x\n", ctrl->put, ctrl->get);
	return -1;
}

static int begin(void)
{
	uint32_t start = info.ioif_base + info.fifo_start;
	if (wait_get())
		return -1;
	if (ctrl->put != start) {
		fifo[(ctrl->put - info.ioif_base) / 4] = CMD_JMP(start);
		__sync_synchronize();
		ctrl->put = start;
		if (wait_get())
			return -1;
	}
	p = cmd = fifo + (start - info.ioif_base) / 4;
	return 0;
}

#define OUT(m, v) do { *p++ = MTH(1, 0, (m)); *p++ = (v); } while (0)
static void words(const uint32_t *w, size_t n) { memcpy(p, w, n * 4); p += n; }
#define WORDS(...) do { static const uint32_t w_[] = { __VA_ARGS__ }; words(w_, sizeof(w_) / 4); } while (0)

/* Ends with SET_REFERENCE; returns once the engine has run everything (ref is a real fence, E11) */
static int kick(const char *what)
{
	OUT(0x50, ++token);
	__sync_synchronize();
	ctrl->put = info.ioif_base + info.fifo_start + (uint32_t)((p - cmd) * 4);
	for (int i = 0; i < 20000; i++) {
		if (ctrl->ref == token) {
			printf("%s: done, exception flag %u\n", what, fifo[GPU_USER_DEBUG]);
			return 0;
		}
		usleep(100);
	}
	fprintf(stderr, "%s: TIMEOUT put=%08x get=%08x ref=%08x exc=%u\n", what, ctrl->put,
		ctrl->get, ctrl->ref, fifo[GPU_USER_DEBUG]);
	return -1;
}

static void upload_fp(uint32_t off, const uint32_t *mem_words, size_t n)
{
	/* The images in docs/RSX-PROGRAMS.md are already in memory form (halfword-swapped, A.2) */
	for (size_t i = 0; i < n; i++)
		vram[off / 4 + i] = mem_words[i];
	__sync_synchronize();
}

/* A.2 / B.2: fragment program images, memory form */
static const uint32_t fp_col[] = {
	0x3e010100, 0xc8011c9d, 0xc8000001, 0xc8000001,
	0x00010001, 0x00000000, 0x00000000, 0x00000000,
};
static const uint32_t fp_const[] = {
	0x1e010100, 0xc8021c9d, 0xc8000001, 0xc8000001,
	0x00003f80, 0x00000000, 0x00003f80, 0x00003f80,
	0x00010001, 0x00000000, 0x00000000, 0x00000000,
};
static const uint32_t fp_tex[] = {
	0x9e011700, 0xc8011c9d, 0xc8000001, 0xc8003fe1,
	0x00010001, 0x00000000, 0x00000000, 0x00000000,
};
static const uint32_t tex4x4[16] = {
	0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff,
	0xff000000, 0xffffff00, 0xff00ffff, 0xffff00ff,
	0xff808080, 0xffff8000, 0xff8000ff, 0xff00ff80,
	0xff404040, 0xffc0c0c0, 0xff800000, 0xff008000,
};

/* rsx-clear's surface: the visible 1080p frame at VRAM 0, cleared to black */
static void surface_and_clear(void)
{
	OUT(0x194, 0xfeed0000);
	OUT(0x200, 1920 << 16);
	OUT(0x204, 1080 << 16);
	OUT(0x208, 0x148);
	OUT(0x20c, 7680);
	OUT(0x210, 0);
	OUT(0x220, 1);
	OUT(0x2b8, 0);
	OUT(0x8c0, 1920 << 16);
	OUT(0x8c4, 1080 << 16);
	OUT(0x324, 0x01010101);
	OUT(0x1d90, 0xff000000);
	OUT(0x1d94, 0xf0);
}

static void state_viewport(void)	/* A.1.1, A.1.2 */
{
	WORDS(0x00040a74, 0, 0x00040310, 0, 0x0004183c, 0, 0x00040304, 0, 0x00040348, 0,
	      0x00040374, 0, 0x00040368, 0x00001d01, 0x00081828, 0x00001b02, 0x00001b02,
	      0x00041d88, 0x00001438,
	      0x00080a00, 0x07800000, 0x04380000,
	      0x00080394, 0x00000000, 0x3f800000,
	      0x00200a20, 0x44700000, 0x44070000, 0x3f000000, 0, 0x44700000, 0xc4070000, 0x3f000000, 0,
	      0x00200a20, 0x44700000, 0x44070000, 0x3f000000, 0, 0x44700000, 0xc4070000, 0x3f000000, 0);
}

static void vp_colour(void)		/* A.1.3 */
{
	WORDS(0x00081e9c, 0, 0,
	      0x00200b80, 0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,
	                  0x401f9c6c, 0x0040030d, 0x8106c083, 0x6041ff85,
	      0x00041ff0, 0x00000009, 0x00041ff4, 0x00000025, 0x00041ef8, 0x0020ffff);
}

static void vp_texcoord(void)		/* B.2.1 */
{
	WORDS(0x00081e9c, 0, 0,
	      0x00200b80, 0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,
	                  0x401f9c6c, 0x0040080d, 0x8106c083, 0x6041ff9d,
	      0x00041ff0, 0x00000101, 0x00041ff4, 0x00004020, 0x00041ef8, 0x0020ffff);
}

static void fp_select(uint32_t off)	/* A.1.4 */
{
	OUT(0x8e4, off | 1);
	OUT(0x1d60, 0x02000440);
}

static void tri_immediate(void)		/* A.1.5 */
{
	WORDS(0x00041808, 0x00000005,
	      0x00101c30, 0x3f800000, 0, 0, 0x3f800000, 0x00101c00, 0, 0x3f000000, 0, 0x3f800000,
	      0x00101c30, 0, 0x3f800000, 0, 0x3f800000, 0x00101c00, 0xbf000000, 0xbf000000, 0, 0x3f800000,
	      0x00101c30, 0, 0, 0x3f800000, 0x3f800000, 0x00101c00, 0x3f000000, 0xbf000000, 0, 0x3f800000,
	      0x00041808, 0x00000000);
}

static void tri_varray(void)		/* B.4: {x,y,z,w, r,g,b,a} x3 at VB_OFF */
{
	static const float vb[24] = {
		0, 0.5f, 0, 1, 1, 0, 0, 1,
		-0.5f, -0.5f, 0, 1, 0, 1, 0, 1,
		0.5f, -0.5f, 0, 1, 0, 0, 1, 1,
	};
	/* word stores: memcpy into the uncached VRAM mapping may use dcbz and fault */
	for (int i = 0; i < 24; i++) {
		uint32_t u;
		memcpy(&u, &vb[i], 4);
		vram[VB_OFF / 4 + i] = u;
	}
	__sync_synchronize();
	WORDS(0x00041740, 0x00002042, 0x00041680, VB_OFF, 0x0004174c, 0x00002042,
	      0x0004168c, VB_OFF + 16, 0x400c1714, 0, 0, 0,
	      0x00041808, 0x00000005, 0x00041814, 0x02000000, 0x00041808, 0x00000000,
	      0x00041740, 0x00000002, 0x0004174c, 0x00000002);
}

static void texture_unit(void)		/* B.2.2 */
{
	for (int i = 0; i < 16; i++)
		vram[TEX_OFF / 4 + i] = tex4x4[i];
	__sync_synchronize();
	WORDS(0x00041fd8, 0x00000001, 0x00081a00, TEX_OFF, 0x0001a529, 0x00041a10, 0x0000aae4,
	      0x00041a18, 0x00040004, 0x00041840, 0x00100010, 0x00041a0c, 0x80060000,
	      0x00041a14, 0x01012000, 0x00041a08, 0x10030303, 0x00041a1c, 0x00000000,
	      0x00040b40, 0x00000000);
}

static void quad_immediate(void)	/* B.2.4 */
{
	WORDS(0x00041808, 0x00000005,
	      0x00101c80, 0, 0, 0, 0x3f800000, 0x00101c00, 0xbf100000, 0x3f000000, 0, 0x3f800000,
	      0x00101c80, 0, 0x3f800000, 0, 0x3f800000, 0x00101c00, 0xbf100000, 0xbf000000, 0, 0x3f800000,
	      0x00101c80, 0x3f800000, 0, 0, 0x3f800000, 0x00101c00, 0x3f100000, 0x3f000000, 0, 0x3f800000,
	      0x00101c80, 0x3f800000, 0, 0, 0x3f800000, 0x00101c00, 0x3f100000, 0x3f000000, 0, 0x3f800000,
	      0x00101c80, 0, 0x3f800000, 0, 0x3f800000, 0x00101c00, 0xbf100000, 0xbf000000, 0, 0x3f800000,
	      0x00101c80, 0x3f800000, 0x3f800000, 0, 0x3f800000, 0x00101c00, 0x3f100000, 0xbf000000, 0, 0x3f800000,
	      0x00041808, 0x00000000);
}

int main(int argc, char **argv)
{
	static const char *stages[] = { "state", "vp", "empty", "const", "col", "varray",
					 "texconst", "tex" };
	int stage = -1, hold = argc > 2 ? atoi(argv[2]) : 8;

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (int i = 0; argc > 1 && i < (int)(sizeof(stages) / sizeof(stages[0])); i++)
		if (!strcmp(argv[1], stages[i]))
			stage = i;
	if (stage < 0) {
		fprintf(stderr, "usage: %s state|vp|empty|const|col|varray|texconst|tex [hold s]\n", argv[0]);
		return 2;
	}

	int fb = open("/dev/fb0", O_RDWR);
	if (fb < 0 || ioctl(fb, PS3GPU_IOCTL_INFO, &info)) { perror("ps3fb"); return 1; }
	fifo = map("/dev/ps3gpu_fifo", info.fifo_size_max);
	ctrl = map("/dev/ps3gpu_ctrl", info.ctrl_size);
	vram = map("/dev/ps3gpu_vram", info.video_size);
	upload_fp(FP_OFF, fp_const, 12);
	upload_fp(FP_OFF + 0x100, fp_col, 8);
	upload_fp(FP_OFF + 0x200, fp_tex, 8);

	int arg = 0, rc = 1;
	if (ioctl(fb, PS3GPU_IOCTL_ENTER, &arg)) { perror("PS3GPU_IOCTL_ENTER"); return 1; }
	/* ENTER stops ps3fb's vsync blit task, but a blit already under way can still land on
	 * frame 0 after our clear: give it a few frames */
	usleep(200000);
	if (begin())
		goto out;
	surface_and_clear();
	if (kick("clear to black"))
		goto out;

	if (begin())
		goto out;
	state_viewport();
	if (stage >= 1 && stage <= 5) {
		vp_colour();
		fp_select(stage == 4 ? FP_OFF + 0x100 : FP_OFF);
	}
	if (stage == 2)
		WORDS(0x00041808, 0x00000005, 0x00041808, 0x00000000);
	if (stage == 3 || stage == 4)
		tri_immediate();
	if (stage == 5)
		tri_varray();
	if (stage >= 6) {
		vp_texcoord();
		texture_unit();
		fp_select(stage == 7 ? FP_OFF + 0x200 : FP_OFF);
		quad_immediate();
	}
	rc = kick(stages[stage]);
	sleep(hold);
out:
	if (ioctl(fb, PS3GPU_IOCTL_EXIT, &arg)) {
		perror("PS3GPU_IOCTL_EXIT");
		rc = 1;
	}
	return rc;
}
