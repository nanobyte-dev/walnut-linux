#!/bin/bash
# cache-mac.sh record|verify PATH MACFILE: authenticates what an Actions cache restored. An Actions cache entry
# can be created by any job of the repository (the runtime token every job holds writes caches), so a trusted
# job trusts the content only when it matches a MAC made with the secret salt WALNUT_CACHE_SALT, which an
# untrusted job never has.
#   record   MACFILE := HMAC-SHA256(salt, sha256 of PATH's content); PATH is a file or a directory
#   verify   recomputes it; on a mismatch (or a missing MACFILE) PATH and MACFILE are removed and the
#            exit status is 3: the caller builds fresh. Nothing is restored on trust.
# Env: SALT (the secret). A directory's content is its tar in name order, owners and times zeroed.
set -euo pipefail
mode=$1 path=$2 mac=$3
: "${SALT:?no salt}"
content_hash() {
	if [ -d "$path" ]; then
		tar -C "$(dirname "$path")" --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 -cf - "$(basename "$path")" | sha256sum | cut -d' ' -f1
	else
		sha256sum < "$path" | cut -d' ' -f1
	fi
}
compute() { content_hash | openssl dgst -sha256 -hmac "$SALT" -r | cut -d' ' -f1; }
case $mode in
	record)
		[ -e "$path" ] || exit 0
		compute > "$mac"
		;;
	verify)
		[ -e "$path" ] || exit 0
		if [ -f "$mac" ] && [ "$(cat "$mac")" = "$(compute)" ]; then
			echo "-- $path: matches its MAC"
		else
			echo "!! $path: restored from the cache but not authentic: ignored, rebuilt" >&2
			rm -rf "$path" "$mac"
			exit 3
		fi
		;;
	*) echo "usage: $0 record|verify PATH MACFILE" >&2; exit 2 ;;
esac
