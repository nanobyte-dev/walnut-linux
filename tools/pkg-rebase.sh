#!/bin/bash
# pkg-rebase.sh fetch|rebase <pkg>: moves our patches onto a newer Debian version (NEWVERSION),
# inside the deb builder. fetch (as root): downloads the new source package (apt checks it against
# the snapshot's signed index). rebase (as the user): adds "Debian <new>" as a new base commit next
# to the old one in build/work/<pkg> (made with pkg-work.sh if missing), rebases our commits onto
# it, then exports the patches and updates <pkg>/package.conf (VERSION, DSC_SHA256). On a
# conflict it stops: resolve it in build/work/<pkg> (git status, git rebase --continue), then
# make <pkg>-patches and set package.conf yourself (the .dsc hash is printed).
set -euo pipefail
{
MODE=$1 PKG=$2
: "${NEWVERSION:?}"
. "$PKG/package.conf"
SRC=build/sources/$PKG
NEWDSC=$SRC/${SOURCE}_${NEWVERSION#*:}.dsc
case $MODE in
fetch)
	if [ ! -e "$NEWDSC" ]; then
		apt-get update -qq
		(cd "$SRC" && apt-get source --download-only -qq "$SOURCE=$NEWVERSION")
	fi
	chown -R "${OWNER:-0:0}" "$SRC"
	echo ">> $NEWDSC (sha256 $(sha256sum "$NEWDSC" | cut -d' ' -f1))"
	exit 0 ;;
rebase) ;;
*) echo "usage: pkg-rebase.sh fetch|rebase <pkg>"; exit 2 ;;
esac
W=build/work/$PKG
[ -d "$W/.git" ] || tools/pkg-work.sh "$PKG"
[ -z "$(git -C "$W" status --porcelain)" ] || { echo "!! $W has uncommitted changes"; exit 1; }
T=build/tmp/rebase-$PKG
rm -rf "$T"; mkdir -p "$T"
dpkg-source -x "$NEWDSC" "$T/src" >/dev/null
rm -rf "$T/src/.pc"
cd "$W"
git checkout -q --detach "base/$VERSION"
git ls-files -z | xargs -0 rm -f
find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -a "/work/$T/src/." .
git add -A -f
GIT_AUTHOR_DATE="@0 +0000" GIT_COMMITTER_DATE="@0 +0000" git commit -q -m "Debian $NEWVERSION" \
	-m "$SOURCE $NEWVERSION as Debian ships it (its own patches applied)."
git tag -f "base/$NEWVERSION" >/dev/null
rm -rf "/work/$T"
if ! git rebase -q --onto "base/$NEWVERSION" "base/$VERSION" work; then
	echo "!! conflict: resolve it in $W (git status; git rebase --continue), then make $PKG-patches"
	echo "   and set VERSION=$NEWVERSION, DSC_SHA256=$(sha256sum "/work/$NEWDSC" | cut -d' ' -f1) in $PKG/package.conf"
	exit 1
fi
cd /work
sed -i -e "s/^VERSION=.*/VERSION=$NEWVERSION/" \
	-e "s/^DSC_SHA256=.*/DSC_SHA256=$(sha256sum "$NEWDSC" | cut -d' ' -f1)/" "$PKG/package.conf"
tools/pkg-export.sh "$PKG"
echo ">> $PKG rebased onto $NEWVERSION; review with git diff -- $PKG"
exit
}
