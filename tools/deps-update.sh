#!/bin/bash
# deps-update.sh: refreshes our repository's `deps` component from the debian-ports snapshot
# (inside the deb builder): an aptly mirror filtered to image/deps.txt with their dependencies
# (the PS3 sees only our repository: an application's libraries the base layer lacks must be
# here too; the ones it has come along at the same snapshot versions), checked against the ports
# keyring, imported into the ps3-deps repo. tools/repo-publish.sh then publishes and syncs as usual.
# Env: DEBIAN_MIRROR (the debian-ports snapshot URL)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${DEBIAN_MIRROR:?}"
A="aptly -config=/work/tools/aptly.conf"
names=$(grep -v '^#' image/deps.txt | grep -v '^$' | sort -u)
filter=$(echo "$names" | sed 's/.*/Name (= &)/' | paste -sd'|' | sed 's/|/ | /g')
M=debian-ports-deps
$A mirror show $M >/dev/null 2>&1 && $A mirror drop $M >/dev/null
$A mirror create -architectures=ppc64,all -keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
	-filter="$filter" -filter-with-deps $M "$DEBIAN_MIRROR" unstable main
$A mirror update -keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg $M
$A repo show ps3-deps >/dev/null 2>&1 || $A repo create -distribution=ps3 -component=deps ps3-deps >/dev/null
$A repo import -with-deps $M ps3-deps "$filter"
missing=$(for n in $names; do $A repo search ps3-deps "Name (= $n)" >/dev/null 2>&1 || echo "$n"; done)
[ -z "$missing" ] || { echo "!! not found in the snapshot: $missing"; exit 1; }
echo ">> deps: $(echo "$names" | wc -l) listed, $($A repo show ps3-deps | sed -n 's/^Number of packages: //p') with dependencies"
exit
}
