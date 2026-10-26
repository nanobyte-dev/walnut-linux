#!/bin/sh
# torture.sh <label> [cc1 flags]: compiles gcc.c-torture/{compile,execute} at -O2 with the ported
# compiler to assembly, in parallel; counts outcomes (ICE = internal compiler error).
L=$1; shift
cd /work/build/spu-port
T=gcc-16.2.0/gcc/testsuite/gcc.c-torture
mkdir -p "t-$L"
export FLAGS="$*" CC
ls $T/compile/*.c $T/execute/*.c | xargs -P 24 -n 1 sh -c '
	out=$(timeout 60 ${CC:-obj/gcc/xgcc -Bobj/gcc/} -isystem /work/build/spu/install/opt/spu/spu-elf/include \
		-O2 -w -S -o /dev/null $FLAGS "$0" 2>&1); rc=$?
	if echo "$out" | grep -q "internal compiler error"; then
		echo "ICE $0 $(echo "$out" | grep -m1 "internal compiler error" | sed "s/.*internal compiler error: //")"
	elif [ $rc = 124 ]; then echo "TIMEOUT $0"
	elif [ $rc = 0 ]; then echo "OK $0"
	else echo "ERROR $0"; fi' > "t-$L/results"
echo "$L: $(cut -d' ' -f1 t-$L/results | sort | uniq -c | tr '\n' ' ')"
