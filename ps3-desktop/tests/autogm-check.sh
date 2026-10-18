#!/bin/bash
# autogm-check.sh [DIR]: automatic game mode (ps3-game-detect with ps3-game-guard and ps3-game)
# on this host, with no PS3. Runs the package's scripts, renamed so nothing of the host is touched
# (slices ps3t-*, state in $XDG_RUNTIME_DIR/ps3t-game, the root side a stub that logs, no sudo, no
# PCManFM), on the host's own systemd --user (scopes, freezing), against a nested X server with
# Openbox (Xvfb; in a container when the host has none: Docker or Podman, network host so the
# host's clients reach it on its abstract socket, and pid host: XRes gives the clients' pids). The games are tests/fsgame.c (GLX through Mesa,
# llvmpipe) and glxgears -fullscreen when the host has it.
# Covers: entering (the game moved out of the "desktop" slice before it is frozen, still drawing),
# leaving when the game exits, F11 out of fullscreen (game mode stays for LEAVE_DELAY, then ends;
# back to fullscreen enters again), vid_restart (a new window within the delay: no leave), an
# override-redirect window without _NET_WM_PID (XRes), PS3_GAME_MODE=off and =pads in the game's
# environment, fullscreen without the GPU, the exclusion list (a program named mpv) and
# PS3_GAME_EXCLUDE, output into a pipe read in the desktop (gamepads only), a game started through
# ps3-game next to a detected one (counted once each), the guard killed (nothing left frozen; the
# next window change enters again), the detector stopping and starting again with a game running;
# the games' own prefetch (ps3-game-prefetch game-watch, game-learn): a detected game's files learnt
# once game mode has left, read back at the next start of the same program, through ps3-game too, a
# watcher started again for a game that comes after the first one's looking is over, and
# PS3_GAME_PREFETCH_GAME=off (lists in DIR/gp, not the host's ~/.cache).
# Settings: AUTOGM_DISPLAY (:93), AUTOGM_IMAGE (a container image with Xvfb, openbox and x11-utils;
# built from AUTOGM_BASE, default debian:sid-slim, when missing). DIR (default: a new temporary
# directory) is removed at the end unless KEEP=1.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
W=${1:-$(mktemp -d)}
DPY=${AUTOGM_DISPLAY:-:93}
IMG=${AUTOGM_IMAGE:-ps3-autogm-x}
mkdir -p "$W/bin" "$W/lib" || exit 1
W=$(cd "$W" && pwd)
S=$XDG_RUNTIME_DIR/ps3t-game
export PS3_GAME_PREFETCH_DIR=$W/gp	# the games' lists (the games' environment says where)
export PS3_GAME_PREFETCH_SAMPLE_S=1	# each game looked at for 1 s: the watcher gone before the next game comes
DCG=	# the "desktop" slice's cgroup, once it runs
pass=0 fail=0
ok() { echo "PASS: $1"; pass=$((pass + 1)); }
bad() { echo "FAIL: $1"; fail=$((fail + 1)); }
check() { local n=$1; shift; if "$@"; then ok "$n"; else bad "$n"; fi; }
die() { echo "autogm-check: $*" >&2; exit 1; }
wait_for() {	# SECONDS COMMAND...: until it holds
	local t=$(($1 * 5)); shift
	while [ $t -gt 0 ]; do "$@" && return 0; sleep 0.2; t=$((t - 1)); done
	return 1
}
for t in systemd-run busctl cc python3 flock; do command -v $t > /dev/null || die "needs $t"; done
systemctl --user show-environment > /dev/null || die "needs a systemd --user manager"

# --- X: Xvfb + Openbox ---
ctr=
X=() XPID=
if command -v Xvfb > /dev/null && command -v openbox > /dev/null; then
	Xvfb "$DPY" -screen 0 1280x720x24 -nolisten tcp > "$W/x.log" 2>&1 & XPID=$!
	sleep 1; DISPLAY=$DPY openbox > "$W/openbox.log" 2>&1 & OBPID=$!
