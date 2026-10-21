#!/bin/bash
# ccache-remote.sh CMD...: runs CMD (a build in a docker container) with ccache's remote storage, the
# shared cache at walnut.nanobyte.dev/ccache, when WALNUT_CCACHE_AUTH (user:password; config.local.mk,
# or the CI secret) is set; otherwise just runs CMD. Starts tools/ccache-relay.py on the docker
# bridge's address for the duration and exports CCACHE_REMOTE_STORAGE for the container
# (Makefile DEB_ENV passes it on; tools/deb-build.sh hands it to ccache). Every build machine then
# reuses what any other compiled from the same sources with the same compiler.
set -euo pipefail
[ -n "${WALNUT_CCACHE_AUTH:-}" ] || exec "$@"
addr=$(ip -4 -o addr show docker0 2>/dev/null | awk '{print $4}' | cut -d/ -f1)
[ -n "$addr" ] || { echo "-- ccache-remote: no docker0 address, building without the shared cache" >&2; exec "$@"; }
here=$(cd "$(dirname "$0")" && pwd)
portfile=$(mktemp)
rm -f "$portfile"
python3 "$here/ccache-relay.py" "$addr" "$portfile" &
relay=$!
trap 'kill $relay 2>/dev/null || true; rm -f "$portfile"' EXIT
for _ in $(seq 50); do [ -s "$portfile" ] && break; sleep 0.1; done
[ -s "$portfile" ] || { echo "!! ccache-relay didn't start" >&2; exit 1; }
export CCACHE_REMOTE_STORAGE="http://$addr:$(cat "$portfile")/ccache|layout=subdirs"
echo "-- ccache: shared cache through $CCACHE_REMOTE_STORAGE"
"$@"
