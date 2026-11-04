#!/bin/bash
# linux-config-check.sh <flavour> <.config> <fragment>...: the checks on a kernel flavour's finished
# .config (tools/linux-config.sh runs it; tests: packages/patched/kernel/linux/tests/config-check.sh).
# Fails (exit 1, every failure printed with !!) when
#  - an option the fragments set did not stick (the last fragment's line for it decides): "=v" lines must be
#    in the .config exactly; "is not set" lines are met by that line or by the option being absent;
#  - an option of the package.conf lists is wrong, whatever the fragments say:
#      REQUIRE_SET[_<flavour>]    "CONFIG_X=y" (or "=y|m"): must be in the .config,
#      REQUIRE_UNSET[_<flavour>]  "CONFIG_X": must be "is not set" or absent
#    so deleting a listed option from the fragments, or a dependency change dropping it, stops the build.
# Env: PKGCONF (default packages/patched/kernel/linux/package.conf).
set -euo pipefail
{
FLAVOUR=$1 CONFIG=$2; shift 2
# shellcheck source=/dev/null
. "${PKGCONF:-packages/patched/kernel/linux/package.conf}"
fail=0
bad() { echo "!! $FLAVOUR: $*" >&2; fail=1; }
state() {	# the .config's line for a symbol, or "absent"
	grep -E "^$1=|^# $1 is not set\$" "$CONFIG" || echo absent
}
# 1. the fragments' options (a later fragment overrides an earlier one)
while read -r line; do
	sym=$(echo "$line" | grep -oE 'CONFIG_[A-Za-z0-9_]+' | head -1)
	now=$(state "$sym")
	case $line in
	"# $sym is not set") [ "$now" = absent ] || [ "$now" = "$line" ] || bad "not applied: $line (is: $now)" ;;
	*) [ "$now" = "$line" ] || bad "not applied: $line (is: $now)" ;;
	esac
done < <(cat "$@" | sed -nE 's/^(CONFIG_[A-Za-z0-9_]+)=.*/\1 &/p; s/^# (CONFIG_[A-Za-z0-9_]+) is not set$/\1 &/p' \
	| awk '{k=$1; sub(/^[^ ]+ /, ""); v[k]=$0; if (!(k in o)) o[k]=n++} END {for (k in o) print v[k]}')
# 2. the lists
fv=${FLAVOUR//-/_}
for v in REQUIRE_SET REQUIRE_SET_$fv; do
	for want in ${!v:-}; do
		sym=${want%%=*} vals=${want#*=}
		now=$(state "$sym")
		ok=0
		IFS='|' read -ra alts <<< "$vals"
		for a in "${alts[@]}"; do [ "$now" = "$sym=$a" ] && ok=1; done
		[ $ok = 1 ] || bad "$v: $want is required (is: $now)"
	done
done
for v in REQUIRE_UNSET REQUIRE_UNSET_$fv; do
	for sym in ${!v:-}; do
		now=$(state "$sym")
		[ "$now" = absent ] || [ "$now" = "# $sym is not set" ] || bad "$v: $sym must stay off (is: $now)"
	done
done
exit $fail
}