else
	for c in docker podman; do command -v $c > /dev/null && ctr=$c && break; done
	[ -n "$ctr" ] || die "needs Xvfb and openbox, or docker/podman"
	if ! $ctr image inspect "$IMG" > /dev/null 2>&1; then
		printf 'FROM %s\nRUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends xvfb openbox x11-utils x11-xserver-utils && rm -rf /var/lib/apt/lists/*\n' \
			"${AUTOGM_BASE:-debian:sid-slim}" | $ctr build -q -t "$IMG" - > /dev/null || die "image $IMG didn't build"
	fi
	$ctr rm -f ps3-autogm-x > /dev/null 2>&1
	$ctr run -d --rm --name ps3-autogm-x --network host --pid host "$IMG" sh -c \
		"Xvfb $DPY -screen 0 1280x720x24 -nolisten tcp & sleep 1; DISPLAY=$DPY exec openbox" > /dev/null || die "no X container"
	# the X tools the detector needs, from the image if the host lacks them (the host's libraries)
	id=$($ctr create "$IMG")
	for b in xev xwininfo xprop; do
		command -v $b > /dev/null || $ctr cp "$id:/usr/bin/$b" "$W/bin/" > /dev/null
	done
	$ctr rm "$id" > /dev/null
fi
export DISPLAY=$DPY PATH=$W/bin:$PATH
wait_for 15 sh -c 'xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id"' || die "no Openbox on $DPY"
for b in xev xwininfo xprop; do command -v $b > /dev/null || die "no $b"; done

# --- the package's scripts, renamed for the test ---
cc -O1 -o "$W/fsgame" "$here/tests/fsgame.c" -lX11 -lGL || die "fsgame didn't build (libX11, libGL headers)"
cp "$W/fsgame" "$W/mpv"; cp "$W/fsgame" "$W/bigtoy"
printf '#!/bin/sh\necho "$(date +%%s.%%N) $*" >> %s/root.log\n' "$W" > "$W/lib/ps3-game-mode"
ren() {
	sed -e "s|/usr/lib/ps3/desktop/|$W/lib/|g" -e "s|/usr/lib/ps3/sbin/ps3-game-mode|$W/lib/ps3-game-mode|g" \
		-e 's|sudo -n ||g' -e 's|ps3-desktop\.slice|ps3t-desktop.slice|g' -e 's|ps3-display\.slice|ps3t-display.slice|g' \
		-e 's|ps3-game\.slice|ps3t-game.slice|g' -e 's|app\.slice session\.slice|ps3t-app.slice ps3t-session.slice|g' \
		-e 's|--unit=ps3-game-mode |--unit=ps3t-game-mode |' -e 's|ps3-game-mode\.service|ps3t-game-mode.service|g' \
		-e 's|}/ps3-game$|}/ps3t-game|' -e 's|^desktop() {.*|desktop() { false; }	# test: no PCManFM|' \
		-e "s|\$HOME/.jgmenu-lockfile|$W/jgmenu-lockfile|g" "$1" > "$2"
	chmod +x "$2"
}
ren "$here/files/libexec/ps3-game-guard" "$W/lib/ps3-game-guard"
ren "$here/files/libexec/ps3-game-detect" "$W/lib/ps3-game-detect"
ren "$here/files/bin/ps3-game" "$W/bin/ps3-game"
ren "$here/files/libexec/ps3-game-escape" "$W/lib/ps3-game-escape"
ren "$here/files/libexec/ps3-pad-escape" "$W/lib/ps3-pad-escape"
# the PS button's listener reads a FIFO, not the host's gamepads (the guard's environment says so)
mkfifo "$W/pad"
export PS3_GAME_PAD_DEVICES=$W/pad
cp "$here/files/libexec/ps3-game-prefetch" "$here/files/libexec/ps3_game_prefetch.py" "$here/files/libexec/ps3-x-ready" "$W/lib/"
# the menu (tests/grabmenu.py): "jgmenu" on the PATH (the guard starts it again on leave), and a
# tray applet's popup by another name
cp "$here/tests/grabmenu.py" "$W/bin/jgmenu"; cp "$here/tests/grabmenu.py" "$W/bin/trayapplet"
chmod +x "$W/bin/jgmenu" "$W/bin/trayapplet"
chmod +x "$W/lib/ps3-game-mode"
# nothing of the host's left in the copies' code
if grep -v '^\s*#' "$W/lib/ps3-game-guard" "$W/lib/ps3-game-detect" "$W/bin/ps3-game" "$W/lib/ps3-game-escape" "$W/lib/ps3-pad-escape" |
	grep -nE 'ps3-(desktop|game|display)\.slice|[ "=]app\.slice|[ "=]session\.slice|/usr/lib/ps3|sudo -n|ps3-game-mode\.service|unit=ps3-game-mode'; then
	die "the renamed scripts still name the host's units (above)"
