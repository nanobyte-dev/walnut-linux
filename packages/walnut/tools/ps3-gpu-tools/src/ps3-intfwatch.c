/*
 * ps3-intfwatch: the interference detector of games/bench (docs/STALLS.md "Interference"). A run
 * somebody disturbed doesn't count: a key or controller button pressed, a stick or the mouse
 * moved for real, a VT switch, a device plugged in or out, an SSH login or a command typed on the
 * PS3 while the game ran. ps3-bench.sh starts it as root next to every run (x and run) and stops
 * it with the stop file; the run's interference.log gets one line per event and a summary that
 * ps3-bench.sh copies into bench.txt (interfered=, interference=) and the run's result.
 *
 *   ps3-intfwatch -o LOG [-s STOPFILE] [-P ROOTPID] [-e ENV=VALUE] [-g PIDFILE] [-x XVT] [-H VT]
 *                 [-X CMDSUBSTR]... [-I NAME]... [-q MS] [-d INPUTDIR] [-L SESSIONDIR] [-t VTFILE]
 *                 [-k KMSG] [-S CGSUBSTR]
 *   ps3-intfwatch -r SPEC [-o LOG]          replay recorded input events (host tests)
 *
 *   -o LOG        the log (default stdout); -s STOPFILE stop once it exists (SIGTERM/SIGINT too)
 *   -P ROOTPID    the harness (ps3-bench.sh's $$): it, its descendants and its process group
 *                 are the run's own processes; its ancestors (run.sh, the SSH session that
 *                 started it) are the harness too, but what they start meanwhile is not
 *   -e ENV=VALUE  a process whose environment holds this (BENCH_DIR=<run dir>) is the run's own
 *                 too, also when its parent is gone (a daemon a game forks)
 *   -g PIDFILE    the game's pid (client.sh's .pid): the X server's switch back to the console
 *                 counts as expected once the game is gone
 *   -x XVT        the run's X server is on this VT (x mode: 1): the first switch to it and the
 *                 switch back to the home VT after the game are expected
 *   -H VT         the home VT: the bench console (ps3-bench.sh BENCH_VT, 8) where the X server
 *                 returns (default: the VT active at the start)
 *   -X CMDSUBSTR  a process whose command line holds this, and what it starts, is exempt (but
 *                 recorded). Built in: tools/bench-cam.sh's watcher loop (its command line has
 *                 "pgrep -x run.sh" and "/dev/shm/ps3-bench-"), and so is its SSH session
 *   -I NAME       an input device whose name holds NAME is not watched
 *   -q MS         the /proc scan period (default 250)
 *   -d, -L, -t, -k, -S  other places for the host tests: /dev/input, /run/systemd/sessions,
 *                 /sys/class/tty/tty0/active, /dev/kmsg; the cgroup substring of a login
 *                 session (default "/session-")
 *
 * What counts (each a "!" line):
 *   input   every key press (value 1; repeats and releases are not counted), controller and
 *           mouse button, wheel step; a stick or trigger leaving its rest position by a quarter
 *           of its range (DualShock 3 jitter is a few units of 255: ignored), counted once per
 *           excursion (re-armed within half that); a hat (d-pad axis) at any non-zero value;
 *           mouse motion only when deliberate: a burst (events less than 0.5 s apart) of 2 s or
 *           more, or 1500 counts within 10 s; a bump is recorded but doesn't count (the user,
 *           2026-10-18). Accelerometers (INPUT_PROP_ACCELEROMETER: the DS3's motion sensors)
 *           are not watched. Key codes of keyboard keys are not logged, only that one was
 *           pressed (no key logger); button codes are
 *   devices an input device that appears or goes away (a DS3 switched on over Bluetooth or
 *           plugged in), kernel lines of a USB device connected or disconnected. Not ours:
 *           uinput devices (sysfs /devices/virtual/input/) whose /dev/uinput is held by
 *           evemu-device, the bench's virtual keyboards (they carry the real receiver's name).
 *           XTEST (xkeys.py) makes no kernel input event at all
 *   VT      a change of /sys/class/tty/tty0/active other than the X server's own two
 *   logins  a new logind session (every SSH connection, also `ssh ps3 cmd`), a new process in a
 *           login session that isn't the run's own or exempt, a process of a login session that
 *           existed before and used 1 s of CPU or more during the run
 * Recorded, not counted ("-" lines): system services' and the user manager's new processes,
 *   mouse bumps, kernel input/HID/Bluetooth lines, exempt processes, the run's own devices.
 * Not seen: processes that live less than one scan period outside a new session (every SSH
 *   login is seen through its session), the PS3's power and reset buttons (the System Manager
 *   sends init SIGINT: the reboot ends the run and its /dev/shm directory), anything before the
 *   detector starts (just before the job) or after the stop.
 * The kernel has no proc connector (CONFIG_PROC_EVENTS), hence the /proc scan: only the
 * directory is read each period, a new pid's stat, cgroup, command line (and environment, when
 * not the run's own) once.
 *
 * Log: "# ..." header, device and summary lines; event lines "<s since start> <epoch> ! KIND ..."
 * (counts) or "- KIND ..." (recorded). The end: "# interfered=0|1", "# interference: <summary>",
 * "# info: <recorded>", "# self cpu=<s>".
 *
 * Replay (-r SPEC): a text file of "device NAME", "prop accel", "rel", "abs CODE MIN MAX VALUE
 * FLAT" and "events FILE be|le|native" lines; each events file is a stream of 64-bit struct
 * input_event records (u64 sec, u64 usec, u16 type, u16 code, s32 value: 24 bytes) of the given
 * byte order, fed to the last device's filter with the events' own times.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if __SIZEOF_LONG__ == 8
_Static_assert(sizeof(struct input_event) == 24, "64-bit struct input_event is 24 bytes");
#endif

#define MAXDEV 32
#define NBITS(x) ((((x) - 1) / (8 * sizeof(long))) + 1)
#define TESTBIT(b, a) (((a)[(b) / (8 * sizeof(long))] >> ((b) % (8 * sizeof(long)))) & 1)

#define BURST_GAP 0.5		/* mouse events closer than this are one burst */
#define BURST_LONG 2.0		/* a burst this long is deliberate */
#define BURST_DIST 1500		/* counts within BURST_WIN s: deliberate */
#define BURST_WIN 10.0
#define BUSY_TICKS_S 1.0	/* CPU seconds of an older login-session process: counted */
#define MAXLINES 100		/* event lines per device and kind; counts go on */

