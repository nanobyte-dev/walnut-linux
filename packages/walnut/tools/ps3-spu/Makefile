# ps3-spu: libspeq (lib/), its tools (tools/) and spurun (src/) for the PPE; the SPU programs
# and libspeq-spu.a, the runtime they link (spu/, spu-elf GCC). debian/rules builds `ppe` (-B,
# in the ppc64 chroot) and `spu` (-A, our toolchain on PATH); the top-level make spu-tests
# builds both in containers.
#   make test       with the host cc: speq-dxt --check, the dlopen check, and the queue against
#                   the fake backend (SPEQ_FAKE=1: the SPU programs' ops on host threads):
#                   queue-test, speq-bench-fake --quick (bench.spu's ops), speq-dxt-fake --check
#                   on the spu and auto paths (dxt.spu's op and encoder: the whole SPU path),
#                   synth-test (deadline.spu's synth op, every read checked against its DMA plan,
#                   against libspeq's PPE path), speq-skin-fake --check on the spu and ppe paths
#                   (deadline.spu's skinning op and libspeq's PPE path against Sauerbraten's
#                   skinning written out in C; the spu path again with SPEQ_FAKE_RZ=1, rounding
#                   toward zero as the SPU does), pool-test (synth and skinning sharing the deadline
#                   pool next to DXT's bulk queue: deadlines, a slow job, faults)
#   make test-fluidsynth  fluidsynth with the libspeq hook against unpatched fluidsynth, both
#                   built for the host, on a generated MIDI file (tests/fluidsynth-compare.sh)
#   make test-mesa  our DXT1 and DXT5 encoders against Mesa's on a corpus (tests/mesa-compare.sh)
CC      ?= cc
SPU_CC  ?= spu-elf-gcc
SPU_AR  ?= $(patsubst %gcc,%ar,$(SPU_CC))
OUT     ?= b
CFLAGS  ?= -O2 -g
override CFLAGS += -Wall -std=gnu11
SPU_CFLAGS ?= -O2 -g
override SPU_CFLAGS += -Wall -std=gnu11 -Ilib -Ispu
TEST_OUT ?= ../../../../build/speq-test
MESA    ?= ../../../../build/work/mesa

