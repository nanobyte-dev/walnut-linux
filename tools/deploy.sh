#!/bin/bash
# deploy.sh <pkg>...: installs our packages on the running PS3 with apt (docs/PLAN.md: never dpkg -i,
# never full-upgrade). The debs (build/repo, build/debs/*) go to the PS3 as a flat repository in
# /var/tmp/ps3-deploy; apt sees only that one (its own lists directory), pinned at 1001:
#   1. upgrade: every installed package of ours that has a newer build (Mesa replaces Debian's this
#      way and keeps its auto marks; the old ps3-mesa becomes the empty transitional package);
#   2. install the named top-level packages (--no-remove: a step that would remove anything fails);
#   3. upgrade again (what step 1 held back until step 2 brought in a new dependency).
# Each step is simulated first and the simulation printed. AUTOREMOVE=1 then shows what
# autoremove would purge and does it (Debian's LLVM, z3, sensors after the Mesa switch).
# Env: PS3 (ssh destination), SSH (ssh command), SCP, REPO_BUILDER_TAG
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${PS3:?}" "${REPO_BUILDER_TAG:?}"
STAGE=build/tmp/deploy
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp build/repo/*.deb "$STAGE/"
find build/debs -mindepth 2 -maxdepth 2 -name "*.deb" -exec cp -t "$STAGE/" {} +
rm -f "$STAGE"/*-dbgsym_*.deb	# debug symbols: from the ps3-dbgsym repository when needed
# Debian packages ours depend on that the PS3 may not have yet (the repository's deps component)
find build/aptly/public/ps3/pool/deps -name '*.deb' -exec cp {} "$STAGE/" \; 2>/dev/null || true
docker run --rm -v "$PWD:/work" -w "/work/$STAGE" --user "$(id -u):$(id -g)" "$REPO_BUILDER_TAG" \
	sh -c 'apt-ftparchive packages . > Packages'
$SSH "$PS3" 'rm -rf /var/tmp/ps3-deploy && mkdir -p /var/tmp/ps3-deploy/lists/partial'
$SCP "$STAGE"/* "$PS3:/var/tmp/ps3-deploy/"
O="-o Dir::Etc::SourceList=/var/tmp/ps3-deploy/sources.list -o Dir::Etc::SourceParts=/dev/null \
-o Dir::State::Lists=/var/tmp/ps3-deploy/lists -o Dir::Etc::PreferencesParts=/dev/null \
-o Dir::Etc::Preferences=/var/tmp/ps3-deploy/pref -o Dpkg::Use-Pty=0 -o APT::Get::Assume-Yes=true"
$SSH "$PS3" "set -e; cd /var/tmp/ps3-deploy
echo 'deb [trusted=yes] file:/var/tmp/ps3-deploy ./' > sources.list
printf 'Package: *\nPin: origin \"\"\nPin-Priority: 1001\n' > pref
sudo apt-get $O -qq update
echo '--- upgrade (simulated)'; sudo apt-get $O -s --no-remove upgrade | grep -E '^(Inst|Remv|Conf)' | grep -v ^Conf || true
echo '--- install $* (simulated)'; sudo apt-get $O -s --no-remove install $* | grep -E '^(Inst|Remv)' || true
sudo DEBIAN_FRONTEND=noninteractive apt-get $O -q --no-remove upgrade | grep -E '^(Setting up|Unpacking|E:|W:)' || true
sudo DEBIAN_FRONTEND=noninteractive apt-get $O -q --no-remove install $* | grep -E '^(Setting up|Unpacking|E:|W:)' || true
# again: what the first pass held back for a dependency the install step just brought in
sudo DEBIAN_FRONTEND=noninteractive apt-get $O -q --no-remove upgrade | grep -E '^(Setting up|Unpacking|E:|W:)' || true
sudo apt-get $O check >/dev/null && echo '>> apt-get check: ok'
if [ '${AUTOREMOVE:-0}' = 1 ]; then
	echo '--- autoremove --purge'; sudo apt-get $O -s autoremove --purge | grep ^Purg || true
	sudo DEBIAN_FRONTEND=noninteractive apt-get $O -q autoremove --purge | grep -E '^(Removing|Purging)' || true
fi
cd / && sudo rm -rf /var/tmp/ps3-deploy"
exit
}
