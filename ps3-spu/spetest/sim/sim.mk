# host build of the runner on the SPU simulator; called from ../Makefile (make test)
CC ?= cc
OUT ?= ../../build/spetest-sim
SPU_DIR ?= ../../build/spetest
T := $(dir $(lastword $(MAKEFILE_LIST)))..
CFLAGS ?= -O2 -g
override CFLAGS += -Wall -std=gnu11 -I$(T)

.PHONY: check
$(OUT)/spetest-sim: $(T)/runner.c $(T)/ref.c $(T)/sim/be_sim.c $(T)/sim/spusim.c $(T)/sim/main_sim.c $(wildcard $(T)/*.h $(T)/sim/*.h $(T)/sim/*.inc)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(T)/runner.c $(T)/ref.c $(T)/sim/be_sim.c $(T)/sim/spusim.c $(T)/sim/main_sim.c -lm -lpthread

check: $(OUT)/spetest-sim
	SPT_SIMBIN=$(abspath $<) SPT_SPU_DIR=$(abspath $(SPU_DIR)) $(T)/tests/run-tests.sh
