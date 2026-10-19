#!/bin/bash
# bench-audio.sh [SECONDS] [HOURS]: alongside tools/bench-cam.sh, records SECONDS (default 8) of the
# room through the webcam microphone after every new photo in build/bench-cam/, as
# <photo name>.wav, and appends webcam-audio.sh's analysis to build/bench-cam/audio.log. Only
# meaningful while the room is quiet (nobody home, no podcast): announce it, see
# docs/STALLS.md / memory "sound checks by the user". Runs for HOURS (default 10).
set -u
cd "$(dirname "$0")/.."
secs=${1:-8} end=$(( $(date +%s) + ${2:-10} * 3600 ))
seen=$(mktemp); trap 'rm -f "$seen"' EXIT
ls build/bench-cam/*.jpg 2>/dev/null | sort > "$seen"
while [ "$(date +%s)" -lt "$end" ]; do
	ls build/bench-cam/*.jpg 2>/dev/null | sort | comm -13 "$seen" - | while read -r j; do
		w=${j%.jpg}.wav
		{ echo "== $(basename "$j") $(date +%H:%M:%S)"; tools/webcam-audio.sh "$secs" "$w" 2>&1 | tail -6; } >> build/bench-cam/audio.log
		echo "$j" >> "$seen"; sort -o "$seen" "$seen"
	done
	sleep 5
done
