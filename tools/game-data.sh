#!/bin/bash
# game-data.sh <out dir> <game>:<package>...: game data debs made by game-data-packager (in the games
# builder, docker/Dockerfile.games), e.g. quake:quake-shareware. Their data is non-free: our LAN
# repository only, never anywhere public (like ClassiCube's Mojang audio).
# game-data-packager's output isn't reproducible (file dates are the build time), and the stable suite
# never takes a different file under a published version (docs/PACKAGING.md). So a package already
# in the ps3 suite is downloaded from the repository (checked against the suite's SHA256) instead of
# made again. REGEN=1 makes it anyway, which is only publishable when the version is new (a newer
# game-data-packager): a regenerated file under a published version is refused here.
# Downloads (the shareware archives) are kept in build/downloads/game-data.
# Env: REPO_URL REGEN
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
OUT=$1; shift
: "${REPO_URL:?}"
DL=build/downloads/game-data
rm -rf "$OUT"; mkdir -p "$OUT" "$DL"
IDX=$(mktemp)
trap 'rm -f "$IDX"' EXIT
curl -fsS -m 30 -o "$IDX" "$REPO_URL/ps3/dists/ps3/main/binary-all/Packages" ||
	{ echo "!! can't read the ps3 suite's index at $REPO_URL (it decides whether to reuse or make)"; exit 1; }
# published <package>: "<version> <pool file> <sha256>" of its newest version in the ps3 suite
published() {
	awk -v p="$1" 'BEGIN { RS = ""; FS = "\n" } {
		n = v = f = s = ""
		for (i = 1; i <= NF; i++) {
			if ($i ~ /^Package: /) n = substr($i, 10)
			else if ($i ~ /^Version: /) v = substr($i, 10)
			else if ($i ~ /^Filename: /) f = substr($i, 11)
			else if ($i ~ /^SHA256: /) s = substr($i, 9)
		}
		if (n == p) print v, f, s
	}' "$IDX" | {
		best=; while read -r v f s; do
			if [ -z "$best" ] || dpkg --compare-versions "$v" gt "${best%% *}"; then best="$v $f $s"; fi
		done; echo "$best"; }
}
for spec in "$@"; do
	game=${spec%%:*} pkg=${spec#*:}
	pub=$(published "$pkg")
	if [ -n "$pub" ] && [ "${REGEN:-0}" != 1 ]; then
		read -r v f s <<<"$pub"
		echo ">> $pkg $v: the published file ($REPO_URL/ps3/$f)"
		curl -fsS -o "$OUT/$(basename "$f").tmp" "$REPO_URL/ps3/$f"
		echo "$s  $OUT/$(basename "$f").tmp" | sha256sum -c --quiet
		mv "$OUT/$(basename "$f").tmp" "$OUT/$(basename "$f")"
		continue
	fi
	echo ">> $pkg: game-data-packager $game"
	/usr/games/game-data-packager -d "$OUT" -n --no-search --save-downloads "$DL" "$game" --package "$pkg"
	deb=$(ls "$OUT/${pkg}_"*.deb)
	v=$(dpkg-deb -f "$deb" Version)
	if [ -n "$pub" ] && [ "$v" = "${pub%% *}" ]; then
		rm -f "$deb"
		echo "!! $pkg $v is published already and this file differs from it (game-data-packager isn't"
		echo "   reproducible): leave out REGEN=1 to take the published file"
		exit 1
	fi
done
ls -l "$OUT"
exit
}
