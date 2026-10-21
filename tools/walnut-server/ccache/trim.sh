#!/bin/sh
# trim.sh DIR MAX_GIB: deletes the least recently used files of the ccache store until it is under
# 80% of MAX_GIB (run daily by walnut-ccache-trim.timer). Access times: relatime updates them about
# once a day, enough for a daily trim.
set -eu
dir=$1 max=$(( $2 * 1024 * 1024 ))	# KiB
used=$(du -sk "$dir" | cut -f1)
[ "$used" -gt "$max" ] || exit 0
target=$(( max * 8 / 10 ))
find "$dir" -path "$dir/.incoming" -prune -o -type f ! -name CACHEDIR.TAG -printf '%A@ %k %p\n' | sort -n | while read -r _ kib path; do
	[ "$used" -gt "$target" ] || break
	rm -f "$path"
	used=$(( used - kib ))
done
find "$dir" -mindepth 1 -type d -empty ! -path "$dir/.incoming" -delete
echo "ccache store trimmed to $(du -sh "$dir" | cut -f1)"