static FILE *out;
static double t_start;
static int replaying;
static volatile sig_atomic_t stop_sig;

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void ev_line(double t, int flag, const char *fmt, ...)
{
	va_list ap;
	fprintf(out, "%.3f %.3f %c ", t - t_start, t, flag ? '!' : '-');
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);
}

/* --- the summary ------------------------------------------------------------------------- */
static struct {
	long keys, cbuttons, mbuttons, wheel, sticks, hats, drops, mouse_deliberate, mouse_bumps;
	long mouse_bump_dist, dev_add, dev_del, usb, vt, sessions, procs, busy;
	long sys_procs, usr_procs, other_procs, exempt_procs, vanished, kinfo, motion_ignored;
	char vt_text[256], dev_text[256], usb_text[256], sess_text[256], proc_text[256], busy_text[256];
} S;

static void addtext(char *buf, size_t n, const char *fmt, ...)
{
	size_t l = strlen(buf);
	va_list ap;
	if (l + 8 >= n)
		return;
	if (l) {
		buf[l++] = ',';
		buf[l++] = ' ';
		buf[l] = 0;
	}
	va_start(ap, fmt);
	vsnprintf(buf + l, n - l, fmt, ap);
	va_end(ap);
	if (strlen(buf) >= n - 1)	/* cut: mark it */
		strcpy(buf + n - 5, "...");
}

/* --- input devices --------------------------------------------------------------------- */
struct axis {
	int valid, min, max, flat, rest, thr, armed, hat;
};
struct dev {
	int fd, used, ours, accel, mouse;
	char node[32], name[128], phys[64], sys[256], why[96];
	struct axis ax[ABS_CNT];
	double b_start, b_last;	/* the open mouse burst */
	long b_dist;
	int b_open;
	double hist_t[32];	/* recent bursts: end time, distance */
	long hist_d[32];
	int hist_n;
	long lines;
};
static struct dev devs[MAXDEV];
static const char *ignore_names[16];
static int n_ignore;

static int mouse_button(int code) { return code >= BTN_MOUSE && code < BTN_JOYSTICK; }
static int ctl_button(int code)
{
	return (code >= BTN_MISC && code < BTN_MOUSE) || (code >= BTN_JOYSTICK && code < BTN_DIGI) ||
		(code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT) || code >= BTN_TRIGGER_HAPPY;
}

static void burst_close(struct dev *d)
{
	double dur = d->b_last - d->b_start, t = d->b_last;
	long win = d->b_dist;
	int i;
	if (!d->b_open)
		return;
	d->b_open = 0;
	for (i = 0; i < d->hist_n; i++)
		if (t - d->hist_t[i] <= BURST_WIN)
			win += d->hist_d[i];
	if (d->hist_n == 32) {
		memmove(d->hist_t, d->hist_t + 1, 31 * sizeof(double));
		memmove(d->hist_d, d->hist_d + 1, 31 * sizeof(long));
		d->hist_n--;
	}
	d->hist_t[d->hist_n] = t;
	d->hist_d[d->hist_n++] = d->b_dist;
	if (dur >= BURST_LONG || win >= BURST_DIST) {
		S.mouse_deliberate++;
		ev_line(t, 1, "MOUSE dev=\"%s\" moved %.1f s, %ld counts (%ld within %.0f s): deliberate",
			d->name, dur, d->b_dist, win, BURST_WIN);
	} else {
		S.mouse_bumps++;
		S.mouse_bump_dist += d->b_dist;
		ev_line(t, 0, "MOUSE dev=\"%s\" bump %.2f s, %ld counts", d->name, dur, d->b_dist);
	}
	d->b_dist = 0;
}

/* one event through the filter; t is the event's own time */
static void event(struct dev *d, double t, unsigned type, unsigned code, int value)
{
	int log = d->lines < MAXLINES;
	if (type == EV_KEY) {
		if (value != 1)
			return;
		d->lines++;
		if (mouse_button(code)) {
			S.mbuttons++;
			if (log) ev_line(t, 1, "MOUSEBTN dev=\"%s\" code=0x%x", d->name, code);
		} else if (ctl_button(code)) {
			S.cbuttons++;
			if (log) ev_line(t, 1, "BUTTON dev=\"%s\" code=0x%x", d->name, code);
		} else {
			S.keys++;	/* no key code: this is not a key logger */
			if (log) ev_line(t, 1, "KEY dev=\"%s\"", d->name);
		}
	} else if (type == EV_REL) {
		if (code == REL_WHEEL || code == REL_HWHEEL || code == REL_WHEEL_HI_RES || code == REL_HWHEEL_HI_RES) {
			if (code == REL_WHEEL_HI_RES || code == REL_HWHEEL_HI_RES)
				return;	/* the same step as REL_WHEEL */
			S.wheel++;
			d->lines++;
			if (log) ev_line(t, 1, "WHEEL dev=\"%s\" %d", d->name, value);
			return;
		}
		if (code != REL_X && code != REL_Y)
			return;
		if (d->b_open && t - d->b_last > BURST_GAP)
			burst_close(d);
		if (!d->b_open) {
			d->b_open = 1;
			d->b_start = t;
		}
		d->b_last = t;
		d->b_dist += value < 0 ? -value : value;
	} else if (type == EV_ABS && code < ABS_CNT) {
		struct axis *a = &d->ax[code];
		int dv;
		if (d->accel) {
			S.motion_ignored++;
			return;
		}
		if (!a->valid)
			return;
		dv = value - a->rest;
		if (dv < 0)
			dv = -dv;
		if (a->armed && dv >= a->thr) {
			a->armed = 0;
			d->lines++;
			if (a->hat) S.hats++; else S.sticks++;
			if (log) ev_line(t, 1, "%s dev=\"%s\" axis=0x%x value=%d rest=%d range=%d..%d",
				a->hat ? "HAT" : "STICK", d->name, code, value, a->rest, a->min, a->max);
		} else if (!a->armed && dv <= a->thr / 2) {
			a->armed = 1;
		}
	} else if (type == EV_SYN && code == SYN_DROPPED) {
		S.drops++;
		ev_line(t, 1, "DROPPED dev=\"%s\" (the kernel's buffer overflowed: a flood of events)", d->name);
	}
}

