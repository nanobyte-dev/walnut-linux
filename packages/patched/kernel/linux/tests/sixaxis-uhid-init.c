/*
 * sixaxis-uhid-init: PID 1 of the QEMU guest of packages/patched/kernel/linux/tests/sixaxis-idle-axes.sh. Loads
 * /hid-sony.ko, creates a DS3 over uhid (USB bus, 054c:0268, the real descriptor, from hid-tools'
 * PS3Controller), answers hid-sony's feature requests, and prints the stick axes before any input
 * report (the not-yet-activated pad), then after two reports. Freestanding: no libc, x86_64
 * syscalls only.
 */
#include <linux/types.h>
#include <linux/uhid.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <linux/reboot.h>

#define O_RDONLY 0
#define O_RDWR 2
#define O_NONBLOCK 04000
#define O_CLOEXEC 02000000

static long sc6(long n, long a, long b, long c, long d, long e, long f)
{
	long r;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	__asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return r;
}
#define S3(n, a, b, c) sc6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define sys_read(f, b, l) S3(0, f, b, l)
#define sys_write(f, b, l) S3(1, f, b, l)
#define sys_open(p, fl) S3(2, p, fl, 0)
#define sys_close(f) S3(3, f, 0, 0)
#define sys_poll(p, n, t) S3(7, p, n, t)
#define sys_ioctl(f, r, a) S3(16, f, r, a)
#define sys_mkdir(p) S3(83, p, 0755, 0)

struct pollfd_ { int fd; short events, revents; };

static int con = -1;
static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sys_write(con, s, slen(s)); }
static void outi(long v)
{
	char b[24]; int i = 23; int neg = v < 0; unsigned long u = neg ? -v : v;
	b[i] = 0;
	do { b[--i] = '0' + u % 10; u /= 10; } while (u);
	if (neg) b[--i] = '-';
	out(b + i);
}
static void *mset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = c; return d; }
void *memset(void *d, int c, unsigned long n) { return mset(d, c, n); }
void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

static void halt(void)
{
	sc6(169, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, 0, 0, 0);
	for (;;);
}

static const __u8 rdesc[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02, 0x85, 0x01, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00,
	0x26, 0xff, 0x00, 0x81, 0x03, 0x75, 0x01, 0x95, 0x13, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
	0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x13, 0x81, 0x02, 0x75, 0x01, 0x95, 0x0d, 0x06, 0x00, 0xff,
	0x81, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x75, 0x08, 0x95,
	0x04, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
	0xc0, 0x05, 0x01, 0x75, 0x08, 0x95, 0x27, 0x09, 0x01, 0x81, 0x02, 0x75, 0x08, 0x95, 0x30, 0x09,
	0x01, 0x91, 0x02, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0x02,
	0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0xee, 0x75, 0x08, 0x95,
	0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0xef, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
	0xb1, 0x02, 0xc0, 0xc0,
};

static struct uhid_event ev;
static int uhid;

/* answer whatever hid-sony asks for; returns after a quiet ms_timeout */
static void serve(int ms_timeout)
{
	struct pollfd_ p = { uhid, 1, 0 };

	while (sys_poll(&p, 1, ms_timeout) > 0) {
		if (sys_read(uhid, &ev, sizeof(ev)) <= 0)
			continue;
		if (ev.type == UHID_GET_REPORT) {
			__u32 id = ev.u.get_report.id;
			__u8 rnum = ev.u.get_report.rnum;
			static const __u8 f2[17] = { 0xf2, 0xff, 0xff, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc,
						     0x00, 0x03, 0x40, 0x80, 0x18, 0x01, 0x8a };
			static const __u8 f5[8] = { 0x01, 0x00, 0x18, 0x5e, 0x0f, 0x71, 0xa4, 0xbb };
			mset(&ev, 0, sizeof(ev));
			ev.type = UHID_GET_REPORT_REPLY;
			ev.u.get_report_reply.id = id;
			if (rnum == 0xf2) {
				ev.u.get_report_reply.size = sizeof(f2);
				memcpy(ev.u.get_report_reply.data, f2, sizeof(f2));
			} else if (rnum == 0xf5) {
				ev.u.get_report_reply.size = sizeof(f5);
				memcpy(ev.u.get_report_reply.data, f5, sizeof(f5));
			} else {
				ev.u.get_report_reply.err = 5;
			}
			sys_write(uhid, &ev, sizeof(ev));
		} else if (ev.type == UHID_SET_REPORT) {
			__u32 id = ev.u.set_report.id;
			mset(&ev, 0, sizeof(ev));
			ev.type = UHID_SET_REPORT_REPLY;
			ev.u.set_report_reply.id = id;
			sys_write(uhid, &ev, sizeof(ev));
		}
	}
}

static int find_pad(void)
{
	char path[] = "/dev/input/eventX", name[128];
	int i, fd;

	for (i = 0; i < 10; i++) {
		path[16] = '0' + i;
		fd = sys_open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		mset(name, 0, sizeof(name));
		if (sys_ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) > 0 &&
		    seq(name, "Sony PLAYSTATION(R)3 Controller")) {
			out("pad: ");
			out(path);
			out(" \"");
			out(name);
			out("\"\n");
			return fd;
		}
		sys_close(fd);
	}
	return -1;
}

