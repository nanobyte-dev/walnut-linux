#!/bin/bash
# build.sh [--preview] [--app] OUT: build the Walnut Games site into the directory OUT (docs/GAMEDB.md).
#
# Steps, each of which stops the build on failure:
#   1. check.py: the catalog (gamedb/games, mounted into the site as content/games by hugo.toml) is
#      valid, no licensed package in it
#   2. catalog.py: website/gamedb/data/catalog.json (the ratings, from the sessions' result.toml); with
#      GAMEDB_DEB_INDEX=https://walnut.nanobyte.dev/apt the pages also link the .deb files of the patched
#      games (without it, or offline, no links)
#   3. Hugo: the pinned release of tools/gamedb/hugo.version, downloaded to build/tools/hugo-<version>/
#      on first use and verified with its sha256 (the pinned one and the release's checksums file)
#   4. every link in the pages made relative to its page (and with --app, a folder's index.html named)
#   5. the output against walnut-watch's site rules (servers/walnut.nanobyte.dev/walnut-watch: SITE_EXT,
#      NAME_RE, no symlinks, an index.html): anything the server would refuse fails here, before an upload
# --preview builds for http://localhost:8000/ (serve it with: python3 -m http.server -d OUT 8000);
# without it the links are for https://walnut.nanobyte.dev/gamedb/. Every link in the pages is made
# relative (step 4), so a copy of OUT works offline too.
# --app builds the copy the desktop app shows offline (its Details view, docs/GAMEDB.md "The desktop app"):
# site.Params.app is set (HUGO_PARAMS_APP), so the pages leave out the site's header, footer and script and
# show the game's actions as walnut:// links; and every link to a folder names its index.html, which a
# file:// view (Tkhtml) doesn't add by itself.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=""
app=0
while [ $# -gt 1 ]; do
	case "$1" in
	--preview) base="http://localhost:8000/" ;;
	--app) app=1 ;;
	*) break ;;
	esac
	shift
done
[ $# -eq 1 ] || { echo "usage: $0 [--preview] OUT" >&2; exit 2; }
out=$(realpath -m "$1")

# shellcheck source=hugo.version
. "$here/hugo.version"
tools="$repo/build/tools/hugo-$HUGO_VERSION"
hugo="$tools/hugo"
if [ ! -x "$hugo" ]; then
	tar_name="hugo_extended_${HUGO_VERSION}_linux-amd64.tar.gz"
	url="https://github.com/gohugoio/hugo/releases/download/v$HUGO_VERSION"
	mkdir -p "$tools"
	curl -fsSL -o "$tools/$tar_name" "$url/$tar_name"
	curl -fsSL -o "$tools/checksums.txt" "$url/hugo_${HUGO_VERSION}_checksums.txt"
	got=$(sha256sum "$tools/$tar_name" | cut -d' ' -f1)
	listed=$(awk -v f="$tar_name" '$2 == f {print $1}' "$tools/checksums.txt")
	if [ "$got" != "$HUGO_SHA256" ] || [ "$got" != "$listed" ]; then
		echo "Hugo tarball checksum mismatch: got $got, pinned $HUGO_SHA256, release lists $listed" >&2
		rm -f "$tools/$tar_name"
		exit 1
	fi
	tar -xzf "$tools/$tar_name" -C "$tools" hugo
fi

python3 -I "$here/check.py"
deb=()
[ -z "${GAMEDB_DEB_INDEX:-}" ] || deb=(--deb-index "$GAMEDB_DEB_INDEX")
python3 -I "$here/catalog.py" "${deb[@]}"

mkdir -p "$out"
args=(--source "$repo/website/gamedb" --destination "$out" --cleanDestinationDir --cacheDir "$repo/build/tools/hugo-cache")
[ -z "$base" ] || args+=(--baseURL "$base")
if [ "$app" = 1 ]; then HUGO_PARAMS_APP=true "$hugo" "${args[@]}"; else "$hugo" "${args[@]}"; fi
cp "$repo/website/gamedb/data/catalog.json" "$out/catalog.json"

# every link in the pages relative to the page (Hugo's relativeURLs gets it wrong under a base path like
# /gamedb/), so a copy of OUT works anywhere, offline too; the feeds and the sitemap keep absolute URLs. With
# --app a link to a folder ("../technical/", "./#a") also names its index.html
python3 -I "$repo/tools/gamedb/site_relative.py" "$out" "${base:-$(sed -n 's/^baseURL = "\(.*\)"$/\1/p' "$repo/website/gamedb/hugo.toml")}" "$app"

# the server's rules, read from walnut-watch itself so the two cannot drift apart
python3 -I "$repo/tools/gamedb/site_rules.py" "$repo/servers/walnut.nanobyte.dev/walnut-watch" "$out"
