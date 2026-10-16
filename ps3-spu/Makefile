# ps3-spu: libspeq (lib/), its tools (tools/) and spurun (src/) for the PPE; the SPU programs
# and libspeq-spu.a, the runtime they link (spu/, spu-elf GCC). debian/rules builds `ppe` (-B,
# in the ppc64 chroot) and `spu` (-A, our toolchain on PATH); the top-level make spu-tests
# builds both in containers.
#   make test       with the host cc: speq-dxt --check, the dlopen check, and the queue against
#                   the fake backend (SPEQ_FAKE=1: bench.spu's ops on host threads): queue-test,
#                   speq-bench-fake --quick
#   make test-mesa  our encoder against Mesa's on a corpus (tests/mesa-compare.sh)
CC      ?= cc
SPU_CC  ?= spu-elf-gcc
SPU_AR  ?= $(patsubst %gcc,%ar,$(SPU_CC))
OUT     ?= b
CFLAGS  ?= -O2 -g
override CFLAGS += -Wall -std=gnu11
SPU_CFLAGS ?= -O2 -g
override SPU_CFLAGS += -Wall -std=gnu11 -Ilib -Ispu
TEST_OUT ?= ../build/speq-test
MESA    ?= ../build/work/mesa

SPEQ_SRC := lib/s3tc.c lib/dxt-ref.c lib/queue.c lib/spe.c
SPEQ_HDR := $(wildcard lib/*.h)
# the fake backend: the SPU runtime and a program's ops built for the host (tests only)
FAKE_SRC := lib/fake.c spu/runtime.c spu/bench.c
SPU_HDR  := $(wildcard spu/*.h)
# dxt.spu's encoder at -O3, as timed (135 cycles per block)
DXT_SPU_OPT ?= -O3

.PHONY: ppe spu test test-mesa
ppe: $(OUT)/libspeq.so.0 $(OUT)/speq-dxt $(OUT)/spurun $(OUT)/speq-bench
spu: $(OUT)/hello.spu $(OUT)/libspeq-spu.a $(OUT)/bench.spu $(OUT)/dxt.spu

# only speq_* exported (lib/libspeq.map), SONAME libspeq.so.0
$(OUT)/libspeq.so.0: $(SPEQ_SRC) $(SPEQ_HDR) lib/libspeq.map
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -Wl,-soname,libspeq.so.0 \
		-Wl,--version-script=lib/libspeq.map -Wl,-z,defs $(LDFLAGS) -o $@ $(SPEQ_SRC) -lpthread
# the tools carry their own copy of the library code (no dependency cycle with libspeq0)
$(OUT)/speq-dxt: tools/speq-dxt.c $(SPEQ_SRC) $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tools/speq-dxt.c $(SPEQ_SRC) -lm -lpthread
$(OUT)/speq-bench: tools/speq-bench.c $(SPEQ_SRC) $(SPEQ_HDR) spu/bench.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-bench.c $(SPEQ_SRC) -lpthread
$(OUT)/spurun: src/spurun.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

# SPU side: libspeq-spu.a gives main and the claim loop; a program gives speq_ops[]
$(OUT)/hello.spu: spu/hello.c
	@mkdir -p $(OUT)
	$(SPU_CC) -O2 -Wall -o $@ $<
$(OUT)/runtime.o: spu/runtime.c $(SPU_HDR) lib/speq-abi.h lib/speq.h
	@mkdir -p $(OUT)
	$(SPU_CC) $(SPU_CFLAGS) -c -o $@ $<
$(OUT)/libspeq-spu.a: $(OUT)/runtime.o
	rm -f $@
	$(SPU_AR) rcs $@ $^
$(OUT)/bench.spu: spu/bench.c $(SPU_HDR) $(OUT)/libspeq-spu.a
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/bench.c $(OUT)/libspeq-spu.a
# dxt.spu: speq_s3tc_compress_v1's worker (spu/dxt-op.c: DMA, double buffering; spu/dxt.c)
$(OUT)/dxt.spu: spu/dxt-op.c spu/dxt.c $(SPU_HDR) lib/dxt-job.h $(OUT)/libspeq-spu.a
	$(SPU_CC) $(SPU_CFLAGS) $(DXT_SPU_OPT) -o $@ spu/dxt-op.c spu/dxt.c $(OUT)/libspeq-spu.a

# host tests (x86 or ppc64): nothing but cc
test:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/libspeq.so.0 $(TEST_OUT)/speq-dxt $(TEST_OUT)/dlopen-check \
		$(TEST_OUT)/queue-test $(TEST_OUT)/speq-bench-fake
	$(TEST_OUT)/speq-dxt --check
	$(TEST_OUT)/dlopen-check $(TEST_OUT)/libspeq.so.0
	SPEQ_FAKE=1 $(TEST_OUT)/queue-test $(wildcard $(TEST_OUT)/bench.spu)
	SPEQ_FAKE=1 $(TEST_OUT)/speq-bench-fake --quick --gap 0
$(OUT)/dlopen-check: tests/dlopen-check.c lib/dxt-ref.c $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tests/dlopen-check.c lib/dxt-ref.c -ldl
$(OUT)/queue-test: tests/queue-test.c $(SPEQ_SRC) $(FAKE_SRC) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tests/queue-test.c \
		$(SPEQ_SRC) $(FAKE_SRC) -lpthread
$(OUT)/speq-bench-fake: tools/speq-bench.c $(SPEQ_SRC) $(FAKE_SRC) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-bench.c \
		$(SPEQ_SRC) $(FAKE_SRC) -lpthread

# Mesa's encoder (texcompress_s3tc_tmp.h from make mesa-work) as a host tool, then the corpus
# comparison (needs python3 with PIL, and the OpenArena textures deb in build/apt-cache)
$(OUT)/mesa-dxt: tests/mesa-dxt.c tests/shim/util/glheader.h $(MESA)/src/util/format/texcompress_s3tc_tmp.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -Itests/shim -I$(MESA)/src $(LDFLAGS) -o $@ tests/mesa-dxt.c
test-mesa: test
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/mesa-dxt
	tests/mesa-compare.sh $(TEST_OUT)
