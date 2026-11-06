#!/bin/bash
# deploy.sh: puts walnut.nanobyte.dev's server side in place (docs/release/ALPHA.md M4); idempotent.
# On WALNUT_HOST (config.local.mk), in /data/nanobyte/Walnut:
#   walnut-repo    nginx for www/ (read-only): apt/, download/, licensed/ (basic auth:
#                  WALNUT_LICENSED_USER/PASS), status/; never restarted by a site update
#   walnut-site    nginx with the site built in (walnut-site:<id>, walnut-watch)
#   walnut-aptcache  apt-cacher-ng for Debian's snapshot (snapshot.debian.org throttles), served under
#                  /debian-snapshot/ for the pinned dates only
#   walnut-ccache  ccache's remote storage (GET/PUT, login WALNUT_CCACHE_AUTH), /ccache/, trimmed daily to 25 GB
#   walnut-upload  sshd + rrsync as an ordinary user on port 2223: uploads into incoming/ only, with
#                  the keys in deploy-keys.pub (its host key is made once and pinned in known_hosts)
#   walnut-watch   (bin/, systemd path unit + timer, as the directory's owner) publishes what lands
#                  in incoming/: the site as a new image and container, files without a half-written
#                  moment (walnut-watch's header)
#   deploy.sh --rules COMMIT   installs the server's own rules (the ownership copy of github-ci/packages.toml,
#                  protected-names.txt, pins.json) root-owned into rules/, from that commit (an ancestor of
#                  origin/main), after showing what changes and waiting for the typed word `install`. Run it
#                  BEFORE the first deploy of the new walnut-watch, which refuses to install without them.
# and walnut.nanobyte.dev.conf in the main proxy's conf.d (the old file kept as .bak and put back if
# `nginx -t` fails). The systemd units need sudo. Content is published by uploads, never by this.
set -euo pipefail
cd "$(dirname "$0")"
{ read -r host; read -r user; read -r pass; read -r ccache_auth; } < <(../../tools/config-get.sh WALNUT_HOST WALNUT_LICENSED_USER WALNUT_LICENSED_PASS WALNUT_CCACHE_AUTH)
[ -n "$host" ] && [ -n "$user" ] && [ -n "$pass" ] && [ -n "$ccache_auth" ] || { echo "!! set WALNUT_HOST, WALNUT_LICENSED_USER, WALNUT_LICENSED_PASS, WALNUT_CCACHE_AUTH (user:password) in config.local.mk" >&2; exit 2; }
[ -s deploy-keys.pub ] || { echo "!! no deploy-keys.pub (the upload keys' public halves)" >&2; exit 2; }
# the keys' comments say what each is for; a comment that cannot be read stops the deploy before anything is written
auth=$(python3 -I deploy_keys.py authorized deploy-keys.pub) || exit 2
keydirs=$(python3 -I deploy_keys.py dirs deploy-keys.pub) || exit 2
DIR=/data/nanobyte/Walnut
PROXY=/data/nanobyte/MainProxy/Config/conf.d
CONF=walnut.nanobyte.dev.conf
# one ssh connection for every step: the server limits how fast new connections may come in
CTL=$(mktemp -u "${TMPDIR:-/tmp}/walnut-ssh.XXXXXX")
SSHO=(-o ControlMaster=auto -o ControlPath="$CTL" -o ControlPersist=120)
ssh() { command ssh "${SSHO[@]}" "$@"; }
rsync() { command rsync -e "ssh ${SSHO[*]}" "$@"; }
trap 'command ssh -o ControlPath="$CTL" -O exit "$host" 2>/dev/null || true' EXIT

if [ "${1:-}" = --rules ]; then
	commit=${2:?usage: deploy.sh --rules COMMIT}
	git fetch -q origin main 2>/dev/null || true
	git merge-base --is-ancestor "$commit" origin/main || { echo "!! $commit is not an ancestor of origin/main" >&2; exit 2; }
	new=$(mktemp -d)
	git show "$commit:github-ci/packages.toml" > "$new/ownership.toml"
	git show "$commit:github-ci/protected-names.txt" > "$new/protected-names.txt"
	git show "$commit:servers/walnut.nanobyte.dev/rules/pins.json" > "$new/pins.json"
	# the same loader walnut-watch uses: a copy it would refuse is not installed
	python3 -I -c 'import sys; sys.path.insert(0, "."); import walnut_checks as w; r = w.load_rules(sys.argv[1]); print(">> loads:", len(r.lists.entries), "packages,", len(r.protected), "protected names,", len(r.pins), "pins")' "$new" || exit 1
	for f in ownership.toml protected-names.txt pins.json; do
		echo "== $f"
		ssh "$host" "cat $DIR/rules/$f 2>/dev/null || true" | diff -u --label "installed $f" --label "$commit $f" - "$new/$f" || true
	done
	read -r -p "Type install to put these rules on $host (root-owned, read-only): " ans
	[ "$ans" = install ] || { echo "!! not installed" >&2; rm -rf "$new"; exit 1; }
	ssh "$host" "umask 022; rm -rf $DIR/state/rules-new; mkdir -p $DIR/state/rules-new"
	rsync -t --chmod=644 "$new"/ownership.toml "$new"/protected-names.txt "$new"/pins.json "$host:$DIR/state/rules-new/"
	ssh "$host" "set -e
		sudo -n install -d -m 755 -o root -g root $DIR/rules
		for f in ownership.toml protected-names.txt pins.json; do sudo -n install -m 444 -o root -g root $DIR/state/rules-new/\$f $DIR/rules/\$f; done
		rm -rf $DIR/state/rules-new
		ls -l $DIR/rules"
	rm -rf "$new"
	echo ">> rules installed from $commit"
	exit 0
