#!/bin/bash
# desktop-cases.sh mpv|xloop LABEL: the A/B's two desktop cases on the PS3 (run as tibi), each through
# `ps3-bench.sh x` (desktop stopped, swaps emptied, samples, the interference detector, a bare X on
# vt1 as the only client), so they are measured like the games and bench-cam.sh sees them.
#   mpv     playback of CLIP (default the 480p Big Buck Bunny of the browser tests) for SECS seconds
#           (default 150: the harness's 60 s warm-up and 90 s more) with --vo=gpu; the dropped and
#           delayed frame counts at the end: "rsxab: mpv drops=N delayed=N" in out.log
#   xloop   xloop.py's resize, pixmap and copy loop (STEPS, default 600): "rsxab: xloop ..." lines
# LABEL names the run directory (rsxab-<case>-<LABEL>); abba-report.py reads the rsxab: lines.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
case=${1:?usage: desktop-cases.sh mpv|xloop LABEL}
label=${2:?usage: desktop-cases.sh mpv|xloop LABEL}
CLIP=${CLIP:-/var/tmp/browser-test/bbb-480p.mp4}
SECS=${SECS:-150}
STEPS=${STEPS:-600}
case $case in
mpv)
	[ -r "$CLIP" ] || { echo "desktop-cases: no $CLIP" >&2; exit 1; }
	# mpv's own counters at exit, printed by a one-line Lua script (no config, no OSD)
	lua=$(mktemp /dev/shm/rsxab-mpv.XXXXXX.lua)
	trap 'rm -f "$lua"' EXIT
	cat > "$lua" <<'EOF'
mp.register_event("shutdown", function()
	print(string.format("rsxab: mpv drops=%s delayed=%s vo_drops=%s",
		mp.get_property("frame-drop-count", "-"), mp.get_property("vo-delayed-frame-count", "-"),
		mp.get_property("vo-drop-frame-count", "-")))
end)
EOF
	BENCH_TIMEOUT=$((SECS + 60)) ps3-bench.sh x "rsxab-mpv-$label" -- \
		mpv --no-config --fs --vo=gpu --hwdec=no --ao=null --length="$SECS" \
		--msg-level=all=error --script="$lua" "$CLIP"
	;;
xloop)
	BENCH_TIMEOUT=600 ps3-bench.sh x "rsxab-xloop-$label" -- python3 -I "$here/xloop.py" "$STEPS"
	;;
*)
	echo "desktop-cases: no case $case" >&2
	exit 2
	;;
esac