SPEQ_SRC := lib/s3tc.c lib/dxt-ref.c lib/synth.c lib/skin.c lib/pool.c lib/queue.c lib/spe.c
SPEQ_HDR := $(wildcard lib/*.h)
# the fake backend: the SPU runtime and a program's ops built for the host (tests only)
FAKE_SRC := lib/fake.c spu/runtime.c spu/bench.c
FAKE_DXT := lib/fake.c spu/runtime.c spu/dxt-op.c spu/dxt.c
# deadline.spu: the deadline pool's program, the synth and skinning ops (lib/pool-job.h)
POOL_SPU := spu/deadline.c spu/synth-op.c spu/skin-op.c
POOL_HDR := lib/pool-job.h lib/synth-kern.h lib/synth-job.h lib/skin-kern.h lib/skin-job.h
FAKE_POOL := lib/fake.c spu/runtime.c $(POOL_SPU)
# every program at once: the pool next to DXT's bulk queue and bench.spu (pool-test)
FAKE_ALL := lib/fake.c spu/runtime.c $(POOL_SPU) spu/dxt-op.c spu/dxt.c spu/bench.c
SPU_HDR  := $(wildcard spu/*.h)
# dxt.spu's encoder at -O3, as timed (135 cycles per block)
DXT_SPU_OPT ?= -O3

.PHONY: ppe spu test test-mesa test-fluidsynth
ppe: $(OUT)/libspeq.so.0 $(OUT)/speq-dxt $(OUT)/spurun $(OUT)/speq-bench $(OUT)/speq-skin
spu: $(OUT)/hello.spu $(OUT)/libspeq-spu.a $(OUT)/bench.spu $(OUT)/dxt.spu $(OUT)/deadline.spu

# only speq_* exported (lib/libspeq.map), SONAME libspeq.so.0
$(OUT)/libspeq.so.0: $(SPEQ_SRC) $(SPEQ_HDR) lib/libspeq.map
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -Wl,-soname,libspeq.so.0 \
		-Wl,--version-script=lib/libspeq.map -Wl,-z,defs $(LDFLAGS) -o $@ $(SPEQ_SRC) -lm -lpthread
# the tools carry their own copy of the library code (no dependency cycle with libspeq0)
$(OUT)/speq-dxt: tools/speq-dxt.c $(SPEQ_SRC) $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tools/speq-dxt.c $(SPEQ_SRC) -lm -lpthread
$(OUT)/speq-bench: tools/speq-bench.c $(SPEQ_SRC) $(SPEQ_HDR) spu/bench.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-bench.c $(SPEQ_SRC) -lm -lpthread
$(OUT)/speq-skin: tools/speq-skin.c $(SPEQ_SRC) $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tools/speq-skin.c $(SPEQ_SRC) -lm -lpthread
$(OUT)/spurun: src/spurun.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

# SPU side: libspeq-spu.a gives main and the claim loop; a program gives its ops (SPEQ_PROGRAM)
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
# deadline.spu: the deadline pool's worker (lib/pool.c), one program for the latency-bound
# services: speq_synth_*_v2's op (spu/synth-op.c: DMA plan, double buffering; the DSP in
# lib/synth-kern.h, shared with the PPE path; -O2 as it was built alone) and speq_skin_*_v1's
# (spu/skin-op.c: DMA, double buffering; the kernel in lib/skin-kern.h; -O3), their buffers in one
# arena (spu/deadline.c)
$(OUT)/deadline-synth.o: spu/synth-op.c $(POOL_HDR) $(SPU_HDR) lib/speq-abi.h lib/speq.h
	@mkdir -p $(OUT)
	$(SPU_CC) $(SPU_CFLAGS) -c -o $@ spu/synth-op.c
$(OUT)/deadline-skin.o: spu/skin-op.c $(POOL_HDR) $(SPU_HDR) lib/speq-abi.h lib/speq.h
	@mkdir -p $(OUT)
	$(SPU_CC) $(SPU_CFLAGS) -O3 -c -o $@ spu/skin-op.c
$(OUT)/deadline.spu: spu/deadline.c $(OUT)/deadline-synth.o $(OUT)/deadline-skin.o $(POOL_HDR) \
		$(SPU_HDR) $(OUT)/libspeq-spu.a
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/deadline.c $(OUT)/deadline-synth.o $(OUT)/deadline-skin.o \
		$(OUT)/libspeq-spu.a

# host tests (x86 or ppc64): nothing but cc
test:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/libspeq.so.0 $(TEST_OUT)/speq-dxt $(TEST_OUT)/dlopen-check \
		$(TEST_OUT)/queue-test $(TEST_OUT)/speq-bench-fake $(TEST_OUT)/speq-dxt-fake \
		$(TEST_OUT)/synth-test $(TEST_OUT)/speq-skin-fake $(TEST_OUT)/pool-test
	$(TEST_OUT)/speq-dxt --check
	SPEQ_FAKE=1 $(TEST_OUT)/speq-dxt-fake --check --path spu
	SPEQ_FAKE=1 $(TEST_OUT)/speq-dxt-fake --check --path auto
	$(TEST_OUT)/dlopen-check $(TEST_OUT)/libspeq.so.0
	SPEQ_FAKE=1 $(TEST_OUT)/queue-test $(wildcard $(TEST_OUT)/bench.spu)
	SPEQ_FAKE=1 $(TEST_OUT)/speq-bench-fake --quick --gap 0
	SPEQ_FAKE=1 $(TEST_OUT)/synth-test
	SPEQ_FAKE=1 $(TEST_OUT)/speq-skin-fake --check --path spu
	SPEQ_FAKE=1 SPEQ_FAKE_RZ=1 $(TEST_OUT)/speq-skin-fake --check --path spu
	SPEQ_FAKE=1 $(TEST_OUT)/speq-skin-fake --check --path auto
	$(TEST_OUT)/speq-skin-fake --check --path ppe
	SPEQ_FAKE=1 $(TEST_OUT)/pool-test
$(OUT)/dlopen-check: tests/dlopen-check.c lib/dxt-ref.c $(SPEQ_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ilib $(LDFLAGS) -o $@ tests/dlopen-check.c lib/dxt-ref.c -ldl
$(OUT)/queue-test: tests/queue-test.c $(SPEQ_SRC) $(FAKE_SRC) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tests/queue-test.c \
		$(SPEQ_SRC) $(FAKE_SRC) -lm -lpthread
$(OUT)/speq-dxt-fake: tools/speq-dxt.c $(SPEQ_SRC) $(FAKE_DXT) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-dxt.c \
		$(SPEQ_SRC) $(FAKE_DXT) -lm -lpthread
$(OUT)/speq-bench-fake: tools/speq-bench.c $(SPEQ_SRC) $(FAKE_SRC) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-bench.c \
		$(SPEQ_SRC) $(FAKE_SRC) -lm -lpthread
$(OUT)/synth-test: tests/synth-test.c $(SPEQ_SRC) $(FAKE_POOL) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -DSK_CHECK -Ilib -Ispu $(LDFLAGS) -o $@ \
		tests/synth-test.c $(SPEQ_SRC) $(FAKE_POOL) -lm -lpthread
$(OUT)/speq-skin-fake: tools/speq-skin.c $(SPEQ_SRC) $(FAKE_POOL) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-skin.c \
		$(SPEQ_SRC) $(FAKE_POOL) -lm -lpthread
$(OUT)/pool-test: tests/pool-test.c $(SPEQ_SRC) $(FAKE_ALL) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -DSK_CHECK -Ilib -Ispu $(LDFLAGS) -o $@ \
		tests/pool-test.c $(SPEQ_SRC) $(FAKE_ALL) -lm -lpthread
# libspeq.so.0 with the fake backend and deadline.spu's ops (SPEQ_FAKE=1), for fluidsynth's hook
$(OUT)/fake/libspeq.so.0: $(SPEQ_SRC) $(FAKE_POOL) $(SPEQ_HDR) $(SPU_HDR) lib/libspeq.map
	@mkdir -p $(OUT)/fake
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -DSK_CHECK -Ilib -Ispu -fPIC -shared \
		-Wl,-soname,libspeq.so.0 -Wl,--version-script=lib/libspeq.map $(LDFLAGS) -o $@ \
		$(SPEQ_SRC) $(FAKE_POOL) -lm -lpthread

# Mesa's encoder (texcompress_s3tc_tmp.h from make mesa-work) as a host tool, then the corpus
# comparison (needs python3 with PIL, and the OpenArena textures deb in build/apt-cache)
$(OUT)/mesa-dxt: tests/mesa-dxt.c tests/shim/util/glheader.h $(MESA)/src/util/format/texcompress_s3tc_tmp.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -Itests/shim -I$(MESA)/src $(LDFLAGS) -o $@ tests/mesa-dxt.c
test-mesa: test
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/mesa-dxt
	tests/mesa-compare.sh $(TEST_OUT)

# fluidsynth (make fluidsynth-work: build/work/fluidsynth, the hook's commits on Debian's tree)
# and its base built for the host with cmake, the hook loading $(TEST_OUT)/fake/libspeq.so.0
FLUIDSYNTH ?= ../../../../build/work/fluidsynth
test-fluidsynth:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/fake/libspeq.so.0
	tests/fluidsynth-compare.sh $(FLUIDSYNTH) $(TEST_OUT)
