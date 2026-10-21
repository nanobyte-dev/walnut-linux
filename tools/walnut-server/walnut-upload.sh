#!/bin/bash
# walnut-upload.sh KIND ID DIR: drops DIR into walnut.nanobyte.dev's incoming/KIND/ID/ through the
# walnut-upload container (upload/: rrsync, write-only, no overwrite, no delete), then its manifest ID.ready
# (sha256sum of every file) last. The server's walnut-watch picks the bundle up only once the
# manifest is there, checks every file against it and puts it in place without a half-written
# moment (tools/walnut-server/walnut-watch). KIND: site, download, apt, licensed. An ID is used
# once; a new upload takes a new one (e.g. date-sha).
# Env: WALNUT_DEPLOY_KEY (the private key file; default ~/.ssh/walnut-deploy), WALNUT_DEPLOY_HOST
# (walnut.nanobyte.dev), WALNUT_DEPLOY_PORT (2223, the container's), WALNUT_KNOWN_HOSTS (this directory's known_hosts:
# the server's host key, pinned).
set -euo pipefail
[ $# = 3 ] || { echo "usage: $0 site|download|apt|licensed ID DIR" >&2; exit 2; }
kind=$1 id=$2 dir=${3%/}
case $kind in site|download|apt|licensed) ;; *) echo "!! unknown kind $kind" >&2; exit 2 ;; esac
[[ $id =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || { echo "!! bad id $id" >&2; exit 2; }
[ -d "$dir" ] || { echo "!! no directory $dir" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
key=${WALNUT_DEPLOY_KEY:-$HOME/.ssh/walnut-deploy}
host=${WALNUT_DEPLOY_HOST:-walnut.nanobyte.dev}
port=${WALNUT_DEPLOY_PORT:-2223}
known=${WALNUT_KNOWN_HOSTS:-$here/known_hosts}
[ -r "$key" ] || { echo "!! no key $key" >&2; exit 2; }

[ -z "$(find "$dir" ! -type f ! -type d -print -quit)" ] || { echo "!! $dir holds symlinks or special files" >&2; exit 1; }
[ -z "$(find "$dir" -mindepth 1 -name '.*' -print -quit)" ] || { echo "!! $dir holds hidden files" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'ssh -o ControlPath="$tmp/ctl" -O exit "walnut-deploy@$host" 2>/dev/null || true; rm -rf "$tmp"' EXIT
(cd "$dir" && find . -type f -printf '%P\0' | LC_ALL=C sort -z | xargs -0r sha256sum) > "$tmp/$id.ready"
[ -s "$tmp/$id.ready" ] || { echo "!! $dir is empty" >&2; exit 1; }

# one connection for both steps
ssh_cmd="ssh -i $key -p $port -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes"
ssh_cmd+=" -o UserKnownHostsFile=$known -o ControlMaster=auto -o ControlPath=$tmp/ctl -o ControlPersist=60"
up() { rsync -rt --chmod=D755,F644 -e "$ssh_cmd" "$@"; }
n=$(wc -l < "$tmp/$id.ready")
echo ">> $kind/$id: $n files, $(du -sh "$dir" | cut -f1)"
up "$dir/" "walnut-deploy@$host:$kind/$id/"
up "$tmp/$id.ready" "walnut-deploy@$host:$kind/$id.ready"
echo ">> uploaded; walnut-watch publishes it within seconds (its result: state/done/${kind}__$id on the server)"
