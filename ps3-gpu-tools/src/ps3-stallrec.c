/*
 * ps3-stallrec: the stall attribution recorder (docs/STALLS.md). One long-lived process that
 * samples, every few milliseconds, what the game's main thread is doing and what the memory
 * system and the disks did since the last sample, so that games/bench/stalls.py can say why a
 * frame took more than 100 ms. Run by ps3-bench.sh (BENCH_STALLS=1) as root.
 *
 *   ps3-stallrec [-o OUT] [-i MS] (-p PIDFILE | -P PID) [-T COMM] [-f FRAMEFILE] [-s STOPFILE]
 *                [-x COMM]... [-d DISK]... [-S N] [-k MS] [-K N] [-R] [-n] [-L]
 *
 *   -o OUT        the record (default stdout); -i MS the period (default 10, fractions allowed)
 *   -p PIDFILE    the game's pid from this file (client.sh's $BENCH_DIR/.pid), polled until it
 *                 appears and again when the game exits; -P PID a fixed pid
 *   -T COMM       follow the thread named COMM instead of the main thread (tid == pid)
 *   -f FRAMEFILE  the Gallium HUD's frame time dump (hud/frametime_(ms)): its new lines are
 *                 counted every sample, which places every frame on this clock
 *   -s STOPFILE   stop when it exists (checked every slow tick); SIGTERM/SIGINT stop too
 *   -x COMM       another process to follow (state, faults, CPU ticks): Xorg, kswapd0 (up to 4)
 *   -d DISK       a further /proc/diskstats name (sda, zram0, ps3vswap0 and the VRAM swap's
 *                 backing partition are always recorded)
 *   -S N          slow tick every N samples (default 10): VRAM swap stats, /proc/stat, free
 *                 pages, pid file, stop file
 *   -k MS, -K N   the main thread's kernel stack once it has been blocked MS ms (default 50;
 *                 D state, or S outside futex/poll/sleep), at most every 250 ms per episode and
 *                 N times per run (default 1000, 0: never)
 *   -R            SCHED_FIFO 1 (default: nice -10); -n neither
 *   -L            lock its memory (mlockall: it never page-faults, but pins ~0.8 MB more than it
 *                 uses, libc whole; off by default: a late sample shows in the trailer instead)
 *
 * Cheap on purpose: every /proc and sysfs file stays open and is re-read with pread at offset 0
 * (seq_file regenerates it), the record is sparse text (only what changed), output is flushed
 * once a second. The trailer gives the
 * recorder's own CPU time.
 *
 * Record format (one line per sample; t = microseconds since the start, CLOCK_MONOTONIC):
 *   # header lines: stallrec version, clock pair (mono/real), vmstat/disk/sys/other index tables
 *   <t> s=<state> [w=<wchan id>] [y=<syscall nr>] [d=<fd id>] [c=<process CPU us>]
 *       [mj=,mn=,mu=,ms=  main thread major/minor fault and user/sys tick deltas]
 *       [Mj=,Mn=          process major/minor fault deltas] [f=<frames written>]
 *       [v<i>=<vmstat delta>] [k<i>=reads:sectors:writes:wsectors:io_ms:inflight]
 *       [o<i>=<state>:<majflt>:<ticks>[:<wchan id>]]
 *       slow ticks: [vs=vram_reads:disk_reads:vram_writes:disk_writes:written_back]
 *       [cpu=busy:idle:iowait (USER_HZ ticks, all CPUs)] [fr=<free pages>]
 *   W <id> <wchan name>        D <id> <fd target>        P <t> pid=<pid> tid=<tid> comm=<comm>
 *   P <t> gone                 F <t> n0=<lines already there>        C <t> real=<s.ns>
 *   K <t> <tid> <state> <frame>|<frame>|...   (kernel stack)
 *   # end ...                  (samples, late samples, largest gap, own CPU, peak RSS)
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define VERSION 1
#define MAXV 96		/* vmstat counters kept */
#define MAXD 8		/* disks */
#define MAXO 4		/* other processes */
#define MAXS 512	/* wchan / fd strings */
#define MAXFD 1024	/* fd cache */
#define BUF 65536

static FILE *out;
static char line[8192];	/* the sample line being built: definitions (W, D, K) go out first */
static int linelen;

static void ap(const char *fmt, ...)
{
	va_list ap_;
	va_start(ap_, fmt);
	if (linelen < (int)sizeof(line) - 1) {
		int n = vsnprintf(line + linelen, sizeof(line) - linelen, fmt, ap_);
		linelen += n > 0 ? n : 0;
		if (linelen > (int)sizeof(line) - 1)
			linelen = sizeof(line) - 1;
	}
	va_end(ap_);
}
static volatile sig_atomic_t stop;
static int64_t t_start;		/* ns, CLOCK_MONOTONIC */

