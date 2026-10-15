# SPU test programs (spu-elf toolchain) and the PPE-side runner, built in containers by the
# top-level Makefile (make spu-tests)
SPU_CC  ?= spu-elf-gcc
PPE_CC  ?= powerpc64-linux-gnu-gcc
OUT     ?= ../build/spu-tests

all: $(OUT)/hello.spu $(OUT)/spurun
$(OUT)/hello.spu: hello.c $(wildcard $(SPU_CC))
	@mkdir -p $(OUT)
	$(SPU_CC) -O2 -Wall -o $@ $<
$(OUT)/spurun: spurun.c
	@mkdir -p $(OUT)
	$(PPE_CC) -O2 -Wall -mcpu=cell -o $@ $<
