#!/bin/bash
# Host test of tools/ps3 (the PS3 lease) and of the scripts that honour it; no PS3, ssh is a stub.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
. "$root/tools/lib/t.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export XDG_RUNTIME_DIR=$T/run; mkdir -p "$XDG_RUNTIME_DIR" "$T/bin"
unset PS3_LEASE
ps3=$root/tools/ps3
lease=$XDG_RUNTIME_DIR/walnut-ps3.lease
expiry() { sed -n s/^expiry=//p "$lease"; }
status() { "$ps3" status >/dev/null; }
later() { [ "$(expiry)" -gt $(( $(date +%s) + 3000 )) ]; }

ok "free at the start" $ps3 status
ok "check passes when free" $ps3 check
A=$($ps3 lock --owner agentA --note "agent A" --for 30); ok "lock gives a token" test -n "$A"
no "a second lock is refused" $ps3 lock --note B
msg=$($ps3 status); ok "status names the holder" test "${msg#*held by agentA}" != "$msg"
# a dead pid does not make a live lease stale
ok "the lease has an expiry" grep -q "^expiry=" "$lease"
sed -i "s/^pid=.*/pid=999999/" "$lease"
no "a dead pid does not free the lease" $ps3 lock --note thief
no "unlock without the token is refused" $ps3 unlock
msg=$($ps3 lock 2>&1 >/dev/null); ok "the refusal says who holds it" test "${msg#*agent A}" != "$msg"
expect 1 "status exits 1 while held" status
no "check fails for someone else" $ps3 check
ok "check passes with the token" env PS3_LEASE=$A $ps3 check
no "unlock with a wrong token" env PS3_LEASE=wrong $ps3 unlock
no "renew with a wrong token" $ps3 renew bogus
ok "renew with the token" env PS3_LEASE=$A $ps3 renew --for 60
ok "renew extended the expiry" later
ok "the owner stays" grep -q "^owner=agentA" "$lease"
# --owner: the label of the current owner stands in for a lost token; no other label does
no "renew by another owner label" $ps3 renew --owner agentB
no "unlock by another owner label" $ps3 unlock --owner agentB
no "a wrong token is not rescued by the owner label" env PS3_LEASE=wrong $ps3 unlock --owner agentA
ok "renew by the owner label" $ps3 renew --owner agentA --for 60
ok "which extended it" later
ok "and kept the token" grep -q "^token=$A" "$lease"
ok "unlock by the owner label" $ps3 unlock --owner agentA
expect 0 "free again" status
A=$($ps3 lock --owner agentA --note "agent A" --for 30)
# two concurrent locks: exactly one wins
$ps3 unlock "$A" >/dev/null
for i in 1 2 3 4 5 6; do ( $ps3 lock --note "r$i" >/dev/null 2>&1 && echo won >> "$T/wins" ) & done; wait
ok "six concurrent locks: one wins" test "$(wc -l < "$T/wins")" = 1
ok "unlock --force removes it" bash -c "'$ps3' unlock --force | grep -q removed"
# expiry: takeover
$ps3 lock --for 1 >/dev/null
sed -i "s/^expiry=.*/expiry=$(( $(date +%s) - 5 ))/" "$lease"
expect 2 "an expired lease: status exits 2" status
ok "check passes on an expired lease" $ps3 check
B=$($ps3 lock --note B 2>"$T/err"); ok "taking over says it was expired" grep -q "expired" "$T/err"
ok "unlock with the new token" env PS3_LEASE=$B $ps3 unlock
expect 0 "free after the takeover" status

# every route to the console refuses under a foreign lease, before any ssh
cat > "$T/bin/ssh" <<EOS
#!/bin/sh
echo "\$*" >> "$T/ssh.calls"
exit 1
EOS
chmod +x "$T/bin/ssh"; cp "$T/bin/ssh" "$T/bin/scp"; cp "$T/bin/ssh" "$T/bin/rsync"
C=$($ps3 lock --note "someone else")
export PS3=u@h REPO_BUILDER_TAG=x SSH=ssh PATH=$T/bin:$PATH PS3_IP=192.0.2.1 PS3_USER=u PS3_SSH_KEY=$T/key
for s in tools/deploy.sh tools/play-test.sh; do
	out=$(cd "$root" && bash $s check 2>&1)
	expect 1 "$s refuses under another's lease" bash -c "cd '$root' && bash $s check"
	ok "$s names the holder" test "${out#*someone else}" != "$out"
done
for k in ssh scp rsync; do
	out=$($ps3 $k x 2>&1)
	expect 1 "tools/ps3 $k refuses under another's lease" $ps3 $k x
	ok "tools/ps3 $k names the holder" test "${out#*someone else}" != "$out"
done
for t in bench-push bench-pull ps3-reboot; do
	no "make $t reaches the console only through tools/ps3 ssh" bash -c "cd '$root' && make -n $t | grep -E '(^| )(ssh|scp) ' | grep -v 'tools/ps3 ssh'"
done
ok "no ssh, scp or rsync ran" test ! -e "$T/ssh.calls"
# with the lease ours, ssh runs once, with the project's options and USER@IP
expect 1 "the holder gets in (the stub exits 1)" env PS3_LEASE=$C $ps3 ssh uptime
ok "one call was made" test "$(wc -l < "$T/ssh.calls")" = 1
ok "with the key, the repository's known_hosts, BatchMode and the target" grep -q -- "-i $T/key -o UserKnownHostsFile=$root/build/ps3_known_hosts -o BatchMode=yes -o ConnectTimeout=10 u@192.0.2.1 uptime" "$T/ssh.calls"
done_tests