static int64_t now_ns(clockid_t c)
{
	struct timespec ts;
	clock_gettime(c, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static long t_us(int64_t ns) { return (long)((ns - t_start) / 1000); }

static void on_sig(int s) { (void)s; stop = 1; }

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "ps3-stallrec: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

/* pread at 0 into buf; NUL-terminated; length or -1 */
static int rd(int fd, char *buf, int size)
{
	ssize_t n;
	if (fd < 0)
		return -1;
	n = pread(fd, buf, size - 1, 0);
	if (n < 0)
		return -1;
	buf[n] = 0;
	return (int)n;
}

static int ropen(const char *fmt, ...)
{
	char p[PATH_MAX];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p, sizeof(p), fmt, ap);
	va_end(ap);
	return open(p, O_RDONLY | O_CLOEXEC);
}
static void xclose(int *fd) { if (*fd >= 0) close(*fd); *fd = -1; }

/* --- strings (wchan names, fd targets): an id the first time, defined with a W/D line --- */
struct strtab { char *s[MAXS]; int n; char tag; };
static struct strtab wchans = { .tag = 'W' }, fdnames = { .tag = 'D' };

static int str_id(struct strtab *t, const char *s)
{
	int i;
	for (i = 0; i < t->n; i++)
		if (!strcmp(t->s[i], s))
			return i + 1;
	if (t->n == MAXS)
		return 0;
	t->s[t->n] = strdup(s);
	fprintf(out, "%c %d %s\n", t->tag, t->n + 1, s);
	return ++t->n;
}

/* --- /proc/<pid>/stat --- */
struct pstat { char state; unsigned long minflt, majflt, utime, stime; };

static int parse_stat(const char *b, struct pstat *p)
{
	const char *c = strrchr(b, ')');
	if (!c)
		return -1;
	/* after ")": state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
	if (sscanf(c + 2, "%c %*d %*d %*d %*d %*d %*u %lu %*u %lu %*u %lu %lu", &p->state,
		   &p->minflt, &p->majflt, &p->utime, &p->stime) != 5)
		return -1;
	return 0;
}

/* --- vmstat: the counters worth knowing about a stall --- */
static const char *vm_want[] = {
	"pswpin", "pswpout", "pgmajfault", "pgpgin", "pgpgout", "allocstall_", "pgscan_",
	"pgsteal_", "compact_", "pgmigrate_", "workingset_", "cma_alloc_", "thp_", "kswapd_",
	"pageoutrun", "slabs_scanned", "pgrefill", "pgrotated", "pgactivate", "pgdeactivate",
	"oom_kill", "drop_", "nr_vmscan_write", "nr_vmscan_immediate_reclaim", NULL };
static int vm_line[MAXV], vm_n, vm_free_line = -1, vm_fd = -1;
static unsigned long long vm_prev[MAXV];

static int vm_selected(const char *name)
{
	for (const char **w = vm_want; *w; w++) {
		size_t l = strlen(*w);
		if ((*w)[l - 1] == '_' ? !strncmp(name, *w, l) : !strcmp(name, *w))
			return 1;
	}
	return 0;
}

/* walks the vmstat text: cb(line index, value) for every selected line */
static unsigned long long vm_free;
static void vm_read(char *buf, int n, int emit)
{
	char *p = buf, *e = buf + n;
	int line = 0, k = 0;
	while (p < e && k <= vm_n) {
		char *nl = memchr(p, '\n', e - p);
		if (!nl)
			nl = e;
		if ((k < vm_n && line == vm_line[k]) || line == vm_free_line) {
			char *sp = memchr(p, ' ', nl - p);
			unsigned long long v = sp ? strtoull(sp + 1, NULL, 10) : 0;
			if (line == vm_free_line)
				vm_free = v;
			if (k < vm_n && line == vm_line[k]) {
				if (emit && v != vm_prev[k])
					ap(" v%d=%llu", k, v - vm_prev[k]);
				vm_prev[k] = v;
				k++;
			}
		}
		line++;
		p = nl + 1;
	}
}

static void vm_init(char *buf)
{
	int n, line = 0;
	char *p, *save;
	vm_fd = ropen("/proc/vmstat");
	if ((n = rd(vm_fd, buf, BUF)) <= 0)
		die("/proc/vmstat: %s", strerror(errno));
	fprintf(out, "# vmstat");
	for (p = strtok_r(buf, "\n", &save); p; p = strtok_r(NULL, "\n", &save), line++) {
		char *sp = strchr(p, ' ');
		if (!sp)
			continue;
		*sp = 0;
		if (!strcmp(p, "nr_free_pages"))
			vm_free_line = line;
		if (vm_n < MAXV && vm_selected(p) && strcmp(p, "workingset_nodes")) {
			fprintf(out, " %d=%s", vm_n, p);
			vm_line[vm_n++] = line;
		}
	}
	fputc('\n', out);
	n = rd(vm_fd, buf, BUF);
	vm_read(buf, n, 0);
}

/* --- disks: /sys/class/block/<name>/stat (one device each: /proc/diskstats costs ten times more) --- */
struct disk { char name[32]; int fd; unsigned long long r, rs, w, ws, io; int seen; };
static struct disk disks[MAXD];
static int ndisks;

static void add_disk(const char *name)
{
	for (int i = 0; i < ndisks; i++)
		if (!strcmp(disks[i].name, name))
			return;
	if (ndisks < MAXD && *name && !strchr(name, '/')) {
		struct disk *d = &disks[ndisks];
		snprintf(d->name, sizeof(d->name), "%s", name);
		d->fd = ropen("/sys/class/block/%s/stat", name);
		if (d->fd >= 0)
			ndisks++;
	}
}

static void ds_read(char *buf, int emit)
{
	for (int i = 0; i < ndisks; i++) {
		struct disk *d = &disks[i];
		unsigned long long f[10];
		/* reads merged sectors ms, writes merged sectors ms, in flight, io ticks */
		if (rd(d->fd, buf, 512) <= 0 ||
		    sscanf(buf, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &f[0], &f[1], &f[2],
			   &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9]) != 10)
			continue;
		if (emit && d->seen && (f[0] != d->r || f[2] != d->rs || f[4] != d->w || f[6] != d->ws ||
					f[9] != d->io || f[8]))
			ap(" k%d=%llu:%llu:%llu:%llu:%llu:%llu", i, f[0] - d->r, f[2] - d->rs, f[4] - d->w,
			   f[6] - d->ws, f[9] - d->io, f[8]);
		d->r = f[0]; d->rs = f[2]; d->w = f[4]; d->ws = f[6]; d->io = f[9];
		d->seen = 1;
	}
}

/* --- the VRAM swap's own counters (sysfs; the driver takes its lock: slow ticks only) --- */
static int vs_fd = -1, vs_seen;
static unsigned long long vs_prev[5];

static void vs_read(char *buf, int emit)
{
	unsigned long long v[5], vw, dw, vr, dr, ev;
	if (rd(vs_fd, buf, 4096) <= 0)
		return;
	char *p = strstr(buf, "writes VRAM");
	if (!p || sscanf(p, "writes VRAM %llu disk %llu, reads VRAM %llu disk %llu, written back %llu",
			 &vw, &dw, &vr, &dr, &ev) != 5)
		return;
	v[0] = vr; v[1] = dr; v[2] = vw; v[3] = dw; v[4] = ev;
	if (emit && vs_seen && memcmp(v, vs_prev, sizeof(v)))
		ap(" vs=%llu:%llu:%llu:%llu:%llu", v[0] - vs_prev[0], v[1] - vs_prev[1],
			v[2] - vs_prev[2], v[3] - vs_prev[3], v[4] - vs_prev[4]);
	memcpy(vs_prev, v, sizeof(v));
	vs_seen = 1;
}

/* --- /proc/stat's cpu line: is the machine busy (CPU contention)? --- */
static int st_fd = -1;
static unsigned long long st_busy, st_idle, st_iow;
static int st_seen;

static void st_read(char *buf, int emit)
{
	unsigned long long u, ni, sy, id, iw, irq, sirq, steal;
	if (rd(st_fd, buf, BUF) <= 0 ||
	    sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &ni, &sy, &id, &iw, &irq,
		   &sirq, &steal) != 8)
		return;
	unsigned long long busy = u + ni + sy + irq + sirq + steal;
	if (emit && st_seen)
		ap(" cpu=%llu:%llu:%llu", busy - st_busy, id - st_idle, iw - st_iow);
	st_busy = busy; st_idle = id; st_iow = iw; st_seen = 1;
}

