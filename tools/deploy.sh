#!/bin/bash
# deploy.sh <pkg>...: deploys our current builds to the running PS3 through the dev suite
# (docs/PACKAGING.md): every local build (build/debs/*, debug symbols included) is
# published to ps3-dev (licensed ones to ps3-licensed), then the PS3 installs from there with apt:
#   1. our lists only are refreshed (not Debian's: ~100 MB on a 256 MB machine);
#   2. every installed package of ours that has a newer build is upgraded (Mesa replaces
#      Debian's this way and keeps its auto marks); kernels and their modules only with KERNELS=1
#      or when named (a kernel build left in build/debs, unfinished and without its ps3rsx
#      modules, got installed by a deploy of two desktop packages on 2026-10-16: no graphics);
#   3. the named top-level packages are installed (--no-remove: a step that would remove anything
#      fails; "name-" entries remove exactly those packages, e.g. one replaced by a renamed one);
#   4. step 2 again (what it held back until step 3 brought in a new dependency).
# Each step is simulated first. The deploy uses its own copy of our sources and pins (ps3-system's
# current ones), so it works before the PS3 has them. AUTOREMOVE=1 then purges what nothing
# needs any more (simulated first). ps3-migrate-home runs once for the deploying user. Kernel and
# Mesa installs stop the desktop first (STOP_DESKTOP=auto|1|0).
# DRY_RUN=1: publish, then only the simulations on the PS3. KERNELS=1: upgrade kernels too (step 2).
# Env: PS3 (ssh destination), SSH, SCP, REPO_BUILDER_TAG, REPO_HOST, REPO_DIR
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${PS3:?}" "${REPO_BUILDER_TAG:?}" "${REPO_HOST:?}" "${REPO_DIR:?}"
# never during a bench session (ps3-bench.sh: measurements from a quiet PS3; desktop ends it)
if $SSH "$PS3" 'test -e /dev/shm/ps3-bench.lock'; then
	echo "!! a bench session runs on the PS3 (ps3-bench.sh desktop ends it)"; exit 1
fi
debs=$(find build/debs -mindepth 2 -maxdepth 2 -name '*.deb' | sort)
[ -n "$debs" ] || { echo "!! no packages built"; exit 1; }
DIST=dev tools/repo-publish.sh $debs

$SSH "$PS3" 'rm -rf /var/tmp/ps3-deploy && mkdir -p /var/tmp/ps3-deploy/ours'
$SCP ps3-system/files/apt/ps3.sources ps3-system/files/apt/ps3.pref "$PS3:/var/tmp/ps3-deploy/"
$SSH "$PS3" "set -e; cd /var/tmp/ps3-deploy
cp ps3.sources ours/
# Only our repository's lists, never Debian's: with Debian's ~60000 packages apt's solves (five of
# them here) exhausted the PS3's 256 MB and stalled it (2026-10-16). Debian packages ours depend on
# come from our deps component. apt's caches stay in memory (the system's are for Debian's lists).
U='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/ours -o APT::Get::List-Cleanup=0'
O='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/ours \
-o Dir::Cache::pkgcache= -o Dir::Cache::srcpkgcache= \
-o Dir::Etc::Preferences=/var/tmp/ps3-deploy/ps3.pref -o Dir::Etc::PreferencesParts=/dev/null \
-o Dpkg::Use-Pty=0 -o APT::Get::Assume-Yes=true -o Dpkg::Options::=--force-confdef \
-o Dpkg::Options::=--force-confold -t ps3-dev'
sudo DEBIAN_FRONTEND=noninteractive dpkg --configure -a	# an install interrupted earlier
sudo apt-get \$U -qq update
ours() {	# installed packages that the dev or licensed suite has (kernels only with KERNELS=1)
	sed -n 's/^Package: //p' /var/lib/apt/lists/*_ps3-dev_dists_ps3-dev_*Packages \
		\$(ls /var/lib/apt/lists/*_ps3-licensed_dists_ps3-licensed_*Packages 2>/dev/null) | sort -u > avail
	dpkg-query -W -f='\${Package}\n' | sort -u | comm -12 - avail |
		if [ '${KERNELS:-0}' = 1 ]; then cat; else grep -vE '^(linux-(image|headers|kbuild)|ps3rsx-modules)'; fi
}
upgrade() {
	pk=\$(ours)
	sudo DEBIAN_FRONTEND=noninteractive apt-get \$O -q --no-remove install --only-upgrade \$pk |
		grep -E '^(Setting up|Unpacking|E:|W:)' || true
}
echo '--- upgrade (simulated)'; sudo apt-get \$O -s --no-remove install --only-upgrade \$(ours) | grep -E '^(Inst|Remv)' || true
echo '--- install $* (simulated)'; sudo apt-get \$O -s install $* | grep -E '^(Inst|Remv|E:)' || true
if [ '${DRY_RUN:-0}' = 1 ]; then
	echo '>> DRY_RUN: simulations only'
	[ -z \"\$stopped\" ] || sudo systemctl start getty@tty1
	cd / && rm -rf /var/tmp/ps3-deploy; exit 0
fi
# Kernel and Mesa installs need the RAM the desktop holds: two kernels unpacked next to a
# running desktop thrashed swap on the USB disk into a 30-minute stall (2026-10-16). The desktop
# is stopped for them (STOP_DESKTOP=auto; 1 always, 0 never) and started again afterwards.
heavy=\$(sudo apt-get \$O -s install $* \$(ours) 2>/dev/null | grep -cE '^Inst (linux-image|linux-headers|mesa-libgallium|libgl1-mesa-dri)' || true)
stopped=
if [ '${STOP_DESKTOP:-auto}' = 1 ] || { [ '${STOP_DESKTOP:-auto}' = auto ] && [ \"\$heavy\" -gt 0 ]; }; then
	if pidof Xorg >/dev/null; then
		echo '>> stopping the desktop for this install'
		sudo systemctl stop getty@tty1
		for s in \$(loginctl list-sessions --no-legend | awk '\$4==\"tty1\" || \$5==\"tty1\" {print \$1}'); do sudo loginctl terminate-session \$s; done
		sleep 3; stopped=1
		# the TV shows the text console with a notice instead of the last frame X left behind
		sudo chvt 1 2>/dev/null || true
		printf '\\033[2J\\033[H\\n\\n\\n    Updating the system.\\n\\n    The desktop comes back by itself when it is done.\\n' | sudo tee /dev/tty1 >/dev/null || true
	fi
fi
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
[ -z \"\$stopped\" ] || { echo '>> starting the desktop again'; sudo systemctl start getty@tty1; }
if [ '${AUTOREMOVE:-0}' = 1 ]; then
	echo '--- autoremove --purge'; sudo apt-get \$O -s autoremove --purge | grep ^Purg || true
	sudo DEBIAN_FRONTEND=noninteractive apt-get \$O -q autoremove --purge | grep -E '^(Removing|Purging)' || true
fi
cd / && rm -rf /var/tmp/ps3-deploy"
exit
}