fi

# the server's umask is 002: modes are set here, sshd (StrictModes) refuses group-writable key dirs
ssh "$host" "umask 022; mkdir -p $DIR/{incoming/{site,download,apt,licensed},www/{apt,download,licensed},site,state,bin,keys,upload-keys,repo-conf}
	chmod 755 $DIR $DIR/{incoming,incoming/*,www,www/*,site,state,bin,keys,upload-keys,repo-conf}"
# each key's own upload directories (incoming/<name>-key/<kind>), from the comments in deploy-keys.pub
for d in $keydirs; do ssh "$host" "umask 022; mkdir -p $DIR/incoming/$d && chmod 755 $DIR/incoming/${d%%/*} $DIR/incoming/$d"; done
# nginx.conf with the snapshot dates the builds use (the only ones the Debian cache passes): config.mk,
# the initramfs's pins and every package's package.conf and upstream.mk (tools/pkg-dir.sh --all)
pkgs=$(../../tools/pkg-dir.sh --all) || { echo "!! tools/pkg-dir.sh --all failed" >&2; exit 1; }
dates=$(cat ../../config.mk ../../tools/ssdimg/upstream.mk \
	$(for d in $pkgs; do for f in package.conf upstream.mk; do [ ! -f "../../$d/$f" ] || echo "../../$d/$f"; done; done) |
	grep -oE '[0-9]{8}T[0-9]{6}Z' | sort -u | paste -sd'|')
[ -n "$dates" ] || { echo "!! no snapshot dates found" >&2; exit 1; }
sed "s/@SNAPSHOT_DATES@/$dates/" repo.conf > "$CTL.repo.conf"
rsync -rt --chmod=D755,F644 docker-compose.yml Dockerfile site.conf upload aptcache ccache "$host:$DIR/"
# a directory of configs (not single files: rsync renames into place, which a bind-mounted file would never see)
rsync -t --chmod=644 "$CTL.repo.conf" "$host:$DIR/repo-conf/repo.conf"
rsync -t --chmod=644 repo-headers.inc "$host:$DIR/repo-conf/repo-headers.inc"
rm -f "$CTL.repo.conf"
ssh "$host" "umask 022; mkdir -p $DIR/aptcache-data
	[ -e $DIR/aptcache-data/CACHEDIR.TAG ] || printf 'Signature: 8a477f597d28d172789f06886806bc55\n# walnut-aptcache: Debian snapshot cache, refetchable\n' > $DIR/aptcache-data/CACHEDIR.TAG"
# walnut-watch refuses everything without the server's rules (deploy.sh --rules): do not install a server that would
ssh "$host" "[ -s $DIR/rules/ownership.toml ] && [ -s $DIR/rules/protected-names.txt ] && [ -s $DIR/rules/pins.json ]" ||
	{ echo "!! no rules/ownership.toml (or protected-names.txt, pins.json) on the server: run deploy.sh --rules COMMIT first" >&2; exit 1; }
rsync -t --chmod=755 walnut-watch walnut_checks.py ../../github-ci/walnut_lists.py "$host:$DIR/bin/"
# the public keys: archive (releases, stable, licensed) and CI (testing only); a suite whose key is
# missing is refused
for k in ../../apt-repo/keyrings/{archive,testing}-keyring.gpg; do
	if [ -s "$k" ]; then
		rsync -t --chmod=644 "$k" "$host:$DIR/keys/"
	else
		echo "-- no ${k##*/} yet: what it signs is refused until there is one"
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
printf '%s\n' "$auth" |
	ssh "$host" "umask 077; cat > $DIR/upload-keys/authorized_keys.new && mv $DIR/upload-keys/authorized_keys.new $DIR/upload-keys/authorized_keys"
printf '%s:%s\n' "$user" "$(openssl passwd -apr1 -stdin <<< "$pass")" |
	ssh "$host" "umask 022; cat > $DIR/htpasswd.new && mv $DIR/htpasswd.new $DIR/htpasswd"
