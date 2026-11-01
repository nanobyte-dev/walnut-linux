#!/bin/bash
# Host test of ps3-gpu-crash-report's display-only restart. The test copy of the
# script has every system-changing command (loginctl, systemctl, kill, pgrep, logger) rewritten to
# an absolute path of a stub in a temporary directory, so nothing on the PC can be reached even if
# PATH were wrong; its sysfs, log, run and config paths point into that directory too. The stubs
# record every call; `sleep` advances a fake clock that the Xorg stubs read. One fake Xorg, pid 4242.
#   gpu-crash-restart-test.sh [PATH/TO/ps3-gpu-crash-report] (default: this package's files/sbin)
set -u
SCRIPT=$(realpath "${1:-$(dirname "$0")/../files/sbin/ps3-gpu-crash-report}")
fails=0
# name display_only xorg(exit second | term | never) owner_uid exe marker(0/1)
run_case() {
	local name=$1 donly=$2 xexit=$3 xuid=$4 xexe=$5 mark=$6 T
	T=$(mktemp -d)
	mkdir -p "$T/sys" "$T/log" "$T/bin" "$T/run"
	echo "$donly" > "$T/sys/display_only"; echo 1 > "$T/sys/recoveries"; echo 1 > "$T/sys/recovery_failed"
	echo record > "$T/sys/crash_report"
	echo 'PS3_DESKTOP_USER=tibi' > "$T/desktop"
	[ "$mark" = 1 ] && : > "$T/run/desktop-restarted"
	echo 0 > "$T/clock"; : > "$T/calls"; echo alive > "$T/xstate"
	sed -e "s|^SYS=.*|SYS=$T/sys|" -e "s|^DIR=.*|DIR=$T/log|" -e "s|^DESKTOP_CONF=.*|DESKTOP_CONF=$T/desktop|" \
	    -e "s|^MARK=.*|MARK=$T/run/desktop-restarted|" \
	    -e "s#\bloginctl #$T/bin/loginctl #g" -e "s#\bsystemctl #$T/bin/systemctl #g" \
	    -e "s#\bkill -#$T/bin/xkill -#g" -e "s#\bpgrep #$T/bin/pgrep #g" -e "s#\blogger #$T/bin/logger #g" \
	    -e "s#\bsleep #$T/bin/sleep #g" -e "s#\bstat -c#$T/bin/stat -c#g" -e "s#\breadlink #$T/bin/readlink #g" \
	    -e "s#\bid -u#$T/bin/id -u#g" -e "s#\bdmesg\$#$T/bin/dmesg#g" -e "s#^sync\$#$T/bin/sync#" \
	    -e "s#^sync -f#$T/bin/sync -f#" -e "s#\bdpkg-query #$T/bin/dpkg-query #g" \
	    "$SCRIPT" > "$T/script.sh"
	if grep -n -E '(^|[^/])\b(loginctl|systemctl|pkill) ' "$T/script.sh" | grep -v '^[0-9]*:[[:space:]]*#' | grep -q .; then
		echo "FAIL $name: a system command is not stubbed in the test copy"; fails=$((fails + 1)); rm -rf "$T"; return
	fi
	for c in logger dmesg dpkg-query sync; do
		printf '#!/bin/sh\necho "%s $*" >> %s/calls\n' "$c" "$T" > "$T/bin/$c"
	done
	printf '#!/bin/sh\necho $(( $(cat %s/clock) + ${1%%%%.*} )) > %s/clock\n' "$T" "$T" > "$T/bin/sleep"
	cat > "$T/bin/loginctl" <<S
#!/bin/sh
echo "loginctl \$*" >> $T/calls
case "\$1" in
list-sessions) printf ' c2 1000 tibi -     1239 manager -    no -\n c32 1000 tibi seat0 4179 user tty1 no -\n c43 1000 tibi -  4736 user - no -\n' ;;
show-session) [ "\$2" = c32 ] && echo tty1 || echo ;;
esac
S
	printf '#!/bin/sh\necho "systemctl $* @$(cat %s/clock)" >> %s/calls\n' "$T" "$T" > "$T/bin/systemctl"
	cat > "$T/bin/pgrep" <<S
