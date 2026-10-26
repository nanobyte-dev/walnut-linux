#!/bin/bash
# image-packages.sh dev|public: our packages the root filesystem image installs (tools/build-image.sh),
# one per line, from system/image/packages-ours.txt. The public profile (release images) leaves out the
# debug kernel and its ps3rsx modules, every licensed package (tools/licensed.py names; it fails
# closed, and so does this) and the packages that depend on licensed data (NEEDS_LICENSED below), and
# installs ps3-games-free in place of ps3-games.
set -euo pipefail
cd "$(dirname "$0")/.."
# free packages whose Depends name licensed data: classicube (classicube-data). A game fact, kept here
# until the game's gamedb build.toml says it (the GameDB plan, Phase 3)
NEEDS_LICENSED="classicube"
profile=${1:-}
list=$(grep -v -e '^\s*#' -e '^\s*$' system/image/packages-ours.txt)
case $profile in
dev) printf '%s\n' "$list" ;;
public)
	lic=$(python3 -I tools/licensed.py names) && [ -n "$lic" ] ||
		{ echo "!! tools/licensed.py names failed" >&2; exit 2; }
	drop=$(printf '%s\n' linux-image-ps3-debug ps3rsx-modules-ps3-debug $lic $NEEDS_LICENSED)
	printf '%s\n' "$list" | grep -vxF -f <(printf '%s\n' "$drop") | sed 's/^ps3-games$/ps3-games-free/' ;;
*) echo "usage: $0 dev|public" >&2; exit 2 ;;
esac
