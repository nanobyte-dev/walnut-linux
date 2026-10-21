#!/bin/bash
# public-export.sh [DIR]: the public GitHub copy of this repository (docs/release/ALPHA.md M8). A fresh
# clone of main into DIR (default /mnt/data_linux/Temp/ps3/walnut-public, or $WALNUT_PUBLIC_DIR), its
# commit messages without the AI attribution trailers (exact lines only: "Co-Authored-By: Claude...",
# "Claude-Session: ...", "Generated with [Claude Code]..."), then a scan of the whole history that
# stops on anything that must never be public (tools/public-strip-blobs.txt lists blobs left out, e.g. a
# third-party image whose licence forbids redistribution; tools/public-replace.local.txt, untracked, text
# replaced in every file of every commit, e.g. the console's NOR dump hash). It checks for backups/ or firmware/ paths,
# NOR/NAND dumps and disk images, licensed game data paths, private keys. The rewrite is deterministic: running it again on a
# longer main gives the same commit IDs for the old commits, so the public repository fast-forwards.
# Nothing is pushed: the push to github.com/nanobyte-dev is the user's step (printed at the end).
# Needs git-filter-repo (pip install --user git-filter-repo).
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=$PWD
DIR=${1:-${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public}}
command -v git-filter-repo >/dev/null || { echo "!! git-filter-repo is missing: pip install --user git-filter-repo" >&2; exit 2; }
# text to replace in every file of every commit: an untracked local file (it names what it hides, so
# it can't be published itself), one filter-repo expression per line (regex:...==>...)
REPL=${WALNUT_PUBLIC_REPLACE:-$SRC/tools/public-replace.local.txt}
[ -r "$REPL" ] || { echo "!! no $REPL (the private replace list)" >&2; exit 2; }
rm -rf "$DIR"
git clone -q --no-local --single-branch --branch main "$SRC" "$DIR"
cd "$DIR"
git remote remove origin
grep -Eo '^[0-9a-f]{40}' "$SRC/tools/public-strip-blobs.txt" > "$DIR/.git/strip-blobs" || true
git filter-repo --quiet --force --strip-blobs-with-ids "$DIR/.git/strip-blobs" \
	--replace-text "$REPL" --message-callback '
import re
lines = message.split(b"\n")
drop = re.compile(rb"^\s*(Co-Authored-By:\s*Claude.*|Claude-Session:.*|.*Generated with \[Claude Code\].*)$", re.I)
kept = [l for l in lines if not drop.match(l)]
out = b"\n".join(kept)
while out.endswith(b"\n\n"):
    out = out[:-1]
return out
'
bad=0
left=$(git log --all --format=%B | grep -ciE '^\s*(co-authored-by:\s*claude|claude-session:)|generated with \[claude code\]' || true)
[ "$left" = 0 ] || { echo "!! $left attribution lines left in commit messages"; bad=1; }
paths=$(git log --all --name-only --format= | sort -u)
hits=$(printf '%s\n' "$paths" | grep -E '^(backups|firmware)/|\.(bin|img|pup|PUP|nor|nand)$|(^|/)(sauerbraten-nonfree-data|classicube-data|quake-data|rct2-data|tyrian-data)/.*\.(pk3|ogz|cfg|dat|zip|ogg|wav|png|jpg)$' || true)
[ -z "$hits" ] || { echo "!! private or licensed paths in the history:"; printf '   %s\n' $hits | head -40; bad=1; }
for b in $(cat .git/strip-blobs); do
	git cat-file -e "$b" 2>/dev/null && { echo "!! stripped blob $b is still in the history"; bad=1; }
done
while IFS= read -r e; do	# every replaced pattern must be gone
	case $e in regex:*) pat=${e#regex:}; pat=${pat%%==>*} ;; *) pat=$(printf '%s' "${e%%==>*}" | sed 's/[][\\.*^$]/\\&/g') ;; esac
	[ -z "$pat" ] || [ -z "$(git log --all -p -G "$pat" --format=%h | head -1)" ] || { echo "!! a replaced text is still in the history"; bad=1; }
done < "$REPL"
keys=$(git log --all -p -G '^[+-]?-----BEGIN [A-Z ]*PRIVATE KEY-----' --format=%h | head -5 || true)
[ -z "$keys" ] || { echo "!! private key text in commits: $keys"; bad=1; }
n=$(git rev-list --count main)
echo ">> $DIR: $n commits, $(git rev-parse --short main), attribution trailers removed"
if [ $bad = 1 ]; then echo "!! not ready to publish (see above)"; exit 1; fi
echo ">> history scan clean. To publish (the user's step):"
echo "   git -C $DIR remote add origin git@github.com:nanobyte-dev/walnut-linux.git && git -C $DIR push -u origin main"
