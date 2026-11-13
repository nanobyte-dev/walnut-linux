#!/bin/bash
# vswap-init-test.sh OUTDIR [VSWAP_C]: ps3rsx_vswap_init (cut out of VSWAP_C, default ../src/vswap.c) against
# stand-ins that fail one fallible call at a time (vswap-init-test.c), then the fix off: the same with the
# bounce's unpin taken out of the error path, which must fail with its FAIL line.
set -u
[ $# -ge 1 ] || { echo "usage: $0 OUTDIR [VSWAP_C]" >&2; exit 2; }
mkdir -p "$1" && O=$(cd "$1" && pwd) || exit 2
here=$(cd "$(dirname "$0")" && pwd)
SRC=$(realpath "${2:-$here/../src/vswap.c}")
cut_init() {	# cut_init SRC OUT: the function from its first line to the first "}" at column 0
	awk '/^int ps3rsx_vswap_init\(/ { on = 1 } on { print } on && /^}/ { exit }' "$1" > "$2"
	grep -q '^int ps3rsx_vswap_init(' "$2" || { echo "!! no ps3rsx_vswap_init in $1" >&2; exit 1; }
}
build() {	# build NAME INIT.c
	gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined \
		-fno-sanitize-recover=undefined -DVSWAP_INIT_C="\"$2\"" -o "$O/$1" "$here/vswap-init-test.c"
}
cut_init "$SRC" "$O/vswap-init.c"
build vswap-init-test "$O/vswap-init.c" || exit 1
"$O/vswap-init-test" || exit 1
# fix off: the error path without the unpin (as 0.1403 shipped)
perl -0777 -pe '
	my $n = s/\n\tif \(vs->bounce_ok\)\n\t\tps3rsx_gart_unpin\(rsx, &vs->bounce_node\);\n(?=(?:\tvswap_seal_fini\(vs\);\n)?\tbioset_exit)/\n/g;
	$n == 1 or die "vswap-init-test.sh: the error path\x27s unpin found $n times\n"' \
	"$O/vswap-init.c" > "$O/vswap-init-off.c" || exit 1
build vswap-init-off "$O/vswap-init-off.c" || exit 1
out=$("$O/vswap-init-off" 2>&1)
if grep -q 'FAIL: failure at call .*: the GART bounce stays pinned after the error' <<<"$out"; then
	echo "vswap-init-test: fix off (no unpin on the error path): caught"
else
	echo "!! fix off not caught:"; echo "$out"; exit 1
fi
