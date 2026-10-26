#!/bin/sh
# exec-build16.sh: gcc.c-torture/execute at -O2 with the GCC 16.2 SPU toolchain (make spu-toolchain)
cd /work/build/spu-port
T=gcc-16.2.0/gcc/testsuite/gcc.c-torture/execute
G=/work/build/spu/install/opt/spu/bin/spu-elf-gcc
rm -rf bin16; mkdir -p bin16
ls $T/*.c | xargs -P 24 -n 1 sh -c 'n=$(basename "$0" .c); '"$G"' -O2 -w -fpermissive -std=gnu17 -o bin16/$n.spu "$0" -lm 2>/dev/null; true'
echo "built: $(ls bin16 | wc -l)"
