/*
 * rsx-peek: read or write a word in one of the ps3gpu mappings, for poking at the GPU state
 * while a test has it stalled.   rsx-peek <vram|fifo|ctrl|driver|reports> <offset> [value]
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <area> <offset> [value]\n", argv[0]); return 2; }
	char path[64];
	snprintf(path, sizeof(path), "/dev/ps3gpu_%s", argv[1]);
	unsigned long off = strtoul(argv[2], NULL, 0), page = off & ~0xfffUL;
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); return 1; }
	volatile uint8_t *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page);
	if (m == MAP_FAILED) { perror("mmap"); return 1; }
	volatile uint32_t *w = (volatile uint32_t *)(m + (off - page));
	if (argc > 3)
		*w = strtoul(argv[3], NULL, 0);
	for (int i = 0; i < 4; i++)
		printf("%s+%#lx: %08x\n", argv[1], off + i * 4, w[i]);
	return 0;
}
