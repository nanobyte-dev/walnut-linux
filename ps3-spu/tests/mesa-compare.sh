#!/bin/bash
# mesa-compare.sh BINDIR [CORPUS]: our DXT1 encoder (speq-dxt) against Mesa's (mesa-dxt), both
# measured against the originals: the mean RMSE per set and format, how many images each wins,
# the worst image for us (relative to Mesa), and both encoders' speed on one texture.
# BINDIR holds speq-dxt and mesa-dxt (make test-mesa); CORPUS defaults to build/tmp/speq-corpus
# and is made by make-corpus.py from the OpenArena debs in build/apt-cache when missing.
set -euo pipefail
{
BIN=$1
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../.." && pwd)
CORPUS=${2:-$TOP/build/tmp/speq-corpus}
if [ ! -d "$CORPUS/oa" ]; then
	mkdir -p "$CORPUS"
	python3 -I "$HERE/make-corpus.py" "$CORPUS" "$TOP"/build/apt-cache/openarena-081-textures_*.deb \
		"$TOP"/build/apt-cache/openarena-088-data_*.deb
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

compare() {	# set fmt: one line per image, "ours mesa name"
	local set=$1 fmt=$2 img
	for img in "$CORPUS/$set"/*.pam; do
		"$BIN/mesa-dxt" "$fmt" "$img" "$TMP/mesa.blocks"
		"$BIN/speq-dxt" --fmt "$fmt" --compare "$TMP/mesa.blocks" "$img" |
			awk -v n="$(basename "$img" .pam)" '/^ours/ { o = $NF } /^other/ { m = $NF }
				END { print o, m, n }'
	done
}

printf '%-9s %-6s %4s %10s %10s %6s %6s  %s\n' set fmt n ours mesa "ours<" "mesa<" "worst for us (ours/mesa)"
for spec in oa:dxt1 oa:dxt1a oa-alpha:dxt1a synth:dxt1; do
	set=${spec%:*} fmt=${spec#*:}
	compare "$set" "$fmt" > "$TMP/$set-$fmt"
	awk -v s="$set" -v f="$fmt" '{ n++; o += $1; m += $2; if ($1 < $2) w++; if ($2 < $1) l++
			r = $2 > 0 ? $1 / $2 : 1; if (r > wr) { wr = r; wn = $3 " " $1 "/" $2 } }
		END { printf "%-9s %-6s %4d %10.3f %10.3f %6d %6d  %s (%.2f)\n", s, f, n, o / n, m / n, w, l, wn, wr }' \
		"$TMP/$set-$fmt"
done
echo "synthetic, per image (ours mesa):"
sed 's/^/  /' "$TMP/synth-dxt1"

# speed on the largest opaque texture
big=$(for f in "$CORPUS"/oa/*.pam; do echo "$(stat -c %s "$f") $f"; done | sort -n | tail -1 | cut -d' ' -f2-)
echo "speed on $(basename "$big" .pam):"
"$BIN/speq-dxt" --fmt dxt1a --bench 50 "$big" | grep bench
"$BIN/mesa-dxt" dxt1a "$big" "$TMP/mesa.blocks" 10
exit
}