/* --- syscalls (numbers of this architecture, for stalls.py) --- */
struct sysname { const char *name; long nr; int fdarg; };
#define S(n, f) { #n, SYS_##n, f }
static const struct sysname sysnames[] = {
#ifdef SYS_read
	S(read, 1),
#endif
#ifdef SYS_write
	S(write, 1),
#endif
#ifdef SYS_pread64
	S(pread64, 1),
#endif
#ifdef SYS_pwrite64
	S(pwrite64, 1),
#endif
#ifdef SYS_readv
	S(readv, 1),
#endif
#ifdef SYS_writev
	S(writev, 1),
#endif
#ifdef SYS_preadv
	S(preadv, 1),
#endif
#ifdef SYS_preadv2
	S(preadv2, 1),
#endif
#ifdef SYS_ioctl
	S(ioctl, 1),
#endif
#ifdef SYS_recvmsg
	S(recvmsg, 1),
#endif
#ifdef SYS_recvfrom
	S(recvfrom, 1),
#endif
#ifdef SYS_sendmsg
	S(sendmsg, 1),
#endif
#ifdef SYS_sendto
	S(sendto, 1),
#endif
#ifdef SYS_fsync
	S(fsync, 1),
#endif
#ifdef SYS_fdatasync
	S(fdatasync, 1),
#endif
#ifdef SYS_getdents64
	S(getdents64, 1),
#endif
#ifdef SYS_futex
	S(futex, 0),
#endif
#ifdef SYS_futex_waitv
	S(futex_waitv, 0),
#endif
#ifdef SYS_poll
	S(poll, 0),
#endif
#ifdef SYS_ppoll
	S(ppoll, 0),
#endif
#ifdef SYS_select
	S(select, 0),
#endif
#ifdef SYS__newselect
	S(_newselect, 0),
#endif
#ifdef SYS_pselect6
	S(pselect6, 0),
#endif
#ifdef SYS_epoll_wait
	S(epoll_wait, 0),
#endif
#ifdef SYS_epoll_pwait
	S(epoll_pwait, 0),
#endif
#ifdef SYS_epoll_pwait2
	S(epoll_pwait2, 0),
#endif
#ifdef SYS_nanosleep
	S(nanosleep, 0),
#endif
#ifdef SYS_clock_nanosleep
	S(clock_nanosleep, 0),
#endif
#ifdef SYS_sched_yield
	S(sched_yield, 0),
#endif
#ifdef SYS_wait4
	S(wait4, 0),
#endif
#ifdef SYS_mmap
	S(mmap, 0),
#endif
#ifdef SYS_munmap
	S(munmap, 0),
#endif
#ifdef SYS_mprotect
	S(mprotect, 0),
#endif
#ifdef SYS_madvise
	S(madvise, 0),
#endif
#ifdef SYS_brk
	S(brk, 0),
#endif
#ifdef SYS_open
	S(open, 0),
#endif
#ifdef SYS_openat
	S(openat, 0),
#endif
#ifdef SYS_close
	S(close, 0),
#endif
#ifdef SYS_newfstatat
	S(newfstatat, 0),
#endif
#ifdef SYS_statx
	S(statx, 0),
#endif
#ifdef SYS_exit_group
	S(exit_group, 0),
#endif
};
#define NSYS (sizeof(sysnames) / sizeof(sysnames[0]))

