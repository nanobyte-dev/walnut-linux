#!/bin/sh
# exec-build.sh: compiles gcc.c-torture/execute at -O2 into SPU programs, with GCC 9.5 and with the
# ported GCC 16 (old reload; it emits assembly, GCC 9.5's driver assembles and links with newlib)
cd /work/build/spu-port
T=gcc-16.2.0/gcc/testsuite/gcc.c-torture/execute
G95=/work/build/spu/install/opt/spu/bin/spu-elf-gcc
mkdir -p bin95 binport
ls $T/*.c | xargs -P 24 -n 1 sh -c '
	n=$(basename "$0" .c)
	'"$G95"' -O2 -w -fpermissive -std=gnu17 -o bin95/$n.spu "$0" -lm 2>/dev/null
	obj/gcc/xgcc -Breload-bin/ -Bobj/gcc/ -isystem /work/build/spu/install/opt/spu/spu-elf/include \
		-O2 -w -fpermissive -std=gnu17 -S -o binport/$n.s "$0" 2>/dev/null &&
		'"$G95"' -o binport/$n.spu binport/$n.s -lm 2>/dev/null; rm -f binport/$n.s'
echo "built: gcc 9.5 $(ls bin95 | wc -l), port $(ls binport | wc -l)"
