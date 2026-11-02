#!/bin/bash
# ps3-system's render node rule (debian/73-ps3-render-node.rules): its content, that it sorts after
# udev's 50-udev-default.rules (which makes render nodes 0666) so its mode wins, that debian/rules
# installs it, that both ways an account is made put the desktop user in render, and, where this
# udevadm has it, that udev parses it.
set -u
cd "$(dirname "$0")/.." || exit 2
R=debian/73-ps3-render-node.rules
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
rule=$(grep -v '^#' "$R" | grep -v '^$')
check "one rule line" '[ "$(wc -l <<<"$rule")" = 1 ]'
check "matches the ps3rsx render node" 'grep -q "SUBSYSTEM==\"drm\"" <<<"$rule" && grep -q "KERNEL==\"renderD\*\"" <<<"$rule" && grep -q "DRIVERS==\"ps3rsx\"" <<<"$rule"'
check "group render, mode 0660, both final (:=)" 'grep -q "GROUP:=\"render\"" <<<"$rule" && grep -q "MODE:=\"0660\"" <<<"$rule"'
check "nothing wider (one mode, no world bits)" '[ "$(grep -o "MODE" <<<"$rule" | wc -l)" = 1 ] && ! grep -q "MODE:\?=\"0[0-7][0-7][1-7]\"" <<<"$rule"'
check "sorts after 50-udev-default.rules" '[ "$(printf "%s\n" 50-udev-default.rules "$(basename "$R")" | LC_ALL=C sort | tail -1)" = "$(basename "$R")" ]'
check "installed by debian/rules into /usr/lib/udev/rules.d" 'grep -q "install -D -m 644 $R \$(P)/usr/lib/udev/rules.d/$(basename "$R")" debian/rules'
check "walnut-firstboot's account is in render" 'grep -q "useradd .*-G [a-z,]*render" files/sbin/walnut-firstboot'
check "the image's account is in render" 'grep -q "useradd .*-G [a-z,]*render" ../../../../system/image/customize.sh'
if udevadm verify --help >/dev/null 2>&1; then
	check "udevadm verify" 'udevadm verify --no-style "$R" >/dev/null 2>&1 || udevadm verify "$R" >/dev/null 2>&1'
else
	echo "SKIP render-node:verify no udevadm verify here"
fi
[ $fails -eq 0 ] && echo "render-node: all passed"
exit $((fails > 0))
