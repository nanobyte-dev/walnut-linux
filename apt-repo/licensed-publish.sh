#!/bin/bash
# licensed-publish.sh [FILE.deb|FILE.dsc]...: publishes licensed packages built on this PC to
# walnut.nanobyte.dev's password-protected `licensed` suite (https://walnut.nanobyte.dev/licensed,
# docs/PACKAGING.md "The licensed suite"). Licensed packages are built here only, never on GitHub
# Actions (no game data there); every free package is published by CI.
#   Without arguments: every licensed .deb in build/debs/*/ and the .dsc of each licensed source in
#   build/src/<dir>/ next to them. A package is licensed when tools/leak-check.py says so, which asks
#   tools/licensed.py (its Section non-free/* or local/*, or a licensed name); licensed.py check runs
#   first, and declarations that disagree or can't be read stop the publish.
#   Refused: a free package (CI publishes those), a ~dev version (commit, rebuild), a version
#   already published with other bytes (the suite keeps every file forever: a new file needs a new
#   version; apt-repo/walnut-repo.py --strict), and a new package whose name, or a name in its
#   Provides/Replaces/Conflicts/Breaks, is Debian's at the pinned snapshot or one of ours
#   (apt-repo/licensed-names.py; the server has no Debian index to check this against).
# The suite is signed with the archive key (pass Projects/Walnut/ArchiveKey, imported into a
# throwaway GNUPGHOME; GNUPGHOME + WALNUT_SIGN_KEY_FPR use a keyring of yours instead). Its passphrase:
# the file WALNUT_SIGN_PASSPHRASE_FILE names, or asked on the terminal. The suite's login:
# apt-repo/licensed-auth.sh. The upload goes through servers/walnut.nanobyte.dev/walnut-upload.sh (kind licensed,
# the deploy key ~/.ssh/walnut-deploy); walnut-watch's verdict is waited for, and a stale bundle
# (another publish landed first) is made again on the new state.
# Env: DRY_RUN=1 makes and checks the bundle (build/tmp/licensed-bundle) without uploading it.
# DEBIAN_INDEX_URL (the pinned snapshot's dists/<suite>) and BUILDER_TAG (the builder image, whose
# debian-ports keyring checks that index's InRelease) come from make publish.
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
cd "$(dirname "$0")/.."
URL=${WALNUT_LICENSED_URL:-https://walnut.nanobyte.dev/licensed}
STATUS=${WALNUT_STATUS_URL:-https://walnut.nanobyte.dev/status}
RING=apt-repo/keyrings/archive-keyring.gpg
python3 -I tools/licensed.py check || { echo "!! tools/licensed.py check failed: nothing published" >&2; exit 1; }
is_licensed_deb() {	# leak-check: 1 licensed, 0 free, anything else stops (fails closed)
	local rc=0
	python3 -I tools/leak-check.py debs "$1" >/dev/null 2>&1 || rc=$?
	case $rc in 0) return 1 ;; 1) return 0 ;; *) echo "!! tools/leak-check.py debs $1 failed (exit $rc)" >&2; exit 1 ;; esac
}

files=("$@")
if [ ${#files[@]} = 0 ]; then
	for d in build/debs/*/; do
		lic=
		for f in "$d"*.deb; do
			[ -e "$f" ] || continue
			if is_licensed_deb "$f"; then files+=("$f"); lic=1; fi
		done
		[ -n "$lic" ] || continue
		for s in build/src/"$(basename "$d")"/*.dsc; do [ -e "$s" ] && files+=("$s"); done
	done
	[ ${#files[@]} -gt 0 ] || { echo "!! no licensed package in build/debs (make <pkg>-deb, make game-data, make licensed-data)" >&2; exit 1; }
fi
bad=
for f in "${files[@]}"; do
	case $f in
	*.deb) is_licensed_deb "$f" || { echo "!! free, published by CI, not from here: $f" >&2; bad=1; } ;;
	*.dsc) [ "$(python3 -I tools/leak-check.py classify "$f" | cut -d' ' -f1)" = licensed ] ||
		{ echo "!! a free source, published by CI, not from here: $f" >&2; bad=1; } ;;
	*) echo "!! not a .deb or .dsc: $f" >&2; bad=1 ;;
	esac
	case ${f##*/} in *~dev*) echo "!! a dev build (commit, rebuild): $f" >&2; bad=1 ;; esac
