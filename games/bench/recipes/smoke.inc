# Shared by the smoke recipes (docs/GAME-PLAN.md, "Smoke tests (not benchmarked)"): games tried once
# to see whether they start and play, not measured. A recipe sources this file first. Not a recipe
# itself (run.sh list only shows recipes/*.sh).
#   SUITE=0     kept out of `run.sh suite`
#   DURATION    120 s by default, SMOKE_SECS=N overrides it (run.sh smoke caps a run at 360 s,
#               BENCH_TIMEOUT=N raises that: SMOKE_SECS=600 BENCH_TIMEOUT=700 run.sh smoke GAME)
#   WARMUP, MEASURE   only for the HUD summary in the report (10 s after the load, then 90 s); no
#               fps is parsed from the game and nothing is compared with it
#   vblank_mode=0     no swap interval from Mesa, whatever the game asks for (X-Moto's recipe does this too)
# The game is started in the closest thing to its play state the command line allows; a recipe
# says in its header when that is only a menu and the player has to start it (keyboard or the DS3).
SUITE=0
DURATION=${SMOKE_SECS:-120}
WARMUP=10
MEASURE=90
ENV+=(vblank_mode=0)
# smoke_keys TITLE WAIT KEY...: for a game that waits for a key after its load (a title card, a
# "press a key" screen). A virtual keyboard (root: /dev/uinput, through sudo -n; the device
# description of assets/etr-race.evemu, as armagetronad.sh does) gets the keys once a window whose
# name starts with TITLE (case-insensitive, xwininfo's tree) is up and WAIT seconds more have gone:
# each KEY an evdev code in hex (001c Return, 0039 Space, 006c Down, 0001 Esc), pressed and released
# 1 s apart; +N waits N seconds more. Called from a recipe's during(), the run's log is during.log.
smoke_keys() {
	local title=$1 wait=$2 i k node kbd=${BENCH_DIR:-$D}/.smoke-kbd
	shift 2
	grep -v '^E:' "$ASSETS/etr-race.evemu" > "$kbd.desc"
	sudo -n timeout $((DURATION + 30)) evemu-device "$kbd.desc" > "$kbd.log" 2>&1 &
	for i in $(seq 50); do node=$(awk '{print $NF}' "$kbd.log" | grep -m1 /dev/input/); [ -n "$node" ] && break; sleep 0.1; done
	[ -n "$node" ] || { echo "no virtual keyboard"; cat "$kbd.log"; return 1; }
	for i in $(seq 1200); do xwininfo -root -tree 2>/dev/null | grep -qi "\"$title" && break; sleep 0.1; done
	echo "window \"$title\" at $EPOCHREALTIME (or the 120 s wait ran out)"
	sleep "$wait"
	for k; do
		case $k in +*) sleep "${k#+}"; continue ;; esac
		printf 'E: 0.000000 0001 %s 0001\nE: 0.000000 0000 0000 0000\nE: 0.050000 0001 %s 0000\nE: 0.050000 0000 0000 0000\n' "$k" "$k" > "$kbd.keys"
		evemu-play "$node" < "$kbd.keys"; echo "key $k at $EPOCHREALTIME"; sleep 1
	done
	rm -f "$kbd.desc" "$kbd.keys"
}
