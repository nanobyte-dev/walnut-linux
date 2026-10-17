# ps3-spu: libspeq (lib/), its tools (tools/) and spurun (src/) for the PPE; the SPU programs
# and libspeq-spu.a, the runtime they link (spu/, spu-elf GCC). debian/rules builds `ppe` (-B,
# in the ppc64 chroot) and `spu` (-A, our toolchain on PATH); the top-level make spu-tests
# builds both in containers.
#   make test       with the host cc: speq-dxt --check, the dlopen check, and the queue against
#                   the fake backend (SPEQ_FAKE=1: the SPU programs' ops on host threads):
#                   queue-test, speq-bench-fake --quick (bench.spu's ops), speq-dxt-fake --check
#                   on the spu and auto paths (dxt.spu's op and encoder: the whole SPU path),
#                   synth-test (synth.spu's op, every read checked against its DMA plan, against
#                   libspeq's PPE path), speq-skin-fake --check on the spu and ppe paths (skin.spu's
#                   op and libspeq's PPE path against Sauerbraten's skinning written out in C)
#   make test-fluidsynth  fluidsynth with the libspeq hook against unpatched fluidsynth, both
#                   built for the host, on a generated MIDI file (tests/fluidsynth-compare.sh)
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

SPEQ_SRC := lib/s3tc.c lib/dxt-ref.c lib/synth.c lib/skin.c lib/queue.c lib/spe.c
SPEQ_HDR := $(wildcard lib/*.h)
# the fake backend: the SPU runtime and a program's ops built for the host (tests only)
FAKE_SRC := lib/fake.c spu/runtime.c spu/bench.c
FAKE_DXT := lib/fake.c spu/runtime.c spu/dxt-op.c spu/dxt.c
FAKE_SYNTH := lib/fake.c spu/runtime.c spu/synth-op.c
FAKE_SKIN := lib/fake.c spu/runtime.c spu/skin-op.c
SPU_HDR  := $(wildcard spu/*.h)
# dxt.spu's encoder at -O3, as timed (135 cycles per block)
DXT_SPU_OPT ?= -O3

.PHONY: ppe spu test test-mesa test-fluidsynth
ppe: $(OUT)/libspeq.so.0 $(OUT)/speq-dxt $(OUT)/spurun $(OUT)/speq-bench $(OUT)/speq-skin
spu: $(OUT)/hello.spu $(OUT)/libspeq-spu.a $(OUT)/bench.spu $(OUT)/dxt.spu $(OUT)/synth.spu \
	$(OUT)/skin.spu

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
# synth.spu: speq_synth_*_v1's worker (spu/synth-op.c: DMA plan, double buffering; the DSP in
# lib/synth-kern.h, shared with the PPE path)
$(OUT)/synth.spu: spu/synth-op.c lib/synth-kern.h lib/synth-job.h $(SPU_HDR) $(OUT)/libspeq-spu.a
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/synth-op.c $(OUT)/libspeq-spu.a
# skin.spu: speq_skin_*_v1's worker (spu/skin-op.c: DMA, double buffering; the kernel in
# lib/skin-kern.h, shared with the PPE path)
$(OUT)/skin.spu: spu/skin-op.c lib/skin-kern.h lib/skin-job.h $(SPU_HDR) $(OUT)/libspeq-spu.a
	$(SPU_CC) $(SPU_CFLAGS) -O3 -o $@ spu/skin-op.c $(OUT)/libspeq-spu.a

# host tests (x86 or ppc64): nothing but cc
test:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/libspeq.so.0 $(TEST_OUT)/speq-dxt $(TEST_OUT)/dlopen-check \
		$(TEST_OUT)/queue-test $(TEST_OUT)/speq-bench-fake $(TEST_OUT)/speq-dxt-fake \
		$(TEST_OUT)/synth-test $(TEST_OUT)/speq-skin-fake
	$(TEST_OUT)/speq-dxt --check
	SPEQ_FAKE=1 $(TEST_OUT)/speq-dxt-fake --check --path spu
	SPEQ_FAKE=1 $(TEST_OUT)/speq-dxt-fake --check --path auto
	$(TEST_OUT)/dlopen-check $(TEST_OUT)/libspeq.so.0
	SPEQ_FAKE=1 $(TEST_OUT)/queue-test $(wildcard $(TEST_OUT)/bench.spu)
	SPEQ_FAKE=1 $(TEST_OUT)/speq-bench-fake --quick --gap 0
	SPEQ_FAKE=1 $(TEST_OUT)/synth-test
	SPEQ_FAKE=1 $(TEST_OUT)/speq-skin-fake --check --path spu
	SPEQ_FAKE=1 $(TEST_OUT)/speq-skin-fake --check --path auto
	$(TEST_OUT)/speq-skin-fake --check --path ppe
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
$(OUT)/synth-test: tests/synth-test.c $(SPEQ_SRC) $(FAKE_SYNTH) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -DSK_CHECK -Ilib -Ispu $(LDFLAGS) -o $@ \
		tests/synth-test.c $(SPEQ_SRC) $(FAKE_SYNTH) -lm -lpthread
$(OUT)/speq-skin-fake: tools/speq-skin.c $(SPEQ_SRC) $(FAKE_SKIN) $(SPEQ_HDR) $(SPU_HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -Ilib -Ispu $(LDFLAGS) -o $@ tools/speq-skin.c \
		$(SPEQ_SRC) $(FAKE_SKIN) -lm -lpthread
# libspeq.so.0 with the fake backend and synth.spu's op (SPEQ_FAKE=1), for fluidsynth's hook
$(OUT)/fake/libspeq.so.0: $(SPEQ_SRC) $(FAKE_SYNTH) $(SPEQ_HDR) $(SPU_HDR) lib/libspeq.map
	@mkdir -p $(OUT)/fake
	$(CC) $(CPPFLAGS) $(CFLAGS) -DSPEQ_FAKE -DSK_CHECK -Ilib -Ispu -fPIC -shared \
		-Wl,-soname,libspeq.so.0 -Wl,--version-script=lib/libspeq.map $(LDFLAGS) -o $@ \
		$(SPEQ_SRC) $(FAKE_SYNTH) -lm -lpthread

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
FLUIDSYNTH ?= ../build/work/fluidsynth
test-fluidsynth:
	$(MAKE) OUT=$(TEST_OUT) $(TEST_OUT)/fake/libspeq.so.0
	tests/fluidsynth-compare.sh $(FLUIDSYNTH) $(TEST_OUT)
