#!/bin/bash
# Host test of tools/ps3 (the PS3 lease) and of the scripts that honour it; no PS3, ssh is a stub.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export XDG_RUNTIME_DIR=$T/run; mkdir -p "$XDG_RUNTIME_DIR" "$T/bin"
unset PS3_LEASE
ps3=$root/tools/ps3
fail=0
ok() { if "$@"; then echo "ok   $*"; else echo "FAIL $*"; fail=1; fi; }
no() { if "$@" >/dev/null 2>&1; then echo "FAIL (should fail) $*"; fail=1; else echo "ok   !$*"; fi; }

ok $ps3 status
ok $ps3 check
A=$($ps3 lock --note "agent A" --for 30); ok test -n "$A"
no $ps3 lock --note B
msg=$($ps3 lock 2>&1 >/dev/null); ok test "${msg#*agent A}" != "$msg"
$ps3 status >/dev/null; ok test $? = 1
no $ps3 check
ok env PS3_LEASE=$A $ps3 check
no env PS3_LEASE=wrong $ps3 unlock
no $ps3 renew bogus
ok env PS3_LEASE=$A $ps3 renew --for 60
ok test "$(sed -n s/^expiry=//p $XDG_RUNTIME_DIR/walnut-ps3.lease)" -gt $(( $(date +%s) + 3000 ))
# two concurrent locks: exactly one wins
$ps3 unlock "$A" >/dev/null
for i in 1 2 3 4 5 6; do ( $ps3 lock --note "r$i" >/dev/null 2>&1 && echo won >> "$T/wins" ) & done; wait
ok test "$(wc -l < "$T/wins")" = 1
$ps3 unlock --force | grep -q removed; ok test $? = 0
# expiry: takeover
$ps3 lock --for 1 >/dev/null
sed -i "s/^expiry=.*/expiry=$(( $(date +%s) - 5 ))/" "$XDG_RUNTIME_DIR/walnut-ps3.lease"
$ps3 status >/dev/null; ok test $? = 2
ok $ps3 check
B=$($ps3 lock --note B 2>"$T/err"); ok grep -q "expired" "$T/err"
ok env PS3_LEASE=$B $ps3 unlock
$ps3 status >/dev/null; ok test $? = 0

# deploy.sh and play-test.sh refuse under a foreign lease, before any ssh
cat > "$T/bin/ssh" <<EOS
#!/bin/sh
echo "\$*" >> "$T/ssh.calls"
exit 1
EOS
chmod +x "$T/bin/ssh"
C=$($ps3 lock --note "someone else")
export PS3=u@h REPO_BUILDER_TAG=x SSH=ssh PATH=$T/bin:$PATH
for s in tools/deploy.sh tools/play-test.sh; do
	out=$(cd "$root" && bash $s check 2>&1); rc=$?
	ok test $rc = 1
	ok test "${out#*someone else}" != "$out"
done
ok test ! -e "$T/ssh.calls"
exit $fail
