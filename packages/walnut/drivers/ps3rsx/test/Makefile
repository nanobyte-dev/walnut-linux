# Userspace tests of ps3rsx code (inc/: kernel stand-ins). Outputs in O (build/, not here):
# shadow-equiv, the equivalence fuzz of shadow.c's fast replay (shadow_fast) against the word-by-word
# one, zero-test, ps3rsx_zero (src/zero.h, clear_dcbz) with the real dcbz loop on ppc64, and
# validate-test, the validator against the streams that raised graph exceptions (validate.c;
# fixtures/: mpv's first frame, captured with mesa-shim/), and validate-capture, a mesa-shim
# capture's pushes through the validator; fix-off, validate-test with one stability rule turned off
# at a time (fix-off.sh: a copy of validate.c edited under O, or an OLD_* define for the submit.c
# model), each build expected to fail with its own FAIL line.
#   make && make run		x86 with ASan/UBSan, x86 plain, ppc64 (big-endian) under qemu
#   make fix-off			the rules off, one at a time: exits 0 only if each is caught
#   ./count.sh			ppc64 instructions per word, both paths (qemu exec log)
# ppc64 needs a ppc64 sysroot (SYSROOT: the Mesa cross build's) and qemu-ppc64-static.
O ?= ../../../../../build/tmp/ps3rsx-test
SYSROOT ?= ../../../../../build/mesa/sysroot
SRC := shadow-equiv.c ../src/shadow.c inc/shim.h
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -fno-strict-aliasing -Iinc
PPCFLAGS := --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include/powerpc64-linux-gnu \
	-isystem $(SYSROOT)/usr/include -B$(SYSROOT)/usr/lib/powerpc64-linux-gnu -L$(O) \
	-L$(SYSROOT)/usr/lib/powerpc64-linux-gnu -L$(SYSROOT)/lib/powerpc64-linux-gnu -static

all: $(O)/shadow-equiv $(O)/shadow-equiv-fast $(O)/shadow-equiv-ppc64 $(O)/zero-test $(O)/zero-test-ppc64 \
	$(O)/validate-test $(O)/validate-capture
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
# ps3rsx_zero (src/zero.h, clear_dcbz): x86 with the sanitizers, ppc64 with the real dcbz loop
$(O)/zero-test: zero-test.c ../src/zero.h inc/shim.h
	@mkdir -p $(O)
	gcc $(CFLAGS) -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
$(O)/zero-test-ppc64: zero-test.c ../src/zero.h inc/shim.h $(O)/libatomic_asneeded.a
	powerpc64-linux-gnu-gcc $(CFLAGS) -Wno-unused-function $(PPCFLAGS) -o $@ $<
# the validator against the streams that raised graph exceptions (src/validate.c, fixtures/), and
# recovery from a rejected push (the GPU's stream checked whole, its state through src/shadow.c)
$(O)/validate-test: validate-test.c ../src/validate.c ../src/shadow.c inc/shim.h \
	fixtures/mpv-vo-gpu-0x105.h fixtures/webkit-compositing.h
	@mkdir -p $(O)
	gcc $(CFLAGS) -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
# a mesa-shim capture's pushes through the validator: validate-capture PUSH.BIN...
$(O)/validate-capture: validate-capture.c ../src/validate.c ../src/shadow.c inc/shim.h
	@mkdir -p $(O)
	gcc $(CFLAGS) -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $<
# the old behaviour of each stability rule fails its case (fix-off.sh)
fix-off: fix-off.sh validate-test.c ../src/validate.c ../src/shadow.c inc/shim.h
	./fix-off.sh $(O)
$(O)/cnt-ppc64: $(SRC) $(O)/libatomic_asneeded.a
	powerpc64-linux-gnu-gcc -O2 -mcpu=cell -mno-altivec -fno-strict-aliasing -w -Iinc \
		$(PPCFLAGS) -o $@ $<
run: all
	$(O)/shadow-equiv 2000000 200000 1
	$(O)/shadow-equiv-fast 5000000 500000 2
	qemu-ppc64-static $(O)/shadow-equiv-ppc64 1000000 100000 3
	$(O)/zero-test 20000 1
	qemu-ppc64-static $(O)/zero-test-ppc64 20000 2
	$(O)/validate-test
	./fix-off.sh $(O)
clean:
	rm -rf $(O)
.PHONY: all run clean fix-off
