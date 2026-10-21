#!/bin/bash
# deploy.sh: puts walnut.nanobyte.dev's server side in place (docs/release/ALPHA.md M4). Copies the
# walnut-repo container's files to /data/nanobyte/Walnut on WALNUT_HOST (config.local.mk), writes the
# licensed suite's htpasswd from WALNUT_LICENSED_USER/PASS, starts the container, then installs
# walnut.nanobyte.dev.conf into the main proxy's conf.d and reloads it: the old file is kept as .bak
# and put back if `nginx -t` fails, so a bad config never reaches the running proxy. Idempotent.
# The content (www/apt, www/download) is published separately; this never deletes any of it.
set -euo pipefail
cd "$(dirname "$0")"
{ read -r host; read -r user; read -r pass; } < <(../config-get.sh WALNUT_HOST WALNUT_LICENSED_USER WALNUT_LICENSED_PASS)
[ -n "$host" ] && [ -n "$user" ] && [ -n "$pass" ] || { echo "!! set WALNUT_HOST, WALNUT_LICENSED_USER, WALNUT_LICENSED_PASS in config.local.mk" >&2; exit 2; }
DIR=/data/nanobyte/Walnut
PROXY=/data/nanobyte/MainProxy/Config/conf.d
CONF=walnut.nanobyte.dev.conf
# one ssh connection for every step: the server limits how fast new connections may come in
CTL=$(mktemp -u "${TMPDIR:-/tmp}/walnut-ssh.XXXXXX")
SSHO=(-o ControlMaster=auto -o ControlPath="$CTL" -o ControlPersist=120)
ssh() { command ssh "${SSHO[@]}" "$@"; }
rsync() { command rsync -e "ssh ${SSHO[*]}" "$@"; }
trap 'command ssh -o ControlPath="$CTL" -O exit "$host" 2>/dev/null || true' EXIT

ssh "$host" "mkdir -p $DIR/www/apt $DIR/www/licensed $DIR/www/download"
rsync -a docker-compose.yml nginx.conf "$host:$DIR/"
rsync -a www/index.html "$host:$DIR/www/"
printf '%s:%s\n' "$user" "$(openssl passwd -apr1 -stdin <<< "$pass")" | ssh "$host" "umask 022; cat > $DIR/htpasswd.new && mv $DIR/htpasswd.new $DIR/htpasswd"
ssh "$host" "cd $DIR && docker compose up -d --remove-orphans && docker compose ps --format '{{.Name}} {{.Status}}'"

rsync -a "$CONF" "$host:$DIR/$CONF"
ssh "$host" "set -e
	[ ! -e $PROXY/$CONF ] || cp -p $PROXY/$CONF $PROXY/$CONF.bak
	cp $DIR/$CONF $PROXY/$CONF
	if docker exec main-proxy nginx -t 2>&1 | tail -2; then
		docker exec main-proxy nginx -s reload && echo '>> main-proxy reloaded'
	else
		echo '!! nginx -t failed: the old config is back, nothing reloaded' >&2
		if [ -e $PROXY/$CONF.bak ]; then mv $PROXY/$CONF.bak $PROXY/$CONF; else rm -f $PROXY/$CONF; fi
		exit 1
	fi"
