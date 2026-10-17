#!/bin/bash
# aptly-publish.sh <staging dir>: adds the staged .deb/.ddeb files to our repository and publishes it
# (inside the deb builder; tools/repo-publish.sh calls it and then syncs to the server).
#   DIST=ps3 (default): stable, kept forever: versions must grow, nothing is ever removed
#   DIST=dev:           dev builds (suite ps3-dev, its own prefix and pool), debug symbols
#                       included; the newest KEEP (5) versions of each package are kept
# Debug symbols of stable builds (.ddeb) go to ps3-dbgsym, also pruned to the newest KEEP.
# Licensed packages (content we may not redistribute: Section non-free/* or local/*, or named in
# tools/licensed-packages.txt) go to the ps3-licensed suite (its own prefix and pool) whatever
# DIST is, never to ps3 or ps3-dev. ps3-licensed is kept like the stable suite: versions must grow,
# nothing is removed, no ~dev builds (a dev publish leaves a licensed ~dev build out, with a note).
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
STAGE=$1
DIST=${DIST:-ps3}
A="aptly -config=/work/tools/aptly.conf"
ARCHS=ppc64,all
LICENSED_LIST=/work/tools/licensed-packages.txt

ensure_repo() {	# name distribution component
	$A repo show "$1" >/dev/null 2>&1 || $A repo create -distribution="$2" -component="$3" "$1" >/dev/null
}
published() { $A publish list -raw 2>/dev/null | grep -qx "$1 $2"; }	# prefix distribution
has_packages() { $A repo show "$1" 2>/dev/null | grep -q "Number of packages: [1-9]"; }
licensed_names() { grep -v -e '^\s*#' -e '^\s*$' "$LICENSED_LIST"; }
is_licensed() {	# deb
	local n s
	n=$(dpkg-deb -f "$1" Package) s=$(dpkg-deb -f "$1" Section)
	case $s in non-free/*|local/*) return 0 ;; esac
	licensed_names | grep -qxF "$n"
}
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
# versions_grow <repo> <deb>...: refuses a package whose version isn't above every published one
# (an identical rebuild of the same file is accepted by aptly as a no-op)
versions_grow() {
	local repo=$1 d n v a old o; shift
	for d in "$@"; do
		[ -e "$d" ] || continue
		n=$(dpkg-deb -f "$d" Package) v=$(dpkg-deb -f "$d" Version) a=$(dpkg-deb -f "$d" Architecture)
		old=$($A repo search -format='{{.Version}}' "$repo" "Name (= $n), \$Architecture (= $a)" 2>/dev/null || true)
		for o in $old; do
			[ "$o" = "$v" ] && continue
			if dpkg --compare-versions "$v" le "$o"; then
				echo "!! $n $v ($a) is not above the published $o in $repo: refusing (bump the package)" >&2
				exit 1
			fi
		done
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
ensure_repo ps3-licensed ps3-licensed main

# Licensed packages leave the stage for their own suite, whatever DIST is
LSTAGE=$STAGE/licensed
mkdir -p "$LSTAGE"
for d in "$STAGE"/*.deb; do
	[ -e "$d" ] || continue
	is_licensed "$d" || continue
	case $(dpkg-deb -f "$d" Version) in
	*~dev*)
		if [ "$DIST" = dev ]; then
			echo ">> licensed dev build left out (ps3-licensed takes no ~dev builds: commit, rebuild): ${d##*/}"
			rm -f "$d"; continue
		fi ;;
	esac
	mv "$d" "$LSTAGE/"
done

# Dev builds (~dev versions, from a tree with uncommitted changes) never go to the stable suite
if [ "$DIST" = ps3 ]; then
	devs=$(for d in "$STAGE"/*.deb "$LSTAGE"/*.deb; do
		[ -e "$d" ] || continue
		case $(dpkg-deb -f "$d" Version) in *~dev*) echo "   ${d##*/}" ;; esac
	done)
	if [ -n "$devs" ]; then
		echo "!! dev builds can't be published to the stable suite (commit, rebuild, then publish):" >&2
		echo "$devs" >&2
		exit 1
	fi
fi
# Official versions only ever grow (stable and licensed)
if [ "$DIST" = ps3 ]; then versions_grow "$REPO" "$STAGE"/*.deb; fi
versions_grow ps3-licensed "$LSTAGE"/*.deb

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
LICENSED_NEW=0
if ls "$LSTAGE"/*.deb >/dev/null 2>&1; then
	# never -force-replace: a different file under a published version fails here
	$A repo add -remove-files ps3-licensed "$LSTAGE"/*.deb
	LICENSED_NEW=1
fi
# The dev suite never holds licensed packages (older dev publishes put some there)
if [ "$DIST" = dev ]; then
	q='Section (~ ^non-free/) | Section (~ ^local/)'
	for n in $(licensed_names); do q="$q | Name (= $n)"; done
	gone=$($A repo search -format='{{.Package}} {{.Version}}' ps3-dev "$q" 2>/dev/null || true)
	if [ -n "$gone" ]; then
		echo "$gone" | sed 's/^/>> licensed, removed from ps3-dev: /'
		$A repo remove ps3-dev "$q" >/dev/null
	fi
fi

# Publish: the kept prefixes (ps3, ps3-licensed) never clean up (old pool files stay), the
# throwaway ones do
pub() {	# prefix distribution repos...
	local prefix=$1 distrib=$2; shift 2
	local comps=main; [ $# -gt 1 ] && comps=main,deps
	if published "$prefix" "$distrib"; then
		case $prefix in
		ps3|ps3-licensed) $A publish update -skip-cleanup "$distrib" "$prefix" ;;
		*) $A publish update "$distrib" "$prefix" ;;
		esac
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
if has_packages ps3-dbgsym; then
	prune ps3-dbgsym
	pub ps3-dbgsym ps3-dbgsym ps3-dbgsym
fi
if has_packages ps3-licensed && { [ "$LICENSED_NEW" = 1 ] || ! published ps3-licensed ps3-licensed; }; then
	pub ps3-licensed ps3-licensed ps3-licensed
fi
rmdir "$LSTAGE" 2>/dev/null || true
echo ">> published $DIST"
exit
}
