#!/bin/bash
# corpus-replay.sh O CORPUS_DIR: every captured case under CORPUS_DIR (<case>/dump.bin.zst, from the
# push dump build) split by pushdump-split.py and each channel's pushes replayed through this tree's
# validator (validate-capture, with the dump's buffer events as the set): exits 1 if any push is
# refused. The corpus is game-derived and stays private (never in the tree); nothing to replay
# without it.
set -euo pipefail
O=$1 C=${2:-}
here=$(cd "$(dirname "$0")" && pwd)
[ -n "$C" ] && [ -d "$C" ] || { echo "corpus-replay: no corpus given, nothing to replay"; exit 0; }
bad=0
for d in "$C"/*/; do
	[ -e "$d/dump.bin.zst" ] || continue
	case=$(basename "$d") w=$O/corpus/$case
	rm -rf "$w"; mkdir -p "$w"
	zstd -dqf "$d/dump.bin.zst" -o "$w/dump.bin"
	python3 -I "$here/pushdump-split.py" "$w/dump.bin" "$w/split"
	rm -f "$w/dump.bin"
	for f in "$w"/split/chan*.push; do
		if ! "$O/validate-capture" "$f" > "$f.log"; then
			bad=1
			echo "corpus-replay: $case $(basename "$f"): $(tail -1 "$f.log")"
			grep REFUSED "$f.log" | sed 's/.*REFUSED: //' | sort | uniq -c | head -5
		fi
	done
	echo "corpus-replay: $case: $(cat "$w"/split/chan*.push.log | grep -c REFUSED || true) refused"
	rm -rf "$w"
done
exit $bad
