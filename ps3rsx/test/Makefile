# Equivalence fuzz of shadow.c's fast replay (shadow_fast) against the word-by-word one, in
# userspace (inc/: kernel stand-ins). Outputs in O (build/, not here).
#   make && make run		x86 with ASan/UBSan, x86 plain, ppc64 (big-endian) under qemu
#   ./count.sh			ppc64 instructions per word, both paths (qemu exec log)
# ppc64 needs a ppc64 sysroot (SYSROOT: the Mesa cross build's) and qemu-ppc64-static.
O ?= ../../build/tmp/ps3rsx-test
SYSROOT ?= ../../build/mesa/sysroot
SRC := shadow-equiv.c ../src/shadow.c inc/shim.h
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -fno-strict-aliasing -Iinc
PPCFLAGS := --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include/powerpc64-linux-gnu \
	-isystem $(SYSROOT)/usr/include -B$(SYSROOT)/usr/lib/powerpc64-linux-gnu -L$(O) \
	-L$(SYSROOT)/usr/lib/powerpc64-linux-gnu -L$(SYSROOT)/lib/powerpc64-linux-gnu -static

all: $(O)/shadow-equiv $(O)/shadow-equiv-fast $(O)/shadow-equiv-ppc64
$(O)/shadow-equiv: $(SRC)
	@mkdir -p $(O)
	gcc $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
$(O)/shadow-equiv-fast: $(SRC)
	@mkdir -p $(O)
	gcc $(CFLAGS) -o $@ $<
# gcc 16 links -latomic_asneeded, which the Debian sysroot lacks: an empty one does
$(O)/libatomic_asneeded.a:
	@mkdir -p $(O)
	powerpc64-linux-gnu-ar rc $@
$(O)/shadow-equiv-ppc64: $(SRC) $(O)/libatomic_asneeded.a
	powerpc64-linux-gnu-gcc $(CFLAGS) $(PPCFLAGS) -o $@ $<
$(O)/cnt-ppc64: $(SRC) $(O)/libatomic_asneeded.a
	powerpc64-linux-gnu-gcc -O2 -mcpu=cell -mno-altivec -fno-strict-aliasing -w -Iinc \
		$(PPCFLAGS) -o $@ $<
run: all
	$(O)/shadow-equiv 2000000 200000 1
	$(O)/shadow-equiv-fast 5000000 500000 2
	qemu-ppc64-static $(O)/shadow-equiv-ppc64 1000000 100000 3
clean:
	rm -rf $(O)
.PHONY: all run clean
