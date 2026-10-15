/* spurun <spu-elf> [symbol]: runs an SPU program on an SPE through spufs (no libspe2), then
 * prints its exit code and, if given, the string at <symbol> in its local store.
 * Needs spufs mounted at /spu (modprobe spufs; mount -t spufs spufs /spu). */
#define _GNU_SOURCE
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LS_SIZE (256 * 1024)
#define SPU_STATUS_STOPPED_BY_STOP 0x2

static void die(const char *m) { perror(m); exit(1); }

int main(int argc, char **argv)
{
	char path[64];
	unsigned int npc, status;
	long ret;
	int fd, ctx, mem, i;
	struct stat st;
	uint8_t *elf, *ls;
	Elf32_Ehdr *eh;
	Elf32_Phdr *ph;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <spu-elf> [symbol]\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0 || fstat(fd, &st))
		die(argv[1]);
	elf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (elf == MAP_FAILED)
		die("mmap elf");
	eh = (Elf32_Ehdr *)elf;	/* SPU ELF is 32-bit big-endian, like the PPE's byte order */
	if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_machine != EM_SPU) {
		fprintf(stderr, "%s: not an SPU ELF\n", argv[1]);
		return 1;
	}

	snprintf(path, sizeof(path), "/spu/spurun-%d", getpid());
	ctx = syscall(SYS_spu_create, path, 0, 0755);
	if (ctx < 0)
		die("spu_create");
	snprintf(path, sizeof(path), "/spu/spurun-%d/mem", getpid());
	mem = open(path, O_RDWR);
	if (mem < 0)
		die("open mem");
	ls = mmap(NULL, LS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
	if (ls == MAP_FAILED)
		die("mmap local store");

	ph = (Elf32_Phdr *)(elf + eh->e_phoff);
	for (i = 0; i < eh->e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (ph[i].p_vaddr + ph[i].p_memsz > LS_SIZE) {
			fprintf(stderr, "segment %d doesn't fit local store\n", i);
			return 1;
		}
		memcpy(ls + ph[i].p_vaddr, elf + ph[i].p_offset, ph[i].p_filesz);
		memset(ls + ph[i].p_vaddr + ph[i].p_filesz, 0, ph[i].p_memsz - ph[i].p_filesz);
	}

	npc = eh->e_entry;
	ret = syscall(SYS_spu_run, ctx, &npc, NULL);	/* returns the SPU status register */
	if (ret < 0)
		die("spu_run");
	status = ret;
	if ((status & SPU_STATUS_STOPPED_BY_STOP) && (status >> 16) == 0x2100)
		printf("stopped: PPE-assisted call (stop 0x2100, e.g. newlib stdio): not supported\n");
	else if ((status & SPU_STATUS_STOPPED_BY_STOP) && ((status >> 16) & 0xff00) == 0x2000)
		printf("exit %u\n", (status >> 16) & 0xff);
	else if (status & SPU_STATUS_STOPPED_BY_STOP)
		printf("stopped: code 0x%04x\n", status >> 16);
	else
		printf("stopped: status 0x%08x, npc 0x%x\n", status, npc);

	if (argc > 2) {	/* look the symbol up in the ELF's symbol table */
		Elf32_Shdr *sh = (Elf32_Shdr *)(elf + eh->e_shoff);
		for (i = 0; i < eh->e_shnum; i++) {
			Elf32_Sym *sym;
			const char *names;
			unsigned int k, n;

			if (sh[i].sh_type != SHT_SYMTAB)
				continue;
			sym = (Elf32_Sym *)(elf + sh[i].sh_offset);
			names = (const char *)(elf + sh[sh[i].sh_link].sh_offset);
			n = sh[i].sh_size / sizeof(*sym);
			for (k = 0; k < n; k++)
				if (!strcmp(names + sym[k].st_name, argv[2]))
					printf("%s: \"%.*s\"\n", argv[2], (int)sym[k].st_size,
					       (const char *)ls + sym[k].st_value);
		}
	}
	return (status & SPU_STATUS_STOPPED_BY_STOP) && ((status >> 16) & 0xff00) == 0x2000 ?
		(status >> 16) & 0xff : 125;
}
