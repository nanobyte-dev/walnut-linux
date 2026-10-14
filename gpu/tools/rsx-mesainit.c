/*
 * rsx-mesainit: E15 on hardware. Replays Mesa nv30_screen_create's init stream (chipset 0x47,
 * handles patched to the PS3's, Mesa's subchannel layout 3D=7 M2MF=2 SF2D=3 SSWZ=4 SIFM=5)
 * through ps3fb's FIFO, in the batches of docs/MESA-METHODS.md section 3.
 *
 *   rsx-mesainit <step> [step ...]
 *
 * Steps (run in this order; each is followed by a SET_REFERENCE fence):
 *   b1     bind 3D on subch 7, the 13 DMA slots 0x180..
 *   safe   B2 values that match libgcm: COLOR2/3 DMA, 0x1fc4..0x1fd4 routing, 0x3b0
 *   v1450  0x1450 = 4           (libgcm 0x80003)
 *   v1ea4  0x1ea4 x3 = 0x10, 0x01000100, 0xff800006  (libgcm third word 0xff000002)
 *   v1ef8  0x1ef8 = 0x0020ffff  (no libgcm default)
 *   v1d64  0x1d64 = 0x01d300d4  (libgcm 0x02000000)
 *   b3..b6 bind M2MF / SF2D / SSWZ / SIFM on Mesa's subchannels with the notifier DMA
 *   clear  a 3D clear of the visible frame through subch 7 (proves 3D still works after)
 * ps3fb's EXIT re-runs its reset program afterwards, restoring its own subchannel bindings.
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
#define S3D 7

static struct ps3gpu_ioctl_info info;
static uint32_t *fifo, *cmd, *p, token = 0x7e000000;
static struct fifo_ctrl *ctrl;

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

static void mth(int sc, uint32_t m, int n, const uint32_t *d)
{
	*p++ = MTH(n, sc, m);
	for (int i = 0; i < n; i++)
		*p++ = d[i];
}
#define M(sc, m, ...) do { static const uint32_t d_[] = { __VA_ARGS__ }; \
	mth((sc), (m), sizeof(d_) / 4, d_); } while (0)

static int kick(const char *what)
{
	M(S3D, 0x0050, 0);		/* placeholder, value patched below */
	p[-1] = ++token;
	__sync_synchronize();
	ctrl->put = info.ioif_base + info.fifo_start + (uint32_t)((p - cmd) * 4);
	for (int i = 0; i < 20000; i++) {
		if (ctrl->ref == token) {
			printf("%-6s ok, exception flag %u\n", what, fifo[GPU_USER_DEBUG]);
			return 0;
		}
		usleep(100);
	}
	fprintf(stderr, "%-6s TIMEOUT put=%08x get=%08x ref=%08x exc=%u\n", what, ctrl->put,
		ctrl->get, ctrl->ref, fifo[GPU_USER_DEBUG]);
	return -1;
}

static void step(const char *s)
{
	if (!strcmp(s, "b1")) {
		M(S3D, 0x0000, 0x31337000);
		M(S3D, 0x0180, 0x66604200, 0xfeed0000, 0xfeed0001, 0xfeed0000, 0x0, 0xfeed0000,
		  0xfeed0000, 0xfeed0000, 0xfeed0001, 0x66606660, 0x66626660, 0x0, 0x0);
	} else if (!strcmp(s, "safe")) {
		M(S3D, 0x01b4, 0xfeed0000, 0xfeed0000);
		M(S3D, 0x1fc4, 0x06144321);
		M(S3D, 0x1fc8, 0xedcba987, 0x0000006f);
		M(S3D, 0x1fd0, 0x00171615);
		M(S3D, 0x1fd4, 0x001b1a19);
		M(S3D, 0x03b0, 0x00100000);
	} else if (!strcmp(s, "v1450")) {
		M(S3D, 0x1450, 0x00000004);
	} else if (!strcmp(s, "v1ea4")) {
		M(S3D, 0x1ea4, 0x00000010, 0x01000100, 0xff800006);
	} else if (!strcmp(s, "v1ef8")) {
		M(S3D, 0x1ef8, 0x0020ffff);
	} else if (!strcmp(s, "v1d64")) {
		M(S3D, 0x1d64, 0x01d300d4);
	} else if (!strcmp(s, "b3")) {
		M(2, 0x0000, 0x31337303);
		M(2, 0x0180, 0x66604200);
	} else if (!strcmp(s, "b4")) {
		M(3, 0x0000, 0x313371c3);
		M(3, 0x0180, 0x66604200);
	} else if (!strcmp(s, "b5")) {
		M(4, 0x0000, 0x31337a73);
		M(4, 0x0180, 0x66604200);
	} else if (!strcmp(s, "b6")) {
		M(5, 0x0000, 0x3137af00);
		M(5, 0x0180, 0x66604200);
		M(5, 0x02fc, 0x00000001);
	} else if (!strcmp(s, "clear")) {
		M(S3D, 0x0194, 0xfeed0000);
		M(S3D, 0x0200, 1920 << 16, 1080 << 16, 0x148, 7680, 0);
		M(S3D, 0x0220, 1);
		M(S3D, 0x02b8, 0);
		M(S3D, 0x08c0, 1920 << 16, 1080 << 16);
		M(S3D, 0x0324, 0x01010101);
		M(S3D, 0x1d90, 0xff0000ff);	/* blue */
		M(S3D, 0x1d94, 0xf0);
	} else {
		fprintf(stderr, "unknown step %s\n", s);
		exit(2);
	}
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	int fb = open("/dev/fb0", O_RDWR);
	if (fb < 0 || ioctl(fb, PS3GPU_IOCTL_INFO, &info)) { perror("ps3fb"); return 1; }
	fifo = map("/dev/ps3gpu_fifo", info.fifo_size_max);
	ctrl = map("/dev/ps3gpu_ctrl", info.ctrl_size);

	int arg = 0, rc = 0;
	if (ioctl(fb, PS3GPU_IOCTL_ENTER, &arg)) { perror("PS3GPU_IOCTL_ENTER"); return 1; }
	usleep(200000);
	for (int a = 1; a < argc && !rc; a++) {
		if (begin()) { rc = 1; break; }
		step(argv[a]);
		rc = kick(argv[a]) ? 1 : 0;
	}
	if (!rc)
		sleep(3);
	if (ioctl(fb, PS3GPU_IOCTL_EXIT, &arg)) {
		perror("PS3GPU_IOCTL_EXIT");
		rc = 1;
	}
	return rc;
}
