#!/bin/bash
# gamemode-check.sh: what game mode (ps3-desktop's ps3-game) frees, and whether everything it
# freezes or unloads comes back. On the PS3, as the desktop user with the desktop up (over SSH is
# fine; uses sudo): make bench-push, then /var/tmp/bench/gamemode-check.sh [options]
#   -t SECONDS  how long the dummy game runs once game mode is on (default 20)
#   -a MB       the dummy game allocates and touches MB of memory (default 0: it only sleeps)
#   -m MODE     PS3_GAME_MODE for the run: on (default), pads (baseline: nothing frozen)
#   --crash     also: the launcher's process group SIGKILLed mid-game, then the guard SIGKILLed
#               mid-game; everything must come back both times
#   --two       also: two overlapping games (game mode must last until the second one exits)
#   --sound     also: a 2 s test tone through ALSA at the end (speaker-test)
# Phases: idle (the desktop as it is), game (the dummy game under ps3-game), after (game mode
# left, the desktop back). Each one: /proc/meminfo, swaps, zram and VRAM swap, cgroups, ps3rsx's
# VRAM per client, and every process's RSS, anon, file, swap and PSS. Then checks: the guard
# gone, nothing frozen, the same helper processes alive and running, the PCManFM desktop and its
# wallpaper back, the tray icons back, the gamepads back on the pointer, the user and system
# buses answering (notifications, gvfs, polkit, udisks2, NetworkManager, BlueZ), X, ALSA and the
# network. The TV still has to be looked at (wallpaper, panel, applets reacting to clicks).
# Results: /var/tmp/ps3-bench/<UTC stamp>-gamemode/ (make bench-pull): summary.txt (table and
# PASS/FAIL lines), snap-<phase>.txt, procs-<phase>.tsv, samples (1/s: time MemAvailable MemFree
# SwapFree, kB). The snapshots run a Python process (~8 MB) after reading /proc/meminfo.
set -u
SECS=20 MB=0 MODE=on CRASH=0 TWO=0 SOUND=0
while [ $# -gt 0 ]; do
	case $1 in
	-t) SECS=$2; shift ;;
	-a) MB=$2; shift ;;
	-m) MODE=$2; shift ;;
	--crash) CRASH=1 ;;
	--two) TWO=1 ;;
	--sound) SOUND=1 ;;
	*) sed -n '2,/^set -u/{/^set -u/d;s/^# \{0,1\}//p}' "$0"; exit 2 ;;
	esac
	shift
done
die() { echo "!! $*" >&2; exit 1; }
uid=$(id -u)
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$uid}
export DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}
eval "$(systemctl --user show-environment 2>/dev/null | grep -E '^(DISPLAY|XAUTHORITY|XDG_CONFIG_DIRS|XDG_DATA_DIRS)=' | sed 's/^/export /')"
[ -n "${DISPLAY:-}" ] && timeout 5 xset q >/dev/null 2>&1 || die "no desktop: X isn't answering on '${DISPLAY:-}'"
[ -e /dev/shm/ps3-bench.lock ] && die "a bench session runs (ps3-bench.sh desktop ends it)"
command -v ps3-game >/dev/null || die "ps3-game isn't installed (ps3-desktop)"
sudo -n true || die "needs sudo"
GS=$XDG_RUNTIME_DIR/ps3-game
guard_live() { case $(systemctl --user show -P ActiveState ps3-game-mode.service) in active|activating|deactivating) return 0 ;; esac; return 1; }
guard_live && die "game mode is on already (a game runs?)"
D=/var/tmp/ps3-bench/$(date -u +%Y%m%dT%H%M%SZ)-gamemode
mkdir -p "$D"
echo ">> $D"
{ echo "args: -t $SECS -a $MB -m $MODE crash=$CRASH two=$TWO"; uname -r
  dpkg-query -W -f '${Package} ${Version}\n' ps3-desktop ps3-system 2>/dev/null; } > "$D/info"

