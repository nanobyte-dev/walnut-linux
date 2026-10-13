/*
 * rsx-clear: Phase 1 test. Takes over the ps3fb GPU FIFO and clears the visible framebuffer
 * in VRAM to a colour using the RSX 3D engine (object 0x31337000 on subchannel 0, bound by
 * ps3fb's reset program). Success = the screen turns that colour.
 *
 *   rsx-clear [ARGB hex colour, default ffff0000] [seconds to hold, default 5]
 *
 * Needs /dev/ps3gpu_fifo (minor 1) and /dev/ps3gpu_ctrl (minor 2) on ps3fb's major.
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

/* FIFO control block (struct gpu_fifo_ctrl in ps3fb.c) */
struct fifo_ctrl {
	uint8_t res[64];
	volatile uint32_t put, get, ref;
};

#define GPU_USER_DEBUG 1023		/* word in the reset area the IRQ handler sets on graph exception */
#define CMD_JMP(addr)  (0x20000000u | (addr))
#define MTH(count, subch, method) (((count) << 18) | ((subch) << 13) | (method))

/* NV40/RSX 3D (class 0x4097) methods, as in nouveau's nv30-40 3D headers and RPCS3 */
#define NV4097_SET_CONTEXT_DMA_COLOR_A    0x0194
#define NV4097_SET_SURFACE_CLIP_HORIZONTAL 0x0200
#define NV4097_SET_SURFACE_CLIP_VERTICAL   0x0204
#define NV4097_SET_SURFACE_FORMAT         0x0208
#define NV4097_SET_SURFACE_PITCH_A        0x020c
#define NV4097_SET_SURFACE_COLOR_AOFFSET  0x0210
#define NV4097_SET_SURFACE_COLOR_TARGET   0x0220
#define NV4097_SET_WINDOW_OFFSET          0x02b8
#define NV4097_SET_COLOR_MASK             0x0324
#define NV4097_SET_SCISSOR_HORIZONTAL     0x08c0
#define NV4097_SET_SCISSOR_VERTICAL       0x08c4
#define NV4097_SET_COLOR_CLEAR_VALUE      0x1d90
#define NV4097_CLEAR_SURFACE              0x1d94

#define DMA_LOCAL_VRAM 0xfeed0000u	/* context DMA handle for VRAM, as in ps3fb's reset program */

static void *map(const char *path, size_t len, int *fd_out)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); exit(1); }
	void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	*fd_out = fd;
	return p;
}

static int wait_idle(struct fifo_ctrl *c, const char *what)
{
	for (int i = 0; i < 2000; i++) {
		if (c->put == c->get)
			return 0;
		usleep(1000);
	}
	fprintf(stderr, "timeout waiting for %s: put=%08x get=%08x\n", what, c->put, c->get);
	return -1;
}

int main(int argc, char **argv)
{
	uint32_t colour = argc > 1 ? strtoul(argv[1], NULL, 16) : 0xffff0000;
	int hold = argc > 2 ? atoi(argv[2]) : 5;
	const uint32_t width = 1920, height = 1080, pitch = 1920 * 4, offset = 0;

	int fb = open("/dev/fb0", O_RDWR);
	if (fb < 0) { perror("/dev/fb0"); return 1; }
	struct ps3gpu_ioctl_info info;
	if (ioctl(fb, PS3GPU_IOCTL_INFO, &info)) { perror("PS3GPU_IOCTL_INFO"); return 1; }
	printf("ioif=%#x reset=%#x+%#x fifo=%#x+%#x (max %#x) ctrl=%#x vram=%#x\n",
	       info.ioif_base, info.reset_start, info.reset_size, info.fifo_start,
	       info.fifo_size, info.fifo_size_max, info.ctrl_size, info.video_size);

	int ffd, cfd;
	uint32_t *fifo = map("/dev/ps3gpu_fifo", info.fifo_size_max, &ffd);
	struct fifo_ctrl *ctrl = map("/dev/ps3gpu_ctrl", info.ctrl_size, &cfd);

	int arg = 0;
	if (ioctl(fb, PS3GPU_IOCTL_ENTER, &arg)) { perror("PS3GPU_IOCTL_ENTER"); return 1; }
	printf("took over FIFO: put=%08x get=%08x, exception flag=%u\n",
	       ctrl->put, ctrl->get, fifo[GPU_USER_DEBUG]);
	if (wait_idle(ctrl, "initial idle"))
		goto out;

	/* Start our commands at the beginning of the ring: jump there from wherever put is */
	uint32_t start = info.ioif_base + info.fifo_start;
	if (ctrl->put != start) {
		fifo[(ctrl->put - info.ioif_base) / 4] = CMD_JMP(start);
		__sync_synchronize();
		ctrl->put = start;
		if (wait_idle(ctrl, "jump to ring start"))
			goto out;
	}

	uint32_t *cmd = fifo + (start - info.ioif_base) / 4, *p = cmd;
#define OUT(m, v) do { *p++ = MTH(1, 0, (m)); *p++ = (v); } while (0)
	OUT(NV4097_SET_CONTEXT_DMA_COLOR_A, DMA_LOCAL_VRAM);
	OUT(NV4097_SET_SURFACE_CLIP_HORIZONTAL, width << 16);
	OUT(NV4097_SET_SURFACE_CLIP_VERTICAL, height << 16);
	OUT(NV4097_SET_SURFACE_FORMAT, 0x148);	/* A8R8G8B8 | Z24S8 << 5 | PITCH << 8 */
	OUT(NV4097_SET_SURFACE_PITCH_A, pitch);
	OUT(NV4097_SET_SURFACE_COLOR_AOFFSET, offset);
	OUT(NV4097_SET_SURFACE_COLOR_TARGET, 1);
	OUT(NV4097_SET_WINDOW_OFFSET, 0);
	OUT(NV4097_SET_SCISSOR_HORIZONTAL, width << 16);
	OUT(NV4097_SET_SCISSOR_VERTICAL, height << 16);
	OUT(NV4097_SET_COLOR_MASK, 0x01010101);
	OUT(NV4097_SET_COLOR_CLEAR_VALUE, colour);
	OUT(NV4097_CLEAR_SURFACE, 0xf0);	/* clear R, G, B, A */
#undef OUT
	__sync_synchronize();
	ctrl->put = start + (uint32_t)((p - cmd) * 4);
	printf("submitted %ld words, put=%08x\n", (long)(p - cmd), ctrl->put);

	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	int rc = wait_idle(ctrl, "clear");
	clock_gettime(CLOCK_MONOTONIC, &t1);
	printf("%s after %.3f ms, exception flag=%u\n", rc ? "NOT consumed" : "consumed",
	       (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6, fifo[GPU_USER_DEBUG]);
	sleep(hold);
out:
	if (ioctl(fb, PS3GPU_IOCTL_EXIT, &arg))
		perror("PS3GPU_IOCTL_EXIT");
	printf("handed FIFO back to ps3fb\n");
	return 0;
}
