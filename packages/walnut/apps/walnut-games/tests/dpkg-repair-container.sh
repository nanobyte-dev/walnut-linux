#!/bin/bash
# Real-dpkg check of walnut-games-helper's repair_dpkg (not part of make test: it needs docker and the
# network). In a Debian container: a package whose postinst sleeps is installed, dpkg and its children
# are killed mid-run (as a power cut would), dpkg --audit lists it, repair_dpkg finishes it.
set -eu
cd "$(dirname "$0")/.."
helper=$PWD/files/sbin
w=$(mktemp -d "${TMPDIR:-/tmp}/r4.XXXXXX"); trap 'rm -rf "$w"' EXIT
mkdir -p "$w/pkg/DEBIAN"
printf 'Package: r4-victim\nVersion: 1\nArchitecture: all\nMaintainer: t <t@example.org>\nDescription: test\n' > "$w/pkg/DEBIAN/control"
printf '#!/bin/sh\nsleep 20\necho configured > /var/tmp/r4-configured\n' > "$w/pkg/DEBIAN/postinst"; chmod 755 "$w/pkg/DEBIAN/postinst"
cat > "$w/run.sh" <<'IN'
set -eu
apt-get update -qq >/dev/null; apt-get install -y -qq python3-minimal procps >/dev/null
dpkg-deb --root-owner-group -b /w/pkg /w/r4.deb >/dev/null
setsid dpkg -i /w/r4.deb >/dev/null 2>&1 &
sleep 4
pg=$(ps -o pgid= -p "$(pgrep -x dpkg | head -1)" | tr -d ' '); kill -9 -- "-$pg"; sleep 1
dpkg --audit | grep -q r4-victim || { echo "FAIL: nothing left half-done"; exit 1; }
python3 - <<'PY'
import importlib.machinery, importlib.util
l = importlib.machinery.SourceFileLoader("h", "/h/walnut-games-helper")
spec = importlib.util.spec_from_loader("h", l); h = importlib.util.module_from_spec(spec); l.exec_module(h)
h.repair_dpkg(h.run)
PY
[ -z "$(dpkg --audit)" ] && dpkg -s r4-victim | grep -q 'Status: install ok installed' && [ -e /var/tmp/r4-configured ] && echo "ok: repaired"
IN
docker run --rm -v "$w:/w" -v "$helper:/h:ro" debian:trixie-slim bash /w/run.sh