fi

cleanup() {
	systemctl --user stop ps3t-game-detect.service 2>/dev/null
	for p in $(jobs -p); do kill "$p" 2>/dev/null; done
	for p in "${GAMES[@]}"; do kill "$p" 2>/dev/null; done
	sleep 1
	systemctl --user stop ps3t-game-mode.service ps3t-panel.scope 2>/dev/null
	[ -e "$DCG/cgroup.freeze" ] && echo 0 > "$DCG/cgroup.freeze"
	systemctl --user stop ps3t-game.slice ps3t-desktop.slice ps3t-other.slice 2>/dev/null
	[ -n "$ctr" ] && $ctr rm -f ps3-autogm-x > /dev/null 2>&1
	[ -n "$XPID" ] && kill "$XPID" "$OBPID" 2>/dev/null
	rm -rf "$S"
	[ "${KEEP:-0}" = 1 ] || rm -rf "$W"
}
GAMES=()
trap cleanup EXIT
rm -rf "$S"

# --- the "desktop": a panel in ps3t-desktop.slice counting, and the detector ---
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-panel -q \
	sh -c "i=0; while :; do i=\$((i + 1)); echo \$i > '$W/panel'; sleep 0.2; done" > /dev/null 2>&1 &
wait_for 5 eval '[ -n "$(systemctl --user show -P ControlGroup ps3t-desktop.slice)" ]' || die "no desktop slice"
DCG=/sys/fs/cgroup$(systemctl --user show -P ControlGroup ps3t-desktop.slice)
detect_start() {	# [VAR=value...]: the detector as its service
	local e=() v
	for v in "$@"; do e+=("--setenv=$v"); done
	systemd-run --user --unit=ps3t-game-detect --collect -q -p "StandardOutput=append:$W/detect.log" \
		-p "StandardError=append:$W/detect.log" "--setenv=DISPLAY=$DPY" "--setenv=PATH=$W/bin:$PATH" \
		"--setenv=PS3_GAME_PAD_DEVICES=$W/pad" "${e[@]}" \
		"$W/lib/ps3-game-detect"
	wait_for 10 grep -q "watching" "$W/detect.log"
}
detect_start || die "the detector didn't start (log: $W/detect.log)"
sleep 0.5

entered() { [ -e "$S/entered" ]; }
left() { ! entered && ! systemctl --user is-active -q ps3t-game-mode.service && ! frozen; }
frozen() { grep -q '^frozen 1' "$DCG/cgroup.events" 2>/dev/null; }
runs() {	# FILE: its counter moves
	local a b; a=$(cat "$1" 2>/dev/null); sleep 0.7; b=$(cat "$1" 2>/dev/null); [ -n "$b" ] && [ "$a" != "$b" ]
}
auto_regs() { ls "$S/games" 2>/dev/null | grep -c '^auto-'; }
root_offs() { grep -c ' off' "$W/root.log" 2>/dev/null; }
cg() { cat "/proc/$1/cgroup" 2>/dev/null; }
game() {	# NAME [VAR=value...] -- PROGRAM ARGS: started from the "desktop" (as from its menu); pid in $GPID
	local n=$1; shift
	local e=()
	while [ "$1" != -- ]; do e+=("$1"); shift; done; shift
	systemd-run --user --scope --slice=ps3t-desktop.slice --unit="ps3t-$n" -q env "${e[@]}" "$@" \
		> "$W/$n.out" 2>&1 &
	GPID=$!; GAMES+=("$GPID")
}
stop_game() { kill "$1" 2>/dev/null; wait "$1" 2>/dev/null; }
jr() { journalctl --user -q -o cat -u ps3t-game-mode.service --since "@$1" 2>/dev/null; }	# the guard's log since

