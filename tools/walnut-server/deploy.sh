#!/bin/bash
# deploy.sh: puts walnut.nanobyte.dev's server side in place (docs/release/ALPHA.md M4); idempotent.
# On WALNUT_HOST (config.local.mk), in /data/nanobyte/Walnut:
#   walnut-repo    nginx with the site built in (walnut-site:<id>), www/ mounted read-only: apt/,
#                  download/, licensed/ (basic auth: WALNUT_LICENSED_USER/PASS)
#   walnut-aptcache  apt-cacher-ng for Debian's snapshot (snapshot.debian.org throttles), served under
#                  /debian-snapshot/ for the pinned dates only
#   walnut-upload  sshd + rrsync as an ordinary user on port 2223: uploads into incoming/ only, with
#                  the keys in deploy-keys.pub (its host key is made once and pinned in known_hosts)
#   walnut-watch   (bin/, systemd path unit + timer, as the directory's owner) publishes what lands
#                  in incoming/: the site as a new image and container, files without a half-written
#                  moment (walnut-watch's header)
# and walnut.nanobyte.dev.conf in the main proxy's conf.d (the old file kept as .bak and put back if
# `nginx -t` fails). The systemd units need sudo. Content is published by uploads, never by this.
set -euo pipefail
cd "$(dirname "$0")"
{ read -r host; read -r user; read -r pass; } < <(../config-get.sh WALNUT_HOST WALNUT_LICENSED_USER WALNUT_LICENSED_PASS)
[ -n "$host" ] && [ -n "$user" ] && [ -n "$pass" ] || { echo "!! set WALNUT_HOST, WALNUT_LICENSED_USER, WALNUT_LICENSED_PASS in config.local.mk" >&2; exit 2; }
[ -s deploy-keys.pub ] || { echo "!! no deploy-keys.pub (the upload keys' public halves)" >&2; exit 2; }
DIR=/data/nanobyte/Walnut
PROXY=/data/nanobyte/MainProxy/Config/conf.d
CONF=walnut.nanobyte.dev.conf
# one ssh connection for every step: the server limits how fast new connections may come in
CTL=$(mktemp -u "${TMPDIR:-/tmp}/walnut-ssh.XXXXXX")
SSHO=(-o ControlMaster=auto -o ControlPath="$CTL" -o ControlPersist=120)
ssh() { command ssh "${SSHO[@]}" "$@"; }
rsync() { command rsync -e "ssh ${SSHO[*]}" "$@"; }
trap 'command ssh -o ControlPath="$CTL" -O exit "$host" 2>/dev/null || true' EXIT

