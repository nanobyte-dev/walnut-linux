# shellcheck shell=bash
# tools/lib/ps3.sh: the one way to reach the PS3 from the PC; . tools/lib/ps3.sh (PC only, see tools/lib/README.md).
# Every call first runs `tools/ps3 check`: someone else's lease on the console refuses it (exit 1, who holds it).
# PS3_IP, PS3_USER and PS3_SSH_KEY come from config.mk / config.local.mk (tools/config-get.sh) unless the
# environment has them. Options: the repository's known_hosts file, no password prompts, a connect timeout.
# PS3_TIMEOUT=SECONDS limits a call (timeout); the default is none.
#   ps3_ssh ARGS...    ssh USER@IP ARGS...      ps3_scp ARGS...   scp with the same options (name the PS3 as "$PS3_HOST:")
# A script that needs its own ssh options: ps3_check, then ssh "${PS3_SSH_OPTS[@]}" -o ... "$PS3_HOST" ...
#   ps3_rsync ARGS...  rsync -e "ssh ..."       tools/ps3 ssh|scp|rsync ARGS... is the command form
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [ -z "${PS3_IP:-}" ] || [ -z "${PS3_USER:-}" ] || [ -z "${PS3_SSH_KEY:-}" ]; then
	{ read -r PS3_IP; read -r PS3_USER; read -r PS3_SSH_KEY; } < <("$ROOT/tools/config-get.sh" PS3_IP PS3_USER PS3_SSH_KEY)
fi
[ -n "$PS3_IP" ] && [ -n "$PS3_USER" ] || die "PS3_IP / PS3_USER: set them in config.local.mk"
PS3_HOST=$PS3_USER@$PS3_IP
PS3_SSH_OPTS=(-i "$PS3_SSH_KEY" -o "UserKnownHostsFile=$ROOT/build/ps3_known_hosts" -o BatchMode=yes -o ConnectTimeout=10)

ps3_check() { "$ROOT/tools/ps3" check; }	# someone else's lease: non-zero, so the call below never runs
ps3_ssh() { ps3_check && ${PS3_TIMEOUT:+timeout "$PS3_TIMEOUT"} ssh "${PS3_SSH_OPTS[@]}" "$PS3_HOST" "$@"; }
ps3_scp() { ps3_check && ${PS3_TIMEOUT:+timeout "$PS3_TIMEOUT"} scp -q "${PS3_SSH_OPTS[@]}" "$@"; }
ps3_rsync() { local e; printf -v e '%q ' ssh "${PS3_SSH_OPTS[@]}"; ps3_check && ${PS3_TIMEOUT:+timeout "$PS3_TIMEOUT"} rsync -e "$e" "$@"; }
