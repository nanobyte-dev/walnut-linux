#!/bin/bash
# lockdep-variant.sh: round 3 M2. LINUX_VARIANT=lockdep (the lockdep kernel):
#  - tools/deb-version.sh gives the linux package a test-build version ("<n>~dev<stamp>", now), so the kernel's
#    release names and the ps3rsx-modules built for it are its own, never those of the plain debug build of the
#    same commit (no CONFIG_MODVERSIONS: a module built for another config would load and corrupt memory);
#    other packages and a plain build are unchanged;
#  - tools/linux-config.sh makes the release flavour as always (no error) and the debug flavour with
#    PROVE_LOCKING, when a patched 6.12 tree is given (LINUX_TREE, else build/tmp/lane-km/t115; skipped without
#    it or without the cross compiler).
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
fail=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fail=1; }
plain=$(tools/deb-version.sh linux | head -1)
var=$(LINUX_VARIANT=lockdep tools/deb-version.sh linux | head -1)
case $var in *"~dev"*) ok "the variant's version is a test build ($var)" ;; *) bad "variant version $var" ;; esac
case $plain in *"~dev"*) echo "skip: the plain build is a test build too (dirty tree)" ;; *) [ "$var" != "$plain" ] && ok "...and not the plain build's ($plain)" || bad "same version as the plain build" ;; esac
other=$(LINUX_VARIANT=lockdep tools/deb-version.sh mesa | head -1)
[ "$other" = "$(tools/deb-version.sh mesa | head -1)" ] && ok "another package is not affected" || bad "mesa's version changed"
e=$(env LINUX_VARIANT="" tools/deb-version.sh linux | head -1)
case $plain in *"~dev"*) ;; *) [ "$e" = "$plain" ] && ok "an empty variant is a plain build" || bad "empty variant" ;; esac

T=${LINUX_TREE:-build/tmp/lane-km/t115}
if [ -f "$T/Makefile" ] && command -v powerpc64-linux-gnu-gcc >/dev/null; then
	out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
	export LINUX_CONFIG_CC=powerpc64-linux-gnu-gcc
	LINUX_VARIANT=lockdep tools/linux-config.sh "$T" release "$out/rel" 2>"$out/rel.err" && ok "the release flavour is made with the variant set" || { bad "release with the variant: $(cat "$out/rel.err")"; }
	LINUX_VARIANT=lockdep tools/linux-config.sh "$T" debug "$out/dbg" 2>"$out/dbg.err" && ok "the debug flavour is made" || bad "debug with the variant: $(cat "$out/dbg.err")"
	grep -qx CONFIG_PROVE_LOCKING=y "$out/dbg" && ok "debug + lockdep has PROVE_LOCKING" || bad "no PROVE_LOCKING in the lockdep debug config"
	! grep -qx CONFIG_PROVE_LOCKING=y "$out/rel" && ok "the release config has none" || bad "PROVE_LOCKING in release"
else
	echo "skip: no patched kernel tree at $T or no cross compiler (the config half)"
fi
exit $fail