static const struct sysname *sys_find(long nr)
{
	for (unsigned i = 0; i < NSYS; i++)
		if (sysnames[i].nr == nr)
			return &sysnames[i];
	return NULL;
}
/* a thread asleep in one of these waits on purpose (another thread, X, a timer): no stack */
static int sys_voluntary(long nr)
{
	const struct sysname *s = sys_find(nr);
	static const char *v[] = { "futex", "futex_waitv", "poll", "ppoll", "select", "_newselect",
				   "pselect6", "epoll_wait", "epoll_pwait", "epoll_pwait2", "nanosleep",
				   "clock_nanosleep", "wait4", "recvmsg", "recvfrom", NULL };
	if (!s)
		return 0;
	for (const char **p = v; *p; p++)
		if (!strcmp(*p, s->name))
			return 1;
	return 0;
}

/* --- the game --- */
static pid_t pid, tid;
static int g_stat = -1, t_stat = -1, t_wchan = -1, t_sys = -1, t_stack = -1;
static clockid_t g_clock;
static int have_clock;
static int64_t g_cpu;
static struct pstat g_prev, t_prev;
static pid_t dead_pid;
static const char *thread_comm;
static struct { int id; int64_t at; } fdcache[MAXFD];

static void game_close(void)
{
	xclose(&g_stat); xclose(&t_stat); xclose(&t_wchan); xclose(&t_sys); xclose(&t_stack);
	pid = tid = 0;
	have_clock = 0;
}

