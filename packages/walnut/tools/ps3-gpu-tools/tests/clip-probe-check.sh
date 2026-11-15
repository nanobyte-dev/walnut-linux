#!/bin/bash
# clip-probe-check.sh: rsx-clip-probe's cases against ps3rsx's shipping validator on the host
# (clip-probe-check.c), then, with the ppc64 cross compiler, the Mesa sysroot and qemu, the same
# check big-endian and the probe itself cross-compiled for the PS3 (-mcpu=cell, -Werror).
set -u
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../../../.." && pwd)
drv=$top/packages/walnut/drivers/ps3rsx
O=${O:-$top/build/tmp/clip-probe-check}
mkdir -p "$O"
CF=(-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -fno-strict-aliasing)
VAL=(-I"$drv/test/inc" -I"$drv/src" -I"$here" -Wno-unused-function)
# the validator half: ps3rsx's src/validate.c with the driver tests' kernel stand-ins (test/inc)
( gcc "${CF[@]}" "${VAL[@]}" -fsanitize=address,undefined \
	-fno-sanitize-recover=undefined -c -o "$O/validator.o" "$here/clip-probe-validator.c" ) || exit 1
gcc "${CF[@]}" -I"$here" -DDRV_C="\"$drv/src/drv.c\"" -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-c -o "$O/check.o" "$here/clip-probe-check.c" || exit 1
gcc -fsanitize=address,undefined -o "$O/clip-probe-check" "$O/check.o" "$O/validator.o" || exit 1
"$O/clip-probe-check" || exit 1
SYSROOT=$top/build/mesa/sysroot
if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
   [ -d "$SYSROOT" ]; then
	PPC=(--sysroot="$SYSROOT" -isystem "$SYSROOT/usr/include/powerpc64-linux-gnu"
	     -isystem "$SYSROOT/usr/include" -B"$SYSROOT/usr/lib/powerpc64-linux-gnu"
	     -L"$O" -L"$SYSROOT/usr/lib/powerpc64-linux-gnu" -L"$SYSROOT/lib/powerpc64-linux-gnu")
	# gcc 16 links -latomic_asneeded, which the Debian sysroot lacks: an empty one does
	[ -f "$O/libatomic_asneeded.a" ] || powerpc64-linux-gnu-ar rc "$O/libatomic_asneeded.a"
	( powerpc64-linux-gnu-gcc "${CF[@]}" "${VAL[@]}" "${PPC[@]}" \
		-c -o "$O/validator-ppc64.o" "$here/clip-probe-validator.c" ) || exit 1
	powerpc64-linux-gnu-gcc "${CF[@]}" -I"$here" -DDRV_C="\"$drv/src/drv.c\"" "${PPC[@]}" -c -o "$O/check-ppc64.o" \
		"$here/clip-probe-check.c" || exit 1
	powerpc64-linux-gnu-gcc "${PPC[@]}" -static -o "$O/clip-probe-check-ppc64" \
		"$O/check-ppc64.o" "$O/validator-ppc64.o" || exit 1
	qemu-ppc64-static "$O/clip-probe-check-ppc64" || exit 1
	# the probe as the package builds it (Makefile: -O2 -Wall -Wextra -mcpu=cell), warnings fatal
	powerpc64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -mcpu=cell "${PPC[@]}" -static \
		-o "$O/rsx-clip-probe" "$here/../src/rsx-clip-probe.c" -lm || exit 1
	qemu-ppc64-static "$O/rsx-clip-probe" --list > "$O/list-ppc64.txt" || exit 1
	grep -c ' ok$' "$O/list-ppc64.txt" | grep -qx 20 || { echo "FAIL: ppc64 --list"; exit 1; }
	echo "ok: ppc64 (big-endian) check and the cross-compiled probe's --list"
else
	echo "SKIP ${TEST_SUITE:-clip-probe}:ppc64 no cross gcc, qemu or sysroot"
fi
echo "clip-probe-check: ok"
