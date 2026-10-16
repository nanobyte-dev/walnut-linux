# The SPU DXT1 encoder's test (standalone until it's wired into ps3-spu/Makefile):
#   make -f ps3-spu/spu/dxt-test.mk [O=dir] [all|check|clean]
# all:   $(O)/dxt-test.spu (SIMD, -O3) and $(O)/dxt-test-scalar.spu (DXT_SCALAR), for spurun
#        on the PS3, plus the host builds below
# check: on the host, the SIMD path through dxt-emu.h and the scalar path against the
#        reference: the embedded images at every offset and 2M fuzzed blocks per mode
# The data header comes from dxt-gen (host) with lib/dxt-ref.c.
HERE   := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ROOT   := $(HERE)/../..
O      ?= $(ROOT)/build/tmp/spu-dxt
SPU_CC ?= $(ROOT)/build/spu/install/opt/spu/bin/spu-elf-gcc
CC     ?= cc
SPU_CFLAGS ?= -O3
HOST_CFLAGS ?= -O2 -g
WARN   := -Wall -Wextra -std=gnu11
LIB    := $(HERE)/../lib

SPU_DEPS  := $(HERE)/dxt.c $(HERE)/dxt.h $(HERE)/dxt-test.c $(O)/dxt-test-data.h
HOST_DEPS := $(SPU_DEPS) $(HERE)/dxt-emu.h $(LIB)/dxt-ref.c $(LIB)/dxt-ref.h

.PHONY: all check clean
all: $(O)/dxt-test.spu $(O)/dxt-test-scalar.spu $(O)/dxt-test-host $(O)/dxt-test-host-scalar

$(O)/dxt-gen: $(HERE)/dxt-gen.c $(LIB)/dxt-ref.c $(LIB)/dxt-ref.h
	@mkdir -p $(O)
	$(CC) $(HOST_CFLAGS) $(WARN) -I$(LIB) -o $@ $(HERE)/dxt-gen.c $(LIB)/dxt-ref.c -lm

$(O)/dxt-test-data.h: $(O)/dxt-gen
	$(O)/dxt-gen > $@.tmp && mv $@.tmp $@

$(O)/dxt-test.spu: $(SPU_DEPS)
	$(SPU_CC) $(SPU_CFLAGS) $(WARN) -I$(O) -o $@ $(HERE)/dxt-test.c $(HERE)/dxt.c

$(O)/dxt-test-scalar.spu: $(SPU_DEPS)
	$(SPU_CC) $(SPU_CFLAGS) $(WARN) -DDXT_SCALAR -I$(O) -o $@ $(HERE)/dxt-test.c $(HERE)/dxt.c

$(O)/dxt-test-host: $(HOST_DEPS)
	$(CC) $(HOST_CFLAGS) $(WARN) -I$(O) -I$(LIB) -o $@ $(HERE)/dxt-test.c $(HERE)/dxt.c $(LIB)/dxt-ref.c

$(O)/dxt-test-host-scalar: $(HOST_DEPS)
	$(CC) $(HOST_CFLAGS) $(WARN) -DDXT_SCALAR -I$(O) -I$(LIB) -o $@ $(HERE)/dxt-test.c $(HERE)/dxt.c $(LIB)/dxt-ref.c

check: $(O)/dxt-test-host $(O)/dxt-test-host-scalar
	$(O)/dxt-test-host-scalar
	$(O)/dxt-test-host

clean:
	rm -rf $(O)