static pid_t find_thread(pid_t p, const char *comm)
{
	char path[64], c[64];
	struct dirent *de;
	pid_t found = 0;
	snprintf(path, sizeof(path), "/proc/%d/task", p);
	DIR *d = opendir(path);
	if (!d)
		return 0;
	while (!found && (de = readdir(d))) {
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		int fd = ropen("/proc/%d/task/%s/comm", p, de->d_name);
		int n = rd(fd, c, sizeof(c));
		xclose(&fd);
		if (n > 0 && c[n - 1] == '\n')
			c[n - 1] = 0;
		if (n > 0 && !strncmp(c, comm, 15))
			found = atoi(de->d_name);
	}
	closedir(d);
	return found;
}

static int game_open(pid_t p, int64_t t, char *buf)
{
	pid_t th = thread_comm ? find_thread(p, thread_comm) : p;
	char comm[64] = "?";
	int fd;
	if (!th)
		th = p;
	game_close();
	if ((g_stat = ropen("/proc/%d/stat", p)) < 0 || rd(g_stat, buf, 4096) <= 0 ||
	    parse_stat(buf, &g_prev)) {
		xclose(&g_stat);
		return -1;
	}
	t_stat = ropen("/proc/%d/task/%d/stat", p, th);
	t_wchan = ropen("/proc/%d/task/%d/wchan", p, th);
	t_sys = ropen("/proc/%d/task/%d/syscall", p, th);
	t_stack = ropen("/proc/%d/task/%d/stack", p, th);
	if (rd(t_stat, buf, 4096) <= 0 || parse_stat(buf, &t_prev))
		memset(&t_prev, 0, sizeof(t_prev));
	fd = ropen("/proc/%d/task/%d/comm", p, th);
	if (rd(fd, comm, sizeof(comm)) > 0)
		comm[strcspn(comm, "\n")] = 0;
	xclose(&fd);
	have_clock = !clock_getcpuclockid(p, &g_clock);
	g_cpu = have_clock ? now_ns(g_clock) : 0;
	pid = p;
	tid = th;
	memset(fdcache, 0, sizeof(fdcache));
	fprintf(out, "P %ld pid=%d tid=%d comm=%s\n", t_us(t), p, th, comm);
	return 0;
}

/* the fd's target, as a D id (cached 2 s: fds get reused) */
static int fd_id(long fd, int64_t t)
{
	char path[64], target[256];
	ssize_t n;
	if (fd < 0)
		return 0;
	if (fd < MAXFD && fdcache[fd].id && t - fdcache[fd].at < 2000000000LL)
		return fdcache[fd].id;
	snprintf(path, sizeof(path), "/proc/%d/fd/%ld", pid, fd);
	n = readlink(path, target, sizeof(target) - 1);
	if (n <= 0)
		return 0;
	target[n] = 0;
	if (!strncmp(target, "socket:", 7))
		strcpy(target, "socket");
	else if (!strncmp(target, "pipe:", 5))
		strcpy(target, "pipe");
	for (char *c = target; *c; c++)
		if (isspace((unsigned char)*c))
			*c = '_';
	int id = str_id(&fdnames, target);
	if (fd < MAXFD) {
		fdcache[fd].id = id;
		fdcache[fd].at = t;
	}
	return id;
}

/* --- other processes followed by name (Xorg, kswapd0) --- */
struct other { const char *comm; pid_t pid; int fd, wfd; struct pstat prev; };
static struct other others[MAXO];
static int nothers;

static void others_find(char *buf)
{
	int missing = 0;
	for (int i = 0; i < nothers; i++)
		missing += others[i].fd < 0;
	if (!missing)
		return;
	DIR *d = opendir("/proc");
	struct dirent *de;
	if (!d)
		return;
	while ((de = readdir(d))) {
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		int fd = ropen("/proc/%s/comm", de->d_name), n = rd(fd, buf, 64);
		xclose(&fd);
		if (n <= 0)
			continue;
		buf[strcspn(buf, "\n")] = 0;
		for (int i = 0; i < nothers; i++) {
			struct other *o = &others[i];
			if (o->fd >= 0 || strncmp(buf, o->comm, 15))
				continue;
			o->pid = atoi(de->d_name);
			o->fd = ropen("/proc/%d/stat", o->pid);
			o->wfd = ropen("/proc/%d/wchan", o->pid);
			if (rd(o->fd, buf, 4096) <= 0 || parse_stat(buf, &o->prev)) {
				xclose(&o->fd);
				xclose(&o->wfd);
			}
			break;
		}
	}
	closedir(d);
}

