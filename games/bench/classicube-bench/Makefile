# ccbench.so, ClassiCube's benchmark plugin, for the PS3 (ppc64 big-endian, ELFv1), built against the
# headers of the upstream source our classicube package is built from (CC_SRC: the unpacked
# tarball, with src/). The top-level `make classicube-bench` runs this in the builder image (its
# cross GCC is the snapshot's, the same as the package's) and puts the result in
# games/bench/assets/classicube-bench.so.
ifeq ($(origin CC),default)
CC      := powerpc64-linux-gnu-gcc
endif
CC_SRC  ?= $(error CC_SRC: the unpacked ClassiCube source)
OUT     ?= ccbench.so
# as the package builds the game (classicube/debian/rules: -mcpu=cell, SDL3 window); a host test
# build: CC=gcc CPU=
CPU     ?= -mcpu=cell -mtune=cell
CFLAGS  := -O2 $(CPU) -fPIC -fvisibility=hidden -fno-ident -Wall -Wno-unused-function \
	-DCC_WIN_BACKEND=CC_WIN_BACKEND_SDL3 -ffile-prefix-map=$(CC_SRC)=classicube
LDFLAGS := -shared -s -Wl,--build-id=none

$(OUT): ccbench.c
	$(CC) $(CFLAGS) -I$(CC_SRC) -o $@ $< $(LDFLAGS)

.PHONY: clean
clean:
	rm -f ccbench.so