static const int codes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ };
static const char *const names[] = { "ABS_X", "ABS_Y", "ABS_RX", "ABS_RY", "ABS_Z", "ABS_RZ" };

static void dump_abs(int fd, int *vals)
{
	struct input_absinfo a;
	int i;

	for (i = 0; i < 6; i++) {
		mset(&a, 0, sizeof(a));
		if (sys_ioctl(fd, EVIOCGABS(codes[i]), &a) < 0) {
			out("  "); out(names[i]); out(" missing\n");
			vals[i] = -999;
			continue;
		}
		out("  "); out(names[i]); out(" value="); outi(a.value);
		out(" min="); outi(a.minimum); out(" max="); outi(a.maximum);
		out(" flat="); outi(a.flat); out("\n");
		vals[i] = a.value;
	}
}

static void dump_js(void)
{
	struct js_event e;
	int fd = sys_open("/dev/input/js0", O_RDONLY | O_NONBLOCK);

	if (fd < 0) { out("  js0 missing\n"); return; }
	out("  js0 init axes:");
	while (sys_read(fd, &e, sizeof(e)) == sizeof(e))
		if (e.type == (JS_EVENT_INIT | JS_EVENT_AXIS)) {
			out(" a"); outi(e.number); out("="); outi(e.value);
		}
	out("\n");
	sys_close(fd);
}

static void send_report(int lx, int ly, int rx, int ry)
{
	mset(&ev, 0, sizeof(ev));
	ev.type = UHID_INPUT2;
	ev.u.input2.size = 49;
	ev.u.input2.data[0] = 0x01;
	ev.u.input2.data[6] = lx;
	ev.u.input2.data[7] = ly;
	ev.u.input2.data[8] = rx;
	ev.u.input2.data[9] = ry;
	ev.u.input2.data[30] = 0x05;	/* battery full */
	ev.u.input2.data[41] = 0x01;	/* accelerometers near rest (swapped by hid-sony) */
	ev.u.input2.data[42] = 0xff;
	ev.u.input2.data[43] = 0x01;
	ev.u.input2.data[44] = 0xff;
	ev.u.input2.data[45] = 0x01;
	ev.u.input2.data[46] = 0xff;
	sys_write(uhid, &ev, sizeof(ev));
	serve(200);
}

__asm__(".globl _start\n_start:\n\txor %ebp, %ebp\n\tand $-16, %rsp\n\tcall ds3_main\n\thlt\n");

void ds3_main(void)
{
	int fd, i, pad = -1, v[6], ok;

	sys_mkdir("/dev");
	sys_mkdir("/sys");
	sc6(165, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0, 0, 0);
	con = sys_open("/dev/console", O_RDWR);
	out("ds3init: loading /hid-sony.ko\n");
	fd = sys_open("/hid-sony.ko", O_RDONLY | O_CLOEXEC);
	if (fd < 0 || sc6(313, fd, (long)"", 0, 0, 0, 0) < 0) {
		out("ds3init: FAIL module load\n");
		halt();
	}
	uhid = sys_open("/dev/uhid", O_RDWR | O_CLOEXEC);
	if (uhid < 0) { out("ds3init: FAIL /dev/uhid\n"); halt(); }
	mset(&ev, 0, sizeof(ev));
	ev.type = UHID_CREATE2;
	memcpy(ev.u.create2.name, "Sony PLAYSTATION(R)3 Controller", 32);
	memcpy(ev.u.create2.phys, "usb-0000:00:1d.0-1/input0", 26);
	ev.u.create2.rd_size = sizeof(rdesc);
	ev.u.create2.bus = BUS_USB;
	ev.u.create2.vendor = 0x054c;
	ev.u.create2.product = 0x0268;
	ev.u.create2.version = 0x0100;
	memcpy(ev.u.create2.rd_data, rdesc, sizeof(rdesc));
	if (sys_write(uhid, &ev, sizeof(ev)) < 0) { out("ds3init: FAIL create\n"); halt(); }

	for (i = 0; i < 50 && pad < 0; i++) {
		serve(100);
		pad = find_pad();
	}
	if (pad < 0) { out("ds3init: FAIL no evdev node for the pad\n"); halt(); }
	serve(300);

	out("before any input report (DS3 not activated):\n");
	dump_abs(pad, v);
	dump_js();
	ok = v[0] == 128 && v[1] == 128 && v[2] == 128 && v[3] == 128 && v[4] == 0 && v[5] == 0;
	out(ok ? "RESULT idle: sticks centered (PASS)\n" : "RESULT idle: sticks NOT centered (FAIL)\n");

	send_report(128, 128, 128, 128);
	send_report(30, 220, 0, 255);
	out("after reports (30,220,0,255):\n");
	dump_abs(pad, v);
	ok = v[0] == 30 && v[1] == 220 && v[2] == 0 && v[3] == 255;
	out(ok ? "RESULT reports: values follow (PASS)\n" : "RESULT reports: wrong values (FAIL)\n");
	halt();
}
