#!/bin/bash
# mesa-compare.sh BINDIR [CORPUS]: our DXT1 and DXT5 encoders (speq-dxt) against Mesa's
# (mesa-dxt), both measured against the originals: the mean RMSE per set and format (DXT5: R, G,
# B and A over all pixels; and the PSNR of the summed squared error), how many images each wins,
# the worst image for us (relative to Mesa), and both encoders' speed on one texture.
# BINDIR holds speq-dxt and mesa-dxt (make test-mesa); CORPUS defaults to build/tmp/speq-corpus
# and is made by make-corpus.py when missing: from the OpenArena debs in build/apt-cache, plus the
# PNGs with alpha of the game source trees in build/work that are there (SuperTuxKart, Neverball,
# Extreme Tux Racer: make <game>-work).
set -euo pipefail
{
BIN=$1
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../../../.." && pwd)
CORPUS=${2:-$TOP/build/tmp/speq-corpus}
if [ ! -d "$CORPUS/oa-a5" ]; then
	mkdir -p "$CORPUS"
	png=()
	for d in supertuxkart/data neverball etr; do
		[ -d "$TOP/build/work/$d" ] && png+=(--png "$TOP/build/work/$d")
	done
	python3 -I "$HERE/make-corpus.py" "$CORPUS" "$TOP"/build/apt-cache/openarena-081-textures_*.deb \
		"$TOP"/build/apt-cache/openarena-088-data_*.deb "${png[@]}"
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

compare() {	# set fmt: one line per image, "ours mesa name pixels"
	local set=$1 fmt=$2 img
	for img in "$CORPUS/$set"/*.pam; do
		[ -e "$img" ] || continue
		"$BIN/mesa-dxt" "$fmt" "$img" "$TMP/mesa.blocks"
		"$BIN/speq-dxt" --fmt "$fmt" --compare "$TMP/mesa.blocks" "$img" |
			awk -v n="$(basename "$img" .pam)" \
			    -v px="$(head -c 100 "$img" | awk '/^WIDTH/ { w = $2 } /^HEIGHT/ { h = $2 } END { print w * h }')" \
			    '/^ours/ { o = $(NF - (/psnr/ ? 2 : 0)) } /^other/ { m = $(NF - (/psnr/ ? 2 : 0)) }
				END { print o, m, n, px }'
	done
}

# per set: the mean of the images' RMSE, wins, the worst ratio; the PSNR of the set's pixels
printf '%-9s %-6s %4s %10s %10s %6s %6s %9s %9s  %s\n' set fmt n ours mesa "ours<" "mesa<" \
	"psnr ours" "psnr mesa" "worst for us (ours/mesa)"
for spec in oa:dxt1 oa:dxt1a oa-alpha:dxt1a synth:dxt1 oa-a5:dxt5 png-a5:dxt5 synth-a5:dxt5 \
	    oa-alpha:dxt5; do
	set=${spec%:*} fmt=${spec#*:}
	compare "$set" "$fmt" > "$TMP/$set-$fmt"
	[ -s "$TMP/$set-$fmt" ] || { echo "$set: no images"; continue; }
	awk -v s="$set" -v f="$fmt" '{ n++; o += $1; m += $2; if ($1 < $2) w++; if ($2 < $1) l++
			so += $1 * $1 * $4; sm += $2 * $2 * $4; np += $4
			r = $2 > 0 ? $1 / $2 : 1; if (r > wr) { wr = r; wn = $3 " " $1 "/" $2 } }
		END { po = so > 0 ? 20 * log(255 / sqrt(so / np)) / log(10) : 99
		      pm = sm > 0 ? 20 * log(255 / sqrt(sm / np)) / log(10) : 99
		      printf "%-9s %-6s %4d %10.3f %10.3f %6d %6d %9.2f %9.2f  %s (%.2f)\n", s, f, n, o / n,
			     m / n, w, l, po, pm, wn, wr }' "$TMP/$set-$fmt"
done
echo "synthetic, per image (ours mesa):"
sed 's/^/  /' "$TMP/synth-dxt1"

# speed on the largest opaque texture
big=$(for f in "$CORPUS"/oa/*.pam; do echo "$(stat -c %s "$f") $f"; done | sort -n | tail -1 | cut -d' ' -f2-)
echo "speed on $(basename "$big" .pam):"
"$BIN/speq-dxt" --fmt dxt1a --bench 50 "$big" | grep bench
"$BIN/mesa-dxt" dxt1a "$big" "$TMP/mesa.blocks" 10
echo "DXT5, the same:"
"$BIN/speq-dxt" --fmt dxt5 --bench 20 "$big" | grep bench
"$BIN/mesa-dxt" dxt5 "$big" "$TMP/mesa.blocks" 5
exit
}