kb() { awk -v k="$1:" '$1 == k { print $2 }' /proc/meminfo; }
cgdir() { local p; p=$(systemctl --user show -P ControlGroup "$1" 2>/dev/null); [ -n "$p" ] && echo "/sys/fs/cgroup$p"; }
DESKCG=$(cgdir ps3-desktop.slice)
UNITS="udisks2.service polkit.service blueman-mechanism.service"
pcmanfm_desktop() { pgrep -u "$uid" -x -f '(/[^ ]*/)?pcmanfm --desktop' >/dev/null; }
tray_children() { xwininfo -name tint2 -children 2>/dev/null | sed -n 's/^ *\([0-9]*\) child.*/\1/p' | head -1; }
pads() {	# "id:enabled" of the gamepads attached as X pointers
	local d name id
	for d in /sys/class/input/event*; do
		udevadm info -q property "$d" 2>/dev/null | grep -q '^ID_INPUT_JOYSTICK=1' || continue
		name=$(cat "$d/device/name")
		for id in $(xinput list 2>/dev/null | grep -F "$name" | grep 'slave  pointer' | sed 's/.*id=\([0-9]*\).*/\1/'); do
			echo "$id:$(xinput list-props "$id" | awk -F'\t' '/Device Enabled/ { print $NF }')"
		done
	done | sort -u | tr '\n' ' '
}

# every process: pid comm state cgroup rss anon file shmem swap pss (kB), biggest RSS first
PROCS='
import os
rows = []
for p in os.listdir("/proc"):
    if not p.isdigit(): continue
    try:
        st = {}
        for l in open(f"/proc/{p}/status"):
            k, _, v = l.partition(":"); st[k] = v.strip()
        if "VmRSS" not in st: continue
        cg = open(f"/proc/{p}/cgroup").read().strip().split("::")[-1]
        pss = "0"
        try:
            for l in open(f"/proc/{p}/smaps_rollup"):
                if l.startswith("Pss:"): pss = l.split()[1]
        except OSError: pass
        kbv = lambda k: st.get(k, "0 kB").split()[0]
        cg = "/".join(cg.split("/")[-2:]) or "/"
        rows.append((int(kbv("VmRSS")), p, st["Name"], st["State"].split()[0], cg, kbv("VmRSS"),
                     kbv("RssAnon"), kbv("RssFile"), kbv("RssShmem"), kbv("VmSwap"), pss))
    except (OSError, KeyError): pass
print("pid\tcomm\tstate\tcgroup\trss\tanon\tfile\tshmem\tswap\tpss")
for r in sorted(rows, reverse=True): print("\t".join(r[1:]))
'
snap() {	# snap PHASE
	local f=$D/snap-$1.txt c
	{ echo "== $1 $(date -u +%FT%T.%NZ)"
	  grep -E '^(MemTotal|MemFree|MemAvailable|Buffers|Cached|Shmem|SwapTotal|SwapFree|AnonPages|Mapped|Slab|KernelStack|VmallocUsed|CmaFree):' /proc/meminfo
	  cat /proc/swaps
	  echo "zram mm_stat: $(cat /sys/block/zram0/mm_stat 2>/dev/null)"
	  cat /sys/block/ps3vswap0/stats 2>/dev/null | head -2
	  for c in system.slice user.slice; do echo "cgroup $c $(cat /sys/fs/cgroup/$c/memory.current)"; done
	  for c in ps3-desktop.slice ps3-display.slice ps3-game.slice app.slice session.slice; do
		d=$(cgdir $c) && echo "cgroup $c $(cat "$d/memory.current") swap $(cat "$d/memory.swap.current" 2>/dev/null) $(grep frozen "$d/cgroup.events" 2>/dev/null)"
	  done
	  for u in $UNITS; do echo "unit $u $(systemctl show -P ActiveState $u) $(systemctl show -P FreezerState $u)"; done
	  echo "guard $(systemctl --user show -P ActiveState ps3-game-mode.service) $(ls "$GS" 2>/dev/null | tr '\n' ' ')"
	  pcmanfm_desktop && echo "pcmanfm desktop: running" || echo "pcmanfm desktop: not running"
	  echo "root pixmap: $(xprop -root _XROOTPMAP_ID 2>/dev/null)"
	  echo "tint2 children: $(tray_children)"
	  echo "pads: $(pads)"
	  echo "VRAM clients:"; sudo sh -c 'cat /sys/kernel/debug/dri/*/ps3rsx_mm 2>/dev/null' | grep -E ' handles, VRAM |^VRAM|^GART'
	} > "$f" 2>&1
	sudo python3 -I -c "$PROCS" > "$D/procs-$1.tsv"
}
field() { awk -v k="$2" '$1 == k { print $2; exit }' "$D/snap-$1.txt"; }	# field PHASE Key:
cgfield() { awk -v k="$2" '$1 == "cgroup" && $2 == k { print int($3 / 1024); exit }' "$D/snap-$1.txt"; }