echo "== 1. a fullscreen GL game (_NET_WM_STATE_FULLSCREEN) started from the desktop"
T1=$(date +%s)
game g1 -- "$W/fsgame" -f -c "$W/c1"; G1=$GPID
check "enters game mode" wait_for 10 entered
check "the desktop is frozen" wait_for 5 frozen
check "the game moved to its own scope in ps3t-game.slice" eval '[[ $(cg $G1) == */ps3t-game.slice/ps3-game-auto-$G1-* ]]'
check "the game still draws" runs "$W/c1"
check "the panel doesn't" eval '! runs "$W/panel"'
check "registered once (auto-$G1)" eval '[ "$(auto_regs)" = 1 ] && [ -e "$S/games/auto-$G1" ]'
echo "== 2. the game exits"
stop_game "$G1"
check "game mode ends" wait_for 5 left
check "the panel runs again" runs "$W/panel"
check "its registration is gone" eval '[ "$(auto_regs)" = 0 ]'
sleep 1
check "the desktop's files were recorded on entering" eval 'jr $T1 | grep -q "ps3-game-prefetch: recorded [1-9]"'
check "leave: thawed, files fetched back, left (in that order)" eval 'jr $T1 | grep -E "leave: desktop thawed|fetched [0-9]+ files|left game mode" | tr "\n" " " | grep -qE "thawed.*fetched.*left game mode"'
check "the game's own files: nothing learnt yet at its start" eval 'jr $T1 | grep -q "game-fetch .*fsgame: nothing learnt yet"'
check "... learnt once game mode has left (the desktop back first)" eval 'jr $T1 | grep -E "game-learn .*fsgame: [1-9][0-9]* files|left game mode" | tr "\n" " " | grep -qE "left game mode.*game-learn"'
check "... its record gone" eval '[ -z "$(ls "$S/gp" 2>/dev/null)" ]'
check "... into its list (the program's key)" eval 'ls "$W/gp" | grep -q "fsgame.list$"'

echo "== 3. F11 out of fullscreen and back (Luanti)"
game g3 -- "$W/fsgame" -f -c "$W/c3"; G3=$GPID
wait_for 10 entered && wait_for 5 frozen
offs=$(root_offs)
kill -USR1 "$G3"; sleep 2.5
check "still in game mode 2.5 s after leaving fullscreen" eval 'entered && frozen'
check "leaves within ~5 s more" wait_for 6 left
check "the game runs on, in its scope" eval 'runs "$W/c3" && [[ $(cg $G3) == */ps3t-game.slice/ps3-game-auto-* ]]'
kill -USR1 "$G3"
check "fullscreen again: enters again" wait_for 10 eval 'entered && frozen'
echo "== 4. vid_restart (window destroyed, a new one 1.5 s later)"
offs=$(root_offs)
kill -USR2 "$G3"; sleep 4
check "in game mode through the new window" eval 'entered && frozen'
sleep 4
check "still, after the leave delay" eval 'entered && frozen && [ "$(root_offs)" = "$offs" ]'
stop_game "$G3"; wait_for 5 left

echo "== 5. override-redirect fullscreen window without _NET_WM_PID (SDL 1.2): XRes"
game g5 -- "$W/fsgame" -o -n -c "$W/c5"; G5=$GPID
check "enters game mode" wait_for 10 eval 'entered && frozen'
check "the game still draws" runs "$W/c5"
stop_game "$G5"
check "leaves when it exits" wait_for 5 left
if command -v glxgears > /dev/null; then
	game gears LIBGL_ALWAYS_SOFTWARE=1 -- glxgears -fullscreen; GG=$GPID
	check "glxgears -fullscreen enters game mode" wait_for 10 eval 'entered && frozen'
	stop_game "$GG"; check "and leaves" wait_for 5 left
fi

echo "== 6. opt-outs in the game's environment"
game g6 PS3_GAME_MODE=off -- "$W/fsgame" -f -c "$W/c6"; G6=$GPID
check "PS3_GAME_MODE=off: no game mode" eval '! wait_for 4 entered'
stop_game "$G6"; sleep 1
game g6b PS3_GAME_MODE=pads -- "$W/fsgame" -f -c "$W/c6b"; G6=$GPID
check "PS3_GAME_MODE=pads: game mode, nothing frozen" eval 'wait_for 10 entered && sleep 1 && ! frozen && runs "$W/panel"'
stop_game "$G6"; wait_for 5 left
game g6c PS3_GAME_MODE=on -- "$W/fsgame" -f -c "$W/c6c"; G6=$GPID
check "PS3_GAME_MODE=on: game mode" wait_for 10 eval 'entered && frozen'
stop_game "$G6"; wait_for 5 left
sleep 1.5; T6=$(date +%s)	# whole seconds: the run before ends earlier
game g6d PS3_GAME_PREFETCH=off -- "$W/fsgame" -f -c "$W/c6d"; G6=$GPID
wait_for 10 eval 'entered && frozen'; stop_game "$G6"; wait_for 5 left; sleep 1
check "PS3_GAME_PREFETCH=off: nothing recorded or fetched" eval 'jr $T6 | grep -q "left game mode" && ! jr $T6 | grep -qE "recorded|fetched"'
sleep 1.5; T6=$(date +%s)	# whole seconds: the run before ends earlier
game g6e PS3_GAME_PREFETCH=keep -- "$W/fsgame" -f -c "$W/c6e"; G6=$GPID
wait_for 10 eval 'entered && frozen'; stop_game "$G6"; wait_for 5 left; sleep 1
check "PS3_GAME_PREFETCH=keep: the desktop keeps its file pages, fetched anyway" eval 'jr $T6 | grep -q "anonymous only" && jr $T6 | grep -q fetched'
game g7 -- "$W/fsgame" -f -x -c "$W/c7"; G7=$GPID
check "fullscreen without the GPU: no game mode" eval '! wait_for 6 entered'
stop_game "$G7"
game g8 -- "$W/mpv" -f -c "$W/c8"; G8=$GPID
check "excluded by name (mpv): no game mode" eval '! wait_for 4 entered'

