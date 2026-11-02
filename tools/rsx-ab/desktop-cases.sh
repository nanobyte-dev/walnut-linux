#!/bin/bash
# desktop-cases.sh mpv|xloop LABEL: the A/B's two desktop cases on the PS3 (run as tibi), each through
# `ps3-bench.sh x` (desktop stopped, swaps emptied, samples, the interference detector, a bare X on
# vt1 as the only client), so they are measured like the games and bench-cam.sh sees them.
#   mpv     playback of CLIP (default the 480p Big Buck Bunny of the browser tests, about 60 s) looped
#           for SECS seconds (default 150) with --vo=gpu; the dropped and delayed frames summed over
#           the loops: "rsxab: mpv drops=N delayed=N" (sampled every 0.5 s, since the counters start
#           again at each loop and are gone at shutdown; written to stderr, which --msg-level doesn't
#           hide)
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
	# mpv's own counters, summed by a small Lua script (no config, no OSD)
	lua=$(mktemp /dev/shm/rsxab-mpv.XXXXXX.lua)
	trap 'rm -f "$lua"' EXIT
	cat > "$lua" <<'EOF'
local keys = { "frame-drop-count", "vo-delayed-frame-count" }
local last, total = { 0, 0 }, { 0, 0 }
local function sample()
	for i, k in ipairs(keys) do
		local v = mp.get_property_number(k, 0)
		if v < last[i] then total[i] = total[i] + last[i] end	-- a new loop started from 0
		last[i] = v
	end
end
mp.add_periodic_timer(0.5, sample)
-- the time limit: --end is a position in the file, which a loop starts again
mp.add_timeout(tonumber(mp.get_opt("rsxab-secs") or "150"), function() mp.command("quit") end)
mp.register_event("shutdown", function()
	io.stderr:write(string.format("rsxab: mpv drops=%d delayed=%d\n", total[1] + last[1],
		total[2] + last[2]))
end)
EOF
	BENCH_TIMEOUT=$((SECS + 60)) ps3-bench.sh x "rsxab-mpv-$label" -- \
		mpv --no-config --fs --vo=gpu --hwdec=no --ao=null --loop-file=inf --script-opts=rsxab-secs="$SECS" \
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
