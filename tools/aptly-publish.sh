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
# Source packages (<stage>/src/<name>/*.dsc, from repo-publish.sh) are routed the same way: a source
# is licensed when its name is in the list or any package it builds is licensed
# (tools/leak-check.py classify). A free source goes in only after tools/leak-check.py sources found
# none of its files among the files of the licensed packages we have (this stage, build/debs, the
# ps3-licensed pool) and none of tools/licensed-files.txt's paths; one leak stops the whole publish.
# A publication serves sources only if it was created with the source architecture (new ones are;
# the existing LAN ones were not: a note says so).
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
STAGE=$1
DIST=${DIST:-ps3}
A="aptly -config=/work/tools/aptly.conf"
ARCHS=ppc64,all,source
LEAK=/work/tools/leak-check.py
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

# Source packages: licensed ones join the licensed stage, free ones are checked for licensed files
SSTAGE=$STAGE/src
LSSTAGE=$LSTAGE/src
mkdir -p "$SSTAGE" "$LSSTAGE"
free_dscs=()
for dsc in "$SSTAGE"/*/*.dsc; do
	[ -e "$dsc" ] || continue
	dir=$(dirname "$dsc")
	if [ "$(python3 "$LEAK" classify "$dsc" | cut -d' ' -f1)" = licensed ]; then
		case $(basename "$dsc") in
		*~dev*) echo ">> licensed dev source left out (ps3-licensed takes no ~dev builds): ${dsc##*/}"; rm -rf "$dir"; continue ;;
		esac
		mv "$dir" "$LSSTAGE/"
	else
		free_dscs+=("$dsc")
	fi
done
if [ ${#free_dscs[@]} -gt 0 ]; then
	licensed_debs=("$LSTAGE")
	[ -d /work/build/aptly/public/ps3-licensed/pool ] && licensed_debs+=(/work/build/aptly/public/ps3-licensed/pool)
	[ -d /work/build/debs ] && licensed_debs+=(/work/build/debs)
	if ! python3 "$LEAK" sources $(printf -- '--licensed %s ' "${licensed_debs[@]}") "${free_dscs[@]}"; then
		echo "!! licensed content in a free source package: nothing published" >&2
		exit 1
	fi
fi

# Dev builds (~dev versions, from a tree with uncommitted changes) never go to the stable suite
if [ "$DIST" = ps3 ]; then
	devs=$(for d in "$STAGE"/*.deb "$LSTAGE"/*.deb "$SSTAGE"/*/*.dsc "$LSSTAGE"/*/*.dsc; do
		[ -e "$d" ] || continue
		case ${d##*/} in *~dev*) echo "   ${d##*/}"; continue ;; esac
		case $d in *.deb) case $(dpkg-deb -f "$d" Version) in *~dev*) echo "   ${d##*/}" ;; esac ;; esac
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
# free sources into the suite's repository, licensed ones into ps3-licensed (aptly takes the .dsc
# with the files it lists from the same directory)
SRC_ADDED=
if ls "$SSTAGE"/*/*.dsc >/dev/null 2>&1; then
	if [ "$DIST" = dev ]; then
		$A repo add -force-replace -remove-files "$REPO" "$SSTAGE"
	else
		$A repo add -remove-files "$REPO" "$SSTAGE"
	fi
	SRC_ADDED="$SRC_ADDED $PREFIX/$DISTRIB"
fi
LICENSED_NEW=0
if ls "$LSTAGE"/*.deb >/dev/null 2>&1; then
	# never -force-replace: a different file under a published version fails here
	$A repo add -remove-files ps3-licensed "$LSTAGE"/*.deb
	LICENSED_NEW=1
fi
if ls "$LSSTAGE"/*/*.dsc >/dev/null 2>&1; then
	$A repo add -remove-files ps3-licensed "$LSSTAGE"
	LICENSED_NEW=1
	SRC_ADDED="$SRC_ADDED ps3-licensed/ps3-licensed"
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
# sources are only served by a publication made with the source architecture
for pd in $SRC_ADDED; do
	$A publish show "${pd#*/}" "${pd%%/*}" 2>/dev/null | grep -q '^Architectures:.*source' ||
		echo ">> note: sources added to ${pd%%/*}'s repository, but $pd was published without the source architecture: not served (docs/PACKAGING.md)"
done
rm -rf "$SSTAGE" "$LSSTAGE"
rmdir "$LSTAGE" 2>/dev/null || true
echo ">> published $DIST"
exit
}
