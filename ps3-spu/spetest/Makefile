# spetest: the SPE health tests (README.md). Called from ../Makefile (ppe, spu) and by hand:
#   make ppe spu OUT=dir [CC=...] [SPU_CC=...]   the runner and the five SPU programs
#   make test                                    host unit tests: the runner against the SPU simulator
#                                                (sim/), the programs built with SPU_CC
CC      ?= cc
SPU_CC  ?= spu-elf-gcc
OUT     ?= b
CFLAGS  ?= -O2 -g
override CFLAGS += -Wall -std=gnu11
SPU_CFLAGS ?= -O2 -g
override SPU_CFLAGS += -Wall -std=gnu11
TESTS   := ls reg exec chan mfc
SPU_DIR := $(OUT)/spetest.d
SPU_OBJ := $(addprefix $(SPU_DIR)/spetest-,$(addsuffix .spu,$(TESTS)))
HDR     := proto.h gen.h oplist.h
PPE_SRC := main.c runner.c ref.c be_spufs.c
SIM_OUT ?= $(OUT)/sim

.PHONY: ppe spu test sim-test
ppe: $(OUT)/spetest
spu: $(SPU_OBJ)

$(OUT)/spetest: $(PPE_SRC) runner.h spt.h ref.h $(HDR)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(PPE_SRC) -lm -lpthread

SPU_COMMON := spu/common.c spu/common.h $(HDR)
$(SPU_DIR)/spetest-ls.spu: spu/t_ls.c $(SPU_COMMON)
	@mkdir -p $(SPU_DIR)
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/t_ls.c spu/common.c
$(SPU_DIR)/spetest-reg.spu: spu/t_reg.c spu/regs.S $(SPU_COMMON)
	@mkdir -p $(SPU_DIR)
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/t_reg.c spu/common.c spu/regs.S
$(SPU_DIR)/spetest-exec.spu: spu/t_exec.c $(SPU_COMMON)
	@mkdir -p $(SPU_DIR)
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/t_exec.c spu/common.c
$(SPU_DIR)/spetest-chan.spu: spu/t_chan.c $(SPU_COMMON)
	@mkdir -p $(SPU_DIR)
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/t_chan.c spu/common.c
$(SPU_DIR)/spetest-mfc.spu: spu/t_mfc.c $(SPU_COMMON)
	@mkdir -p $(SPU_DIR)
	$(SPU_CC) $(SPU_CFLAGS) -o $@ spu/t_mfc.c spu/common.c

# host tests: the real runner (be_spufs replaced by the simulator backend) driving the real SPU
# programs through an interpreter of the SPU instruction set (sim/)
test: $(SPU_OBJ)
	@mkdir -p $(SIM_OUT)
	$(MAKE) -f sim/sim.mk OUT=$(SIM_OUT) SPU_DIR=$(abspath $(SPU_DIR)) check