static void axis_setup(struct axis *a, int code, int min, int max, int value, int flat)
{
	memset(a, 0, sizeof(*a));
	if (max <= min)
		return;
	a->valid = a->armed = 1;
	a->min = min;
	a->max = max;
	a->flat = flat;
	if (code >= ABS_HAT0X && code <= ABS_HAT3Y) {
		a->hat = 1;
		a->rest = 0;
		a->thr = 1;
		return;
	}
	a->rest = value;
	a->thr = (max - min) / 4;
	if (flat * 2 > a->thr)
		a->thr = flat * 2 < (max - min) / 2 ? flat * 2 : (max - min) / 2;
	if (a->thr < 1)
		a->thr = 1;
}

/* the holders of /dev/uinput: their comms, space-separated */
static void uinput_holders(char *buf, size_t n)
{
	DIR *p = opendir("/proc"), *f;
	struct dirent *e, *g;
	char path[600], link[64];
	ssize_t l;
	buf[0] = 0;
	if (!p)
		return;
	while ((e = readdir(p))) {
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/fd", e->d_name);
		if (!(f = opendir(path)))
			continue;
		while ((g = readdir(f))) {
			snprintf(path, sizeof(path), "/proc/%s/fd/%s", e->d_name, g->d_name);
			l = readlink(path, link, sizeof(link) - 1);
			if (l <= 0)
				continue;
			link[l] = 0;
			if (strcmp(link, "/dev/uinput"))
				continue;
			char comm[32] = "?";
			FILE *c;
			snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
			if ((c = fopen(path, "r"))) {
				if (fgets(comm, sizeof(comm), c))
					comm[strcspn(comm, "\n")] = 0;
				fclose(c);
			}
			snprintf(buf + strlen(buf), n - strlen(buf), "%s%.20s:%.12s", buf[0] ? " " : "", comm, e->d_name);
			break;
		}
		closedir(f);
	}
	closedir(p);
}

static const char *input_dir = "/dev/input";

static void open_dev(const char *node, int startup)
{
	char path[300], sys[300], rp[PATH_MAX];
	unsigned long ev[NBITS(EV_CNT)] = {0}, absb[NBITS(ABS_CNT)] = {0}, relb[NBITS(REL_CNT)] = {0};
	unsigned long props[NBITS(INPUT_PROP_CNT)] = {0};
	struct dev *d = NULL;
	int i, fd;
	for (i = 0; i < MAXDEV; i++)
		if (devs[i].used && !strcmp(devs[i].node, node))
			return;	/* already known */
	for (i = 0; i < MAXDEV; i++)
		if (!devs[i].used) {
			d = &devs[i];
			break;
		}
	if (!d) {
		fprintf(out, "# device %s: too many devices, not watched\n", node);
		return;
	}
	snprintf(path, sizeof(path), "%s/%s", input_dir, node);
	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(out, "# device %s unreadable: %s\n", node, strerror(errno));
		return;
	}
	memset(d, 0, sizeof(*d));
	if (ioctl(fd, EVIOCGNAME(sizeof(d->name)), d->name) < 0) {	/* not an evdev node */
		close(fd);
		return;
	}
	d->used = 1;
	d->fd = fd;
	snprintf(d->node, sizeof(d->node), "%s", node);
	ioctl(fd, EVIOCGPHYS(sizeof(d->phys)), d->phys);
	d->name[sizeof(d->name) - 1] = d->phys[sizeof(d->phys) - 1] = 0;	/* a long name comes cut, unterminated */
	ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev);
	ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absb)), absb);
	ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relb)), relb);
	ioctl(fd, EVIOCGPROP(sizeof(props)), props);
	d->accel = TESTBIT(INPUT_PROP_ACCELEROMETER, props);
	d->mouse = TESTBIT(EV_REL, ev) && TESTBIT(REL_X, relb);
	for (i = 0; i < ABS_CNT; i++) {
		struct input_absinfo ai;
		if (TESTBIT(EV_ABS, ev) && TESTBIT(i, absb) && !ioctl(fd, EVIOCGABS(i), &ai))
			axis_setup(&d->ax[i], i, ai.minimum, ai.maximum, ai.value, ai.flat);
	}
	snprintf(sys, sizeof(sys), "/sys/class/input/%s/device", node);
	if (realpath(sys, rp))
		snprintf(d->sys, sizeof(d->sys), "%s", rp + (strncmp(rp, "/sys", 4) ? 0 : 4));
	if (strstr(d->sys, "/devices/virtual/input/")) {	/* a uinput device: whose? */
		char h[256];
		uinput_holders(h, sizeof(h));
		if (strstr(h, "evemu-device")) {
			d->ours = 1;
			snprintf(d->why, sizeof(d->why), "the bench's virtual device (%.60s)", h);
		} else {
			snprintf(d->why, sizeof(d->why), "uinput, held by %.70s", h[0] ? h : "nobody seen");
		}
	}
	for (i = 0; i < n_ignore && !d->ours; i++)
		if (strstr(d->name, ignore_names[i])) {
			d->ours = 1;
			snprintf(d->why, sizeof(d->why), "ignored (-I %.60s)", ignore_names[i]);
		}
	fprintf(out, "# device %s %s name=\"%s\" phys=\"%s\" sys=%s%s%s%s%s%s\n", node,
		d->ours ? "not-watched" : "watched", d->name, d->phys, d->sys[0] ? d->sys : "?",
		d->mouse ? " mouse" : "", d->accel ? " accelerometer(not watched)" : "",
		TESTBIT(EV_KEY, ev) ? " keys" : "", d->why[0] ? " " : "", d->why);
	if (d->ours) {
		close(fd);
		d->fd = -1;
	}
	if (!startup) {
		if (d->ours) {
			ev_line(now(), 0, "DEVADD %s \"%s\" (%s)", node, d->name, d->why);
		} else {
			S.dev_add++;
			addtext(S.dev_text, sizeof(S.dev_text), "%s connected at %.0f s", d->name, now() - t_start);
			ev_line(now(), 1, "DEVADD %s \"%s\" sys=%s", node, d->name, d->sys);
		}
	}
	fflush(out);
}

static void close_dev(struct dev *d, const char *why)
{
	burst_close(d);
	if (d->ours) {
		ev_line(now(), 0, "DEVDEL %s \"%s\" (%s; %s)", d->node, d->name, d->why, why);
	} else {
		S.dev_del++;
		addtext(S.dev_text, sizeof(S.dev_text), "%s gone at %.0f s", d->name, now() - t_start);
		ev_line(now(), 1, "DEVDEL %s \"%s\" (%s)", d->node, d->name, why);
	}
	if (d->fd >= 0)
		close(d->fd);
	d->used = 0;
	d->fd = -1;
}

