#!/bin/bash
# repo-snapshot.sh: freezes the stable suite as img-<stamp> (inside the deb builder): aptly
# snapshots of ps3-main and ps3-deps, published as the distribution img-<stamp> under the ps3
# prefix (same pool, nothing copied). Images built with REPO_SUITE=img-<stamp> install exactly
# that set. Kept forever, like the stable suite. tools/repo-publish.sh then syncs it.
set -euo pipefail
{
A="aptly -config=/work/tools/aptly.conf"
stamp=img-$(date -u +%Y%m%d%H%M)
$A snapshot create "$stamp-main" from repo ps3-main >/dev/null
$A snapshot create "$stamp-deps" from repo ps3-deps >/dev/null
$A publish snapshot -origin=PS3 -label=PS3 -distribution="$stamp" -architectures=ppc64,all \
	-component=main,deps -acquire-by-hash "$stamp-main" "$stamp-deps" ps3 >/dev/null
echo ">> published $stamp: $($A snapshot show "$stamp-main" | sed -n 's/^Number of packages: //p') packages in main"
echo "$stamp" > /work/build/aptly/last-snapshot
exit
}
