# The SPE client's SPU program (vseal.c) -> ../src/vseal_spu.h (the release image) and ../src/vseal_spu_test.h (the
# test image: VSEAL_TEST_OPS), which the module embeds. Needs the spu-elf toolchain (make spu-toolchain at the top:
# build/spu/install/opt/spu/bin). The module build itself doesn't: it uses the committed headers;
# ../test/vseal-test.sh rebuilds them when the toolchain is there and checks the bytes are the same.
#   make -C spu [SPU_TC=...] [O=...]
SPU_TC ?= ../../../../../build/spu/install/opt/spu/bin
SPU_CC := $(SPU_TC)/spu-elf-gcc
SPU_OBJCOPY := $(SPU_TC)/spu-elf-objcopy
O ?= ../../../../../build/tmp/ps3rsx-vseal
SRCS := vseal.c v2_chacha20.c v2_poly1305.c v2_poly1305_x4.c v2_aead.c
DEPS := crt0.S vseal.ld $(SRCS) v2_crypto.h ../src/vseal_abi.h
CFLAGS := -O3 -Wall -ffreestanding -nostdlib -nostartfiles -fno-tree-loop-distribute-patterns -I. -I../src
LDFLAGS := -T vseal.ld -Wl,--no-warn-rwx-segments

all: ../src/vseal_spu.h ../src/vseal_spu_test.h
../src/vseal_spu.h: $(O)/vseal.bin bin2c.py
	python3 -I bin2c.py $(O)/vseal.bin vseal_spu > $@
../src/vseal_spu_test.h: $(O)/vseal-test.bin bin2c.py
	python3 -I bin2c.py $(O)/vseal-test.bin vseal_spu_test > $@
$(O)/%.bin: $(O)/%.elf
	$(SPU_OBJCOPY) -O binary $< $@
$(O)/vseal.elf: $(DEPS)
	@mkdir -p $(O)
	$(SPU_CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S $(SRCS) -lgcc
$(O)/vseal-test.elf: $(DEPS)
	@mkdir -p $(O)
	$(SPU_CC) $(CFLAGS) -DVSEAL_TEST_OPS $(LDFLAGS) -o $@ crt0.S $(SRCS) -lgcc
# the images into O only (the test's reproducibility check)
images: $(O)/vseal.bin $(O)/vseal-test.bin
.PHONY: all images
