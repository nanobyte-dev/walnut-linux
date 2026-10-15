/*
 * rsx-hold: takes the GPU FIFO from ps3fb (which also stops its vsync blit/flip thread), holds
 * it for N seconds, then hands it back. Lets a test flip the display without ps3fb flipping
 * straight back.   rsx-hold [seconds, default 10]
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PS3GPU_IOCTL_EXIT  _IOW('r', 8, int)
#define PS3GPU_IOCTL_ENTER _IOW('r', 15, int)

int main(int argc, char **argv)
{
	int arg = 0, fb = open("/dev/fb0", O_RDWR);
	if (fb < 0) { perror("/dev/fb0"); return 1; }
	if (ioctl(fb, PS3GPU_IOCTL_ENTER, &arg)) { perror("PS3GPU_IOCTL_ENTER"); return 1; }
	sleep(argc > 1 ? atoi(argv[1]) : 10);
	if (ioctl(fb, PS3GPU_IOCTL_EXIT, &arg)) { perror("PS3GPU_IOCTL_EXIT"); return 1; }
	return 0;
}