#!/bin/sh
s=\$(cat $T/xstate); t=\$(cat $T/clock)
case "$xexit" in
[0-9]*) [ "\$t" -lt "$xexit" ] && echo 4242 ;;
term) [ "\$s" = alive ] && echo 4242 ;;
never) echo 4242 ;;
esac
exit 0
S
	printf '#!/bin/sh\n[ "$3" = /proc/4242 ] && echo %s || exit 1\n' "$xuid" > "$T/bin/stat"
	printf '#!/bin/sh\n[ "$1" = /proc/4242/exe ] && echo %s || exit 1\n' "$xexe" > "$T/bin/readlink"
	printf '#!/bin/sh\n[ "$2" = tibi ] && echo 1000 || exit 1\n' > "$T/bin/id"
	cat > "$T/bin/xkill" <<S
#!/bin/sh
echo "kill \$* @\$(cat $T/clock)" >> $T/calls
[ "\$1" = -TERM ] && echo gone > $T/xstate
exit 0
S
	chmod +x "$T/bin/"*
	env -i PATH="$T/bin:/usr/bin:/bin" sh "$T/script.sh" > /dev/null 2>&1
	CALLS=$T/calls
	check() { if ! eval "$2"; then echo "FAIL $name: $1"; sed 's/^/    /' "$CALLS"; fails=$((fails + 1)); fi; }
	case $name in
	healthy)
		check "no restart when not display-only" "! grep -q 'getty' $CALLS" ;;
	exits)
		check "terminates tty1's session only" "grep -q 'loginctl terminate-session c32' $CALLS && ! grep -q 'terminate-session c[24]' $CALLS"
		check "stops getty before starting it" "[ \$(grep -n 'systemctl stop getty@tty1' $CALLS | cut -d: -f1) -lt \$(grep -n 'systemctl start getty@tty1' $CALLS | cut -d: -f1) ]"
		check "starts getty only after X is gone (fake second 3)" "grep -q 'systemctl start getty@tty1.service @3' $CALLS"
		check "no signal when X exits by itself" "! grep -q '^kill' $CALLS"
		check "no restart verb (a stop and a start)" "! grep -q 'systemctl restart' $CALLS"
		check "marker written" "[ -e $T/run/desktop-restarted ]" ;;
	term)
		check "SIGTERM to pid 4242 after 20 s" "grep -q 'kill -TERM 4242 @20' $CALLS"
		check "no SIGKILL when TERM works" "! grep -q 'kill -KILL' $CALLS"
		check "getty started after" "grep -q 'systemctl start getty@tty1' $CALLS" ;;
	never)
		check "SIGKILL after TERM fails" "grep -q 'kill -KILL 4242 @30' $CALLS"
		check "still starts the desktop, logged" "grep -q 'systemctl start getty@tty1.service @35' $CALLS && grep -q \"didn't exit\" $CALLS" ;;
	root-x)
		check "root's Xorg counts too" "grep -q 'kill -TERM 4242 @20' $CALLS" ;;
	other-user)
		check "another user's Xorg is neither waited for nor signalled" "! grep -q '^kill' $CALLS && grep -q 'systemctl start getty@tty1.service @0' $CALLS" ;;
	renamed)
		check "a process named Xorg with another binary is ignored" "! grep -q '^kill' $CALLS && grep -q 'systemctl start getty@tty1.service @0' $CALLS" ;;
	twice)
		check "a second display-only event doesn't restart again" "! grep -q 'getty' $CALLS && ! grep -q 'terminate-session' $CALLS && grep -q 'once this boot' $CALLS" ;;
	esac
	rm -rf "$T"
}
run_case healthy 0 0 1000 /usr/lib/xorg/Xorg 0
run_case exits 1 3 1000 /usr/lib/xorg/Xorg 0
run_case term 1 term 1000 /usr/lib/xorg/Xorg 0
run_case never 1 never 1000 /usr/lib/xorg/Xorg 0
run_case root-x 1 term 0 /usr/lib/xorg/Xorg 0
run_case other-user 1 never 1001 /usr/lib/xorg/Xorg 0
run_case renamed 1 never 1000 /home/tibi/Xorg 0
run_case twice 1 never 1000 /usr/lib/xorg/Xorg 1
[ "$fails" = 0 ] && echo "gpu-crash-restart-test: all passed" || echo "gpu-crash-restart-test: $fails failure(s)"
exit $((fails != 0))
