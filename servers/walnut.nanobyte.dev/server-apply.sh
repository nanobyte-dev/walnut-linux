#!/bin/bash
# server-apply.sh STEP: the steps of deploy.sh that run on the server, as one file ShellCheck sees. deploy.sh uploads it
# to $DIR/bin/ and runs it over ssh, with the values it computed in the environment (all required):
#   DIR     the deployment (/data/nanobyte/Walnut), its state/ holds what deploy.sh just rsynced
#   STEP    fail2ban  the walnut-upload and walnut-401 filters and jails into /etc/fail2ban, then a reload
#           units     UNITS ("a.service b.path ...") from $DIR/state into /etc/systemd/system, then the watcher's
#                     timers; MODE=managed also enables and restarts walnut-upload.service
#           proxy     PROXY CONF: the main proxy's copy of CONF replaced after a backup, nginx -t, a reload, or the
#                     old file back
# APPLY_DRY=1 (a test switch, not part of deploy.sh) prints each command that would change something (with a +, on
# stderr) and runs nothing.
# Needs sudo -n for the root steps; the user is the deploy user, no password.
set -euo pipefail

run() { if [ -n "${APPLY_DRY:-}" ]; then echo "+ $*" >&2; else "$@"; fi; }
root() { run sudo -n "$@"; }
install_if_changed() {	# SRC DEST: a root-owned 644 copy when it differs
	cmp -s "$1" "$2" || root install -m 644 "$1" "$2"
}

fail2ban() {
	local s=$DIR/state f=/etc/fail2ban
	install_if_changed "$s/walnut-upload.filter" $f/filter.d/walnut-upload.conf
	install_if_changed "$s/walnut-upload.jail" $f/jail.d/walnut-upload.conf
	install_if_changed "$s/walnut-401.filter" $f/filter.d/walnut-401.conf
	# shipped disabled; never overwritten once the sitting has set it up
	[ -e $f/jail.d/walnut-401.conf ] || root install -m 644 "$s/walnut-401.jail" $f/jail.d/walnut-401.conf
	run rm "$s/walnut-upload.filter" "$s/walnut-upload.jail" "$s/walnut-401.filter" "$s/walnut-401.jail"
	root fail2ban-client reload >/dev/null
	if [ -z "${APPLY_DRY:-}" ]; then
		sudo -n fail2ban-client status walnut-upload | grep -E 'Currently (failed|banned)' | xargs echo '>> fail2ban walnut-upload:' || true
	fi
}

units() {
	: "${UNITS:?}" "${MODE:?}"
	local u
	for u in $UNITS; do
		install_if_changed "$DIR/state/$u" "/etc/systemd/system/$u"
		run rm "$DIR/state/$u"
	done
	root systemctl daemon-reload
	root systemctl reset-failed walnut-watch.path walnut-watch.service || true
	root systemctl restart walnut-watch.path
	root systemctl enable --now walnut-watch.path walnut-watch.timer walnut-ccache-trim.timer >/dev/null
	[ -n "${APPLY_DRY:-}" ] || systemctl is-active walnut-watch.path walnut-watch.timer walnut-ccache-trim.timer | xargs echo '>> watcher, trim:' || true
	if [ "$MODE" = managed ]; then
		root systemctl enable walnut-upload.service >/dev/null
		root systemctl restart walnut-upload.service
		[ -n "${APPLY_DRY:-}" ] || systemctl is-active walnut-upload.service | xargs echo '>> upload:' || true
	fi
}

proxy() {
	: "${PROXY:?}" "${CONF:?}"
	local live=$PROXY/$CONF new=$PROXY/$CONF.new bak=$PROXY/$CONF.bak had='' out
	run rm -f "$bak"	# a .bak left by an earlier deploy is not this deploy's old config
	if [ -e "$live" ]; then run cp -p "$live" "$bak"; had=1; fi
	# the new file is complete before it takes the live name; a failed copy leaves the live one alone
	# shellcheck disable=SC2064  # expanded now on purpose: the locals are gone when the trap runs
	trap "rm -f '$new'" EXIT
	run cp "$DIR/$CONF" "$new"
	run mv "$new" "$live"
	if [ -n "${APPLY_DRY:-}" ]; then echo "+ docker exec main-proxy nginx -t, then nginx -s reload" >&2; return 0; fi
	if out=$(docker exec main-proxy nginx -t 2>&1); then
		echo "$out" | tail -1
		docker exec main-proxy nginx -s reload && echo '>> main-proxy reloaded'
	else
		echo "$out" >&2
		echo '!! nginx -t failed: the old config is back, nothing reloaded' >&2
		if [ -n "$had" ]; then mv "$bak" "$live"; else rm -f "$live"; fi
		exit 1
	fi
}

[ "${BASH_SOURCE[0]}" = "$0" ] || return 0	# sourced (the test): the functions only
: "${DIR:?}"
case ${1:-} in
fail2ban | units | proxy) "$1" ;;
*) echo "usage: server-apply.sh fail2ban | units | proxy (see the header)" >&2; exit 2 ;;
esac
