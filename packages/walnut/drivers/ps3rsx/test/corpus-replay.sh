#!/bin/bash
# corpus-replay.sh O CORPUS_DIR: every captured case under CORPUS_DIR (<case>/dump.bin.zst, from the
# push dump build) split by pushdump-split.py and each channel's pushes replayed through this tree's
# validator (validate-capture, with the dump's buffer events as the set): exits 1 if any push is
# refused. Pushes whose submit a gap in the dump took are skipped (the counts follow each case);
# a case with any skipped push or a submit missing one isn't fully replayed: "!!" and exit 1 too.
# The corpus is game-derived and stays private (never in the tree); nothing to replay without it.
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
	rc=0
	python3 -I "$here/pushdump-split.py" "$w/dump.bin" "$w/split" || rc=$?
	rm -f "$w/dump.bin"
	if [ $rc = 3 ]; then
		bad=1
		echo "!! corpus-replay: $case: not fully replayed ($(grep -h '^pushes without' "$w/split/summary.txt"))"
	elif [ $rc != 0 ]; then
		echo "corpus-replay: $case: the splitter failed (exit $rc)"; exit 1
	fi
	for f in "$w"/split/chan*.push; do
		if ! "$O/validate-capture" "$f" > "$f.log"; then
			bad=1
			echo "corpus-replay: $case $(basename "$f"): $(tail -1 "$f.log")"
			grep REFUSED "$f.log" | sed 's/.*REFUSED: //' | sort | uniq -c | head -5
		fi
	done
	echo "corpus-replay: $case: $(cat "$w"/split/chan*.push.log | grep -c REFUSED || true) refused" \
		"($(grep -h '^gaps\|^pushes without' "$w/split/summary.txt" | paste -sd ';' - | sed 's/;/; /'))"
	rm -rf "$w"
done
exit $bad
