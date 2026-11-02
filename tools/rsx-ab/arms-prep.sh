#!/bin/bash
# arms-prep.sh B.deb [C.deb]: the ps3rsx modules of a same-boot A/B (tools/rsx-ab/rsx-arm.sh on the
# PS3), on the PC: arm A is walnut-stable's ps3rsx-modules for the kernel B was built for (its signed
# index checked with the archive key, the deb with the index's sha256), arm B the new build, arm C
# (optional) the same source built with the test switch (a "testclear" version). Writes
# build/tmp/rsx-ab/arms/{A,B,C}/ps3rsx.ko and arms.txt (arm, package, version, sha256 prefix, vermagic),
# then copy the directory to the PS3:  scp -r build/tmp/rsx-ab/arms PS3:/var/tmp/rsx-ab/
# Nothing is installed here, and nothing goes to build/debs.
set -euo pipefail
cd "$(dirname "$0")/../.."
[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: $0 B.deb [C.deb]" >&2; exit 2; }
O=build/tmp/rsx-ab/arms
W=$(mktemp -d build/tmp/rsx-ab-prep.XXXXXX)
trap 'rm -rf "$W"' EXIT
mkdir -p "$O"
URL=https://walnut.nanobyte.dev/apt

# the .ko and the control fields of a deb, without dpkg-deb (the PC may not have it)
deb_x() {	# deb_x DEB DIR
	mkdir -p "$2"
	(cd "$2" && ar x "$OLDPWD/$1" && tar -xf data.tar.* && tar -xf control.tar.* ./control)
}
field() { sed -n "s/^$2: //p" "$1/control"; }

arm() {	# arm NAME DEB
	local d=$W/$1 ko
	deb_x "$2" "$d"
	ko=$(ls "$d"/lib/modules/*/updates/ps3rsx.ko)
	mkdir -p "$O/$1"
	install -m 644 "$ko" "$O/$1/ps3rsx.ko"
	printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$(field "$d" Package)" "$(field "$d" Version)" \
		"$(sha256sum "$O/$1/ps3rsx.ko" | cut -c1-16)" \
		"$(grep -a -o 'vermagic=[^ ]*' "$O/$1/ps3rsx.ko" | head -1 | cut -d= -f2)" >> "$O/arms.txt"
}

: > "$O/arms.txt"
deb_x "$1" "$W/b-control"
pkg=$(field "$W/b-control" Package)
case $pkg in ps3rsx-modules-6.*) ;; *) echo "!! $1 is $pkg, not a ps3rsx-modules-<release> deb" >&2; exit 1 ;; esac

# arm A: the published stable build of the same package (same kernel release)
curl -sf -o "$W/InRelease" "$URL/dists/stable/InRelease"
gpgv --keyring apt-repo/keyrings/archive-keyring.gpg "$W/InRelease" 2>/dev/null ||
	{ echo "!! stable InRelease: bad or missing signature" >&2; exit 1; }
curl -sf -o "$W/Packages" "$URL/dists/stable/main/binary-ppc64/Packages"
[ "$(sha256sum "$W/Packages" | cut -d' ' -f1)" = "$(grep ' main/binary-ppc64/Packages$' "$W/InRelease" | tail -1 | awk '{print $1}')" ] ||
	{ echo "!! Packages doesn't match InRelease" >&2; exit 1; }
read -r file sha < <(python3 -I - "$W/Packages" "$pkg" <<'EOF'
import re, sys
best = None
for para in open(sys.argv[1]).read().split("\n\n"):
    f = dict(re.findall(r"^([A-Za-z0-9-]+): (.*)$", para, re.M))
    if f.get("Package") == sys.argv[2]:
        best = f		# the index lists one version per package and suite
if best:
    print(best["Filename"], best["SHA256"])
EOF
)
[ -n "${file:-}" ] || { echo "!! $pkg is not in walnut-stable" >&2; exit 1; }
curl -sf -o "$W/a.deb" "$URL/$file"
echo "$sha  $W/a.deb" | sha256sum -c --quiet - || { echo "!! $file: sha256 mismatch" >&2; exit 1; }
arm A "$W/a.deb"
arm B "$1"
[ $# -lt 2 ] || arm C "$2"
column -t "$O/arms.txt"
echo ">> $O: copy to the PS3's /var/tmp/rsx-ab/arms"
