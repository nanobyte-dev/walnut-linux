#!/bin/bash
# config-get.sh VAR...: prints each setting from config.mk (with config.local.mk, the developer's
# LAN), one per line, for the scripts the Makefile doesn't start. A variable set in the
# environment wins.
set -euo pipefail
cd "$(dirname "$0")/.."
for v in "$@"; do
	if [ -n "${!v:-}" ]; then
		echo "${!v}"
	else
		make -s --no-print-directory -f config.mk --eval "config-get-print: ; @echo '\$($v)'" config-get-print
	fi
done
