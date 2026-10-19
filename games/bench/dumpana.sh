#!/bin/bash
# dumpana.sh RUN_DIR: the PC-side analysis of a failure dump (client.sh BENCH_DUMPON, run.sh DUMPON=1,
# docs/games/LOG-FINDINGS.md "Catching failures"). RUN_DIR is a run directory copied from the PS3
# (/var/tmp/ps3-bench/<stamp>-<game>-<variant>/, with dump/ and versions). It builds a sysroot under
# build/tmp/dumpana/sysroot (shared by runs; delete it to start over): the base image's libraries
# (build/chroot/sid-ppc64.tar), then the packages named in the run's `versions` file at exactly those
# versions and their -dbgsym debs, found in build/debs, build/aptly/pool and build/apt-cache. libc6-dbg is
# not built here: gdb asks debuginfod (DEBUGINFOD_URLS, default debuginfod.debian.net) by build-id.
# Then gdb (gdb-multiarch, else gdb) runs on the core (dump/core.zst|xz, else core.crash.*):
# info sharedlibrary, thread apply all bt full and, when the run has OpenAL wedge lines, the state of
# the threads sitting in ALSA/OpenAL. Writes RUN_DIR/dump/analysis.txt. KEEP=1 keeps the unpacked core.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
run=${1:?usage: dumpana.sh RUN_DIR}
run=${run%/}
x=$run/dump
[ -d "$x" ] || { echo "!! $run/dump: no failure dump (run with DUMPON=1)"; exit 2; }
work=$root/build/tmp/dumpana; sys=$work/sysroot
mkdir -p "$sys"
out=$x/analysis.txt
GDB=${GDB:-$(command -v gdb-multiarch || command -v gdb)} || { echo "!! no gdb or gdb-multiarch on this PC"; exit 2; }

unpack() {	# unpack DEB: its files into the sysroot
	local m
	m=$(ar t "$1" | grep '^data\.tar' | head -1) || return 1
	ar p "$1" "$m" | { case $m in *.zst) zstd -dc ;; *.xz) xz -dc ;; *.gz) gzip -dc ;; *.bz2) bzip2 -dc ;; *) cat ;; esac; } |
		tar -x -C "$sys" --no-same-owner --no-same-permissions 2>/dev/null
}
finddeb() {	# finddeb NAME VERSION: the deb (or ddeb) in the local pools
	local n=$1 v=${2#*:} f
	for f in "${n}_${v}_*.deb" "${n}_${v}_*.ddeb" "${n}_${2/:/%3a}_*.deb"; do
		find "$root/build/debs" "$root/build/aptly/pool" "$root/build/apt-cache" \( -name "$f" -o -name "*_$f" \) 2>/dev/null | head -1
	done | grep . | head -1
}

# the base image's libraries once
if [ ! -e "$sys/.base" ]; then
	echo ">> base libraries from build/chroot/sid-ppc64.tar"
	tar -xf "$root/build/chroot/sid-ppc64.tar" -C "$sys" --no-same-owner --wildcards \
		'./usr/lib' './usr/lib64' './lib' './lib64' 2>/dev/null
	touch "$sys/.base"
fi

# the run's packages and their debug symbols
missing=(); have=()
if [ -s "$run/versions" ]; then
	while IFS=$'\t' read -r n v; do
		[ -n "${v:-}" ] || continue
		n=${n%%:*}
		for p in "$n" "$n-dbgsym"; do
			f=$(finddeb "$p" "$v")
			if [ -n "$f" ]; then unpack "$f" && have+=("$p=$v"); else missing+=("$p=$v"); fi
		done
	done < "$run/versions"
else
	echo "!! $run/versions missing: symbols for the base libraries only"
fi

core=
for c in "$x"/core.zst "$x"/core.xz "$x"/core.crash.zst "$x"/core.crash.xz; do [ -s "$c" ] && { core=$c; break; }; done
oal=$(sed -n 's/^oal_wedge=//p' "$run/bench.txt" 2>/dev/null)
{
	echo "== $run"
	echo "== bench.txt"; grep -E '^(game|variant|ended|rc|seconds|dump|dump_at|dump_core|dump_core_crash|oal_wedge|underruns|sound)=' "$run/bench.txt" 2>/dev/null
	echo "== versions"; cat "$run/versions" 2>/dev/null
	echo "== trigger"; cat "$x/trigger.txt" 2>/dev/null
	echo "== dump setup (core_pattern, gdb, sudo)"; cat "$x/setup.txt" 2>/dev/null
	echo "== sysroot: unpacked ${#have[@]}: ${have[*]:-}"
	echo "== sysroot: not found (no symbols): ${missing[*]:-none}"
} > "$out"
if [ -z "$core" ]; then
	echo "== no core in $x (gdb missing on the PS3, or a crash with no core): see the other files in dump/" >> "$out"
	echo "analysis (no core): $out"; exit 0
fi

c=$work/$(basename "$run").core
case $core in *.zst) zstd -dcf "$core" > "$c" ;; *) xz -dc "$core" > "$c" ;; esac
py=$work/wedge.py
cat > "$py" <<'P'
import gdb
cur = gdb.selected_thread()
print("== threads in ALSA / OpenAL code (the mixer and its wait)")
for th in gdb.selected_inferior().threads():
    th.switch()
    names, f = [], gdb.newest_frame()
    while f is not None and len(names) < 40:
        names.append("%s %s" % (f.name() or "??", f.find_sal().symtab.filename if f.find_sal().symtab else ""))
        f = f.older()
    j = " ".join(names).lower()
    if any(k in j for k in ("snd_pcm", "alsa", "mixerproc", "alsoft", "openal")):
        print("---- thread %d (%s) ----" % (th.num, th.name))
        gdb.execute("bt full 20", to_string=False)
if cur:
    cur.switch()
P
export DEBUGINFOD_URLS=${DEBUGINFOD_URLS-https://debuginfod.debian.net}
args=(-batch -nx -ex 'set pagination off' -ex 'set width 0' -ex 'set confirm off'
	-ex "set sysroot $sys" -ex "set debug-file-directory $sys/usr/lib/debug" -ex 'set auto-load safe-path /'
	-ex 'info sharedlibrary' -ex 'info threads' -ex 'thread apply all bt full')
[ "${oal:-0}" -gt 0 ] 2>/dev/null && args+=(-ex "source $py")
{ echo "== gdb on $(basename "$core")"; "$GDB" "${args[@]}" -c "$c" 2>&1; } >> "$out"
[ "${KEEP:-0}" = 1 ] || rm -f "$c"
echo "analysis: $out ($(wc -l < "$out") lines)"
