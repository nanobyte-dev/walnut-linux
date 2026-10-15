# Userspace GPU tools for the PS3, cross-compiled in the builder container (see top-level Makefile).
CROSS   ?= powerpc64-linux-gnu-
CC      := $(CROSS)gcc
CFLAGS  ?= -O2 -Wall -Wextra -mcpu=cell
OUT     ?= ../build/gpu
TOOLS   := $(patsubst tools/%.c,$(OUT)/%,$(wildcard tools/*.c))

all: $(TOOLS)
$(OUT)/%: tools/%.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $<
