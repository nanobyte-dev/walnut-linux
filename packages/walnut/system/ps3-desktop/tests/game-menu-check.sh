#!/bin/sh
# game-menu-check.sh: ps3-game-menu over a fake applications directory (no PS3, no jgmenu).
# Data launchers (X-Walnut-Requires + NoDisplay) are copied without NoDisplay when every listed file
# exists and are absent otherwise; a normal game goes through ps3-game; an entry already using
# ps3-game without the key is skipped; running it again picks up a file that appeared meanwhile.
here=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
A=$W/apps; mkdir -p "$A" "$W/run"
echo x > "$W/have1"; echo x > "$W/have2"
mk() {	# file, extra lines...
	f=$1; shift
	{ printf '[Desktop Entry]\nType=Application\nName=%s\nCategories=Game;\n' "$f"; printf '%s\n' "$@"; } > "$A/$f"
}
mk ok.desktop "Exec=ps3-game dsda-doom -iwad $W/have1" "NoDisplay=true" "X-Walnut-Game=ok" "X-Walnut-Requires=$W/have1"
mk missing.desktop "Exec=ps3-game dsda-doom" "NoDisplay=true" "X-Walnut-Requires=$W/nope"
mk two.desktop "Exec=ps3-game dsda-doom" "NoDisplay=true" "X-Walnut-Requires=$W/have1;$W/nope"
mk both.desktop "Exec=ps3-game dsda-doom" "NoDisplay=true" "X-Walnut-Requires=$W/have1;$W/have2;"
mk plain.desktop "Exec=openarena %U"
mk already.desktop "Exec=ps3-game openarena"
mk term.desktop "Exec=nethack" "Terminal=true"
M() { PS3_APPLICATIONS=$A sh "$here/files/libexec/ps3-game-menu" "$@"; }
M "$W/run" || { echo "FAIL: exit status"; exit 1; }
O=$W/run/applications
fail=0
chk() { if eval "$2"; then :; else echo "FAIL: $1"; fail=1; fi; }
chk "requires present: copied" "[ -f $O/ok.desktop ]"
chk "requires present: NoDisplay dropped" "! grep -q NoDisplay $O/ok.desktop"
chk "requires present: Exec through ps3-game once" "[ \$(grep -c '^Exec=ps3-game dsda-doom' $O/ok.desktop) = 1 ] && ! grep -q 'ps3-game ps3-game' $O/ok.desktop"
chk "requires present: other keys kept" "grep -q '^X-Walnut-Game=ok' $O/ok.desktop"
chk "requires missing: absent" "[ ! -e $O/missing.desktop ]"
chk "two paths, one missing: absent" "[ ! -e $O/two.desktop ]"
chk "two paths present (trailing ;): copied" "[ -f $O/both.desktop ] && ! grep -q NoDisplay $O/both.desktop"
chk "plain game: through ps3-game" "grep -q '^Exec=ps3-game openarena %U' $O/plain.desktop"
chk "already ps3-game, no key: skipped" "[ ! -e $O/already.desktop ]"
chk "terminal: skipped" "[ ! -e $O/term.desktop ]"
chk "no leftovers" "[ ! -e $W/run/applications.new ] && [ ! -e $W/run/applications.req ]"
echo x > "$W/nope"; M "$W/run"
chk "rerun: file appeared, launcher shows" "[ -f $O/missing.desktop ] && [ -f $O/two.desktop ]"
rm -f "$W/nope"; M "$W/run"
chk "rerun: file gone, launcher hidden again" "[ ! -e $O/missing.desktop ]"
[ $fail = 0 ] && echo "game-menu-check: ok"
exit $fail
