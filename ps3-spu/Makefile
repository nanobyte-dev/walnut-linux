# ps3-spu: libspeq (lib/), its tools (tools/) and spurun (src/) for the PPE; the SPU programs
# (spu/, spu-elf GCC). debian/rules builds `ppe` (-B, in the ppc64 chroot) and `spu` (-A, our
# toolchain on PATH); the top-level make spu-tests builds hello.spu and spurun in containers.
#   make test       libspeq and speq-dxt with the host cc, speq-dxt --check, the dlopen check
#   make test-mesa  our encoder against Mesa's on a corpus (tests/mesa-compare.sh)
CC      ?= cc
SPU_CC  ?= spu-elf-gcc
OUT     ?= b
CFLAGS  ?= -O2 -g
override CFLAGS += -Wall -std=gnu11
TEST_OUT ?= ../build/speq-test
MESA    ?= ../build/work/mesa

SPEQ_SRC := lib/s3tc.c lib/dxt-ref.c lib/spe.c
SPEQ_HDR := $(wildcard lib/*.h)

.PHONY: ppe spu test test-mesa
ppe: $(OUT)/libspeq.so.0 $(OUT)/speq-dxt $(OUT)/spurun
spu: $(OUT)/hello.spu

# only speq_* exported (lib/libspeq.map), SONAME libspeq.so.0
$(OUT)/libspeq.so.0: $(SPEQ_SRC) $(SPEQ_HDR) lib/libspeq.map
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -Wl,-soname,libspeq.so.0 \
		-Wl,--version-script=lib/libspeq.map -Wl,-z,defs $(LDFLAGS) -o $@ $(SPEQ_SRC)
# the tools carry their own copy of the library code (no dependency cycle with libspeq0)
$(OUT)/speq-dxt: tools/speq-dxt.c $(SPEQ_SRC) $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tools/speq-dxt.c $(SPEQ_SRC) -lm
$(OUT)/spurun: src/spurun.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<
$(OUT)/hello.spu: spu/hello.c
	@mkdir -p $(OUT)
	$(SPU_CC) -O2 -Wall -o $@ $<

# host tests (x86 or ppc64): nothing but cc
test:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/libspeq.so.0 $(TEST_OUT)/speq-dxt $(TEST_OUT)/dlopen-check
	$(TEST_OUT)/speq-dxt --check
	$(TEST_OUT)/dlopen-check $(TEST_OUT)/libspeq.so.0
$(OUT)/dlopen-check: tests/dlopen-check.c lib/dxt-ref.c $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tests/dlopen-check.c lib/dxt-ref.c -ldl

# Mesa's encoder (texcompress_s3tc_tmp.h from make mesa-work) as a host tool, then the corpus
# comparison (needs python3 with PIL, and the OpenArena textures deb in build/apt-cache)
$(OUT)/mesa-dxt: tests/mesa-dxt.c tests/shim/util/glheader.h $(MESA)/src/util/format/texcompress_s3tc_tmp.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -Itests/shim -I$(MESA)/src $(LDFLAGS) -o $@ tests/mesa-dxt.c
test-mesa: test
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/mesa-dxt
	tests/mesa-compare.sh $(TEST_OUT)
