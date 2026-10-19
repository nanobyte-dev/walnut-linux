#!/bin/bash
# run-test.sh ROOT: in the deb builder (privileged, ROOT on a scratch disk): a ppc64 chroot made from
# build/chroot/sid-ppc64.tar, g++, Debian's boost headers and Boost.Test, our debs from
# build/debs/boost-context; then ctxtest.cpp under qemu-ppc64 (-O2, and -mcpu=cell -maltivec), and
# Boost's own tests of Context, Coroutine and Fiber (single-threaded ones) from build/src/boost-context.
# docker run --rm --privileged -v $PWD:/work -v /scratch:/troot -w /work <deb builder> \
#   bash boost-context/test/run-test.sh /troot/root
set -eu
R=$1
T=/work/boost-context/test
if [ ! -e "$R/.ready" ]; then
	rm -rf --one-file-system "$R"; mkdir -p "$R"
	tar -xf /work/build/chroot/sid-ppc64.tar -C "$R"
fi
mkdir -p "$R/proc"; mount -t proc proc "$R/proc"
trap 'umount -l "$R/proc"' EXIT
rm -rf "$R/local"; mkdir -p "$R/local"
cp /work/build/debs/boost-context/*.deb "$R/local/"
rm -f "$R"/local/*dbgsym*
(cd "$R/local" && apt-ftparchive packages . > Packages)
echo 'deb [trusted=yes] file:/local ./' > "$R/etc/apt/sources.list.d/local.list"
cp /etc/resolv.conf "$R/etc/resolv.conf"
chroot "$R" apt-get update -qq
chroot "$R" env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
	g++ libboost1.92-dev libboost-test1.92-dev libboost-context1.92-dev libboost-coroutine1.92-dev libboost-fiber1.92-dev \
	| grep -E "^Setting up libboost-(context|coroutine|fiber)" || true
touch "$R/.ready"
chroot "$R" dpkg -l 'libboost-context*' 'libboost-coroutine*' 'libboost-fiber*' | grep ^ii
cp "$T/ctxtest.cpp" "$R/tmp/"
L="-lboost_fiber -lboost_coroutine -lboost_context"
for v in "default:-O2" "altivec:-O2 -mcpu=cell -maltivec -mabi=altivec"; do
	n=${v%%:*} f=${v#*:}
	echo "=== $n ($f)"
	chroot "$R" g++ -std=c++17 $f -o /tmp/ctx-$n /tmp/ctxtest.cpp $L
	chroot "$R" /tmp/ctx-$n || echo "exit status $?"
done
# what the fcontext asm would give with the FPRs and VRs at -O2 -mcpu=cell: disassembly of the
# switch, for the notes
chroot "$R" sh -c 'objdump -d /usr/lib/powerpc64-linux-gnu/libboost_context.so.1.92.0 | grep -c "stfd\|stvx"' || true
chroot "$R" sh -c 'nm -D /usr/lib/powerpc64-linux-gnu/libboost_context.so.1.92.0 | c++filt | grep -E "fcontext|fiber_activation"' || true
chroot "$R" sh -c 'nm -D --undefined-only /usr/lib/powerpc64-linux-gnu/libboost_coroutine.so.1.92.0 | c++filt | grep fcontext; readelf -d /usr/lib/powerpc64-linux-gnu/libboost_coroutine.so.1.92.0 | grep NEEDED' || true

# Boost's own tests of the three libraries (from the boost1.92 source, test/ dirs), Boost.Test static
UT=/usr/lib/powerpc64-linux-gnu/libboost_unit_test_framework.a
S=${BOOST_SRC:-/work/build/src/boost-context/boost1.92-1.92.0}/libs
rm -rf "$R/tmp/bt"; mkdir -p "$R/tmp/bt"
cp $S/context/test/test_fiber.cpp $S/context/test/test_callcc.cpp $S/context/test/test_fcontext.cpp \
	$S/coroutine/test/test_asymmetric_coroutine.cpp $S/coroutine/test/test_symmetric_coroutine.cpp "$R/tmp/bt/"
for t in fiber_dispatch fiber_post mutex_dispatch condition_variable_dispatch future_dispatch \
		buffered_channel_dispatch unbuffered_channel_dispatch barrier_dispatch fss_dispatch async_dispatch; do
	cp $S/fiber/test/test_$t.cpp "$R/tmp/bt/"
done
for f in "$R"/tmp/bt/*.cpp; do
	n=$(basename "$f" .cpp)
	case $n in test_fiber|test_callcc|test_fcontext) libs="-lboost_context" ;;
		*coroutine*) libs="$UT -lboost_coroutine -lboost_context" ;;
		*) libs="$UT -lboost_fiber -lboost_context -lpthread" ;; esac
	if ! chroot "$R" g++ -std=c++17 -O2 -o /tmp/bt/$n /tmp/bt/$n.cpp $libs > "$R/tmp/bt/$n.cc.log" 2>&1; then
		echo "BUILD FAIL: $n"; tail -5 "$R/tmp/bt/$n.cc.log"; continue
	fi
	if chroot "$R" timeout 600 /tmp/bt/$n > "$R/tmp/bt/$n.log" 2>&1; then echo "PASS: boost $n"
	else echo "FAIL: boost $n (status $?)"; tail -8 "$R/tmp/bt/$n.log"; fi
done
