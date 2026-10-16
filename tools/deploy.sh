#!/bin/bash
# deploy.sh <pkg>...: deploys our current builds to the running PS3 through the dev suite
# (docs/PACKAGING.md): every local build (build/repo, build/debs/*, debug symbols included) is
# published to ps3-dev, then the PS3 installs from there with apt:
#   1. our lists only are refreshed (not Debian's: ~100 MB on a 256 MB machine);
#   2. every installed package of ours that has a newer build is upgraded (Mesa replaces
#      Debian's this way and keeps its auto marks);
#   3. the named top-level packages are installed (--no-remove: a step that would remove anything
#      fails; "name-" entries remove exactly those packages, e.g. one replaced by a renamed one);
#   4. step 2 again (what it held back until step 3 brought in a new dependency).
# Each step is simulated first. The deploy uses its own copy of our sources and pins (ps3-system's
# current ones), so it works before the PS3 has them. AUTOREMOVE=1 then purges what nothing
# needs any more (simulated first). ps3-migrate-home runs once for the deploying user.
# Env: PS3 (ssh destination), SSH, SCP, REPO_BUILDER_TAG, REPO_HOST, REPO_DIR
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${PS3:?}" "${REPO_BUILDER_TAG:?}" "${REPO_HOST:?}" "${REPO_DIR:?}"
debs=$( (ls build/repo/*.deb 2>/dev/null; find build/debs -mindepth 2 -maxdepth 2 -name '*.deb') | sort)
[ -n "$debs" ] || { echo "!! no packages built"; exit 1; }
DIST=dev tools/repo-publish.sh $debs

$SSH "$PS3" 'rm -rf /var/tmp/ps3-deploy && mkdir -p /var/tmp/ps3-deploy/ours /var/tmp/ps3-deploy/all'
$SCP ps3-system/files/apt/ps3.sources ps3-system/files/apt/ps3.pref "$PS3:/var/tmp/ps3-deploy/"
$SSH "$PS3" "set -e; cd /var/tmp/ps3-deploy
cp ps3.sources ours/; cp ps3.sources all/
[ ! -e /etc/apt/sources.list.d/debian.sources ] || cp /etc/apt/sources.list.d/debian.sources all/
U='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/ours -o APT::Get::List-Cleanup=0'
O='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/all \
-o Dir::Etc::Preferences=/var/tmp/ps3-deploy/ps3.pref -o Dir::Etc::PreferencesParts=/dev/null \
-o Dpkg::Use-Pty=0 -o APT::Get::Assume-Yes=true -t ps3-dev'
sudo apt-get \$U -qq update
ours() {	# installed packages that the dev suite has
	sed -n 's/^Package: //p' /var/lib/apt/lists/*_ps3-dev_dists_ps3-dev_*Packages | sort -u > avail
	dpkg-query -W -f='\${Package}\n' | sort -u | comm -12 - avail
}
upgrade() {
	pk=\$(ours)
	sudo DEBIAN_FRONTEND=noninteractive apt-get \$O -q --no-remove install --only-upgrade \$pk |
		grep -E '^(Setting up|Unpacking|E:|W:)' || true
}
echo '--- upgrade (simulated)'; sudo apt-get \$O -s --no-remove install --only-upgrade \$(ours) | grep -E '^(Inst|Remv)' || true
echo '--- install $* (simulated)'; sudo apt-get \$O -s install $* | grep -E '^(Inst|Remv)' || true
upgrade
want_rm=\$(for p in $*; do case \$p in *-) echo \${p%-} ;; esac; done | sort)
if [ -n \"\$want_rm\" ]; then
	will_rm=\$(sudo apt-get \$O -s install $* | awk '/^Remv /{print \$2}' | sed 's/:.*//' | sort)
	if [ \"\$will_rm\" != \"\$want_rm\" ]; then echo \"!! would remove: \$will_rm (asked: \$want_rm)\"; exit 1; fi
	NOREMOVE=
else
	NOREMOVE=--no-remove
fi
sudo DEBIAN_FRONTEND=noninteractive apt-get \$O -q \$NOREMOVE install $* | grep -E '^(Setting up|Unpacking|Removing|E:|W:)' || true
upgrade
sudo apt-get check >/dev/null && echo '>> apt-get check: ok'
[ ! -x /usr/lib/ps3/sbin/ps3-migrate-home ] || sudo /usr/lib/ps3/sbin/ps3-migrate-home \$(id -un)
if [ '${AUTOREMOVE:-0}' = 1 ]; then
	echo '--- autoremove --purge'; sudo apt-get \$O -s autoremove --purge | grep ^Purg || true
	sudo DEBIAN_FRONTEND=noninteractive apt-get \$O -q autoremove --purge | grep -E '^(Removing|Purging)' || true
fi
cd / && rm -rf /var/tmp/ps3-deploy"
exit
}
