#!/bin/bash
# prebuilt-upload.sh <pkg>: hands a BUILD=xcc package built on this PC (make <pkg>-deb from a committed
# tree: build/debs/<pkg>/ and its source package in build/src/<pkg>/) to CI, which publishes it like
# any other build (docs/PACKAGING.md "Built on the dev PC"): the files go to a release of the public
# GitHub repository (WALNUT_GITHUB_REPO, default nanobyte-dev/walnut-linux) tagged
# prebuilt-<pkg>-<version>, and github-ci/prebuilt/<pkg>.sha256 pins them. Commit that file, push
# (git push origin main; make public-push): the package's workflow fetches and checks them and the
# testing publisher takes them; "Run workflow" with stable publishes the same files to stable.
# Refused: a ~dev build, a build whose version isn't this commit's, licensed content, a release
# that exists already (published files never change: a new build needs a new version).
set -euo pipefail
{
cd "$(dirname "$0")/.."
pkg=$1
repo=${WALNUT_GITHUB_REPO:-nanobyte-dev/walnut-linux}
pd=$(tools/pkg-dir.sh "$pkg")
. "$pd/package.conf"
[ "${BUILD:-}" = xcc ] || { echo "!! $pkg is built by CI (BUILD=${BUILD:-native}), not uploaded" >&2; exit 1; }
n=$(tools/deb-version.sh "$pkg" | sed -n 1p)
case $n in *~dev*) echo "!! uncommitted changes to $pkg ($n): commit, rebuild" >&2; exit 1 ;; esac
ver="$VERSION+ps3.$n"
debs=(build/debs/"$pkg"/*_"${ver#*:}"_*.deb)
[ -e "${debs[0]}" ] || { echo "!! no build of $ver in build/debs/$pkg (make $pkg-deb)" >&2; exit 1; }
others=$(find build/debs/"$pkg" -maxdepth 1 -name '*.deb' ! -name "*_${ver#*:}_*" | head -3)
[ -z "$others" ] || { echo "!! build/debs/$pkg has debs of another version: $others" >&2; exit 1; }
dsc=build/src/$pkg/${SOURCE}_${ver#*:}.dsc
[ -s "$dsc" ] || { echo "!! no source package $dsc (make $pkg-src)" >&2; exit 1; }
src=("$dsc")
while read -r f; do
	p=$(find build/src/"$pkg" build/sources/"$pkg" -maxdepth 2 -type f -name "$f" -print -quit)
	[ -n "$p" ] || { echo "!! $f (from $dsc) not found" >&2; exit 1; }
	src+=("$p")
done < <(sed -n '/^Files:/,/^[^ ]/p' "$dsc" | awk 'NF==3 {print $3}')
python3 -I tools/leak-check.py debs "${debs[@]}"
# the leak check reads the files next to the .dsc: a BUILD=xcc build links the orig tarball there by its
# path inside the container (/work/...), dangling here, so it checks a copy of the .dsc beside links to
# the files found above
chk=$(mktemp -d build/tmp/prebuilt-check.XXXXXX)
for f in "${src[@]}"; do ln -s "$(realpath "$f")" "$chk/${f##*/}"; done
python3 -I tools/leak-check.py sources "$chk/${dsc##*/}"
rm -r "$chk"
tag="prebuilt-$pkg-${ver//[:~]/_}"
if gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
	echo "!! release $tag exists in $repo: published files never change" >&2; exit 1
fi
out=github-ci/prebuilt/$pkg.sha256
mkdir -p github-ci/prebuilt
{
	echo "# $pkg $ver, built on the dev PC (tools/xcc-build.sh), uploaded by tools/prebuilt-upload.sh"
	echo "# version $ver"
	echo "# url https://github.com/$repo/releases/download/$tag"
	for f in "${debs[@]}" "${src[@]}"; do sha256sum "$f" | awk -v n="${f##*/}" '{print $1, n}'; done
} > "$out.tmp"
gh release create "$tag" -R "$repo" --title "$pkg $ver (prebuilt)" \
	--notes "$pkg $ver for Walnut, built on the dev PC (too big for GitHub's runners). CI publishes these files to the apt suites; the checksums are in github-ci/prebuilt/$pkg.sha256." \
	"${debs[@]}" "${src[@]}"
mv "$out.tmp" "$out"
echo ">> $tag: ${#debs[@]} debs and ${#src[@]} source files; commit $out and push"
exit
}