static void others_sample(char *buf)
{
	for (int i = 0; i < nothers; i++) {
		struct other *o = &others[i];
		struct pstat s;
		if (o->fd < 0)
			continue;
		if (rd(o->fd, buf, 4096) <= 0 || parse_stat(buf, &s)) {
			xclose(&o->fd);
			xclose(&o->wfd);
			continue;
		}
		unsigned long mj = s.majflt - o->prev.majflt;
		unsigned long tk = s.utime + s.stime - o->prev.utime - o->prev.stime;
		if ((s.state != 'S' && s.state != 'I') || mj || tk) {
			ap(" o%d=%c:%lu:%lu", i, s.state, mj, tk);
			if (s.state == 'D' && rd(o->wfd, buf, 256) > 0 && strcmp(buf, "0"))
				ap(":%d", str_id(&wchans, buf));
		}
		o->prev = s;
	}
}

/* --- the HUD's frame time dump --- */
static const char *frame_path;
static int frame_fd = -1;

static long count_lines(int fd, char *buf)
{
	long n = 0;
	ssize_t r;
	while ((r = read(fd, buf, BUF)) > 0)
		for (char *p = buf; (p = memchr(p, '\n', buf + r - p)); p++)
			n++;
	return n;
}

static long pidfile_read(const char *path)
{
	char b[32];
	int fd = open(path, O_RDONLY | O_CLOEXEC), n = rd(fd, b, sizeof(b));
	xclose(&fd);
	return n > 0 ? atol(b) : 0;
}

/* the kernel stack, frames joined by '|' with offsets dropped */
static int nstacks;
static void stack_dump(int64_t t, char state, char *buf)
{
	int n = rd(t_stack, buf, 8192);
	if (n <= 0)
		return;
	fprintf(out, "K %ld %d %c ", t_us(t), tid, state);
	int first = 1;
	for (char *l = strtok(buf, "\n"); l; l = strtok(NULL, "\n")) {
		char *f = strchr(l, ']');
		f = f ? f + 2 : l;
		f[strcspn(f, "+ ")] = 0;
		fprintf(out, "%s%s", first ? "" : "|", f);
		first = 0;
	}
	fputc('\n', out);
	nstacks++;
}

