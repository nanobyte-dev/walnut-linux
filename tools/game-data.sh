#!/bin/bash
# game-data.sh <out dir> <game>:<package>[:input]...: game data debs made by game-data-packager (in the
# games builder, build-containers/Dockerfile.games), e.g. quake:quake-shareware. Their data is non-free: the
# password-protected licensed suite on walnut.nanobyte.dev only (gamedb build.toml [[licensed]]; make game-data), never
# anywhere public.
# game-data-packager's output isn't reproducible (file dates are the build time), and the licensed
# suite never takes a different file under a published version (docs/PACKAGING.md). So a package
# already published there is downloaded (checked against the suite's SHA256) instead of made again.
# REGEN=1 makes it anyway, which is only publishable when the version is new (a newer
# game-data-packager): a regenerated file under a published version is refused here.
# Downloads (the shareware archives) are kept in build/downloads/game-data.
# A package marked :input (build.toml [[licensed]] input = true) is made from the owner's copy only: from
# INPUT (a folder, an installer or archive gdp reads; make licensed-data mounts it), never downloaded;
# without INPUT it is skipped unless published.
# A file made here gets X-Walnut-Edition and X-Walnut-Release (tools/gamedb/stamp-data-deb.py: the edition
# from gamedb, the release the import's manifest names for the files gdp packed), as the console's import
# writes them; a published file is taken as it is.
# Env: LICENSED_URL (https://walnut.nanobyte.dev/licensed), LICENSED_NETRC (a curl netrc with the
# suite's login, apt-repo/licensed-auth.sh FILE netrc), REGEN, INPUT
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
OUT=$1; shift
URL=${LICENSED_URL:-https://walnut.nanobyte.dev/licensed}
DL=build/downloads/game-data
rm -rf "$OUT"; mkdir -p "$OUT" "$DL"
IDX=$(mktemp)
trap 'rm -f "$IDX"' EXIT
get() { curl -fsS -m 60 ${LICENSED_NETRC:+--netrc-file "$LICENSED_NETRC"} "$@"; }
# the suite's index; a suite with nothing published yet has no Release (404)
code=$(curl -sS -m 30 -o /dev/null -w '%{http_code}' ${LICENSED_NETRC:+--netrc-file "$LICENSED_NETRC"} "$URL/dists/licensed/Release" || true)
case $code in
200) get "$URL/dists/licensed/main/binary-ppc64/Packages" > "$IDX" ;;
404) echo ">> the licensed suite has nothing published yet" ;;
*) if [ "${REGEN:-0}" = 1 ]; then echo ">> the licensed suite isn't readable (HTTP $code): REGEN=1, making them"
   else echo "!! can't read the licensed suite at $URL (HTTP $code; its login: LICENSED_NETRC): it decides whether to reuse or make"; exit 1; fi ;;
esac
# published <package>: "<version> <prefix>/<pool file> <sha256>" of its newest published version
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
	game=${spec%%:*} pkg=${spec#*:} input=
	case $pkg in *:input) pkg=${pkg%:input} input=1 ;; esac
	pub=$(published "$pkg")
	if [ -n "$pub" ] && [ "${REGEN:-0}" != 1 ]; then
		read -r v f s <<<"$pub"
		echo ">> $pkg $v: the published file ($URL/$f)"
		get -o "$OUT/$(basename "$f").tmp" "$URL/$f"
		echo "$s  $OUT/$(basename "$f").tmp" | sha256sum -c --quiet
		mv "$OUT/$(basename "$f").tmp" "$OUT/$(basename "$f")"
		continue
	fi
	if [ -n "$input" ]; then
		if [ -z "${INPUT:-}" ]; then
			echo ">> $pkg: made from the owner's copy only (make licensed-data PKG=$pkg INPUT=<copy>): skipped"
			continue
		fi
		echo ">> $pkg: game-data-packager $game, from $INPUT"
		/usr/games/game-data-packager -d "$OUT" -n --no-search --no-download "$game" --package "$pkg" "$INPUT"
	else
		echo ">> $pkg: game-data-packager $game"
		/usr/games/game-data-packager -d "$OUT" -n --no-search --save-downloads "$DL" "$game" --package "$pkg"
	fi
	deb=$(ls "$OUT/${pkg}_"*.deb)
	v=$(dpkg-deb -f "$deb" Version)
	if [ -n "$pub" ] && [ "$v" = "${pub%% *}" ]; then
		rm -f "$deb"
		echo "!! $pkg $v is published already and this file differs from it (game-data-packager isn't"
		echo "   reproducible): leave out REGEN=1 to take the published file"
		exit 1
	fi
	python3 -I tools/gamedb/stamp-data-deb.py "$deb"
done
ls -l "$OUT"
exit
}
