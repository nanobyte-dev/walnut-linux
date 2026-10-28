/* rtaudio.c FIFO: a stand-in game for rt-audio-check.sh. Threads: "SDLAudioP1" (named like SDL's
 * audio thread, sleeping), an unnamed one in poll on FIFO and an eventfd (like miniaudio on dmix's
 * timer), an unnamed one in poll on an eventfd only, one in nanosleep; the main thread polls FIFO
 * too (never to be picked). Prints "ready <tid of each>" and runs until killed. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fifo, ev;
static pid_t tids[4];

static void *named(void *a) { tids[0] = syscall(SYS_gettid); prctl(PR_SET_NAME, "SDLAudioP1"); for (;;) usleep(100000); return a; }
static void *sound(void *a) {
	struct pollfd p[2] = { { ev, POLLIN, 0 }, { fifo, POLLIN, 0 } };
	tids[1] = syscall(SYS_gettid);
	for (;;) poll(p, 2, -1);
	return a;
}
static void *other(void *a) {
	struct pollfd p[1] = { { ev, POLLIN, 0 } };
	tids[2] = syscall(SYS_gettid);
	for (;;) poll(p, 1, -1);
	return a;
}
static void *sleeper(void *a) { tids[3] = syscall(SYS_gettid); for (;;) sleep(10); return a; }

int main(int argc, char **argv) {
	pthread_t t;
	struct pollfd p[1];
	if (argc != 2 || (fifo = open(argv[1], O_RDWR)) < 0 || (ev = eventfd(0, 0)) < 0) return 2;
	pthread_create(&t, 0, named, 0); pthread_create(&t, 0, sound, 0);
	pthread_create(&t, 0, other, 0); pthread_create(&t, 0, sleeper, 0);
	while (!tids[0] || !tids[1] || !tids[2] || !tids[3]) usleep(10000);
	usleep(100000);
	printf("ready %d %d %d %d\n", tids[0], tids[1], tids[2], tids[3]);
	fflush(stdout);
	p[0].fd = fifo; p[0].events = POLLIN;
	for (;;) poll(p, 1, -1);
}
