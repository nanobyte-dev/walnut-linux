# Offscreen EGL/GLES2 tests for the PS3, cross-compiled in the Mesa builder container against the
# sysroot's glvnd libEGL/libGLESv2 (see top-level Makefile, `make mesa-tests`).
CROSS   ?= powerpc64-linux-gnu-
CC      := $(CROSS)gcc
SYSROOT ?= ../../build/mesa/sysroot
CFLAGS  ?= -O2 -Wall -Wextra -mcpu=cell
OUT     ?= ../../build/mesa/tests
TESTS   := $(patsubst %.c,$(OUT)/%,$(wildcard *.c))

all: $(TESTS)
$(OUT)/%: %.c egl-common.h
	@mkdir -p $(OUT)
	$(CC) --sysroot=$(SYSROOT) $(CFLAGS) -o $@ $< -lEGL -lGLESv2 -lm
