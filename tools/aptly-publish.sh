#!/bin/bash
# aptly-publish.sh <staging dir>: adds the staged .deb/.ddeb files to our repository and publishes it
# (inside the deb builder; tools/repo-publish.sh calls it and then syncs to the server).
#   DIST=ps3 (default): stable, kept forever: versions must grow, nothing is ever removed
#   DIST=dev:           dev builds (suite ps3-dev, its own prefix and pool), debug symbols
#                       included; the newest KEEP (5) versions of each package are kept
# Debug symbols of stable builds (.ddeb) go to ps3-dbgsym, also pruned to the newest KEEP.
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
STAGE=$1
DIST=${DIST:-ps3}
A="aptly -config=/work/tools/aptly.conf"
ARCHS=ppc64,all

ensure_repo() {	# name distribution component
	$A repo show "$1" >/dev/null 2>&1 || $A repo create -distribution="$2" -component="$3" "$1" >/dev/null
}
published() { $A publish list -raw 2>/dev/null | grep -qx "$1 $2"; }	# prefix distribution
KEEP=${KEEP:-5}
# prune <repo>: keep only the newest $KEEP versions of each package (name and architecture),
# in Debian version order
prune() {
	local repo=$1
	$A repo search -format='{{.Package}} {{.Architecture}} {{.Version}}' "$repo" 'Name (~ .)' 2>/dev/null |
	python3 -c '
import functools, subprocess, sys
from collections import defaultdict
keep, groups = int(sys.argv[1]), defaultdict(list)
for line in sys.stdin:
    name, arch, ver = line.split()
    groups[(name, arch)].append(ver)
def cmp(x, y):
    if x == y:
        return 0
    return -1 if subprocess.run(["dpkg", "--compare-versions", x, "lt", y]).returncode == 0 else 1
for (name, arch), vers in groups.items():
    for ver in sorted(vers, key=functools.cmp_to_key(cmp), reverse=True)[keep:]:
        print(name, arch, ver)
' "$KEEP" | while read -r n a v; do
		$A repo remove "$repo" "Name (= $n), \$Architecture (= $a), \$Version (= $v)" >/dev/null
		echo ">> pruned $n $v ($a)"
	done
}

case $DIST in
ps3) REPO=ps3-main; PREFIX=ps3; DISTRIB=ps3 ;;
dev) REPO=ps3-dev; PREFIX=ps3-dev; DISTRIB=ps3-dev ;;
*) echo "DIST must be ps3 or dev" >&2; exit 2 ;;
esac
ensure_repo "$REPO" "$DISTRIB" main
ensure_repo ps3-deps ps3 deps
ensure_repo ps3-dbgsym ps3-dbgsym main

# Dev builds (~dev versions, from a tree with uncommitted changes) never go to the stable suite
if [ "$DIST" = ps3 ] && ls "$STAGE"/*~dev* >/dev/null 2>&1; then
	echo "!! dev builds can't be published to the stable suite (commit, rebuild, then publish):" >&2
	ls "$STAGE"/*~dev* | sed 's|.*/|   |' >&2
	exit 1
fi
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
if [ "$DIST" = ps3 ]; then
	pub ps3 ps3 ps3-main ps3-deps
else
	prune ps3-dev
	pub ps3-dev ps3-dev ps3-dev
fi
if $A repo show ps3-dbgsym 2>/dev/null | grep -q "Number of packages: [1-9]"; then
	prune ps3-dbgsym
	pub ps3-dbgsym ps3-dbgsym ps3-dbgsym
fi
echo ">> published $DIST"
exit
}
