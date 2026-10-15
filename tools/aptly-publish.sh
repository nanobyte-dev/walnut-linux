#!/bin/bash
# aptly-publish.sh <staging dir>: adds the staged .deb/.ddeb files to our repository and publishes it
# (inside the deb builder; tools/repo-publish.sh calls it and then syncs to the server).
#   DIST=ps3 (default): official, kept forever: versions must grow, nothing is ever removed
#   DIST=dev:           throwaway test builds (ps3-dev), replaced freely
# Debug symbols (.ddeb) always go to the throwaway ps3-dbgsym repository.
set -euo pipefail
STAGE=$1
DIST=${DIST:-ps3}
A="aptly -config=/work/tools/aptly.conf"
ARCHS=ppc64,all

ensure_repo() {	# name distribution component
	$A repo show "$1" >/dev/null 2>&1 || $A repo create -distribution="$2" -component="$3" "$1" >/dev/null
}
published() { $A publish list -raw 2>/dev/null | grep -qx "$1 $2"; }	# prefix distribution

case $DIST in
ps3) REPO=ps3-main; PREFIX=ps3; DISTRIB=ps3 ;;
dev) REPO=ps3-dev; PREFIX=ps3-dev; DISTRIB=ps3-dev ;;
*) echo "DIST must be ps3 or dev" >&2; exit 2 ;;
esac
ensure_repo "$REPO" "$DISTRIB" main
ensure_repo ps3-deps ps3 deps
ensure_repo ps3-dbgsym ps3-dbgsym main

# Official versions only ever grow: refuse a package whose version isn't above every published one
# (an identical rebuild of the same file is accepted by aptly as a no-op)
if [ "$DIST" = ps3 ]; then
	for d in "$STAGE"/*.deb; do
		[ -e "$d" ] || continue
		n=$(dpkg-deb -f "$d" Package) v=$(dpkg-deb -f "$d" Version) a=$(dpkg-deb -f "$d" Architecture)
		old=$($A repo search -format='{{.Version}}' "$REPO" "Name (= $n), \$Architecture (= $a)" 2>/dev/null || true)
		for o in $old; do
			[ "$o" = "$v" ] && continue
			if dpkg --compare-versions "$v" le "$o"; then
				echo "!! $n $v ($a) is not above the published $o: refusing (bump the package)" >&2
				exit 1
			fi
		done
	done
fi

if ls "$STAGE"/*.deb >/dev/null 2>&1; then
	if [ "$DIST" = dev ]; then
		$A repo add -force-replace -remove-files "$REPO" "$STAGE"/*.deb
	else
		$A repo add -remove-files "$REPO" "$STAGE"/*.deb
	fi
fi
if ls "$STAGE"/*.ddeb >/dev/null 2>&1; then
	$A repo add -force-replace -remove-files ps3-dbgsym "$STAGE"/*.ddeb
fi

# Publish: the official prefix never cleans up (old pool files stay), the throwaway ones do
pub() {	# prefix distribution repos...
	local prefix=$1 distrib=$2; shift 2
	local comps=main; [ $# -gt 1 ] && comps=main,deps
	if published "$prefix" "$distrib"; then
		if [ "$prefix" = ps3 ]; then
			$A publish update -skip-cleanup "$distrib" "$prefix"
		else
			$A publish update "$distrib" "$prefix"
		fi
	else
		$A publish repo -origin=PS3 -label=PS3 -distribution="$distrib" -architectures=$ARCHS \
			-component=$comps -acquire-by-hash "$@" "$prefix"
	fi
}
if [ "$DIST" = ps3 ]; then pub ps3 ps3 ps3-main ps3-deps; else pub ps3-dev ps3-dev ps3-dev; fi
$A repo show ps3-dbgsym 2>/dev/null | grep -q "Number of packages: [1-9]" && pub ps3-dbgsym ps3-dbgsym ps3-dbgsym
echo ">> published $DIST"
