#!/bin/sh
# menu-check.sh [DIR]: the Games menu as jgmenu would list it, on this host, with no PS3.
# Builds an applications directory from the package's own entries, ps3-games-integration's (the
# games' launchers, from gamedb: tools/gamedb/pack-integration.py --tree) and stubs of the Debian games'
# entries (those its divert list names are moved away, as dpkg does), runs ps3-game-menu over it,
# then lists what jgmenu shows: every file whose name contains ".desktop" (the worst case: that is
# what showed 0.307's openarena.desktop.ps3-orig as a second OpenArena) in the runtime directory
# then the system one, one per file name, without NoDisplay/Hidden/OnlyShowIn. Fails on a repeated
# Name or a game that is not through ps3-game.
here=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$here/../../../.." && pwd)
W=${1:-$(mktemp -d)}
rm -rf "$W/usr" "$W/run" "$W/integration" "$W/diverted"; mkdir -p "$W/usr/share/applications" "$W/run"
cp "$here"/files/applications/*.desktop "$W/usr/share/applications/"
python3 -I "$repo/tools/gamedb/pack-integration.py" --out "$W/integration.tar" --licensed-out '' \
	--tree "$W/integration" >/dev/null || exit 1
cp "$W"/integration/tree/usr/share/applications/*.desktop "$W/usr/share/applications/"
stub() {	# file, name, exec, icon, categories
	printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nIcon=%s\nCategories=%s\n' "$2" "$3" "$4" "$5" > "$W/usr/share/applications/$1"
}
# Debian's (and our +ps3 packages') entries as they ship; a diverted one lands outside the menu's reach
for f in $(grep '^/usr/share/applications/.*\.desktop$' "$W/integration/tree/usr/share/ps3-games/divert.list"); do
	g=$(basename "$f" .desktop)
	div="$W/diverted/$g.desktop"; mkdir -p "$W/diverted"
	case $g in
	openarena) n=OpenArena e=/usr/games/openarena i=openarena128 ;;
	dsda-doom) n=DSDA-Doom e='dsda-doom %F' i=dsda-doom ;;
	openttd) n=OpenTTD e=openttd i=openttd ;;
	quakespasm) n=QuakeSpasm e=quakespasm i=quakespasm ;;
	*) n=$g e=$g i=$g ;;
	esac
	printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nIcon=%s\nCategories=Game;ActionGame;\n' "$n" "$e" "$i" > "$div"
done
# the old 0.307 leftover, to show it would have been the culprit
[ -z "$OLD" ] || { cp "$W/diverted/openarena.desktop" "$W/usr/share/applications/openarena.desktop.ps3-orig"; }
# games without modes: entries untouched by the package
stub neverball.desktop Neverball neverball neverball 'Game;ArcadeGame;'
stub xmoto.desktop X-Moto xmoto xmoto 'Game;ArcadeGame;'
stub classicube.desktop ClassiCube classicube classicube 'Game;ActionGame;'
stub pcmanfm.desktop PCManFM pcmanfm system-file-manager 'Utility;'
[ -n "$NODIVERT" ] && for f in "$W"/diverted/*; do cp "$f" "$W/usr/share/applications/"; done
PS3_APPLICATIONS="$W/usr/share/applications" sh "$here/files/libexec/ps3-game-menu" "$W/run" || exit 1
python3 -I - "$W" <<'PY'
import os, sys, collections
w = sys.argv[1]
dirs = [w + "/run/applications", w + "/usr/share/applications"]
seen = {}
for d in dirs:
    for f in sorted(os.listdir(d)):
        if ".desktop" in f and f not in seen:
            seen[f] = d
def val(path, key):
    for l in open(path):
        if l.startswith(key + "="):
            return l.split("=", 1)[1].strip()
rows = []
for f, d in sorted(seen.items()):
    p = d + "/" + f
    if val(p, "NoDisplay") == "true" or val(p, "Hidden") == "true" or val(p, "OnlyShowIn"):
        continue
    cats = (val(p, "Categories") or "").split(";")
    if "Game" in cats:
        rows.append((val(p, "Name"), val(p, "Exec"), val(p, "Icon"), f, "run" if d == dirs[0] else "pkg"))
bad = 0
names = collections.Counter(r[0] for r in rows)
print("Games menu:")
for r in sorted(rows):
    flag = ""
    if names[r[0]] > 1: flag = "  DUPLICATE NAME"; bad = 1
    if "ps3-game" not in r[1]: flag += "  NOT IN GAME MODE"; bad = 1
    print("  %-24s %-26s %-3s %s%s" % (r[0], r[3], r[4], r[1][:60], flag))
sys.exit(bad)
PY
rc=$?; [ "$1" ] || rm -rf "$W"; exit $rc
