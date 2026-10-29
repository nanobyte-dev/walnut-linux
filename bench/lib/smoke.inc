# Shared by the smoke recipes (docs/games/SUITE.md "Smoke runs"; what each game showed: its technical.md): games tried once
# to see whether they start and play, not measured. A recipe sources this file first. Not a recipe
# itself (run.sh list shows the games' benchmark/recipe.sh files).
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
# keyboard.evemu, a keyboard's evemu description: the device of gamedb:etr's recording, its events
# left out; as armagetronad's recipe does) gets the keys once a window whose
# name starts with TITLE (case-insensitive, xwininfo's tree) is up and WAIT seconds more have gone:
# each KEY an evdev code in hex (001c Return, 0039 Space, 006c Down, 0001 Esc), pressed and released
# 1 s apart; +N waits N seconds more. Called from a recipe's during(), the run's log is during.log.
smoke_keys() {
	local title=$1 wait=$2 i k node kbd=${BENCH_DIR:-$D}/.smoke-kbd
	shift 2
	cp "$BENCH_LIB/keyboard.evemu" "$kbd.desc"
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
# smoke_xkeys TITLE WAIT KEY...: the same through the X server's XTEST extension (xkeys.py, python3
# ctypes on libX11 and libXtst, both in the image; no input device, no root, no sudo). KEY is an X
# keysym name (Return, space, Up, Escape); +N waits N seconds more. The window gets the input focus
# first and each key is held 0.25 s (a press and release 50 ms apart could land in one frame of a
# game that compares this frame's keys with the last one's: SuperTux's Controller::pressed).
# This is for SDL2 games: SuperTux's run of 2026-10-18 got its virtual-keyboard Returns (smoke_keys:
# the device was created, X added it, both presses were played) and stayed on its title card.
smoke_xkeys() {
	local title=$1 wait=$2 i
	shift 2
	for i in $(seq 1200); do xwininfo -root -tree 2>/dev/null | grep -qi "\"$title" && break; sleep 0.1; done
	echo "window \"$title\" at $EPOCHREALTIME (or the 120 s wait ran out)"
	sleep "$wait"
	python3 "$BENCH_LIB/xkeys.py" "$title" "$@"
}
# smoke_pad EVENT...: a virtual DualShock 3 for the whole run, to try a game's pad scheme without a
# player (ds3.evemu: the DS3 as the kernel's hid-sony shows it, written by hand; SDL calls it "PS3
# Controller"). Call it from setup(): most games open their pad at start-up, so it has to exist before
# the game, and the events are timed from this call, in order. EVENT: a button, pressed and released
# 0.3 s later (cross circle square triangle l1 r1 l2 r2 select start ps l3 r3 up down left right); +N waits N
# seconds; lx=V, ly=V, rx=V or ry=V puts a stick at V (0-255, 128 the middle: ly=0 pushes the left
# stick forward). The pad stays until the run ends. evemu-play plays them from a recording through its
# own uinput device: SDL never saw events written to a device's event node, smoke_keys' way (checked
# on the PC, 2026-10-29). It runs as "evemu-device-pad" (a link to evemu-play, argv[0] evemu-play)
# so ps3-intfwatch takes the device for the bench's own, as it does evemu-device's.
# SMOKE_PAD=generic: the same pad under a made-up name and ID (1209:0001), which SDL maps from its evdev
# codes, so a recipe can hide a real DS3 that is connected (ENV+=(SDL_GAMECONTROLLER_IGNORE_DEVICES=
# 0x054c/0x0268): a game that takes the first pad then gets the virtual one.
smoke_pad() {
	local e rec=${BENCH_DIR:-$D}/.smoke-pad
	{ if [ "${SMOKE_PAD:-ds3}" = generic ]; then
		sed 's/^N: .*/N: Walnut virtual pad/; s/^I: .*/I: 0003 1209 0001 0001/' "$BENCH_LIB/ds3.evemu"
	  else cat "$BENCH_LIB/ds3.evemu"; fi
	  printf '%s\n' "$@" | awk -v end=$((DURATION + 60)) -v generic=$([ "${SMOKE_PAD:-ds3}" = generic ] && echo 1 || echo 0) '
		BEGIN { split("cross 304 circle 305 triangle 307 square 308 l1 310 r1 311 l2 312 r2 313 select 314 start 315 ps 316 l3 317 r3 318 up 544 down 545 left 546 right 547", a, " ")	# BTN_SOUTH.. BTN_THUMBR, BTN_DPAD_*: decimal (mawk has no strtonum)
			for (i = 1; i in a; i += 2) b[a[i]] = a[i + 1]
			if (generic) { b["triangle"] = 308; b["square"] = 307 }	# SDL maps an unknown pad'"'"'s BTN_NORTH to X, BTN_WEST to Y
			x["lx"] = 0; x["ly"] = 1; x["rx"] = 3; x["ry"] = 4; t = 0
			for (k = 0; k < 2; k++) { for (i = 0; i < 5; i++) if (i != 2) printf "E: %.6f 0003 %04x 0128\n", k / 10, i
				printf "E: %.6f 0000 0000 0000\n", k / 10 } }	# both sticks centred (the device starts them at 0: full up and left), twice: two anchors, as evemu-play skips the wait before its second event
		function ev(s, ty, c, v) { printf "E: %.6f %04x %04x %04d\nE: %.6f 0000 0000 0000\n", s, ty, c, v, s }
		/^\+/ { t += substr($0, 2); next }
		/=/ { split($0, kv, "="); if (!(kv[1] in x)) { print "smoke_pad: no stick " kv[1] > "/dev/stderr"; next }
		      ev(t, 3, x[kv[1]], kv[2]); next }
		{ if (!($0 in b)) { print "smoke_pad: no button " $0 > "/dev/stderr"; next }
		  tr = ($0 == "l2") ? 2 : ($0 == "r2") ? 5 : -1	# the triggers are axes too (ABS_Z, ABS_RZ): SDL reads those
		  ev(t, 1, b[$0], 1); if (tr >= 0) ev(t, 3, tr, 255)
		  t += 0.3; ev(t, 1, b[$0], 0); if (tr >= 0) ev(t, 3, tr, 0) }
		END { printf "E: %.6f 0000 0000 0000\n", (end > t + 1 ? end : t + 1) }'
	} > "$rec"
	mkdir -p "$rec.bin" && ln -sf "$(command -v evemu-play)" "$rec.bin/evemu-device-pad"
	sudo -n timeout $((DURATION + 30)) bash -c 'exec -a evemu-play "$0" "$1" < /dev/null' \
		"$rec.bin/evemu-device-pad" "$rec" > "$rec.log" 2>&1 &
	# ready: its event node listed (a virtual device: Sysfs /devices/virtual/), readable, and udev done
	# with it (on the PS3 udev takes a while; a game that looks before that sees no pad). The log line
	# naming the node comes late: evemu-play's stdout is buffered.
	local node= i
	for i in $(seq 100); do
		node=$(awk -v name="$([ "${SMOKE_PAD:-ds3}" = generic ] && echo 'Walnut virtual pad' || echo 'Sony PLAYSTATION(R)3 Controller')" '
			/^N: Name=/ { n = substr($0, 10); gsub(/"/, "", n) } /^S: Sysfs=/ { v = ($0 ~ /\/devices\/virtual\//) }
			/^H: Handlers=/ && n == name && v { sub(/^H: Handlers=/, ""); for (i = 1; i <= NF; i++) if ($i ~ /^event/) print $i }' /proc/bus/input/devices | tail -1)
		[ -n "$node" ] && [ -r "/dev/input/$node" ] && break
		sleep 0.1
	done
	udevadm settle -t 10 2>/dev/null || sleep 2
	echo "virtual pad /dev/input/${node:-?} ready at $EPOCHREALTIME (its events count from the call)"
}