done
[ -z "$bad" ] || exit 1
echo ">> ${#files[@]} files for the licensed suite:"; printf '   %s\n' "${files[@]##*/}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# Debian's index at the pinned snapshot, for the name check (kept in build/downloads/debian-index, checked every run)
[ -n "${DEBIAN_INDEX_URL:-}" ] && [ -n "${BUILDER_TAG:-}" ] ||
	{ echo "!! DEBIAN_INDEX_URL and BUILDER_TAG are unset: run it as make publish" >&2; exit 1; }
docker image inspect "$BUILDER_TAG" >/dev/null 2>&1 ||
	{ echo "!! no builder image $BUILDER_TAG (make builder): its debian-ports keyring checks Debian's index" >&2; exit 1; }
docker run --rm --network=none "$BUILDER_TAG" cat /usr/share/keyrings/debian-ports-archive-keyring.gpg > "$tmp/debian-ports.gpg"
debian_index=$(python3 -I apt-repo/licensed-names.py debian --url "$DEBIAN_INDEX_URL" --keyring "$tmp/debian-ports.gpg" \
	--cache build/downloads/debian-index)
apt-repo/licensed-auth.sh "$tmp/auth"
export WALNUT_APT_AUTH_FILE=$tmp/auth
if [ -n "${WALNUT_SIGN_KEY_FPR:-}" ]; then
	fpr=$WALNUT_SIGN_KEY_FPR
else
	# pass decrypts with the caller's own keyring: the throwaway one is set only for the import and after
	mkdir -m 700 "$tmp/gnupg"
	pass show Projects/Walnut/ArchiveKey | GNUPGHOME=$tmp/gnupg gpg --batch --quiet --import
	export GNUPGHOME=$tmp/gnupg
	fpr=$(gpg --with-colons --list-secret-keys | awk -F: '$1=="fpr" {print $10; exit}')
	[ -n "$fpr" ] || { echo "!! pass Projects/Walnut/ArchiveKey gave no secret key" >&2; exit 1; }
fi
# the archive key has a passphrase (a keyring of yours: WALNUT_SIGN_PASSPHRASE_FILE if its key has one)
if [ -z "${WALNUT_SIGN_PASSPHRASE_FILE:-}" ] && [ -z "${WALNUT_SIGN_KEY_FPR:-}" ]; then
	install -m 600 /dev/null "$tmp/passphrase"
	read -r -s -p "The archive key's passphrase: " pp < /dev/tty; echo
	printf '%s' "$pp" > "$tmp/passphrase"; unset pp
	export WALNUT_SIGN_PASSPHRASE_FILE=$tmp/passphrase
fi

# "$fpr!" makes gpg sign with exactly the primary key: without the "!" it signs with the newest signing subkey whose secret
# it holds, and the server (walnut-watch's pins) accepts the licensed suite from the primary key's fingerprint only
for attempt in 1 2 3 4 5; do
	b=build/tmp/licensed-bundle
	apt-repo/walnut-repo.py --url "$URL" add --suite licensed --keyring "$RING" --sign-key "$fpr!" --strict \
		--out "$b" "${files[@]}"
	[ ! -d "$b/dists/licensed-debug" ] || { echo "!! debug symbols have no place in the licensed suite" >&2; exit 1; }
	# what the bundle adds may take no name of Debian's or ours (walnut-watch can't tell a Debian name from a new one)
	python3 -I apt-repo/licensed-names.py check --bundle "$b" --debian "$debian_index" --lists github-ci/packages.toml \
		--allow apt-repo/licensed-names-allow.txt --ours-suite stable="$RING" --ours-suite testing=apt-repo/keyrings/testing-keyring.gpg
	if [ "${DRY_RUN:-0}" = 1 ]; then echo ">> DRY_RUN: the bundle is in $b, not uploaded"; exit 0; fi
	id=$(date -u +%Y%m%d-%H%M%S)-licensed-$attempt
	servers/walnut.nanobyte.dev/walnut-upload.sh licensed "$id" "$b"
	verdict=
	for _ in $(seq 100); do
		sleep 3
		verdict=$(curl -fsS --retry 2 "$STATUS/licensed__$id" 2>/dev/null) && break
	done
	case "${verdict%%[$' \n']*}" in
	ok) echo ">> published to the licensed suite ($id)"; rm -rf "$b"; exit 0 ;;
	stale) echo "-- stale: another publish landed first; again on the new state"; sleep 5 ;;
	refused) printf '!! the server refused %s:\n%s\n' "$id" "$verdict" >&2; exit 1 ;;
	*) echo "!! no verdict for $id after 5 minutes (journalctl -u walnut-watch on the server)" >&2; exit 1 ;;
	esac
done
echo "!! still stale after 5 attempts" >&2
exit 1
}
