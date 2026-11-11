# The SPE client's SPU program (vseal.c) -> ../src/vseal_spu.h, the image the module embeds. Needs the spu-elf
# toolchain (make spu-toolchain at the top: build/spu/install/opt/spu/bin). The module build itself doesn't:
# it uses the committed vseal_spu.h; ../test/vseal-test.sh rebuilds it when the toolchain is there and checks
# the bytes are the same.
#   make -C spu [SPU_TC=...] [O=...]
SPU_TC ?= ../../../../../build/spu/install/opt/spu/bin
SPU_CC := $(SPU_TC)/spu-elf-gcc
SPU_OBJCOPY := $(SPU_TC)/spu-elf-objcopy
O ?= ../../../../../build/tmp/ps3rsx-vseal
SRCS := vseal.c v2_chacha20.c v2_poly1305.c v2_poly1305_x4.c v2_aead.c
CFLAGS := -O3 -Wall -ffreestanding -nostdlib -nostartfiles -fno-tree-loop-distribute-patterns -I. -I../src

../src/vseal_spu.h: $(O)/vseal.bin bin2c.py
	python3 -I bin2c.py $(O)/vseal.bin > $@
$(O)/vseal.bin: $(O)/vseal.elf
	$(SPU_OBJCOPY) -O binary $< $@
$(O)/vseal.elf: crt0.S vseal.ld $(SRCS) v2_crypto.h ../src/vseal_abi.h
	@mkdir -p $(O)
	$(SPU_CC) $(CFLAGS) -T vseal.ld -Wl,--no-warn-rwx-segments -o $@ crt0.S $(SRCS) -lgcc
# the image into O only (the test's reproducibility check)
image: $(O)/vseal.bin
.PHONY: image