echo "== 7. the detector stops, starts again with PS3_GAME_EXCLUDE='-mpv bigtoy'"
systemctl --user stop ps3t-game-detect.service; : > "$W/detect.log"
detect_start "PS3_GAME_EXCLUDE=-mpv bigtoy"
check "mpv, already fullscreen, now enters" wait_for 10 eval 'entered && frozen'
systemctl --user stop ps3t-game-detect.service
check "the detector stopping ends its game mode" wait_for 5 left
stop_game "$G8"
detect_start "PS3_GAME_EXCLUDE=-mpv bigtoy"
game g9 -- "$W/bigtoy" -f -c "$W/c9"; G9=$GPID
check "bigtoy excluded: no game mode" eval '! wait_for 4 entered'
stop_game "$G9"
systemctl --user stop ps3t-game-detect.service; detect_start

echo "== 8. output into a pipe read in the desktop (frozen, it would block the game)"
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-g10 -q \
	sh -c "'$W/fsgame' -f -c '$W/c10' | cat > /dev/null" > /dev/null 2>&1 &
SH=$!; GAMES+=("$SH")
wait_for 5 pgrep -P "$SH" -x fsgame > /dev/null
G10=$(pgrep -P "$SH" -x fsgame)
check "game mode with the gamepads only (nothing frozen)" eval 'wait_for 10 entered && sleep 1 && ! frozen'
check "says why" grep -q "pipe of the desktop" "$W/detect.log"
kill "$G10"; wait_for 5 left

echo "== 9. a game through ps3-game and a detected one"
T9=$(date +%s)
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-w -q \
	"$W/bin/ps3-game" "$W/fsgame" -f -c "$W/cw" > "$W/w.out" 2>&1 &
WP=$!; GAMES+=("$WP")
check "ps3-game: game mode" wait_for 10 eval 'entered && frozen'
sleep 2
check "the detector leaves it to ps3-game (no auto- registration)" eval '[ "$(auto_regs)" = 0 ]'
# from outside the desktop (frozen now: a game started in it couldn't even map its window)
systemd-run --user --scope --slice=ps3t-other.slice --unit=ps3t-g11 -q "$W/fsgame" -f -c "$W/c11" > /dev/null 2>&1 &
G11=$!; GAMES+=("$G11")
check "a detected game joins (one auto- registration)" wait_for 10 eval '[ "$(auto_regs)" = 1 ]'
check "both had their lists read (game-fetch, the list learnt in 2.)" eval 'sleep 1; [ "$(jr $T9 | grep -c "game-fetch .*fsgame: read")" = 2 ]'
check "both draw" eval 'runs "$W/c11" && runs "$W/cw"'
stop_game "$G11"
sleep 2
check "the detected one gone: still in game mode (ps3-game's)" eval 'entered && frozen && [ "$(auto_regs)" = 0 ]'
kill "$(pgrep -P "$WP" -x fsgame)"
check "ps3-game's game gone: game mode ends" wait_for 5 left

check "PS3_GAME_PREFETCH_GAME=off: game mode, no prefetch" eval 'sleep 2; T=$(date +%s); game g13 PS3_GAME_PREFETCH_GAME=off -- "$W/fsgame" -f -c "$W/c13"; G13=$GPID
	wait_for 10 entered; sleep 1; stop_game $G13; wait_for 5 left; sleep 0.5
	jr $T | grep -q "PS3_GAME_PREFETCH_GAME=off" && ! jr $T | grep -qE "game-(fetch|learn)"'