R=$D/summary.txt
: > "$R"
pass() { echo "PASS $*" | tee -a "$R"; }
fail() { echo "FAIL $*" | tee -a "$R"; FAILS=$((FAILS + 1)); }
FAILS=0
check() { local what=$1; shift; if "$@" >/dev/null 2>&1; then pass "$what"; else fail "$what"; fi; }
waitfor() { local n=$1; shift; while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 1; n=$((n - 1)); done; return 1; }

# probes that must answer after a game as they did before it
probe_notify() { timeout 5 dbus-send --session --print-reply --dest=org.freedesktop.Notifications /org/freedesktop/Notifications org.freedesktop.Notifications.GetServerInformation; }
probe_gvfs() { timeout 15 gio mount -l; }
probe_sysbus() { timeout 8 dbus-send --system --print-reply --dest="$1" "$2" org.freedesktop.DBus.Peer.Ping; }
probe_userbus() { timeout 5 dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.Peer.Ping; }
probe_alsa() { aplay -l 2>/dev/null | grep -q '^card'; }
probe_net() { local gw; gw=$(ip route | awk '/^default/ { print $3; exit }'); [ -n "$gw" ] && ping -c 1 -W 3 "$gw"; }
PROBES="userbus notify gvfs polkit udisks nm bluez alsa net xserver"
probe() {
	case $1 in
	userbus) probe_userbus ;; notify) probe_notify ;; gvfs) probe_gvfs ;;
	polkit) probe_sysbus org.freedesktop.PolicyKit1 /org/freedesktop/PolicyKit1/Authority ;;
	udisks) systemctl is-active -q udisks2.service && probe_sysbus org.freedesktop.UDisks2 /org/freedesktop/UDisks2/Manager ;;
	nm) systemctl is-active -q NetworkManager.service && probe_sysbus org.freedesktop.NetworkManager /org/freedesktop/NetworkManager ;;
	bluez) systemctl is-active -q bluetooth.service && probe_sysbus org.bluez / ;;
	alsa) probe_alsa ;; net) probe_net ;; xserver) timeout 5 xset q ;;
	esac
}

