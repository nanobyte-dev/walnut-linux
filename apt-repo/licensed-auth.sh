#!/bin/bash
# licensed-auth.sh FILE [netrc]: writes the login of walnut.nanobyte.dev's password-protected licensed
# suite to FILE (mode 600): "user:password" (apt-repo/walnut-repo.py's WALNUT_APT_AUTH_FILE), or with
# "netrc" a curl netrc line. It comes from config.local.mk (WALNUT_LICENSED_USER, WALNUT_LICENSED_PASS)
# or else from pass (Projects/Walnut/LicensedRepo: the password on the first line, "user: <name>").
# Nothing is printed: the caller deletes FILE when done. Exit 3 when no login is set anywhere.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$1 form=${2:-plain}
{ read -r user; read -r pass; } < <("$here/../tools/config-get.sh" WALNUT_LICENSED_USER WALNUT_LICENSED_PASS)
if [ -z "$pass" ] && command -v pass >/dev/null && entry=$(pass show Projects/Walnut/LicensedRepo 2>/dev/null); then
	pass=$(sed -n 1p <<<"$entry")
	[ -n "$user" ] || user=$(sed -n 's/^user: *//p' <<<"$entry")
fi
[ -n "$user" ] && [ -n "$pass" ] || { echo "!! no login for the licensed suite: WALNUT_LICENSED_USER/PASS in config.local.mk or pass Projects/Walnut/LicensedRepo" >&2; exit 3; }
umask 077
case $form in
plain) printf '%s:%s\n' "$user" "$pass" > "$out" ;;
netrc) printf 'machine walnut.nanobyte.dev login %s password %s\n' "$user" "$pass" > "$out" ;;
*) echo "usage: $0 FILE [plain|netrc]" >&2; exit 2 ;;
esac