echo "== 10. the guard killed"
game g12 -- "$W/fsgame" -f -c "$W/c12"; G12=$GPID
wait_for 10 eval 'entered && frozen'
kill -KILL "$(systemctl --user show -P MainPID ps3t-game-mode.service)"
check "nothing left frozen" wait_for 5 eval '! frozen && runs "$W/panel"'
check "the root side was undone" eval 'tail -1 "$W/root.log" | grep -q " off"'
kill -USR1 "$G12"; sleep 0.5; kill -USR1 "$G12"
check "the next window change enters again" wait_for 10 eval 'entered && frozen'
stop_game "$G12"
check "and leaves when it exits" wait_for 5 left

echo "== 11. the Games menu (jgmenu) still open, holding the keyboard and pointer, starts a game through ps3-game"
T11=$(date +%s)
rm -f "$W/menu.state"
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-menu -q \
	"$W/bin/jgmenu" --state "$W/menu.state" --spawn "$W/bin/ps3-game" "$W/fsgame" -f -c "$W/c14" > "$W/menu.out" 2>&1 &
MP=$!; GAMES+=("$MP")
check "the menu holds its grabs" wait_for 5 grep -q grabbed "$W/menu.state"
check "game mode: the desktop frozen" wait_for 15 eval 'entered && frozen'
check "the menu closed first" eval '! grep -qs "^State:.[^Z]" /proc/$MP/status && jr $T11 | grep -q "jgmenu closed"'
check "X answers other clients" timeout 5 xwininfo -root -tree > /dev/null
check "the keyboard and pointer are free (the probe)" timeout 8 "$W/lib/ps3-x-ready" "${DCG#/sys/fs/cgroup}"
check "the game draws" wait_for 10 runs "$W/c14"
kill "$(pgrep -x fsgame | while read -r p; do grep -q ps3t-game.slice "/proc/$p/cgroup" && echo "$p"; done | head -1)" 2>/dev/null
check "game mode ends with the game" wait_for 8 left
menu_back() {	# a hidden jgmenu in the desktop slice
	local p
	for p in $(pgrep -x jgmenu); do
		grep -q -- --hide-on-startup "/proc/$p/cmdline" && grep -q ps3t-desktop.slice "/proc/$p/cgroup" && return 0
	done
	return 1
}
check "jgmenu started again, hidden, in the desktop slice" wait_for 10 menu_back
pkill -x jgmenu

echo "== 12. a tray applet's popup open (a grab) when a game is detected"
T12=$(date +%s)
rm -f "$W/pop.state"
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-pop -q \
	"$W/bin/trayapplet" --state "$W/pop.state" > /dev/null 2>&1 &
PP=$!; GAMES+=("$PP")
wait_for 5 grep -q grabbed "$W/pop.state"
systemd-run --user --scope --slice=ps3t-other.slice --unit=ps3t-g15 -q "$W/fsgame" -f -c "$W/c15" > /dev/null 2>&1 &
G15=$!; GAMES+=("$G15")
check "game mode, the desktop not frozen (the popup would keep its grab)" eval 'wait_for 10 entered && sleep 7 && ! frozen && runs "$W/panel"'
check "says why" eval 'jr $T12 | grep -q "not frozen yet: the keyboard and pointer held, a popup of pid $PP"'
kill "$PP"
check "the popup closed: the desktop frozen within seconds" wait_for 8 frozen
check "the game draws" runs "$W/c15"
stop_game "$G15"
check "game mode ends" wait_for 8 left

echo "== 13. Ctrl+Alt+Esc (ps3-game-escape, as Openbox runs it) on a hung game (SIGTERM ignored)"
check "rc.xml binds Ctrl+Alt+Esc to it" eval 'grep -A1 "<keybind key=\"C-A-Escape\">" "$here/files/xdg/openbox/rc.xml" | grep -q "ps3-game-escape key"'
T13=$(date +%s)
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-h -q \
	"$W/bin/ps3-game" "$W/fsgame" -f -H -c "$W/c16" > /dev/null 2>&1 &
