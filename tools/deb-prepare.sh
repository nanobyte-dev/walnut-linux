#!/bin/bash
# deb-prepare.sh <pkg> <N> <date>: our source package from the Debian one fetched by deb-fetch.sh
# (inside the deb builder): dpkg-source -x, then <pkg>/packaging/series applied to debian/ and
# <pkg>/patches/series added as debian/patches/ps3/ (quilt), a changelog entry for
# <debver>+ps3.<N> dated <date>, and dpkg-source -b. Output: build/src/<pkg>/ (.dsc + tarballs,
# and the unpacked tree for inspection), replaced on every run.
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
PKG=$1 N=$2 DATE=$3 HASH=${4:-}
. "$PKG/package.conf"
OUT=build/src/$PKG
if [ "$TYPE" = native ]; then
	# our own code: this directory is the source package, version 0.<N>; kernel module packages
	# (KERNEL_PACKAGE=1) are per kernel release: 0.<N>+k<release>, debian/*.in filled in
	NEWVER="0.$N"
	if [ "${KERNEL_PACKAGE:-0}" = 1 ]; then
		: "${KERNEL_RELEASE:?}"
		NEWVER="$NEWVER+k$(echo "$KERNEL_RELEASE" | tr -- - .)"
	fi
	rm -rf "$OUT"; mkdir -p "$OUT"
	T=$OUT/$SOURCE-$NEWVER
	mkdir -p "$T"
	git -C "$PKG" ls-files -z --cached --others --exclude-standard | (cd "$PKG" && xargs -0 cp --parents -t "$PWD/../$T")
	if [ "${KERNEL_PACKAGE:-0}" = 1 ]; then
		echo "$KERNEL_RELEASE" > "$T/debian/kernel-release"
		for f in "$T"/debian/*.in; do
			[ -e "$f" ] || continue
			b=$(basename "$f" .in)
			case "$b" in postinst|postrm|preinst|prerm) b="$SOURCE-modules-$KERNEL_RELEASE.$b" ;; esac
			sed -e "s/@KERNEL_RELEASE@/$KERNEL_RELEASE/g" -e "s/@KERNEL_FLAVOUR@/${KERNEL_FLAVOUR:-ps3}/g" "$f" > "$T/debian/$b"
			rm "$f"
		done
	fi
	(cd "$T" && DEBEMAIL="ps3@localhost" DEBFULLNAME="PS3 Linux project" \
		dch --create --package "$SOURCE" -v "$NEWVER" --distribution ps3 --force-distribution \
		"Built from the ps3 project, ${HASH:-local}." 2>/dev/null)
	python3 - "$T/debian/changelog" "$DATE" <<'PY2'
import re, sys
p, date = sys.argv[1:]
s = open(p).read()
s = re.sub(r'^( -- .*>)  .*$', lambda m: m.group(1) + '  ' + date, s, count=1, flags=re.M)
open(p, 'w').write(s)
PY2
	(cd "$OUT" && dpkg-source -q -b "$SOURCE-$NEWVER" >/dev/null)
	echo ">> $OUT/${SOURCE}_${NEWVER}.dsc"
	exit 0
fi
if [ "$TYPE" = upstream ]; then
	# software Debian doesn't have: the upstream tarball (ORIG) and component tarballs made from
	# files or directories (COMPONENTS="name:path ..."), unpacked into <name>/ in the tree
	NEWVER="$UPVER-$DEBREV+ps3.$N"
	rm -rf "$OUT"; mkdir -p "$OUT"
	T=$OUT/$SOURCE-${UPVER}
	mkdir -p "$T"
	MTIME=$(date -d "$DATE" +%s)
	case "$ORIG" in *.tar.gz) ext=gz ;; *.tar.xz) ext=xz ;; *) echo "!! $ORIG: not .tar.gz/.tar.xz"; exit 1 ;; esac
	cp "$ORIG" "$OUT/${SOURCE}_$UPVER.orig.tar.$ext"
	tar -xf "$ORIG" -C "$T" --strip-components=1
	for c in $COMPONENTS; do
		name=${c%%:*} path=${c#*:}
		stage=$OUT/.component/$name
		rm -rf "$OUT/.component"; mkdir -p "$stage"
		if [ -d "$path" ]; then cp -a "$path"/. "$stage/"; else cp "$path" "$stage/"; fi
		tar --sort=name --mtime="@$MTIME" --owner=0 --group=0 --numeric-owner -C "$OUT/.component" -cf - "$name" \
			| gzip -n9 > "$OUT/${SOURCE}_$UPVER.orig-$name.tar.gz"
		cp -a "$stage" "$T/$name"
		rm -rf "$OUT/.component"
	done
	cp -a "$PKG/debian" "$T/"
	if [ -s "$PKG/patches/series" ]; then
		mkdir -p "$T/debian/patches"
		while read -r p; do
			[ -n "$p" ] && [ "${p#\#}" = "$p" ] || continue
			cp "$PKG/patches/$p" "$T/debian/patches/"
			echo "$p" >> "$T/debian/patches/series"
			patch -d "$T" -p1 -s --no-backup-if-mismatch < "$PKG/patches/$p"
		done < "$PKG/patches/series"
	fi
	(cd "$T" && DEBEMAIL="ps3@localhost" DEBFULLNAME="PS3 Linux project" \
		dch --create --package "$SOURCE" -v "$NEWVER" --distribution ps3 --force-distribution \
		"Built from the ps3 project, ${HASH:-local}." 2>/dev/null)
	python3 - "$T/debian/changelog" "$DATE" <<'PY2'
import re, sys
p, date = sys.argv[1:]
s = open(p).read()
s = re.sub(r'^( -- .*>)  .*$', lambda m: m.group(1) + '  ' + date, s, count=1, flags=re.M)
open(p, 'w').write(s)
PY2
	(cd "$OUT" && dpkg-source -q -b "$(basename "$T")" >/dev/null)
	echo ">> $OUT/${SOURCE}_${NEWVER}.dsc"
	exit 0
fi
SRC=build/sources/$PKG
UPVER=${VERSION%-*}; UPVER=${UPVER#*:}
NEWVER="$VERSION+ps3.$N"
rm -rf "$OUT"; mkdir -p "$OUT"
dpkg-source -x "$SRC/${SOURCE}_${VERSION#*:}.dsc" "$OUT/$SOURCE-$UPVER" >/dev/null
T=$OUT/$SOURCE-$UPVER
export QUILT_PATCHES=debian/patches QUILT_PC=.pc

# Packaging patches change debian/ itself (rules, control.in...), applied directly
if [ -s "$PKG/packaging/series" ]; then
	while read -r p; do
		[ -n "$p" ] && [ "${p#\#}" = "$p" ] || continue
		patch -d "$T" -p1 -s --no-backup-if-mismatch < "$PKG/packaging/$p"
	done < "$PKG/packaging/series"
fi
# Source patches join Debian's quilt series under ps3/
if [ -s "$PKG/patches/series" ]; then
	mkdir -p "$T/debian/patches/ps3"
	while read -r p; do
		[ -n "$p" ] && [ "${p#\#}" = "$p" ] || continue
		mkdir -p "$T/debian/patches/ps3/$(dirname "$p")"
		cp "$PKG/patches/$p" "$T/debian/patches/ps3/$p"
		echo "ps3/$p" >> "$T/debian/patches/series"
		(cd "$T" && quilt push -q "ps3/$p" >/dev/null)
	done < "$PKG/patches/series"
fi
# Package-specific: Mesa generates debian/control from control.in
if [ "$SOURCE" = mesa ]; then make -s -C "$T" -f debian/rules regen_control; fi

(cd "$T" && DEBEMAIL="ps3@localhost" DEBFULLNAME="PS3 Linux project" \
	dch -v "$NEWVER" --distribution ps3 --force-distribution --no-auto-nmu --force-bad-version \
	"PS3 build: patches from the ps3 project, ${HASH:-local}." 2>/dev/null)
# the entry's date: the commit's, so the same checkout gives the same SOURCE_DATE_EPOCH
python3 - "$T/debian/changelog" "$DATE" <<'PY2'
import re, sys
p, date = sys.argv[1:]
s = open(p).read()
s = re.sub(r'^( -- .*>)  .*$', lambda m: m.group(1) + '  ' + date, s, count=1, flags=re.M)
open(p, 'w').write(s)
PY2
# Package-specific: the kernel (linux/package.conf FLAVOURS). The ABI name gets +ps3.<N> (package
# names can't have '~': a test build's <n>~dev<stamp> becomes <n>.dev<stamp>); each flavour's full
# config (tools/linux-config.sh, on the patched tree) and boot arguments go into
# debian/config/ppc64/; then debian/control is regenerated (gencontrol.py; the target fails on
# purpose) and checked, and its build directory removed.
if [ "$SOURCE" = linux ]; then
	sed -i "s/@PS3_ABI_SUFFIX@/+ps3.${N/\~/.}/" "$T/debian/config/defines.toml"
	for f in $FLAVOURS; do
		tools/linux-config.sh "$T" "$f" "$T/debian/config/ppc64/config.$f"
		cp "$PKG/flavours/$f.args" "$T/debian/config/ppc64/args.$f"
	done
	(cd "$T" && PYTHONDONTWRITEBYTECODE=1 make -f debian/rules debian/control-real > ../gencontrol.log 2>&1) || true
	if ! (cd "$T" && md5sum --check --status debian/control.md5sum) || \
		! grep -q '^Package: linux-image-.*-ps3$' "$T/debian/control"; then
		cat "$OUT/gencontrol.log"; echo "!! linux: debian/control was not regenerated"; exit 1
	fi
	rm -rf "$T/debian/build" "$T/debian/lib/python/debian_linux/__pycache__" "$OUT/gencontrol.log"
fi
for f in "$SRC"/*.orig*.tar.*; do ln -sf "$(realpath "$f")" "$OUT/"; done
(cd "$OUT" && dpkg-source -q -b "$SOURCE-$UPVER" >/dev/null)
echo ">> $OUT/${SOURCE}_${NEWVER}.dsc"
exit
}