# the server's umask is 002: modes are set here, sshd (StrictModes) refuses group-writable key dirs
ssh "$host" "umask 022; mkdir -p $DIR/{incoming/{site,download,apt,licensed},www/{apt,download,licensed},site,state,bin,keys,upload-keys}
	chmod 755 $DIR $DIR/{incoming,incoming/*,www,www/*,site,state,bin,keys,upload-keys}"
# nginx.conf with the snapshot dates the builds use (the only ones the Debian cache passes)
dates=$(cat ../../config.mk ../../*/upstream.mk ../ssdimg/upstream.mk ../../*/package.conf | grep -oE '[0-9]{8}T[0-9]{6}Z' | sort -u | paste -sd'|')
[ -n "$dates" ] || { echo "!! no snapshot dates found" >&2; exit 1; }
sed "s/@SNAPSHOT_DATES@/$dates/" nginx.conf > "$CTL.nginx.conf"
rsync -rt --chmod=D755,F644 docker-compose.yml Dockerfile upload aptcache "$host:$DIR/"
rsync -t --chmod=644 "$CTL.nginx.conf" "$host:$DIR/nginx.conf"
rm -f "$CTL.nginx.conf"
ssh "$host" "umask 022; mkdir -p $DIR/aptcache-data
	[ -e $DIR/aptcache-data/CACHEDIR.TAG ] || printf 'Signature: 8a477f597d28d172789f06886806bc55\n# walnut-aptcache: Debian snapshot cache, refetchable\n' > $DIR/aptcache-data/CACHEDIR.TAG"
rsync -t --chmod=755 walnut-watch "$host:$DIR/bin/"
# the public keys: archive (releases, stable, licensed) and CI (testing only); a suite whose key is
# missing is refused
for k in archive-keyring.gpg testing-keyring.gpg; do
	if [ -s "$k" ]; then
		rsync -t --chmod=644 "$k" "$host:$DIR/keys/"
	else
		echo "-- no $k yet: what it signs is refused until there is one"
	fi
done
ssh "$host" "rm -f $DIR/www/index.html"	# the first placeholder, now in the site

# the upload container's host key, made once, pinned here
ssh "$host" "[ -e $DIR/upload-keys/ssh_host_ed25519_key ] || ssh-keygen -q -t ed25519 -N '' -C walnut-upload -f $DIR/upload-keys/ssh_host_ed25519_key"
hk=$(ssh "$host" "cut -d' ' -f1,2 $DIR/upload-keys/ssh_host_ed25519_key.pub")
printf '[walnut.nanobyte.dev]:2223 %s\n' "$hk" > known_hosts.new
if ! cmp -s known_hosts.new known_hosts; then
	[ ! -e known_hosts ] || echo "!! the upload host key changed: known_hosts updated, commit it" >&2
	mv known_hosts.new known_hosts
else
	rm known_hosts.new
fi
grep -v '^#' deploy-keys.pub | grep . | sed 's|^|restrict,command="rrsync -wo -no-del -no-overwrite -no-lock /incoming" |' |
	ssh "$host" "umask 077; cat > $DIR/upload-keys/authorized_keys.new && mv $DIR/upload-keys/authorized_keys.new $DIR/upload-keys/authorized_keys"
printf '%s:%s\n' "$user" "$(openssl passwd -apr1 -stdin <<< "$pass")" |
	ssh "$host" "umask 022; cat > $DIR/htpasswd.new && mv $DIR/htpasswd.new $DIR/htpasswd"

ssh "$host" "cd $DIR && docker compose build -q walnut-upload walnut-aptcache && docker compose up -d walnut-upload walnut-aptcache"

# the watcher's units
for u in systemd/walnut-watch.{service,path,timer}; do
	rsync -t "$u" "$host:$DIR/state/"
done
ssh "$host" "set -e
	for u in walnut-watch.service walnut-watch.path walnut-watch.timer; do
		cmp -s $DIR/state/\$u /etc/systemd/system/\$u || sudo -n install -m 644 $DIR/state/\$u /etc/systemd/system/\$u
		rm $DIR/state/\$u
	done
	sudo -n systemctl daemon-reload
	sudo -n systemctl enable --now walnut-watch.path walnut-watch.timer >/dev/null
	systemctl is-active walnut-watch.path walnut-watch.timer | xargs echo '>> watcher:'"
# fail2ban's walnut-upload jail (bans in DOCKER-USER, like nginx-scanners)
rsync -t fail2ban/walnut-upload.filter fail2ban/walnut-upload.jail "$host:$DIR/state/"
ssh "$host" "set -e
	cmp -s $DIR/state/walnut-upload.filter /etc/fail2ban/filter.d/walnut-upload.conf || sudo -n install -m 644 $DIR/state/walnut-upload.filter /etc/fail2ban/filter.d/walnut-upload.conf
	cmp -s $DIR/state/walnut-upload.jail /etc/fail2ban/jail.d/walnut-upload.conf || sudo -n install -m 644 $DIR/state/walnut-upload.jail /etc/fail2ban/jail.d/walnut-upload.conf
	rm $DIR/state/walnut-upload.filter $DIR/state/walnut-upload.jail
	sudo -n fail2ban-client reload >/dev/null
	sudo -n fail2ban-client status walnut-upload | grep -E 'Currently (failed|banned)' | xargs echo '>> fail2ban walnut-upload:'"
# a site already published: rebuild it with this config (else the first `make walnut-site` starts it)
ssh "$host" "grep -q WALNUT_SITE_TAG= $DIR/.env 2>/dev/null && $DIR/bin/walnut-watch --rebuild || echo '>> no site yet: make walnut-site'"

rsync -t "$CONF" "$host:$DIR/$CONF"
ssh "$host" "set -e
	[ ! -e $PROXY/$CONF ] || cp -p $PROXY/$CONF $PROXY/$CONF.bak
	cp $DIR/$CONF $PROXY/$CONF
	if docker exec main-proxy nginx -t 2>&1 | tail -1; then
		docker exec main-proxy nginx -s reload && echo '>> main-proxy reloaded'
	else
		echo '!! nginx -t failed: the old config is back, nothing reloaded' >&2
		if [ -e $PROXY/$CONF.bak ]; then mv $PROXY/$CONF.bak $PROXY/$CONF; else rm -f $PROXY/$CONF; fi
		exit 1
	fi"
