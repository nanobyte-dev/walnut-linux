# Userspace GPU tools for the PS3, cross-compiled in the builder container (see top-level Makefile).
CROSS   ?= powerpc64-linux-gnu-
CC      := $(CROSS)gcc
CFLAGS  ?= -O2 -Wall -Wextra -mcpu=cell
OUT     ?= ../../../../build/gpu
LIBOUT  ?= $(OUT)/lib
TOOLS   := $(patsubst src/%.c,$(OUT)/%,$(wildcard src/*.c))
# preloaded libraries (preload/*.c -> <name>.so, installed in /usr/lib/ps3/lib)
LIBS    := $(patsubst preload/%.c,$(LIBOUT)/%.so,$(wildcard preload/*.c))
# ALSA plugins (alsa/*.c -> <name>.so against libasound, installed in /usr/lib/ps3/lib with their
# alsa/*.conf; ps3-xrunlog: bench's sound dropouts)
ALSALIBS := $(patsubst alsa/%.c,$(LIBOUT)/%.so,$(wildcard alsa/*.c))

all: $(TOOLS) $(LIBS) $(ALSALIBS)
$(OUT)/%: src/%.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm
$(LIBOUT)/%.so: preload/%.c
	@mkdir -p $(LIBOUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ $< -lpthread
$(LIBOUT)/%.so: alsa/%.c
	@mkdir -p $(LIBOUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(LDFLAGS) -o $@ $< -lasound -lm
