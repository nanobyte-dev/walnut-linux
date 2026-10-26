#!/bin/bash
# publish-site.sh: uploads website/site/ as walnut.nanobyte.dev's next site version and
# waits until it is live (its version.txt served), with the games database built into /gamedb/
# (tools/gamedb/build.sh: the upload replaces the whole site, so it rides along). The id is
# <UTC time>-<commit>. Used by the GitHub
# workflow (.github/workflows/walnut-site.yml) and `make walnut-site`; the upload key and host come
# from walnut-upload.sh's environment.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
id=$(date -u +%Y%m%d-%H%M%S)-$(git -C "$here" rev-parse --short=7 HEAD)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp -r "$here/../../website/site/." "$tmp/"
"$here/../../tools/gamedb/build.sh" "$tmp/gamedb"
echo "$id" > "$tmp/version.txt"
"$here/walnut-upload.sh" site "$id" "$tmp"
url=${WALNUT_SITE_URL:-https://walnut.nanobyte.dev}
for _ in $(seq 60); do
	if [ "$(curl -fsS "$url/version.txt" 2>/dev/null)" = "$id" ]; then
		echo ">> $url is on $id"
		exit 0
	fi
	sleep 3
done
echo "!! $id is not live after 3 minutes: the watcher refused it or the new container was not healthy" >&2
echo "   (on the server: journalctl -u walnut-watch, state/done/site__$id)" >&2
exit 1