static void read_dev(struct dev *d)
{
	struct input_event ev[64];
	ssize_t n;
	int i;
	while ((n = read(d->fd, ev, sizeof(ev))) > 0)
		for (i = 0; i < n / (ssize_t)sizeof(ev[0]); i++)
			event(d, ev[i].input_event_sec + ev[i].input_event_usec / 1e6, ev[i].type, ev[i].code, ev[i].value);
	if (n < 0 && errno == ENODEV)
		close_dev(d, "read: no device");
}

/* --- VT ---------------------------------------------------------------------------------- */
static int vt_fd = -1, vt_cur, vt_home, vt_home_opt, x_vt, x_entered;
static const char *pidfile;
static int game_pid, game_seen, game_gone;

static int vt_read(void)
{
	char b[32];
	ssize_t n;
	if (vt_fd < 0)
		return 0;
	n = pread(vt_fd, b, sizeof(b) - 1, 0);
	if (n <= 0)
		return 0;
	b[n] = 0;
	return strncmp(b, "tty", 3) ? 0 : atoi(b + 3);
}

static void game_check(void)
{
	if (!pidfile || game_gone)
		return;
	if (!game_pid) {
		FILE *f = fopen(pidfile, "r");
		if (f) {
			if (fscanf(f, "%d", &game_pid) != 1)
				game_pid = 0;
			fclose(f);
		}
		if (game_pid > 0)
			game_seen = 1;
		return;
	}
	if (kill(game_pid, 0) < 0 && errno == ESRCH)
		game_gone = 1;
}

static void vt_check(void)
{
	int v = vt_read(), ok = 0;
	const char *why = "";
	if (!v || v == vt_cur)
		return;
	game_check();
	if (x_vt && v == x_vt && !x_entered && vt_cur == vt_home) {
		ok = 1;
		x_entered = 1;
		why = "the X server takes its VT";
	} else if (x_vt && vt_cur == x_vt && v == vt_home && (game_gone || !game_seen)) {
		ok = 1;
		why = game_gone ? "the X server gives it back after the game" : "the X server gives it back (no game pid seen)";
	}
	if (ok) {
		ev_line(now(), 0, "VT %d -> %d (%s)", vt_cur, v, why);
	} else {
		S.vt++;
		addtext(S.vt_text, sizeof(S.vt_text), "VT switch %d->%d at %.0f s", vt_cur, v, now() - t_start);
		ev_line(now(), 1, "VT %d -> %d", vt_cur, v);
	}
	vt_cur = v;
}

/* --- processes and sessions ---------------------------------------------------------------- */
enum { C_NONE, C_OURS, C_ANC, C_EXEMPT, C_KTHREAD, C_SESSION, C_SYSTEM, C_USER, C_OTHER };
static const char *cname[] = {"?", "ours", "harness", "exempt", "kthread", "session", "system", "user-manager", "other"};
struct pe {
	int pid, cls, pre, busy;
	unsigned gen;
	unsigned long long cpu0;
	char sess[16], comm[20];
};
#define PT 2048	/* the PS3 runs ~150 processes; a full table makes new ones "gone" */
static struct pe ptab[PT], ptab2[PT];
static unsigned gen;
static int root_pid, root_pgid, scan_ms = 250;
static const char *env_mark, *exempt_subs[16], *sess_sub = "/session-";
static int n_exempt;
static DIR *procdir;

static struct pe *plook(int pid, int insert)
{
	unsigned h = (unsigned)pid * 2654435761u % PT, i;
	for (i = 0; i < PT; i++) {
		struct pe *e = &ptab[(h + i) % PT];
		if (e->pid == pid)
			return e;
		if (!e->pid) {
			if (!insert)
				return NULL;
			memset(e, 0, sizeof(*e));
			e->pid = pid;
			return e;
		}
	}
	return NULL;
}

/* the entries seen in this scan (gen) carried over into a fresh table */
static void psweep(void)
{
	int i, stale = 0;
	for (i = 0; i < PT; i++)
		stale += ptab[i].pid && ptab[i].gen != gen;
	if (!stale)
		return;
	memcpy(ptab2, ptab, sizeof(ptab));
	memset(ptab, 0, sizeof(ptab));
	for (i = 0; i < PT; i++)
		if (ptab2[i].pid && ptab2[i].gen == gen) {
			struct pe *e = plook(ptab2[i].pid, 1);
			if (e)
				*e = ptab2[i];
		}
}

static ssize_t readfile(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t l, t = 0;
	if (fd < 0)
		return -1;
	while (t < (ssize_t)n - 1 && (l = read(fd, buf + t, n - 1 - t)) > 0)
		t += l;
	close(fd);
	buf[t] = 0;
	return t;
}

