/*
 * ps3-mallocstat.so: glibc's malloc statistics of a running game, on request (docs/PROFILING.md).
 * games/bench/client.sh (MEM=1) preloads it into the game:
 *
 *   LD_PRELOAD=/usr/lib/ps3/lib/ps3-mallocstat.so PS3_MALLOCSTAT_FIFO=<fifo>
 *   PS3_MALLOCSTAT_DIR=<dir> PS3_MALLOCSTAT_COMM=<comm> <game>
 *
 * In the process whose name (/proc/self/comm, at most 15 characters) is PS3_MALLOCSTAT_COMM
 * (every process when it is unset) the constructor starts one thread with a 64 KiB stack. It
 * blocks reading the fifo; each line written there is a label, and for each the thread writes
 * <dir>/<label>.malloc: a "mallinfo2 ..." line (all arenas summed, bytes) followed by
 * malloc_info()'s XML (per arena: free chunks by size, mmapped, system bytes).
 *
 * No signal handler: a request is a line in a fifo, so a game that uses SIGUSR1/2 or blocks
 * signals is not disturbed, and the statistics are taken from an ordinary thread (malloc_info
 * takes each arena's lock in turn, which a signal handler interrupting malloc could deadlock on).
 * Other processes (the launcher's shell, stdbuf, ps3-game) get the library but start nothing.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char fifo_path[512], out_dir[512];
/* the output stream, opened by the constructor (main thread, main arena) and reopened for every
 * dump with a static buffer: the thread itself never calls malloc, so glibc never gives it an
 * arena of its own (it would add one to the statistics it reports) */
static FILE *out;
static char iobuf[16384];

static void dump(const char *label)
{
	char path[1100], tmp[1110];
	struct mallinfo2 mi;
	struct timespec ts;

	if (!out)
		return;
	snprintf(path, sizeof(path), "%s/%s.malloc", out_dir, label);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (!freopen(tmp, "w", out)) {
		out = NULL;	/* freopen closed it: no more dumps */
		return;
	}
	setvbuf(out, iobuf, _IOFBF, sizeof(iobuf));
	clock_gettime(CLOCK_REALTIME, &ts);
	mi = mallinfo2();
	fprintf(out, "mallinfo2 time=%lld.%06ld arena=%zu ordblks=%zu smblks=%zu hblks=%zu hblkhd=%zu "
		"usmblks=%zu fsmblks=%zu uordblks=%zu fordblks=%zu keepcost=%zu\n",
		(long long)ts.tv_sec, ts.tv_nsec / 1000, mi.arena, mi.ordblks, mi.smblks, mi.hblks,
		mi.hblkhd, mi.usmblks, mi.fsmblks, mi.uordblks, mi.fordblks, mi.keepcost);
	malloc_info(0, out);
	if (!freopen("/dev/null", "w", out)) {	/* flushes and closes the dump */
		out = NULL;
		return;
	}
	setvbuf(out, iobuf, _IOFBF, sizeof(iobuf));
	rename(tmp, path);	/* the reader sees a whole file or none */
}

static void *serve(void *arg)
{
	char buf[256], label[128];
	size_t n = 0;
	int fd;

	(void)arg;
	/* O_RDWR: never EOF when a writer closes, and the open doesn't wait for one */
	fd = open(fifo_path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	for (;;) {
		ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
		char *nl;

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		n += r;
		buf[n] = 0;
		while ((nl = strchr(buf, '\n'))) {
			size_t len = nl - buf, i, j = 0;

			for (i = 0; i < len && j < sizeof(label) - 1; i++)	/* a file name, nothing else */
				if ((buf[i] >= 'a' && buf[i] <= 'z') || (buf[i] >= 'A' && buf[i] <= 'Z') ||
				    (buf[i] >= '0' && buf[i] <= '9') || buf[i] == '-' || buf[i] == '_')
					label[j++] = buf[i];
			label[j] = 0;
			if (j)
				dump(label);
			memmove(buf, nl + 1, n - len - 1);
			n -= len + 1;
			buf[n] = 0;
		}
		if (n == sizeof(buf) - 1)	/* a line too long: dropped */
			n = 0;
	}
	close(fd);
	return NULL;
}

__attribute__((constructor)) static void start(void)
{
	const char *fifo = getenv("PS3_MALLOCSTAT_FIFO"), *dir = getenv("PS3_MALLOCSTAT_DIR");
	const char *want = getenv("PS3_MALLOCSTAT_COMM");
	char comm[32] = "";
	pthread_attr_t a;
	pthread_t t;
	sigset_t all, old;
	int fd;

	if (!fifo || !*fifo || strlen(fifo) >= sizeof(fifo_path))
		return;
	if (want && *want) {
		fd = open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			ssize_t r = read(fd, comm, sizeof(comm) - 1);

			close(fd);
			comm[r > 0 ? r : 0] = 0;
			comm[strcspn(comm, "\n")] = 0;
		}
		if (strncmp(comm, want, 15))	/* the kernel keeps 15 characters of the name */
			return;
	}
	strcpy(fifo_path, fifo);
	snprintf(out_dir, sizeof(out_dir), "%s", dir && *dir ? dir : ".");
	out = fopen("/dev/null", "w");
	if (!out)
		return;
	setvbuf(out, iobuf, _IOFBF, sizeof(iobuf));
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 64 << 10);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	/* the thread takes no signals: the game's handlers stay on its own threads */
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	if (pthread_create(&t, &a, serve, NULL) == 0)
		pthread_setname_np(t, "ps3-mallocstat");
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	pthread_attr_destroy(&a);
}
