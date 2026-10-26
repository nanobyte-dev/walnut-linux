#!/bin/bash
# pkg-dir.sh NAME | --all [--order] | --order: our source packages (a directory with a package.conf;
# docs/LAYOUT.md), relative to the repository's top. Packages are named by their directory alone:
# make NAME-deb, build/src/NAME, build/debs/NAME. Where they may live (PATTERNS below):
#   packages/<group>/<name>/, packages/<group>/<area>/<name>/ (walnut/, patched/, repackaged/, licensed/),
#   gamedb/games/<id>/packages/<name>/, gamedb/games/<id>/licensed/<name>/, gamedb/engines/<e>/packages/<name>/
#   NAME     the directory of package NAME; exit 1 when there is none
#   --all    every package directory, one per line (the Makefile's PKG_DIRS)
#   --order  every package name in build order: a package after the ones its package.conf
#            LOCAL_DEPS names, linux first; ps3rsx last (it builds against the kernels: make packages
#            runs it as ps3rsx-modules, for every kernel built); otherwise by name
#   --all --order  the directories in that order
# Two packages of the same name anywhere are an error (exit 1), in every mode: a name must say which
# directory it is. A LOCAL_DEPS name without a package, or a dependency cycle, is an error for --order.
# PKG_DIR_ROOT=DIR looks in another tree instead of this repository (tests).
set -euo pipefail
cd "${PKG_DIR_ROOT:-$(dirname "$0")/..}"
PATTERNS=(packages/*/*/package.conf packages/*/*/*/package.conf
	gamedb/games/*/packages/*/package.conf gamedb/games/*/licensed/*/package.conf
	gamedb/engines/*/packages/*/package.conf)

all_dirs() {	# every package directory, in PATTERNS order, each pattern's matches sorted bytewise
	local p f
	for p in "${PATTERNS[@]}"; do
		for f in $p; do if [ -f "$f" ]; then echo "${f%/package.conf}"; fi; done | LC_ALL=C sort
	done
}

dirs=$(all_dirs)
dups=$(printf '%s\n' "$dirs" | sed 's#.*/##' | LC_ALL=C sort | uniq -d)
if [ -n "$dups" ]; then
	for n in $dups; do
		echo "!! two packages named $n: $(printf '%s\n' "$dirs" | grep "/$n\$" | paste -sd' ')" >&2
	done
	exit 1
fi

order() {	# the package names in build order (see the header)
	declare -A dir deps built
	local d n names=() out=() left progress
	while read -r d; do
		[ -n "$d" ] || continue
		n=${d##*/}; dir[$n]=$d; names+=("$n")
		deps[$n]=$(sed -n 's/^LOCAL_DEPS="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$d/package.conf")
	done <<< "$dirs"
	[ -z "${dir[ps3rsx]:-}" ] || deps[ps3rsx]="${deps[ps3rsx]} linux"
	for n in "${names[@]}"; do
		for d in ${deps[$n]}; do
			[ -n "${dir[$d]:-}" ] || { echo "!! $n: LOCAL_DEPS names $d, which isn't a package here" >&2; return 1; }
		done
	done
	mapfile -t names < <(printf '%s\n' "${names[@]}" | LC_ALL=C sort)
	# linux first, ps3rsx last, the rest by name; each one once everything it needs is out
	left=()
	[ -z "${dir[linux]:-}" ] || left+=(linux)
	for n in "${names[@]}"; do [ "$n" = linux ] || [ "$n" = ps3rsx ] || left+=("$n"); done
	[ -z "${dir[ps3rsx]:-}" ] || left+=(ps3rsx)
	while [ ${#left[@]} -gt 0 ]; do
		progress=
		for i in "${!left[@]}"; do
			n=${left[$i]}
			ok=1
			for d in ${deps[$n]}; do [ -n "${built[$d]:-}" ] || ok=; done
			if [ -n "$ok" ]; then
				out+=("$n"); built[$n]=1; unset 'left[i]'; progress=1
				break	# start over: an earlier name whose dependency just came out goes first
			fi
		done
		left=("${left[@]}")
		[ -n "$progress" ] || { echo "!! LOCAL_DEPS cycle among: ${left[*]}" >&2; return 1; }
	done
	for n in "${out[@]}"; do
		if [ "${ALL:-}" = 1 ]; then echo "${dir[$n]}"; else echo "$n"; fi
	done
}

case "${1:-}" in
--all)
	if [ "${2:-}" = --order ]; then ALL=1 order; else printf '%s\n' "$dirs" | sed '/^$/d'; fi ;;
--order)
	if [ "${2:-}" = --all ]; then ALL=1 order; else order; fi ;;
*)
	[[ ${1:-} =~ ^[a-z0-9][a-z0-9.+-]*$ ]] && [ $# = 1 ] || { echo "usage: $0 NAME | --all [--order] | --order" >&2; exit 2; }
	d=$(printf '%s\n' "$dirs" | awk -F/ -v n="$1" '$NF == n')
	[ -n "$d" ] && { echo "$d"; exit 0; }
	echo "!! no package $1 (packages/**/$1/package.conf, gamedb/*/*/{packages,licensed}/$1/package.conf)" >&2
	exit 1 ;;
esac