GAMES+=("$!")
wait_for 15 eval 'entered && frozen'
check "the hung game draws, the desktop frozen" eval 'runs "$W/c16" && frozen'
check "the PS button's listener runs in game mode" eval 'jr $T13 | grep -q "ps3-pad-escape: reading $W/pad"'
"$W/lib/ps3-game-escape" key > "$W/esc.out" 2>&1
check "the game is gone (SIGKILL after 3 s)" eval '! runs "$W/c16" && grep -q "SIGKILL" "$W/esc.out"'
check "game mode left, the desktop runs" wait_for 5 eval 'left && runs "$W/panel"'
check "no registration left" eval '[ -z "$(ls "$S/games" 2>/dev/null)" ]'
check "the listener is gone with game mode" eval '! pgrep -u "$(id -u)" -x ps3-pad-escape > /dev/null'
"$W/lib/ps3-game-escape" key > "$W/esc2.out" 2>&1
check "outside game mode, nothing fullscreen: nothing closed" grep -q "no game to close" "$W/esc2.out"

echo "== 14. holding the PS button (BTN_MODE events into the listener's FIFO)"
pad() { python3 -c 'import struct, sys; sys.stdout.buffer.write(struct.pack("llHHi", 0, 0, 1, 0x13c, int(sys.argv[1])) + struct.pack("llHHi", 0, 0, 0, 0, 0))' "$1" > "$W/pad"; }
T14=$(date +%s)
systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-p -q \
	"$W/bin/ps3-game" "$W/fsgame" -f -c "$W/c17" > /dev/null 2>&1 &
GAMES+=("$!")
wait_for 15 eval 'entered && frozen'
wait_for 5 eval 'jr $T14 | grep -q "ps3-pad-escape: reading"'
pad 1; sleep 1; pad 0; sleep 3
check "a short press: the game plays on" eval 'runs "$W/c17" && entered && frozen'
pad 1; sleep 4
check "held 3 s: the game closed, game mode left" wait_for 8 eval '! runs "$W/c17" && left'
pad 0 &	# the listener is gone: nothing reads the FIFO (this write is dropped below)
sleep 0.5; kill $! 2>/dev/null
check "says so" eval 'jr $T14 | grep -q "the PS button held for 3 s"'

echo "== 15. Alt+Tab and Win+D away from a game through ps3-game: the desktop thaws, back: frozen"
if [ -e /usr/lib64/libXtst.so.6 ] || [ -e /usr/lib/x86_64-linux-gnu/libXtst.so.6 ] || ldconfig -p 2>/dev/null | grep -q libXtst.so.6; then
	game term -- "$W/fsgame" -x -c "$W/cterm"; GT=$GPID	# a terminal of the desktop (a window, no GL)
	sleep 1
	systemd-run --user --scope --slice=ps3t-desktop.slice --unit=ps3t-a -q \
		"$W/bin/ps3-game" "$W/fsgame" -f -c "$W/c18" > /dev/null 2>&1 &
	GAMES+=("$!")
	wait_for 15 eval 'entered && frozen'
	sleep 1
	check "the game in front, the desktop frozen" eval 'frozen && ! runs "$W/cterm"'
	python3 "$here/tests/xkey.py" Alt_L+Tab
	check "Alt+Tab to the terminal: the desktop thaws (after ~3 s)" wait_for 8 eval '! frozen && runs "$W/cterm"'
	check "game mode stays on, the game draws" eval 'entered && runs "$W/c18"'
	python3 "$here/tests/xkey.py" Alt_L+Tab
	check "Alt+Tab back to the game: frozen again" wait_for 8 frozen
	python3 "$here/tests/xkey.py" Super_L+d
	check "Win+D (show the desktop): thawed" wait_for 8 eval '! frozen && runs "$W/panel"'
	python3 "$here/tests/xkey.py" Super_L+d
	check "Win+D again (the game back in front): frozen" wait_for 8 frozen
	kill "$(pgrep -x fsgame | while read -r p; do grep -q ps3t-game.slice "/proc/$p/cgroup" && echo "$p"; done | head -1)" 2>/dev/null
	check "the game exits: game mode left" wait_for 8 left
	stop_game "$GT"
else
	echo "(no libXtst: Alt+Tab and Win+D not tested)"
fi

echo "== the end: nothing left"
check "no registrations" eval '[ -z "$(ls "$S/games" 2>/dev/null)" ]'
check "the desktop runs" eval '! frozen && runs "$W/panel"'
check "the detector still runs" systemctl --user is-active -q ps3t-game-detect.service
echo "-- detector log:"; sed 's/^/   /' "$W/detect.log"
echo "== $pass passed, $fail failed"
[ $fail = 0 ]
