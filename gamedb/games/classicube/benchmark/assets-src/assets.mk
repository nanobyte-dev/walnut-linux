# ClassiCube's benchmark plugin (ccbench.c here: a camera path, then the frame rate), against the
# pinned source's headers (CC_TARBALL, the Makefile's), cross-built in the builder (the snapshot's GCC,
# as the package) -> ../assets/classicube-bench.so, tracked so that make bench-push carries it.
# Included by the top-level Makefile (every gamedb/games/*/benchmark/assets-src/assets.mk); `make
# classicube-bench`, or `make bench-assets` for every benchmark's built assets.
CC_BENCH_DIR := gamedb/games/classicube/benchmark
CC_BENCH_SRC := $(BUILD)/classicube/bench-src
BENCH_ASSETS += classicube-bench
.PHONY: classicube-bench
classicube-bench: $(CC_BENCH_DIR)/assets/classicube-bench.so
$(CC_BENCH_DIR)/assets/classicube-bench.so: $(CC_BENCH_DIR)/assets-src/ccbench.c $(CC_BENCH_DIR)/assets-src/Makefile \
		$(CC_TARBALL) | $(BUILDER_STAMP)
	rm -rf $(CC_BENCH_SRC); mkdir -p $(CC_BENCH_SRC)
	tar -xzf $(CC_TARBALL) -C $(CC_BENCH_SRC) --strip-components=1
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) \
		make -C $(CC_BENCH_DIR)/assets-src CC_SRC=/work/$(CC_BENCH_SRC) OUT=/work/$@
	rm -rf $(CC_BENCH_SRC)