printf '%s:%s\n' "${ccache_auth%%:*}" "$(openssl passwd -apr1 -stdin <<< "${ccache_auth#*:}")" |
	ssh "$host" "umask 022; cat > $DIR/ccache.htpasswd.new && mv $DIR/ccache.htpasswd.new $DIR/ccache.htpasswd"
ssh "$host" "umask 022; mkdir -p $DIR/ccache-data
	[ -e $DIR/ccache-data/CACHEDIR.TAG ] || printf 'Signature: 8a477f597d28d172789f06886806bc55\\n# walnut-ccache: build cache, refillable\\n' > $DIR/ccache-data/CACHEDIR.TAG"
rsync -t --chmod=755 ccache/trim.sh "$host:$DIR/bin/ccache-trim.sh"

# the configs are tested in a throwaway container before the real ones restart: a bad include or directive stops
# the deploy here, not in a restart loop that takes apt, downloads and status down for every client
ssh "$host" "cd $DIR
	t() { out=\$(\"\$@\" 2>&1) || { echo \"!! nginx -t failed, nothing restarted:\" >&2; echo \"\$out\" >&2; exit 1; }; echo \"\$out\" | tail -1; }
	t docker run --rm -v $DIR/repo-conf:/etc/nginx/conf.d:ro -v $DIR/htpasswd:/etc/nginx/walnut.htpasswd:ro nginx:stable-alpine@sha256:583b7f2fce1cdb8da5ad5c39ebd3c33b98b87d493190e548bdee1fa8c619c311 nginx -t
	t docker run --rm -v $DIR/ccache:/etc/nginx/ccache-conf:ro -v $DIR/ccache.htpasswd:/etc/nginx/ccache.htpasswd:ro -v $DIR/ccache-data:/srv/ccache nginx:stable-alpine@sha256:583b7f2fce1cdb8da5ad5c39ebd3c33b98b87d493190e548bdee1fa8c619c311 nginx -t -c /etc/nginx/ccache-conf/nginx.conf"
ssh "$host" "cd $DIR && docker compose build -q walnut-upload walnut-aptcache && docker compose up -d walnut-repo walnut-upload walnut-aptcache walnut-ccache && docker compose up -d --force-recreate walnut-repo walnut-ccache"

# the watcher's units
for u in systemd/walnut-watch.{service,path,timer} systemd/walnut-ccache-trim.{service,timer}; do
	rsync -t "$u" "$host:$DIR/state/"
done
ssh "$host" "set -e
	for u in walnut-watch.service walnut-watch.path walnut-watch.timer walnut-ccache-trim.service walnut-ccache-trim.timer; do
		cmp -s $DIR/state/\$u /etc/systemd/system/\$u || sudo -n install -m 644 $DIR/state/\$u /etc/systemd/system/\$u
		rm $DIR/state/\$u
	done
	sudo -n systemctl daemon-reload
	sudo -n systemctl reset-failed walnut-watch.path walnut-watch.service || true
	sudo -n systemctl restart walnut-watch.path
	sudo -n systemctl enable --now walnut-watch.path walnut-watch.timer walnut-ccache-trim.timer >/dev/null
	systemctl is-active walnut-watch.path walnut-watch.timer walnut-ccache-trim.timer | xargs echo '>> watcher, trim:'"
# fail2ban's walnut-upload jail (bans in DOCKER-USER, like nginx-scanners)
rsync -t fail2ban/walnut-upload.filter fail2ban/walnut-upload.jail fail2ban/walnut-401.filter fail2ban/walnut-401.jail "$host:$DIR/state/"
ssh "$host" "set -e
	cmp -s $DIR/state/walnut-upload.filter /etc/fail2ban/filter.d/walnut-upload.conf || sudo -n install -m 644 $DIR/state/walnut-upload.filter /etc/fail2ban/filter.d/walnut-upload.conf
	cmp -s $DIR/state/walnut-upload.jail /etc/fail2ban/jail.d/walnut-upload.conf || sudo -n install -m 644 $DIR/state/walnut-upload.jail /etc/fail2ban/jail.d/walnut-upload.conf
	cmp -s $DIR/state/walnut-401.filter /etc/fail2ban/filter.d/walnut-401.conf || sudo -n install -m 644 $DIR/state/walnut-401.filter /etc/fail2ban/filter.d/walnut-401.conf
	[ -e /etc/fail2ban/jail.d/walnut-401.conf ] || sudo -n install -m 644 $DIR/state/walnut-401.jail /etc/fail2ban/jail.d/walnut-401.conf   # shipped disabled; never overwritten once the sitting has set it up
	rm $DIR/state/walnut-upload.filter $DIR/state/walnut-upload.jail $DIR/state/walnut-401.filter $DIR/state/walnut-401.jail
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
# what the headers promise, from outside: a failed check fails the deploy
./check-headers.sh
