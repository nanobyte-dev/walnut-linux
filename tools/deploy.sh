#!/bin/bash
# deploy.sh <pkg>...: deploys our current builds to the running PS3 (docs/PACKAGING.md "Deploying"):
# every local build (build/debs/*, debug symbols and licensed packages included) becomes a flat apt
# repository in build/tmp/deploy-repo, served on this PC's loopback and reached by the PS3 through
# an SSH reverse tunnel (127.0.0.1:DEPLOY_PORT on both ends), so apt fetches only what it installs
# and nothing is published anywhere. Then on the PS3, with apt:
#   1. our lists only are refreshed (not Debian's: ~100 MB on a 256 MB machine);
#   2. every installed package of ours that has a newer build is upgraded (Mesa replaces
#      Debian's this way and keeps its auto marks); kernels and their modules only with KERNELS=1
#      or when named (a kernel build left in build/debs, unfinished and without its ps3rsx
#      modules, got installed by a deploy of two desktop packages on 2026-10-16: no graphics);
#   3. the named top-level packages are installed (--no-remove: a step that would remove anything
#      fails; "name-" entries remove exactly those packages, e.g. one replaced by a renamed one);
#   4. step 2 again (what it held back until step 3 brought in a new dependency).
# Each step is simulated first. Only this repository's list is read, so a Debian package that one of
# ours newly depends on must be installed from Debian first (apt says which). AUTOREMOVE=1 then
# purges what nothing needs any more (simulated first). ps3-migrate-home runs once for the
# deploying user. Kernel and Mesa installs stop the desktop first (STOP_DESKTOP=auto|1|0).
# DRY_RUN=1: only the simulations on the PS3. KERNELS=1: upgrade kernels too (step 2).
# Env: PS3 (ssh destination), SSH, SCP, REPO_BUILDER_TAG, DEPLOY_PORT (18071), DEBS_DIR (build/debs)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
: "${PS3:?}" "${REPO_BUILDER_TAG:?}"
# someone else holds the PS3 lease (tools/ps3 lock): stop before any ssh
"$(dirname "$0")/ps3" check || exit 1
PORT=${DEPLOY_PORT:-18071}
DEBS_DIR=${DEBS_DIR:-build/debs}
# never during a bench session (ps3-bench.sh: measurements from a quiet PS3; desktop ends it)
if $SSH "$PS3" 'test -e /dev/shm/ps3-bench.lock'; then
	echo "!! a bench session runs on the PS3 (ps3-bench.sh desktop ends it)"; exit 1
