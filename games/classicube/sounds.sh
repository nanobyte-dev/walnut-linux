#!/bin/bash
# ClassiCube's sounds and music, as its launcher would fetch them on first start: the lists
# (name, SHA-1) come from the pinned source's Resources.c, the files from Mojang's resource
# server (addressed by SHA-1, checked). Sounds become 16-bit little-endian WAVs in
# audio/default2.zip (the name the big-endian build loads); music stays .ogg in audio/.
# Inputs (env): CC_TARBALL SOURCE_DATE_EPOCH  Cache: build/downloads/classicube-assets
# Output: build/classicube/audio
set -euo pipefail

CACHE=build/downloads/classicube-assets
OUT=build/classicube/audio
TMP=build/classicube/audio.tmp
rm -rf "$TMP" && mkdir -p "$CACHE" "$TMP/wav"

src=$(tar -xzOf "$CC_TARBALL" --wildcards '*/src/Resources.c')
sounds=$(sed -n '/soundAssets\[\] = {/,/^};/p' <<<"$src" | grep -o '"[a-z_0-9]*\.wav", *"[0-9a-f]\{40\}"' | tr -d '", ' | tr ':' ' ' | sed 's/\.wav/.wav /')
music=$(sed -n '/musicAssets\[\] = {/,/^};/p' <<<"$src" | grep -o '"[a-z0-9]*\.ogg", *"[0-9a-f]\{40\}"' | tr -d '", ' | sed 's/\.ogg/.ogg /')
[ "$(wc -l <<<"$sounds")" -gt 50 ] && [ "$(wc -l <<<"$music")" -ge 7 ] || { echo "!! asset lists not found"; exit 1; }

fetch() {	# hash -> cached file, verified
	local h=$1 f="$CACHE/$1"
	if [ ! -s "$f" ]; then
		curl -fsSL -o "$f.tmp" "https://resources.download.minecraft.net/${h:0:2}/$h"
		mv "$f.tmp" "$f"
	fi
	echo "$h  $f" | sha1sum -c --quiet
	echo "$f"
}

while read -r name h; do
	oggdec -Q -b 16 -e 0 -s 1 -o "$TMP/wav/$name" "$(fetch "$h")"
done <<<"$sounds"
while read -r name h; do
	cp "$(fetch "$h")" "$TMP/$name"
done <<<"$music"

# deterministic zip: sorted names, fixed timestamps
python3 - "$TMP/wav" "$TMP/default2.zip" <<'PY'
import os, sys, time, zipfile
src, dst = sys.argv[1], sys.argv[2]
ts = time.gmtime(int(os.environ.get('SOURCE_DATE_EPOCH', '315532800')))[:6]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
    for n in sorted(os.listdir(src)):
        zi = zipfile.ZipInfo(n, max(ts, (1980, 1, 1, 0, 0, 0)))
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, open(os.path.join(src, n), 'rb').read())
PY
rm -rf "$TMP/wav" "$OUT"
mv "$TMP" "$OUT"
echo ">> ClassiCube audio: $(ls "$OUT" | wc -l) files, $(du -sh "$OUT" | cut -f1)"
