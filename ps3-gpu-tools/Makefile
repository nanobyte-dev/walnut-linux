# Userspace GPU tools for the PS3, cross-compiled in the builder container (see top-level Makefile).
CROSS   ?= powerpc64-linux-gnu-
CC      := $(CROSS)gcc
CFLAGS  ?= -O2 -Wall -Wextra -mcpu=cell
OUT     ?= ../build/gpu
TOOLS   := $(patsubst src/%.c,$(OUT)/%,$(wildcard src/*.c))

all: $(TOOLS)
$(OUT)/%: src/%.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<