fi
debs=$(find "$DEBS_DIR" -mindepth 2 -maxdepth 2 -name '*.deb' | sort)
[ -n "$debs" ] || { echo "!! no packages built"; exit 1; }
# the flat repository: hard links to the builds (copies across file systems), without the packages
# that must never shadow Debian's (linux-libc-dev, linux-bpf-dev, docs and source)
REPO=build/tmp/deploy-repo
rm -rf "$REPO"; mkdir -p "$REPO"
for f in $debs; do
	case ${f##*/} in linux-libc-dev_*|linux-bpf-dev_*|linux-doc*|linux-source*) continue ;; esac
	ln "$f" "$REPO/" 2>/dev/null || cp "$f" "$REPO/"
done
docker run --rm -v "$PWD:/work" -w /work/$REPO --user "$(id -u):$(id -g)" "$REPO_BUILDER_TAG" sh -c '
	apt-ftparchive packages . > Packages
	apt-ftparchive -o APT::FTPArchive::Release::Origin=Walnut-deploy -o APT::FTPArchive::Release::Label=Walnut-deploy \
		-o APT::FTPArchive::Release::Suite=walnut-deploy -o APT::FTPArchive::Release::Codename=walnut-deploy release . > ../deploy-Release
	mv ../deploy-Release Release'
sed -n 's/^Package: //p' "$REPO/Packages" | sort -u > "$REPO/avail"
echo ">> $(wc -l < "$REPO/avail") packages in $REPO, served to the PS3 on 127.0.0.1:$PORT"
python3 -m http.server --bind 127.0.0.1 --directory "$REPO" "$PORT" >build/tmp/deploy-http.log 2>&1 &
http=$!
trap 'kill $http 2>/dev/null || true' EXIT
for _ in $(seq 50); do curl -fsS -o /dev/null "http://127.0.0.1:$PORT/Release" 2>/dev/null && break; sleep 0.1; done
kill -0 $http 2>/dev/null || { echo "!! the repository's HTTP server didn't start (port $PORT taken? build/tmp/deploy-http.log)"; exit 1; }

$SSH "$PS3" 'rm -rf /var/tmp/ps3-deploy && mkdir -p /var/tmp/ps3-deploy/ours'
$SCP "$REPO/avail" "$PS3:/var/tmp/ps3-deploy/"
$SSH -o ExitOnForwardFailure=yes -R "127.0.0.1:$PORT:127.0.0.1:$PORT" "$PS3" "set -e; cd /var/tmp/ps3-deploy
echo 'deb [trusted=yes] http://127.0.0.1:$PORT ./' > ours/walnut-deploy.list
# Only our repository's list, never Debian's: with Debian's ~60000 packages apt's solves (five of
# them here) exhausted the PS3's 256 MB and stalled it (2026-10-16). apt's caches stay in memory
# (the system's are for Debian's lists).
U='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/ours -o APT::Get::List-Cleanup=0'
O='-o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts=/var/tmp/ps3-deploy/ours \
-o Dir::Cache::pkgcache= -o Dir::Cache::srcpkgcache= \
-o Dir::Etc::Preferences=/dev/null -o Dir::Etc::PreferencesParts=/dev/null \
-o Dpkg::Use-Pty=0 -o APT::Get::Assume-Yes=true -o Dpkg::Options::=--force-confdef \
-o Dpkg::Options::=--force-confold -t walnut-deploy'
sudo DEBIAN_FRONTEND=noninteractive dpkg --configure -a	# an install interrupted earlier
sudo apt-get \$U -qq update
# aptf ERE ARG...: sudo apt-get ARG..., showing only the lines of its output that match ERE (its
# errors as they come), and failing with apt-get's status: a filter's || true hid a failed install
# (the deploy reported success). apt-get's status goes through a file: no pipefail needed in this shell.
aptf() {
	re=\$1; shift
	{ sudo DEBIAN_FRONTEND=noninteractive apt-get \"\$@\" && echo 0 > /var/tmp/ps3-deploy/apt.rc || echo \$? > /var/tmp/ps3-deploy/apt.rc; } |
		{ grep -E \"\$re\" || true; }	# no line matched: not a failure
	rc=\$(cat /var/tmp/ps3-deploy/apt.rc)
	[ \"\$rc\" = 0 ] || { echo \"!! apt-get \$* failed (exit \$rc)\"; return \"\$rc\"; }
}
ours() {	# installed packages that this deploy has a build of (kernels only with KERNELS=1)
	dpkg-query -W -f='\${Package}\n' | sort -u | comm -12 - avail |
		if [ '${KERNELS:-0}' = 1 ]; then cat; else grep -vE '^(linux-(image|headers|kbuild)|ps3rsx-modules)'; fi
}
upgrade() {
	pk=\$(ours)
	aptf '^(Setting up|Unpacking|E:|W:)' \$O -q --no-remove install --only-upgrade \$pk
}
echo '--- upgrade (simulated)'; aptf '^(Inst|Remv)' \$O -s --no-remove install --only-upgrade \$(ours)
echo '--- install $* (simulated)'; aptf '^(Inst|Remv|E:)' \$O -s install $*
if [ '${DRY_RUN:-0}' = 1 ]; then
	echo '>> DRY_RUN: simulations only'
	[ -z \"\$stopped\" ] || sudo systemctl start getty@tty1
	cd / && rm -rf /var/tmp/ps3-deploy; exit 0
fi
# Kernel and Mesa installs need the RAM the desktop holds: two kernels unpacked next to a
# running desktop thrashed swap on the USB disk into a 30-minute stall (2026-10-16). The desktop
# is stopped for them (STOP_DESKTOP=auto; 1 always, 0 never) and started again afterwards.
heavy=\$(aptf '^Inst (linux-image|linux-headers|mesa-libgallium|libgl1-mesa-dri)' \$O -s install $* \$(ours) 2>/dev/null) ||
	{ echo '!! the install simulation failed'; exit 1; }
stopped=
if [ '${STOP_DESKTOP:-auto}' = 1 ] || { [ '${STOP_DESKTOP:-auto}' = auto ] && [ -n \"\$heavy\" ]; }; then
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
aptf '^(Setting up|Unpacking|Removing|E:|W:)' \$O -q \$NOREMOVE install $*
upgrade
sudo apt-get check >/dev/null && echo '>> apt-get check: ok'
[ ! -x /usr/lib/ps3/sbin/ps3-migrate-home ] || sudo /usr/lib/ps3/sbin/ps3-migrate-home \$(id -un)
[ -z \"\$stopped\" ] || { echo '>> starting the desktop again'; sudo systemctl start getty@tty1; }
if [ '${AUTOREMOVE:-0}' = 1 ]; then
	echo '--- autoremove --purge'; aptf '^Purg' \$O -s autoremove --purge
	aptf '^(Removing|Purging)' \$O -q autoremove --purge
fi
cd / && rm -rf /var/tmp/ps3-deploy"
rm -rf "$REPO"
exit
}
