#!/bin/bash
# config-check.sh: round 3 Q17. tools/linux-config-check.sh (run by tools/linux-config.sh on every
# flavour's finished .config) on fake configs: it passes a good one, and fails when a fragment
# option didn't stick, when a listed hardening option dropped out (even one no fragment mentions),
# when a must-stay-off driver is on, and when the research drivers are in the release kernel or
# missing from the debug one. Also: the real package.conf's lists name the options the plan requires.
# shellcheck disable=SC2154  # the REQUIRE_* lists come from package.conf
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
CHK=$PWD/tools/linux-config-check.sh REALPC=$PWD/packages/patched/kernel/linux/package.conf
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
cat > "$T/package.conf" <<'EOC'
REQUIRE_SET="CONFIG_STRICT_DEVMEM=y CONFIG_FB_PS3=y CONFIG_PS3_VRAM=y|m"
REQUIRE_UNSET="CONFIG_PS3_FLASH CONFIG_PS3_ENCDEC"
REQUIRE_UNSET_release="CONFIG_PS3_LV1CALL"
REQUIRE_SET_debug="CONFIG_PS3_LV1CALL=m"
EOC
cat > "$T/frag.config" <<'EOC'
CONFIG_FB_PS3=y
CONFIG_PS3_VRAM=m
# CONFIG_PS3_FLASH is not set
CONFIG_PS3_LV1CALL=y
# CONFIG_DEBUG_LOCKDEP is not set
EOC
cat > "$T/frag2.config" <<'EOC'
# CONFIG_PS3_LV1CALL is not set
EOC
good() {	# the release .config
	cat <<'EOC'
CONFIG_STRICT_DEVMEM=y
CONFIG_FB_PS3=y
CONFIG_PS3_VRAM=m
# CONFIG_PS3_FLASH is not set
# CONFIG_PS3_LV1CALL is not set
EOC
}
run() {	# name expected-rc flavour config-file [fragments]
	local name=$1 want=$2 fl=$3 cfg=$4; shift 4
	PKGCONF=$T/package.conf "$CHK" "$fl" "$cfg" "$@" >"$T/out" 2>&1; local rc=$?
	if { [ $want = 0 ] && [ $rc = 0 ]; } || { [ $want = 1 ] && [ $rc != 0 ]; }; then echo "ok: $name"
	else echo "FAIL: $name (rc $rc, wanted $want)"; sed 's/^/    /' "$T/out"; fail=1; fi
}
good > "$T/good.config"
run "a good release config" 0 release "$T/good.config" "$T/frag.config" "$T/frag2.config"

# a fragment option that didn't stick
grep -v FB_PS3 "$T/good.config" > "$T/c"; echo "# CONFIG_FB_PS3 is not set" >> "$T/c"
run "a fragment option (=y) turned off" 1 release "$T/c" "$T/frag.config" "$T/frag2.config"
sed 's/PS3_VRAM=m/PS3_VRAM=y/' "$T/good.config" > "$T/c"
run "a fragment option with another value" 1 release "$T/c" "$T/frag.config" "$T/frag2.config"
# a 'not set' line met by absence, and failed by the option being on
grep -v "PS3_FLASH" "$T/good.config" > "$T/c"
run "an 'is not set' option absent (dependency off) is fine" 0 release "$T/c" "$T/frag.config" "$T/frag2.config"
sed 's/# CONFIG_PS3_FLASH is not set/CONFIG_PS3_FLASH=m/' "$T/good.config" > "$T/c"
run "a must-stay-off driver on" 1 release "$T/c" "$T/frag.config" "$T/frag2.config"
# the later fragment decides
run "a later fragment overrides an earlier one" 0 release "$T/good.config" "$T/frag.config" "$T/frag2.config"
run "the same without the override: LV1CALL=y in a fragment must stick (it is off)" 1 release "$T/good.config" "$T/frag.config"

# listed options, whatever the fragments say
grep -v STRICT_DEVMEM "$T/good.config" > "$T/c"
run "a hardening option dropped, no fragment mentions it" 1 release "$T/c" "$T/frag2.config"
sed 's/CONFIG_STRICT_DEVMEM=y/# CONFIG_STRICT_DEVMEM is not set/' "$T/good.config" > "$T/c"
run "a hardening option turned off" 1 release "$T/c" "$T/frag2.config"
sed 's/PS3_VRAM=m/PS3_VRAM=y/' "$T/good.config" > "$T/c"
run "an =y|m option as y" 0 release "$T/c" "$T/frag2.config"
sed 's/PS3_VRAM=m/# CONFIG_PS3_VRAM is not set/' "$T/good.config" > "$T/c"
run "an =y|m option off" 1 release "$T/c" "$T/frag2.config"
sed 's/# CONFIG_PS3_LV1CALL is not set/CONFIG_PS3_LV1CALL=m/' "$T/good.config" > "$T/c"
run "research drivers in the release kernel" 1 release "$T/c" "$T/frag2.config"
echo "CONFIG_PS3_LV1CALL=m" > "$T/frag3.config"
run "research drivers in the debug kernel" 0 debug "$T/c" "$T/frag3.config"
run "research drivers missing from the debug kernel" 1 debug "$T/good.config" "$T/frag3.config"
echo "CONFIG_PS3_ENCDEC=m" >> "$T/c"
run "a must-stay-off driver on (common list), debug kernel" 1 debug "$T/c" "$T/frag3.config"

# the real package.conf names what the plan requires
# shellcheck source=/dev/null
. "$REALPC"
need_unset="CONFIG_PS3_FLASH CONFIG_PS3_FLASH_NG CONFIG_PS3_NOR_FLASH CONFIG_PS3_STRGMNGR CONFIG_PS3_ENCDEC CONFIG_SPUISO_FS CONFIG_SPULDR_FS CONFIG_PS3_REPOSITORY_WRITE CONFIG_PS3_DISP_MANAGER"
for o in $need_unset; do case " $REQUIRE_UNSET " in *" $o "*) ;; *) echo "FAIL: package.conf REQUIRE_UNSET lacks $o"; fail=1 ;; esac; done
for o in CONFIG_PS3_LV1CALL CONFIG_PS3_PHYSMEM; do
	case " $REQUIRE_UNSET_release " in *" $o "*) ;; *) echo "FAIL: REQUIRE_UNSET_release lacks $o"; fail=1 ;; esac
	case " $REQUIRE_SET_debug " in *" $o=m "*) ;; *) echo "FAIL: REQUIRE_SET_debug lacks $o=m"; fail=1 ;; esac
done
# H1: the dispatcher manager (a bus alias: udev loads it at boot) is off in every flavour
case " $REQUIRE_SET_debug $REQUIRE_SET " in *CONFIG_PS3_DISP_MANAGER*) echo "FAIL: a REQUIRE_SET list wants CONFIG_PS3_DISP_MANAGER"; fail=1 ;; esac
grep -qx "CONFIG_PS3_DISP_MANAGER=m" "$(dirname "$REALPC")"/config/*.config && { echo "FAIL: a config fragment builds CONFIG_PS3_DISP_MANAGER"; fail=1; }
[ -n "$REQUIRE_SET" ] || { echo "FAIL: no REQUIRE_SET (hardening options)"; fail=1; }
[ $fail = 0 ] && echo "ok: package.conf lists"
exit $fail