struct pstat {
	int ppid, pgid, zombie;
	unsigned long long cpu;
	char comm[20];
};
static int pstat(int pid, struct pstat *s)
{
	char path[64], b[1024], *p;
	unsigned long long ut, st;
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	if (readfile(path, b, sizeof(b)) <= 0)
		return -1;
	p = strrchr(b, ')');
	if (!p || !strchr(b, '('))
		return -1;
	*p = 0;
	snprintf(s->comm, sizeof(s->comm), "%s", strchr(b, '(') + 1);
	if (sscanf(p + 2, "%*c %d %d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &s->ppid, &s->pgid, &ut, &st) != 4)
		return -1;
	s->zombie = p[2] == 'Z' || p[2] == 'X';
	s->cpu = ut + st;
	return 0;
}

static void pcmdline(int pid, char *b, size_t n)
{
	char path[64];
	ssize_t l, i;
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	l = readfile(path, b, n);
	if (l <= 0) {
		b[0] = 0;
		return;
	}
	for (i = 0; i < l; i++)
		if (b[i] == 0 || b[i] == '\n' || b[i] == '\t')
			b[i] = ' ';
	while (l > 0 && b[l - 1] == ' ')
		b[--l] = 0;
}

static int penv_has(int pid, const char *mark)
{
	static char b[65536];
	char path[64];
	ssize_t l, i = 0;
	snprintf(path, sizeof(path), "/proc/%d/environ", pid);
	if ((l = readfile(path, b, sizeof(b))) <= 0)
		return 0;
	while (i < l) {
		if (!strcmp(b + i, mark))
			return 1;
		i += strlen(b + i) + 1;
	}
	return 0;
}

/* the cgroup's class; sess gets the login session's id */
static int pcgroup(int pid, char *sess, size_t n)
{
	char path[64], b[2048], *l, *cg = NULL, *s;
	snprintf(path, sizeof(path), "/proc/%d/cgroup", pid);
	sess[0] = 0;
	if (readfile(path, b, sizeof(b)) <= 0)
		return C_OTHER;
	for (l = strtok(b, "\n"); l; l = strtok(NULL, "\n"))
		if (!strncmp(l, "0::", 3) || strstr(l, ":name=systemd:")) {
			cg = strchr(strchr(l, ':') + 1, ':') + 1;
			if (!strncmp(l, "0::", 3))
				break;
		}
	if (!cg)
		return C_OTHER;
	if ((s = strstr(cg, sess_sub))) {
		s += strlen(sess_sub);
		if (!strcmp(sess_sub, "/session-")) {
			snprintf(sess, n, "%.*s", (int)strcspn(s, "./"), s);
		} else {	/* -S (the tests): no session ids, so no session is exempt */
			sess[0] = 0;
		}
		return C_SESSION;
	}
	if (strstr(cg, "/user@"))
		return C_USER;
	if (!strcmp(cg, "/") || !strncmp(cg, "/system.slice", 13) || !strncmp(cg, "/init.scope", 11))
		return C_SYSTEM;
	return C_OTHER;
}

static int exempt_cmd(const char *c)
{
	int i;
	if (strstr(c, "pgrep -x run.sh") && strstr(c, "/dev/shm/ps3-bench-"))	/* tools/bench-cam.sh */
		return 1;
	for (i = 0; i < n_exempt; i++)
		if (strstr(c, exempt_subs[i]))
			return 1;
	return 0;
}

/* foreign events, resolved at the end (a session is exempt once an exempt process is seen in it) */
struct rec {
	double t;
	int kind;	/* 0 process, 1 session */
	char sess[16], text[200];
};
static struct rec recs[256];
static int n_recs;
static char exempt_sess[16][16];
static int n_exempt_sess;
static char known_sess[64][16];
static int n_known_sess;

static void sess_exempt(const char *s)
{
	int i;
	if (!s[0])
		return;
	for (i = 0; i < n_exempt_sess; i++)
		if (!strcmp(exempt_sess[i], s))
			return;
	if (n_exempt_sess < 16)
		snprintf(exempt_sess[n_exempt_sess++], 16, "%s", s);
}
static int is_exempt_sess(const char *s)
{
	int i;
	for (i = 0; s[0] && i < n_exempt_sess; i++)
		if (!strcmp(exempt_sess[i], s))
			return 1;
	return 0;
}

static void add_rec(double t, int kind, const char *sess, const char *fmt, ...)
{
	va_list ap;
	if (n_recs == 256) {
		S.procs++;	/* counted, not described */
		return;
	}
	recs[n_recs].t = t;
	recs[n_recs].kind = kind;
	snprintf(recs[n_recs].sess, 16, "%s", sess);
	va_start(ap, fmt);
	vsnprintf(recs[n_recs].text, sizeof(recs[0].text), fmt, ap);
	va_end(ap);
	n_recs++;
}

static struct { char comm[20]; int n, cls; } seen_info[48];
static int n_seen_info;
static void info_proc(int cls, const char *comm)
{
	int i;
	if (cls == C_SYSTEM) S.sys_procs++;
	else if (cls == C_USER) S.usr_procs++;
	else S.other_procs++;
	for (i = 0; i < n_seen_info; i++)
		if (seen_info[i].cls == cls && !strcmp(seen_info[i].comm, comm)) {
			seen_info[i].n++;
			return;
		}
	if (n_seen_info < 48) {
		snprintf(seen_info[n_seen_info].comm, 20, "%s", comm);
		seen_info[n_seen_info].cls = cls;
		seen_info[n_seen_info++].n = 1;
	}
}

static struct pe *classify(int pid, int depth, int pre)
{
	struct pe *e = plook(pid, 0), *par = NULL;
	struct pstat s;
	char cmd[1024], sess[16] = "";
	int cls;
	if (e) {
		e->gen = gen;
		return e;
	}
	if (pstat(pid, &s) < 0 || (s.zombie && !pre))	/* gone: its command line and environment too */
		return NULL;
	if (pid == 2 || s.ppid == 2 || (s.ppid == 0 && pid != 1)) {
		cls = C_KTHREAD;
	} else {
		if (s.ppid > 1 && depth < 32)
			par = classify(s.ppid, depth + 1, pre);
		cmd[0] = 0;
		if (pid == root_pid || (par && par->cls == C_OURS) || (root_pgid > 0 && s.pgid == root_pgid)) {
			cls = C_OURS;
		} else if (par && par->cls == C_EXEMPT) {
			cls = C_EXEMPT;
		} else {
			pcmdline(pid, cmd, sizeof(cmd));
			if (exempt_cmd(cmd))
				cls = C_EXEMPT;
			else if (env_mark && penv_has(pid, env_mark))
				cls = C_OURS;
			else
				cls = pcgroup(pid, sess, sizeof(sess));
		}
		if (cls == C_EXEMPT) {
			pcgroup(pid, sess, sizeof(sess));
			sess_exempt(sess);
		}
		if (!pre && (cls == C_SESSION || cls == C_EXEMPT || cls == C_OTHER || cls == C_SYSTEM || cls == C_USER) && !cmd[0])
			pcmdline(pid, cmd, sizeof(cmd));
		if (!pre) {
			double t = now();
			if (cls == C_SESSION) {
				add_rec(t, 0, sess, "pid %d (%s) in session %s: %.150s", pid, s.comm, sess, cmd);
				ev_line(t, 1, "PROC pid=%d comm=%s session=%s cmd=%.300s", pid, s.comm, sess, cmd);
			} else if (cls == C_EXEMPT) {
				S.exempt_procs++;
				if (S.exempt_procs <= 20)
					ev_line(t, 0, "PROC exempt pid=%d comm=%s cmd=%.120s", pid, s.comm, cmd);
			} else if (cls == C_SYSTEM || cls == C_USER || cls == C_OTHER) {
				info_proc(cls, s.comm);
				if (S.sys_procs + S.usr_procs + S.other_procs <= 60)
					ev_line(t, 0, "PROC %s pid=%d comm=%s cmd=%.200s", cname[cls], pid, s.comm, cmd);
			}
		}
	}
	e = plook(pid, 1);
	if (!e)
		return NULL;
	e->cls = cls;
	e->pre = pre;
	e->gen = gen;
	e->cpu0 = s.cpu;
	snprintf(e->sess, sizeof(e->sess), "%s", sess);
	snprintf(e->comm, sizeof(e->comm), "%s", s.comm);
	return e;
}

static void proc_scan(int pre)
{
	struct dirent *d;
	gen++;
	rewinddir(procdir);
	while ((d = readdir(procdir))) {
		int pid;
		struct pe *e;
		if (!isdigit((unsigned char)d->d_name[0]))
			continue;
		pid = atoi(d->d_name);
		if ((e = plook(pid, 0))) {
			e->gen = gen;
			continue;
		}
		if (!classify(pid, 0, pre) && !pre)
			S.vanished++;
	}
	psweep();
}

/* the harness's ancestors: theirs, but what they start meanwhile is not */
static void mark_ancestors(void)
{
	struct pstat s;
	int pid = root_pid, n = 0;
	if (pstat(root_pid, &s) == 0)
		root_pgid = s.pgid;
	while (pid > 1 && n++ < 32 && pstat(pid, &s) == 0) {
		struct pe *e = plook(pid, 1);
		e->cls = pid == root_pid ? C_OURS : C_ANC;
		e->pre = 1;
		e->gen = gen;
		e->cpu0 = s.cpu;
		snprintf(e->comm, sizeof(e->comm), "%s", s.comm);
		pid = s.ppid;
	}
}

/* older login-session processes that worked during the run: every few seconds (a process that
 * exits takes its last seconds' CPU with it) and at the end; each counted once */
static void busy_check(void)
{
	long hz = sysconf(_SC_CLK_TCK);
	int i;
	for (i = 0; i < PT; i++) {
		struct pe *e = &ptab[i];
		struct pstat s;
		double cpu;
		if (!e->pid || !e->pre || e->cls != C_SESSION || e->busy || pstat(e->pid, &s) < 0)
			continue;
		cpu = (double)(s.cpu - e->cpu0) / hz;
		if (cpu < BUSY_TICKS_S)
			continue;
		e->busy = 1;
		if (is_exempt_sess(e->sess)) {
			ev_line(now(), 0, "BUSY pid=%d comm=%s %.1f s CPU (exempt session %s)", e->pid, e->comm, cpu, e->sess);
			continue;
		}
		S.busy++;
		addtext(S.busy_text, sizeof(S.busy_text), "%s %.1f s CPU", e->comm, cpu);
		ev_line(now(), 1, "BUSY pid=%d comm=%s session=%s %.1f s CPU during the run (started before it)", e->pid, e->comm, e->sess, cpu);
	}
}

static const char *sess_dir = "/run/systemd/sessions";
static void sess_new(const char *name, int startup)
{
	char path[300], b[4096], *l, user[64] = "?", svc[64] = "?", host[96] = "", cls[32] = "";
	int i;
	if (name[0] == '.' || strchr(name, '.'))	/* temporary files, .ref fifos */
		return;
	for (i = 0; i < n_known_sess; i++)
		if (!strcmp(known_sess[i], name))
			return;
	if (n_known_sess < 64)
		snprintf(known_sess[n_known_sess++], 16, "%.15s", name);
	if (startup)
		return;
	snprintf(path, sizeof(path), "%s/%s", sess_dir, name);
	if (readfile(path, b, sizeof(b)) > 0)
		for (l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
			if (!strncmp(l, "USER=", 5)) snprintf(user, sizeof(user), "%s", l + 5);
			else if (!strncmp(l, "SERVICE=", 8)) snprintf(svc, sizeof(svc), "%s", l + 8);
			else if (!strncmp(l, "REMOTE_HOST=", 12)) snprintf(host, sizeof(host), "%s", l + 12);
			else if (!strncmp(l, "CLASS=", 6)) snprintf(cls, sizeof(cls), "%s", l + 6);
		}
	add_rec(now(), 1, name, "session %s (%s, %s%s%s%s%s)", name, user, svc, host[0] ? " from " : "", host,
		cls[0] ? ", " : "", cls);
	ev_line(now(), 1, "SESSION %s user=%s service=%s remote=%s class=%s (resolved at the end: exempt if bench-cam's)",
		name, user, svc, host[0] ? host : "-", cls[0] ? cls : "-");
}

/* --- kernel log ---------------------------------------------------------------------------- */
static char kbuf[8192];
static size_t klen;
static void kmsg_line(char *l)
{
	char *m = strchr(l, ';');
	if (l[0] == ' ' || !m)	/* continuation (KEY=VALUE) lines */
		return;
	m++;
	if (strstr(m, "USB disconnect") || (strstr(m, "new ") && strstr(m, "USB device number"))) {
		S.usb++;
		addtext(S.usb_text, sizeof(S.usb_text), "usb %s at %.0f s", strstr(m, "disconnect") ? "disconnect" : "connect", now() - t_start);
		ev_line(now(), 1, "KMSG %.200s", m);
	} else if ((strstr(m, "input: ") && strstr(m, " as /devices/") && !strstr(m, "/devices/virtual/input/")) ||
		   strstr(m, "Bluetooth:") || strstr(m, "hidraw") || strstr(m, "hid-sony") || strstr(m, "sony ")) {
		S.kinfo++;
		if (S.kinfo <= 50)
			ev_line(now(), 0, "KMSG %.200s", m);
	}
}
static void kmsg_read(int fd)
{
	ssize_t n;
	while ((n = read(fd, kbuf + klen, sizeof(kbuf) - 1 - klen)) > 0) {
		char *p, *s = kbuf;
		klen += n;
		kbuf[klen] = 0;
		while ((p = memchr(s, '\n', kbuf + klen - s))) {
			*p = 0;
			kmsg_line(s);
			s = p + 1;
		}
		klen = kbuf + klen - s;
		memmove(kbuf, s, klen);
		if (klen == sizeof(kbuf) - 1)
			klen = 0;	/* a line too long: dropped */
	}
}

/* --- the end ------------------------------------------------------------------------------- */
static void finish(void)
{
	char sum[1024] = "", info[1024] = "";
	int i, flagged;
	struct rusage ru;
	for (i = 0; i < MAXDEV; i++)
		if (devs[i].used)
			burst_close(&devs[i]);
	for (i = 0; i < n_recs; i++) {
		if (is_exempt_sess(recs[i].sess)) {
			ev_line(recs[i].t, 0, "%s: exempt (bench-cam's session)", recs[i].text);
			continue;
		}
		if (recs[i].kind == 1) {
			S.sessions++;
			addtext(S.sess_text, sizeof(S.sess_text), "%s at %.0f s", recs[i].text, recs[i].t - t_start);
		} else {
			S.procs++;
			addtext(S.proc_text, sizeof(S.proc_text), "%s", recs[i].text);
		}
	}
	if (S.keys) addtext(sum, sizeof(sum), "%ld key presses", S.keys);
	if (S.cbuttons) addtext(sum, sizeof(sum), "%ld controller buttons", S.cbuttons);
	if (S.sticks) addtext(sum, sizeof(sum), "%ld stick moves", S.sticks);
	if (S.hats) addtext(sum, sizeof(sum), "%ld d-pad (hat) presses", S.hats);
	if (S.mbuttons) addtext(sum, sizeof(sum), "%ld mouse buttons", S.mbuttons);
	if (S.wheel) addtext(sum, sizeof(sum), "%ld wheel steps", S.wheel);
	if (S.mouse_deliberate) addtext(sum, sizeof(sum), "%ld deliberate mouse moves", S.mouse_deliberate);
	if (S.drops) addtext(sum, sizeof(sum), "%ld input floods", S.drops);
	if (S.vt) addtext(sum, sizeof(sum), "%s", S.vt_text);
	if (S.dev_add || S.dev_del) addtext(sum, sizeof(sum), "%s", S.dev_text);
	if (S.usb) addtext(sum, sizeof(sum), "%s", S.usb_text);
	if (S.sessions) addtext(sum, sizeof(sum), "%ld login(s): %s", S.sessions, S.sess_text);
	if (S.procs) addtext(sum, sizeof(sum), "%ld command(s) in login sessions: %s", S.procs, S.proc_text);
	if (S.busy) addtext(sum, sizeof(sum), "busy login processes: %s", S.busy_text);
	flagged = sum[0] != 0;
	if (S.mouse_bumps) addtext(info, sizeof(info), "mouse: %ld bump(s), %ld counts", S.mouse_bumps, S.mouse_bump_dist);
	if (S.exempt_procs) addtext(info, sizeof(info), "bench-cam/exempt: %ld processes", S.exempt_procs);
	if (S.sys_procs || S.usr_procs || S.other_procs) {
		char l[300] = "";
		for (i = 0; i < n_seen_info; i++)
			addtext(l, sizeof(l), "%s%s x%d", seen_info[i].cls == C_SYSTEM ? "" : seen_info[i].cls == C_USER ? "user:" : "other:",
				seen_info[i].comm, seen_info[i].n);
		addtext(info, sizeof(info), "services started %ld processes (%s)", S.sys_procs + S.usr_procs + S.other_procs, l);
	}
	if (S.kinfo) addtext(info, sizeof(info), "%ld kernel input/HID/Bluetooth lines", S.kinfo);
	if (S.motion_ignored) addtext(info, sizeof(info), "%ld motion sensor events (not watched)", S.motion_ignored);
	if (S.vanished) addtext(info, sizeof(info), "%ld short-lived processes gone before a look", S.vanished);
	if (!replaying)
		fprintf(out, "# end t=%.1f\n", now() - t_start);
	fprintf(out, "# interfered=%d\n# interference: %s\n# info: %s\n", flagged, flagged ? sum : "none", info[0] ? info : "-");
	getrusage(RUSAGE_SELF, &ru);
	if (!replaying)
		fprintf(out, "# self cpu=%.3f s maxrss=%ld KiB\n", ru.ru_utime.tv_sec + ru.ru_stime.tv_sec +
		(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6, ru.ru_maxrss);
	fflush(out);
}

/* --- replay -------------------------------------------------------------------------------- */
static uint64_t get(const unsigned char *p, int n, int be)
{
	uint64_t v = 0;
	int i;
	for (i = 0; i < n; i++)
		v |= (uint64_t)p[be ? i : n - 1 - i] << (8 * (n - 1 - i));
	return v;
}

static int replay(const char *spec)
{
	FILE *f = fopen(spec, "r");
	char l[512];
	struct dev *d = NULL;
	int nd = 0;
	if (!f) {
		perror(spec);
		return 2;
	}
	t_start = 0;
	replaying = 1;
	fprintf(out, "# ps3-intfwatch 1 replay %s\n", spec);
	while (fgets(l, sizeof(l), f)) {
		char a[300], b[16];
		int code, mn, mx, val, flat;
		l[strcspn(l, "\n")] = 0;
		if (!strncmp(l, "device ", 7) && nd < MAXDEV) {
			d = &devs[nd++];
			memset(d, 0, sizeof(*d));
			d->used = 1;
			d->fd = -1;
			snprintf(d->name, sizeof(d->name), "%.127s", l + 7);
		} else if (d && !strcmp(l, "prop accel")) {
			d->accel = 1;
		} else if (d && !strcmp(l, "rel")) {
			d->mouse = 1;
		} else if (d && sscanf(l, "abs %i %d %d %d %d", &code, &mn, &mx, &val, &flat) == 5 && code >= 0 && code < ABS_CNT) {
			axis_setup(&d->ax[code], code, mn, mx, val, flat);
		} else if (d && sscanf(l, "events %299s %15s", a, b) == 2) {
			FILE *e = fopen(a, "rb");
			unsigned char r[24];
			int be = !strcmp(b, "be") || (!strcmp(b, "native") && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__);
			if (!e) {
				perror(a);
				return 2;
			}
			while (fread(r, 24, 1, e) == 1) {
				double t = get(r, 8, be) + get(r + 8, 8, be) / 1e6;
				if (!t_start)
					t_start = t;
				event(d, t, get(r + 16, 2, be), get(r + 18, 2, be), (int32_t)get(r + 20, 4, be));
			}
			fclose(e);
		}
	}
	fclose(f);
	finish();
	return 0;
}

static void on_sig(int s) { (void)s; stop_sig = 1; }

int main(int argc, char **argv)
{
	const char *outp = NULL, *stopfile = NULL, *spec = NULL, *vt_path = "/sys/class/tty/tty0/active";
	const char *kmsg_path = "/dev/kmsg";
	int opt, ino, wd_in = -1, wd_s = -1, kfd, kpoll = 0, i;
	struct stat st;
	double last_scan = 0, last_busy = 0;
	struct dirent *e;
	DIR *dd;
	while ((opt = getopt(argc, argv, "o:s:P:e:g:x:H:X:I:q:d:L:t:k:S:r:")) != -1) {
		switch (opt) {
		case 'o': outp = optarg; break;
		case 's': stopfile = optarg; break;
		case 'P': root_pid = atoi(optarg); break;
		case 'e': env_mark = optarg; break;
		case 'g': pidfile = optarg; break;
		case 'x': x_vt = atoi(optarg); break;
		case 'H': vt_home_opt = atoi(optarg); break;
		case 'X': if (n_exempt < 16) exempt_subs[n_exempt++] = optarg; break;
		case 'I': if (n_ignore < 16) ignore_names[n_ignore++] = optarg; break;
		case 'q': scan_ms = atoi(optarg) > 0 ? atoi(optarg) : 250; break;
		case 'd': input_dir = optarg; break;
		case 'L': sess_dir = optarg; break;
		case 't': vt_path = optarg; break;
		case 'k': kmsg_path = optarg; break;
		case 'S': sess_sub = optarg; break;
		case 'r': spec = optarg; break;
		default:
			fprintf(stderr, "usage: see the head of ps3-intfwatch.c (docs/STALLS.md \"Interference\")\n");
			return 2;
		}
	}
	out = stdout;
	if (outp && !(out = fopen(outp, "w"))) {
		perror(outp);
		return 2;
	}
	if (spec)
		return replay(spec);
	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	signal(SIGPIPE, SIG_IGN);
	t_start = now();
	fprintf(out, "# ps3-intfwatch 1 start=%.3f root=%d xvt=%d pidfile=%s\n", t_start, root_pid, x_vt, pidfile ? pidfile : "-");
	/* processes first: the uinput holder check of the devices reads /proc too */
	if (!(procdir = opendir("/proc"))) {
		perror("/proc");
		return 2;
	}
	gen = 1;
	if (root_pid)
		mark_ancestors();
	proc_scan(1);
	ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino >= 0) {
		wd_in = inotify_add_watch(ino, input_dir, IN_CREATE | IN_DELETE | IN_MOVED_TO);
		wd_s = inotify_add_watch(ino, sess_dir, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE);
	}
	if (wd_in < 0) fprintf(out, "# no inotify on %s: %s (devices connected later are not seen)\n", input_dir, strerror(errno));
	if (wd_s < 0) fprintf(out, "# no inotify on %s: logins are seen through their processes only\n", sess_dir);
	if ((dd = opendir(input_dir))) {
		while ((e = readdir(dd)))
			if (!strncmp(e->d_name, "event", 5))
				open_dev(e->d_name, 1);
		closedir(dd);
	}
	if ((dd = opendir(sess_dir))) {
		while ((e = readdir(dd)))
			sess_new(e->d_name, 1);
		closedir(dd);
	}
	vt_fd = open(vt_path, O_RDONLY | O_CLOEXEC);
	vt_cur = vt_home = vt_read();
	if (vt_home_opt)
		vt_home = vt_home_opt;
	if (x_vt && vt_cur == x_vt)
		x_entered = 1;
	kfd = open(kmsg_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (kfd >= 0) {
		lseek(kfd, 0, SEEK_END);
		kpoll = !fstat(kfd, &st) && S_ISCHR(st.st_mode);	/* a regular file is always "ready" */
	}
	else
		fprintf(out, "# %s: %s (USB lines not seen)\n", kmsg_path, strerror(errno));
	fprintf(out, "# vt=%d home=%d sessions=%d ready\n", vt_cur, vt_home, n_known_sess);
	fflush(out);
	last_scan = last_busy = now();
	while (!stop_sig) {
		struct pollfd p[MAXDEV + 3];
		int map[MAXDEV + 3], np = 0, r, tmo;
		double t;
		if (ino >= 0) { p[np].fd = ino; p[np].events = POLLIN; map[np++] = -1; }
		if (kpoll) { p[np].fd = kfd; p[np].events = POLLIN; map[np++] = -2; }
		if (vt_fd >= 0) { p[np].fd = vt_fd; p[np].events = POLLPRI | POLLERR; map[np++] = -3; }
		for (i = 0; i < MAXDEV; i++)
			if (devs[i].used && devs[i].fd >= 0) {
				p[np].fd = devs[i].fd;
				p[np].events = POLLIN;
				map[np++] = i;
			}
		tmo = scan_ms - (int)((now() - last_scan) * 1000);
		r = poll(p, np, tmo < 0 ? 0 : tmo);
		if (r < 0 && errno != EINTR)
			break;
		for (i = 0; r > 0 && i < np; i++) {
			if (!p[i].revents)
				continue;
			if (map[i] >= 0) {
				if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL))
					close_dev(&devs[map[i]], "gone");
				else
					read_dev(&devs[map[i]]);
			} else if (map[i] == -1) {
				char b[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
				ssize_t n = read(ino, b, sizeof(b)), o = 0;
				while (n > 0 && o < n) {
					struct inotify_event *ie = (struct inotify_event *)(b + o);
					if (ie->len && ie->wd == wd_in && !strncmp(ie->name, "event", 5)) {
						if (ie->mask & (IN_CREATE | IN_MOVED_TO)) {
							open_dev(ie->name, 0);
						} else if (ie->mask & IN_DELETE) {
							int j;
							for (j = 0; j < MAXDEV; j++)
								if (devs[j].used && !strcmp(devs[j].node, ie->name))
									close_dev(&devs[j], "node removed");
						}
					} else if (ie->len && ie->wd == wd_s) {
						sess_new(ie->name, 0);
					}
					o += sizeof(*ie) + ie->len;
				}
			} else if (map[i] == -2) {
				kmsg_read(kfd);
			} else if (map[i] == -3) {
				vt_check();
			}
		}
		t = now();
		for (i = 0; i < MAXDEV; i++)	/* a mouse burst that ended */
			if (devs[i].used && devs[i].b_open && t - devs[i].b_last > BURST_GAP)
				burst_close(&devs[i]);
		if ((t - last_scan) * 1000 >= scan_ms) {
			last_scan = t;
			proc_scan(0);
			if (t - last_busy >= 5) {
				last_busy = t;
				busy_check();
			}
			game_check();
			vt_check();
			if (kfd >= 0)
				kmsg_read(kfd);	/* a regular file (the tests) never polls ready */
			if (stopfile && !access(stopfile, F_OK))
				break;
		}
	}
	proc_scan(0);
	vt_check();
	busy_check();
	finish();
	return 0;
}
