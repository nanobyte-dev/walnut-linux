#!/bin/bash
# perfq.sh SUBCOMMAND ARGS...: the PS3's perf (Debian's 7.2 for ppc64) under qemu-user on the PC, its
# files and the symbols from the root $PERFQ_ROOT (default build/tmp/hotpaths-sym): base.tar's
# /usr/bin/perf and /usr/lib/powerpc64-linux-gnu extracted into it, plus what symroot.sh unpacks
# (HOTPATHS_SYM=the same root). script and report get the kernel's symbols from the root's
# System.map (module symbols are not in it). For perf-post.sh on a copy of a run directory:
#   PERFPOST_PERF=bench/analysis/hotpaths/perfq.sh PERFPROF_ROOT=$PERFQ_ROOT bench/harness/perf-post.sh DIR
# (docs/PROFILING.md, "Re-processing a run on the PC").
set -u
top=$(cd "$(dirname "$0")/../../.." && pwd)	# this is bench/analysis/hotpaths/
R=$(cd "${PERFQ_ROOT:-$top/build/tmp/hotpaths-sym}" && pwd) || exit 1
[ -x "$R/usr/bin/perf" ] || { echo "!! no $R/usr/bin/perf: tar -xf build/rootfs/base.tar -C $R ./usr/bin/perf ./usr/lib/powerpc64-linux-gnu ./usr/lib64 ./lib ./lib64 ./bin" >&2; exit 1; }
sub=$1; shift
map=$(ls "$R"/boot/System.map-* 2>/dev/null | tail -1)
case $sub in
script|report) [ -n "$map" ] && set -- --kallsyms "$map" "$@" ;;
esac
exec qemu-ppc64-static -L "$R" "$R/usr/bin/perf" "$sub" "$@"
