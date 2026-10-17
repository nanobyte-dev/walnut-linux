#!/bin/bash
# repo-publish.sh <deb>...: publishes packages to our apt repository on tibi-server.
#   DIST=ps3 (default) stable, kept forever; DIST=dev dev builds (ps3-dev: newest 5 of each, symbols included).
#   Licensed packages (tools/licensed-packages.txt, Section non-free/* or local/*) go to ps3-licensed
#   with either DIST: kept forever like the stable suite, its own prefix and pool.
# 1. stages copies (aptly hardlinks its inputs; a later in-place rebuild must not change the pool),
#    leaving out packages that must never shadow Debian's (linux-libc-dev, linux-bpf-dev, docs/source);
# 2. aptly adds and publishes in the deb builder (tools/aptly-publish.sh);
# 3. syncs to the server: the kept trees (ps3, ps3-licensed) add-only (refuses if any published file
#    would change, indices last), the throwaway trees mirrored; then backs up aptly's database.
# Env: REPO_BUILDER_TAG DOCKER_RUN_ARGS REPO_HOST REPO_DIR DIST
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${REPO_BUILDER_TAG:?}" "${REPO_HOST:?}" "${REPO_DIR:?}"
DIST=${DIST:-ps3}
STAGE=build/tmp/publish-stage
rm -rf "$STAGE"; mkdir -p "$STAGE"
for f in "$@"; do
	case "$(basename "$f")" in
	linux-libc-dev_*|linux-bpf-dev_*|linux-doc*|linux-source*) echo ">> not published: $f"; continue ;;
	esac
	case "$(basename "$f")" in
	# debug symbols: with the build in ps3-dev; stable ones to ps3-dbgsym
	*-dbgsym_*.deb) if [ "$DIST" = dev ]; then cp "$f" "$STAGE/"; else cp "$f" "$STAGE/$(basename "${f%.deb}").ddeb"; fi ;;
	*) cp "$f" "$STAGE/" ;;
	esac
done
docker run --rm -v "$PWD:/work" -w /work --user "$(id -u):$(id -g)" -e HOME=/tmp -e DIST="$DIST" \
	"$REPO_BUILDER_TAG" tools/aptly-publish.sh "$STAGE"

PUB=build/aptly/public
# sync_kept <prefix>: a kept tree (its moving suite has the prefix's name), add-only
sync_kept() {
	local src=$PUB/$1/ dst=$REPO_HOST:$REPO_DIR/public/$1/ moving="dists/$1/***"
	# nothing already published may change (the moving suite's indices excepted)
	local changed
	changed=$(rsync -rlcn --existing --out-format='%n' --exclude="$moving" "$src" "$dst" | grep -v '/$' || true)
	if [ -n "$changed" ]; then
		echo "!! refusing: these published files in $1/ would change:"; echo "$changed"; exit 1
	fi
	rsync -rlt --ignore-existing --exclude="$moving" "$src" "$dst"	# pool, by-hash, new img-*
	rsync -rlt --delay-updates "$src/dists/$1/" "$dst/dists/$1/"		# the moving suite, last
}
if [ "$DIST" = ps3 ]; then sync_kept ps3; else rsync -rlt --delete "$PUB/ps3-dev/" "$REPO_HOST:$REPO_DIR/public/ps3-dev/"; fi
[ -d "$PUB/ps3-licensed/dists/ps3-licensed" ] && sync_kept ps3-licensed
[ -d "$PUB/ps3-dbgsym" ] && rsync -rlt --delete "$PUB/ps3-dbgsym/" "$REPO_HOST:$REPO_DIR/public/ps3-dbgsym/"
rsync -rlt --delete build/aptly/db/ "$REPO_HOST:$REPO_DIR/aptly-db/"
echo ">> synced to $REPO_HOST:$REPO_DIR"
exit
}
