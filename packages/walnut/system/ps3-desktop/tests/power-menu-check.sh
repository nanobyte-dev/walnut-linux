#!/bin/sh
# power-menu-check.sh: Reboot and Shut down sit at jgmenu's top level (files/xdg/jgmenu/append.csv), not in
# System (their .desktop entries are NoDisplay), and ps3-session's login step writes our append.csv into a
# home without one or with our older copy, but leaves a copy the user took over alone. The step is run as
# ps3-session has it, with /etc/xdg/ps3 pointed at the package's files.
here=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d); trap 'rm -rf "${W:?}"' EXIT
fail=0
ok() { echo "ok: $*"; }
bad() { echo "FAIL: $*"; fail=1; }
A=$here/files/xdg/jgmenu/append.csv
head -n 1 "$A" | grep -q '^# walnut:' && ok "append.csv starts with the marker" || bad "append.csv's first line isn't '# walnut:'"
for e in "Reboot,ps3-power reboot,system-reboot" "Shut down,ps3-power poweroff,system-shutdown"; do
	grep -qxF "$e" "$A" && ok "top-level entry: $e" || bad "no top-level entry: $e"
done
grep -qx '\^sep()' "$A" && ok "a separator before them" || bad "no separator"
for f in ps3-reboot ps3-poweroff; do
	grep -qx 'NoDisplay=true' "$here/files/applications/$f.desktop" && ok "$f.desktop hidden from System" || bad "$f.desktop still in System"
done
# the login step, cut from ps3-session (from its jgmenu path to the fi)
sed -n '/^c=.*jgmenu\/append.csv$/,/^fi$/p' "$here/files/bin/ps3-session" | sed "s|/etc/xdg/ps3/|$here/files/xdg/|g" > "$W/step.sh"
grep -q install "$W/step.sh" || { bad "the login step isn't in ps3-session"; exit 1; }
run() { HOME=$W/home XDG_CONFIG_HOME= sh "$W/step.sh"; }
C=$W/home/.config/jgmenu/append.csv
run; cmp -s "$A" "$C" && ok "a new home gets it" || bad "a new home didn't get it"
printf '# walnut: old\nOld,true,x\n' > "$C"; run; cmp -s "$A" "$C" && ok "our older copy is replaced" || bad "our older copy stayed"
printf 'Mine,true,x\n' > "$C"; run; [ "$(cat "$C")" = "Mine,true,x" ] && ok "the user's own copy is kept" || bad "the user's own copy was overwritten"
exit $fail