HELPERS="tint2 nm-applet pnmixer lxpolkit dunst jgmenu gvfsd gvfsd-metadata gvfs-udisks2-volume-monitor"
helper_pids() { local h; for h in $HELPERS; do pgrep -u "$uid" -x "$h" | sed "s/^/$h:/"; done | tr '\n' ' '; }
helpers_back() {	# the helpers of idle: the same pids, not stopped, their cgroup not frozen
	local hp p c ok=0
	for hp in $IDLE_HELPERS; do
		p=${hp#*:}
		if [ ! -d /proc/$p ]; then echo "  ${hp%%:*} ($p) is gone" | tee -a "$R"; ok=1; continue; fi
		st=$(awk '{ print $3 }' /proc/$p/stat)
		c=/sys/fs/cgroup$(cut -d: -f3 /proc/$p/cgroup)
		if [ "$st" = T ] || [ "$st" = t ] || grep -q '^frozen 1' "$c/cgroup.events" 2>/dev/null; then
			echo "  ${hp%%:*} ($p) is stopped or frozen (state $st)" | tee -a "$R"; ok=1
		fi
	done
	return $ok
}

# the checks after game mode, against what idle had
after_checks() {	# after_checks LABEL
	local l=$1 u st
	check "$l: guard gone" waitfor 20 eval '! guard_live'
	[ -n "$DESKCG" ] && check "$l: desktop slice not frozen" grep -q '^frozen 0' "$DESKCG/cgroup.events"
	for u in $UNITS; do
		st=$(systemctl show -P FreezerState $u)
		[ "$(systemctl show -P ActiveState $u)" = active ] && check "$l: $u running (FreezerState $st)" [ "$st" = running ]
	done
	check "$l: no system state left (/run/ps3-game-mode)" eval '[ -z "$(ls /run/ps3-game-mode 2>/dev/null)" ]'
	check "$l: system watcher gone" eval '! systemctl is-active -q ps3-game-mode-watch.service'
	check "$l: helpers alive and running" helpers_back
	if [ "$IDLE_DESKTOP" = 1 ]; then
		check "$l: PCManFM desktop back" waitfor 30 pcmanfm_desktop
		check "$l: wallpaper back on the root window" waitfor 20 eval 'xprop -root _XROOTPMAP_ID | grep -q "# 0x"'
	fi
	[ -n "$IDLE_TRAY" ] && check "$l: tray icons back ($IDLE_TRAY children)" waitfor 15 eval '[ "$(tray_children)" = "$IDLE_TRAY" ]'
	check "$l: gamepads as before ($IDLE_PADS)" eval '[ "$(pads)" = "$IDLE_PADS" ]'
	for p in $IDLE_PROBES; do check "$l: $p answers" probe $p; done
}

# one sample a second, the whole run
( while [ -d "$D" ] && [ ! -e "$D/.stop" ]; do
	echo "$(date +%s.%N | cut -c1-14) $(kb MemAvailable) $(kb MemFree) $(kb SwapFree)"; sleep 1
  done > "$D/samples" ) &
SAMPLER=$!
trap 'touch "$D/.stop"; kill $SAMPLER 2>/dev/null' EXIT

echo ">> idle: settling 5 s"; sleep 5
IDLE_HELPERS=$(helper_pids)
IDLE_DESKTOP=0; pcmanfm_desktop && IDLE_DESKTOP=1
IDLE_TRAY=$(tray_children)
IDLE_PADS=$(pads)
IDLE_PROBES=
for p in $PROBES; do probe $p >/dev/null 2>&1 && IDLE_PROBES="$IDLE_PROBES $p"; done
echo "idle: helpers $IDLE_HELPERS; desktop $IDLE_DESKTOP; tray $IDLE_TRAY; pads '$IDLE_PADS'; answering:$IDLE_PROBES" | tee -a "$R"
snap idle

entered() { [ -e "$GS/entered" ]; }

echo ">> game: ps3-game, PS3_GAME_MODE=$MODE, $SECS s"
t0=$(date +%s.%N)
if [ "$MB" -gt 0 ]; then
	PS3_GAME_MODE=$MODE ps3-game python3 -I -c "import sys, time; open(sys.argv[3], 'w').write(str(time.time())); b = bytearray(b'\\1') * (int(sys.argv[1]) << 20); time.sleep(float(sys.argv[2]))" "$MB" $((SECS + 5)) "$D/game-started" &
else
	PS3_GAME_MODE=$MODE ps3-game sh -c "date +%s.%N > '$D/game-started'; exec sleep $((SECS + 5))" &
fi
GAME=$!
waitfor 60 test -s "$D/game-started" || fail "game: the dummy game didn't start in 60 s"
delay=$(awk -v a="$t0" -v b="$(cat "$D/game-started" 2>/dev/null || echo 0)" 'BEGIN { printf "%.1f", b - a }')
echo "game: started $delay s after ps3-game (game mode's entry)" | tee -a "$R"
sleep 3
if [ "$MODE" = on ]; then
	check "game: guard on" entered
	[ -n "$DESKCG" ] && check "game: desktop slice frozen" grep -q '^frozen 1' "$DESKCG/cgroup.events"
	for u in $UNITS; do
		[ "$(systemctl show -P ActiveState $u)" = active ] && check "game: $u frozen" [ "$(systemctl show -P FreezerState $u)" = frozen ]
	done
	if [ "$IDLE_DESKTOP" = 1 ]; then
		check "game: PCManFM desktop unloaded" eval '! pcmanfm_desktop'
		check "game: no wallpaper on the root window" eval '! xprop -root _XROOTPMAP_ID | grep -q "# 0x"'
	fi
	check "game: network still answers" probe_net
	[ -n "$(echo "$IDLE_PROBES" | grep -w bluez)" ] && check "game: BlueZ still answers" probe bluez
	check "game: ALSA still there" probe_alsa
fi
[ -n "$IDLE_PADS" ] && check "game: gamepads off the pointer" eval '! pads | grep -q ":1"'
snap game
echo ">> waiting for the game to end"
wait $GAME
echo ">> after: game mode leaving, the desktop coming back"
after_checks after
sleep 5
snap after

if [ "$TWO" = 1 ]; then
	echo ">> two games"
	PS3_GAME_MODE=$MODE ps3-game sleep 30 & A=$!
	waitfor 60 entered
	PS3_GAME_MODE=$MODE ps3-game sleep 8 & B=$!
	wait $B; sleep 3
	check "two: game mode still on after the first game ended" eval 'guard_live && entered'
	[ -n "$DESKCG" ] && [ "$MODE" = on ] && check "two: desktop still frozen" grep -q '^frozen 1' "$DESKCG/cgroup.events"
	wait $A
	after_checks two
fi

if [ "$CRASH" = 1 ]; then
	echo ">> crash 1: the launcher's process group killed (game and launcher)"
	PS3_GAME_MODE=$MODE setsid ps3-game sleep 600 & L=$!
	waitfor 60 entered; sleep 2
	kill -KILL -- -$L 2>/dev/null; wait $L 2>/dev/null
	after_checks crash-group
	echo ">> crash 2: the guard killed while the game runs"
	PS3_GAME_MODE=$MODE ps3-game sleep 600 & L=$!
	waitfor 60 entered; sleep 2
	kill -KILL "$(systemctl --user show -P MainPID ps3-game-mode.service)"
	after_checks crash-guard
	pkill -u "$uid" -f '^sleep 600$'; wait $L 2>/dev/null
	check "crash-guard: game end leaves nothing behind" eval '! guard_live && [ -z "$(ls "$GS" 2>/dev/null | grep -v -e games -e lock -e guard)" ]'
fi

if [ "$SOUND" = 1 ]; then
	echo ">> sound: 2 s test tone"
	check "sound: speaker-test played" timeout 10 speaker-test -t sine -f 440 -l 1 -c 2 -s 1
fi

touch "$D/.stop"; wait $SAMPLER 2>/dev/null
# the table: idle, game, after
{
	echo
	printf '%-28s %10s %10s %10s\n' "kB" idle game after
	for k in MemAvailable: MemFree: Cached: AnonPages: Shmem: SwapFree: CmaFree:; do
		printf '%-28s %10s %10s %10s\n' "$k" "$(field idle $k)" "$(field game $k)" "$(field after $k)"
	done
	for c in ps3-desktop.slice ps3-display.slice ps3-game.slice system.slice user.slice; do
		printf '%-28s %10s %10s %10s\n' "cgroup $c" "$(cgfield idle $c)" "$(cgfield game $c)" "$(cgfield after $c)"
	done
	printf '%-28s %10s\n' "min MemAvailable in game" "$(awk 'NR == 1 || $2 < m { m = $2 } END { print m }' "$D/samples")"
	echo
	echo "Processes (RSS / swap kB) idle -> game -> after, the 20 biggest at idle:"
	awk -F'\t' 'FNR == 1 { f++; next } { k = $1 " " $2; r[f, k] = $5 "/" $9 } f == 1 { o[++n] = k }
		END { for (i = 1; i <= n && i <= 20; i++) { k = o[i]
			printf "  %-28s %12s %12s %12s\n", k, r[1, k], ((2, k) in r) ? r[2, k] : "-", ((3, k) in r) ? r[3, k] : "-" } }' \
		"$D/procs-idle.tsv" "$D/procs-game.tsv" "$D/procs-after.tsv"
	echo
	echo "$FAILS failed"
} | tee -a "$R"
echo ">> $R"
[ "$FAILS" = 0 ]
