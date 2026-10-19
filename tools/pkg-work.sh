#!/bin/bash
# pkg-work.sh <pkg>: a git work tree for changing a Debian-source package's patches
# (docs/PACKAGING.md, "Changing patches"), in build/work/<pkg>, inside the deb builder:
#   commit "Debian <version>" (tag base/<version>): Debian's source with Debian's own patches
#   applied, then one commit per patch of ours, packaging patches (<pkg>/packaging) first, then
#   source patches (<pkg>/patches, a subdirectory being the Gbp-Pq topic), each with its message,
#   author and date. Tag "exported" marks the state <pkg>/ holds.
# Work with plain git (commit, rebase -i, fixup), then tools/pkg-export.sh writes the patches back.
# Refuses to replace a tree whose HEAD isn't the exported state (unsaved commits), unless FORCE=1.
set -euo pipefail
{
PKG=$1
. "$PKG/package.conf"
[ "$TYPE" = debsrc ] || { echo "!! $PKG is not a Debian source package (TYPE=$TYPE)"; exit 1; }
W=build/work/$PKG
export GIT_COMMITTER_NAME="PS3 Linux project" GIT_COMMITTER_EMAIL=ps3@localhost
if [ -d "$W/.git" ]; then
	if [ "$(git -C "$W" rev-parse HEAD)" != "$(git -C "$W" rev-parse -q --verify exported 2>/dev/null)" ] ||
	   [ -n "$(git -C "$W" status --porcelain)" ]; then
		if [ "${FORCE:-0}" != 1 ]; then
			echo "!! $W has changes not exported to $PKG/ (make $PKG-patches first, or FORCE=1 to discard)"
			exit 1
		fi
	fi
	rm -rf "$W"
fi
mkdir -p build/work
dpkg-source -x "build/sources/$PKG/${SOURCE}_${VERSION#*:}.dsc" "$W" >/dev/null
cd "$W"
rm -rf .pc
git init -q -b work
git config user.name "PS3 Linux project"; git config user.email ps3@localhost
git config core.autocrlf false
git add -A -f	# whatever the package's .gitignore says (Debian's kernel tree ignores everything)
GIT_AUTHOR_DATE="@0 +0000" GIT_COMMITTER_DATE="@0 +0000" git commit -q -m "Debian $VERSION" \
	-m "$SOURCE $VERSION as Debian ships it (its own patches applied)."
git tag "base/${VERSION//:/%}"	# git refuses ":" in a tag: an epoch as "%" (DEP-14)

apply() {	# <patch file> <topic> <series entry>
	local f=$1 topic=$2 name meta author date
	name=$(basename "$3")
	meta=$(python3 /work/tools/pkg-patchmsg.py "$f" "$topic" "$name")
	author=$(echo "$meta" | sed -n 1p)
	date=$(echo "$meta" | sed -n 2p)
	# DEP-3 patches carry no date: the date of the last change to the file in our repository
	[ -n "$date" ] || date=$(git -C /work log -1 --format=%aD -- "${f#/work/}" 2>/dev/null || true)
	[ -n "$date" ] || date="@0 +0000"
	git apply --index -p1 "$f" || { echo "!! $f does not apply"; exit 1; }
	echo "$meta" | sed 1,2d | GIT_AUTHOR_DATE="$date" GIT_COMMITTER_DATE="$date" \
		git commit -q --author="$author" -F -
}
series() {	# <dir>: its series entries
	[ -s "$1/series" ] || return 0
	grep -v -e '^\s*#' -e '^\s*$' "$1/series" | awk '{print $1}'
}
for p in $(series "/work/$PKG/packaging"); do apply "/work/$PKG/packaging/$p" packaging "$p"; done
for p in $(series "/work/$PKG/patches"); do
	t=$(dirname "$p"); [ "$t" = . ] && t=
	apply "/work/$PKG/patches/$p" "$t" "$p"
done
git tag -f exported >/dev/null
B="base/${VERSION//:/%}"
echo ">> $W: $B + $(git rev-list --count "$B"..HEAD) commits (git log --oneline $B..)"
exit
}
