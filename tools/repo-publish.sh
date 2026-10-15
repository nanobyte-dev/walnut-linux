#!/bin/bash
# repo-publish.sh <deb>...: publishes packages to our apt repository on tibi-server.
#   DIST=ps3 (default) official and kept forever; DIST=dev throwaway test builds.
# 1. stages copies (aptly hardlinks its inputs; a later in-place rebuild must not change the pool),
#    leaving out packages that must never shadow Debian's (linux-libc-dev, linux-bpf-dev, docs/source);
# 2. aptly adds and publishes in the deb builder (tools/aptly-publish.sh);
# 3. syncs to the server: the official tree add-only (refuses if any published file would change,
#    indices last), the throwaway trees mirrored; then backs up aptly's database.
# Env: REPO_BUILDER_TAG DOCKER_RUN_ARGS REPO_HOST REPO_DIR DIST
set -euo pipefail
: "${REPO_BUILDER_TAG:?}" "${REPO_HOST:?}" "${REPO_DIR:?}"
DIST=${DIST:-ps3}
STAGE=build/tmp/publish-stage
rm -rf "$STAGE"; mkdir -p "$STAGE"
for f in "$@"; do
	case "$(basename "$f")" in
	linux-libc-dev_*|linux-bpf-dev_*|linux-doc*|linux-source*) echo ">> not published: $f"; continue ;;
	esac
	cp "$f" "$STAGE/"
done
docker run --rm -v "$PWD:/work" -w /work --user "$(id -u):$(id -g)" -e DIST="$DIST" \
	"$REPO_BUILDER_TAG" tools/aptly-publish.sh "$STAGE"

PUB=build/aptly/public
sync_official() {
	local src=$PUB/ps3/ dst=$REPO_HOST:$REPO_DIR/public/ps3/
	# nothing already published may change (the moving suite's indices excepted)
	local changed
	changed=$(rsync -rlcn --existing --out-format='%n' --exclude='dists/ps3/***' "$src" "$dst" | grep -v '/$' || true)
	if [ -n "$changed" ]; then
		echo "!! refusing: these published files would change:"; echo "$changed"; exit 1
	fi
	rsync -rlt --ignore-existing --exclude='dists/ps3/***' "$src" "$dst"	# pool, by-hash, new img-*
	rsync -rlt --delay-updates "$src/dists/ps3/" "$dst/dists/ps3/"		# the moving suite, last
}
if [ "$DIST" = ps3 ]; then sync_official; else rsync -rlt --delete "$PUB/ps3-dev/" "$REPO_HOST:$REPO_DIR/public/ps3-dev/"; fi
[ -d "$PUB/ps3-dbgsym" ] && rsync -rlt --delete "$PUB/ps3-dbgsym/" "$REPO_HOST:$REPO_DIR/public/ps3-dbgsym/"
rsync -rlt --delete build/aptly/db/ "$REPO_HOST:$REPO_DIR/aptly-db/"
echo ">> synced to $REPO_HOST:$REPO_DIR"
