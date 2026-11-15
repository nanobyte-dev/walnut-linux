#!/bin/bash
# xwrap-stdin-check.sh: ps3-bench.sh's x mode under ps3-desktop's console-only Xwrapper.config (Xorg.wrap
# starts the server only if its stdin, stdout or stderr is a VT). Runs the script's own x-mode block and
# its desktop step's tty lines with quiet, sudo, run_job, stat and die stubbed: x takes vt1's tty
# (chown to the caller) and gives it to the job as stdin, the client's stdin is /dev/null again, run_job
# reads JOB_IN, and desktop gives the tty back to root before the getty only when an x run had it.
here=$(cd "$(dirname "$0")/.." && pwd)
S=$here/scripts/ps3-bench.sh
W=$(mktemp -d); trap 'rm -rf "${W:?}"' EXIT
fail=0
ok() { echo "ok: $*"; }
bad() { echo "FAIL: $*"; fail=1; }
# the x branch of the case statement, from its "run|x)" line to its ";;"
sed -n '/^run|x)$/,/^\t;;$/p' "$S" | sed '1d;$d' > "$W/x.sh"
grep -q xinit "$W/x.sh" || { bad "the x branch isn't where this test expects it"; exit 1; }
cat > "$W/run.sh" <<'EOS'
quiet() { echo "quiet" >> "$LOG"; }
sudo() { echo "sudo $*" >> "$LOG"; }
die() { echo "die $*" >> "$LOG"; exit 1; }
run_job() { shift; echo "job_in=${JOB_IN:-}" >> "$LOG"; printf '%s\n' "$@" > "$ARGS"; }
set -- x lbl -- /usr/games/fake arg
EOS
cat "$W/x.sh" >> "$W/run.sh"
LOG=$W/log ARGS=$W/args bash "$W/run.sh"
grep -qx "sudo chown $(id -u) /dev/tty1" "$W/log" && ok "x takes vt1's tty for the caller" || bad "no chown of /dev/tty1"
[ "$(grep -n quiet "$W/log" | cut -d: -f1)" -lt "$(grep -n 'chown' "$W/log" | cut -d: -f1)" ] 2>/dev/null &&
	ok "after quiet (getty@tty1 stopped)" || bad "the chown comes before quiet"
grep -qx "job_in=/dev/tty1" "$W/log" && ok "the job's stdin is /dev/tty1" || bad "the job's stdin isn't the tty"
head -1 "$W/args" | grep -qx xinit && ok "xinit starts the server" || bad "the job isn't xinit"
sed -n 4p "$W/args" | grep -q '^exec < /dev/null; ' && ok "the client's stdin is /dev/null again" || bad "the client keeps the tty as stdin"
grep -q 'exec timeout "$0" "$@"'"'"' "${BENCH_TIMEOUT:-600}" "$@" \\$' "$S" && grep -q '< "${JOB_IN:-/dev/null}" 2>&1' "$S" &&
	ok "run_job reads JOB_IN (default /dev/null)" || bad "run_job ignores JOB_IN"
# the desktop step: its line that gives vt1's tty back (systemctl, stat and sudo stubbed)
sed -n '/^desktop)$/,/^\t;;$/p' "$S" > "$W/desktop.txt"
grep -E 'chown root /dev/tty1' "$W/desktop.txt" > "$W/d.sh"
[ -s "$W/d.sh" ] || { bad "the desktop step doesn't give the tty back"; exit 1; }
[ "$(grep -n 'chown root /dev/tty1' "$W/desktop.txt" | cut -d: -f1)" -lt "$(grep -n 'chvt 1' "$W/desktop.txt" | cut -d: -f1)" ] &&
	ok "desktop: the tty back before the switch to vt1" || bad "desktop: the tty back only after chvt 1 (autovt's login may own it by then)"
for c in "mine inactive" "root inactive" "mine active"; do
	set -- $c; : > "$W/log"
	u=$(id -u); [ $1 = root ] && u=0
	G=$2 U=$u LOG=$W/log bash -c 'sudo() { echo "sudo $*" >> "$LOG"; }; stat() { echo "$U"; }
		systemctl() { [ "$G" = active ]; }; '"$(cat "$W/d.sh")"
	case $c in
	"mine inactive") grep -qx 'sudo chown root /dev/tty1' "$W/log" && ok "desktop: our tty back to root" || bad "desktop left the tty ours" ;;
	"root inactive") ! grep -q chown "$W/log" && ok "desktop: a root tty left alone" || bad "desktop chowned a tty that wasn't ours" ;;
	"mine active") ! grep -q chown "$W/log" && ok "desktop: a live desktop's tty left alone" || bad "desktop took a live session's tty" ;;
	esac
done
# JOB_IN starts empty in run/x and session mode, so a caller's environment can't set the job's stdin
grep -q '^	XVT= JOB_IN=' "$S" && grep -q 'BENCH_INTF=0 XVT= JOB_IN=' "$S" && ok "JOB_IN cleared in run/x and session" || bad "JOB_IN can come from the environment"
exit $fail