int main(int argc, char **argv)
{
	const char *outp = NULL, *pidfile = NULL, *stopfile = NULL;
	double period_ms = 10;
	int slow = 10, stack_ms = 50, max_stacks = 1000, prio = 1, lock = 0, opt;
	pid_t fixed = 0;
	static char buf[BUF];
	struct utsname un;

	while ((opt = getopt(argc, argv, "o:i:p:P:T:f:s:x:d:S:k:K:RnL")) != -1) {
		switch (opt) {
		case 'o': outp = optarg; break;
		case 'i': period_ms = atof(optarg); break;
		case 'p': pidfile = optarg; break;
		case 'P': fixed = atoi(optarg); break;
		case 'T': thread_comm = optarg; break;
		case 'f': frame_path = optarg; break;
		case 's': stopfile = optarg; break;
		case 'x': if (nothers < MAXO) others[nothers++] = (struct other){ optarg, 0, -1, -1, {0} }; break;
		case 'd': add_disk(optarg); break;
		case 'S': slow = atoi(optarg) > 0 ? atoi(optarg) : 1; break;
		case 'k': stack_ms = atoi(optarg); break;
		case 'K': max_stacks = atoi(optarg); break;
		case 'R': prio = 2; break;
		case 'n': prio = 0; break;
		case 'L': lock = 1; break;
		default:
			fprintf(stderr, "usage: see the head of ps3-stallrec.c (docs/STALLS.md)\n");
			return 2;
		}
	}
	if (period_ms < 0.5)
		die("-i: at least 0.5 ms");
	out = outp ? fopen(outp, "w") : stdout;
	if (!out)
		die("%s: %s", outp, strerror(errno));
	setvbuf(out, NULL, _IOFBF, BUF);
	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	signal(SIGHUP, on_sig);
	if (prio == 2) {
		struct sched_param sp = { .sched_priority = 1 };
		if (sched_setscheduler(0, SCHED_FIFO, &sp))
			prio = 1;
	}
	if (prio == 1)
		setpriority(PRIO_PROCESS, 0, -10);
	int locked = lock && !mlockall(MCL_CURRENT | MCL_FUTURE);

	t_start = now_ns(CLOCK_MONOTONIC);
	int64_t real0 = now_ns(CLOCK_REALTIME);
	uname(&un);
	fprintf(out, "# stallrec %d interval_us=%ld slow=%d arch=%s kernel=%s locked=%d prio=%s\n",
		VERSION, (long)(period_ms * 1000), slow, un.machine, un.release, locked,
		prio == 2 ? "fifo" : prio == 1 ? "nice-10" : "none");
	fprintf(out, "# clock mono=%lld.%09lld real=%lld.%09lld\n", (long long)(t_start / 1000000000),
		(long long)(t_start % 1000000000), (long long)(real0 / 1000000000),
		(long long)(real0 % 1000000000));
	fprintf(out, "# hz=%ld\n", sysconf(_SC_CLK_TCK));
	fprintf(out, "# sys");
	for (unsigned i = 0; i < NSYS; i++)
		fprintf(out, " %s=%ld", sysnames[i].name, sysnames[i].nr);
	fputc('\n', out);

	vm_init(buf);
	add_disk("sda");
	add_disk("zram0");
	add_disk("ps3vswap0");
	{	/* the VRAM swap's backing partition: its disk reads and writes */
		int fd = ropen("/sys/block/ps3vswap0/backing");
		if (rd(fd, buf, 256) > 0) {
			buf[strcspn(buf, "\n")] = 0;
			char *b = strrchr(buf, '/');
			add_disk(b ? b + 1 : buf);
		}
		xclose(&fd);
	}
	fprintf(out, "# disk");
	for (int i = 0; i < ndisks; i++)
		fprintf(out, " %d=%s", i, disks[i].name);
	fputc('\n', out);
	ds_read(buf, 0);
	vs_fd = ropen("/sys/block/ps3vswap0/stats");
	vs_read(buf, 0);
	st_fd = ropen("/proc/stat");
	st_read(buf, 0);
	fprintf(out, "# other");
	for (int i = 0; i < nothers; i++)
		fprintf(out, " %d=%s", i, others[i].comm);
	fputc('\n', out);
	others_find(buf);

	int64_t period = (int64_t)(period_ms * 1e6), next = t_start, last = t_start, maxgap = 0;
	int64_t blocked_since = 0, last_stack = 0, last_clock = t_start, last_flush = t_start;
	long samples = 0, late = 0, tick = 0;
	int64_t cost[6] = { 0 };	/* ns: game, frames+vmstat, disks, others, slow tail, tail */

	while (!stop) {
		next += period;
		struct timespec ts = { next / 1000000000, next % 1000000000 };
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR && !stop)
			;
		if (stop)
			break;
		int64_t t = now_ns(CLOCK_MONOTONIC);
		if (t - last > period * 3 / 2)
			late++;
		if (t - last > maxgap)
			maxgap = t - last;
		if (t - next > period)		/* missed ticks: skip them, keep the phase */
			next += (t - next) / period * period;
		last = t;
		int is_slow = tick++ % slow == 0;

		/* the game: found through the pid file, followed until it exits */
		if (!pid && (fixed || (pidfile && is_slow))) {
			pid_t p = fixed ? fixed : (pid_t)pidfile_read(pidfile);
			if (p > 0 && p != dead_pid && game_open(p, t, buf))
				dead_pid = p;
		}
		if (frame_path && frame_fd < 0 && is_slow) {
			frame_fd = open(frame_path, O_RDONLY | O_CLOEXEC);
			if (frame_fd >= 0)
				fprintf(out, "F %ld n0=%ld\n", t_us(t), count_lines(frame_fd, buf));
		}

		struct pstat g, th;
		int want_stack = 0;
		if (pid && (rd(g_stat, buf, 4096) <= 0 || parse_stat(buf, &g))) {
			fprintf(out, "P %ld gone\n", t_us(t));
			dead_pid = pid;
			game_close();
		}
		linelen = 0;
		ap("%ld", t_us(t));
		int64_t p0 = now_ns(CLOCK_MONOTONIC);
		if (pid) {
			if (rd(t_stat, buf, 4096) <= 0 || parse_stat(buf, &th))
				th = t_prev;
			ap(" s=%c", th.state);
			if (th.state != 'R') {
				long nr = LONG_MIN, a0 = -1;
				if (rd(t_wchan, buf, 256) > 0 && strcmp(buf, "0") && *buf)
					ap(" w=%d", str_id(&wchans, buf));
				if (rd(t_sys, buf, 256) > 0 && strncmp(buf, "running", 7)) {
					if (sscanf(buf, "%ld %lx", &nr, &a0) >= 1) {
						ap(" y=%ld", nr);
						const struct sysname *s = sys_find(nr);
						int id = s && s->fdarg ? fd_id(a0, t) : 0;
						if (id)
							ap(" d=%d", id);
					}
				}
				/* blocked: D, or asleep outside the voluntary waits */
				if (th.state == 'D' || (nr != LONG_MIN && !sys_voluntary(nr))) {
					if (!blocked_since)
						blocked_since = t;
				} else
					blocked_since = 0;
				want_stack = blocked_since && max_stacks && nstacks < max_stacks &&
					     t - blocked_since >= (int64_t)stack_ms * 1000000 &&
					     t - last_stack >= 250000000LL;
			} else
				blocked_since = 0;
			if (have_clock) {
				int64_t c = now_ns(g_clock);
				if (c - g_cpu >= 1000)
					ap(" c=%lld", (long long)((c - g_cpu) / 1000));
				g_cpu = c;
			}
#define D(key, a, b) if ((a) != (b)) ap(" " key "=%lu", (a) - (b))
			D("mj", th.majflt, t_prev.majflt);
			D("mn", th.minflt, t_prev.minflt);
			D("mu", th.utime, t_prev.utime);
			D("ms", th.stime, t_prev.stime);
			D("Mj", g.majflt, g_prev.majflt);
			D("Mn", g.minflt, g_prev.minflt);
			g_prev = g;
			t_prev = th;
		}
		int64_t p1 = now_ns(CLOCK_MONOTONIC);
		if (frame_fd >= 0) {
			long n = count_lines(frame_fd, buf);
			if (n)
				ap(" f=%ld", n);
		}
		{
			int n = rd(vm_fd, buf, BUF);
			if (n > 0)
				vm_read(buf, n, 1);
		}
		int64_t p2 = now_ns(CLOCK_MONOTONIC);
		ds_read(buf, 1);
		int64_t p3 = now_ns(CLOCK_MONOTONIC);
		others_sample(buf);
		int64_t p4 = now_ns(CLOCK_MONOTONIC);
		if (is_slow) {
			vs_read(buf, 1);
			st_read(buf, 1);
			ap(" fr=%llu", vm_free);
		}
		ap("\n");
		fwrite(line, 1, linelen, out);
		int64_t p5 = now_ns(CLOCK_MONOTONIC);
		cost[0] += p1 - p0; cost[1] += p2 - p1; cost[2] += p3 - p2; cost[3] += p4 - p3;
		cost[is_slow ? 4 : 5] += p5 - p4;
		samples++;
		if (want_stack) {
			stack_dump(t, t_prev.state, buf);
			last_stack = t;
		}
		if (is_slow) {
			if (nothers && tick % (slow * 10) == 1)
				others_find(buf);
			if (t - last_clock >= 10000000000LL) {
				int64_t r = now_ns(CLOCK_REALTIME);
				fprintf(out, "C %ld real=%lld.%09lld\n", t_us(t), (long long)(r / 1000000000),
					(long long)(r % 1000000000));
				last_clock = t;
			}
			if (stopfile && !access(stopfile, F_OK))
				stop = 1;
			if (t - last_flush >= 1000000000LL) {
				fflush(out);
				last_flush = t;
			}
		}
	}
	int64_t t = now_ns(CLOCK_MONOTONIC), r = now_ns(CLOCK_REALTIME);
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	fprintf(out, "C %ld real=%lld.%09lld\n", t_us(t), (long long)(r / 1000000000),
		(long long)(r % 1000000000));
	fprintf(out, "# end t=%ld samples=%ld late=%ld maxgap_us=%lld self_user_us=%lld "
		"self_sys_us=%lld maxrss_kb=%ld stacks=%d\n", t_us(t), samples, late,
		(long long)(maxgap / 1000), (long long)ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec,
		(long long)ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec, ru.ru_maxrss, nstacks);
	/* where the time goes, wall clock per sample (the slow tail per slow tick) */
	long ns = samples ? samples : 1, nslow = samples ? (samples + slow - 1) / slow : 1;
	fprintf(out, "# cost_us game=%.1f vmstat=%.1f disks=%.1f others=%.1f slow=%.1f write=%.1f\n",
		cost[0] / 1e3 / ns, cost[1] / 1e3 / ns, cost[2] / 1e3 / ns, cost[3] / 1e3 / ns,
		cost[4] / 1e3 / nslow, cost[5] / 1e3 / (ns - nslow > 0 ? ns - nslow : 1));
	fclose(out);
	return 0;
}
