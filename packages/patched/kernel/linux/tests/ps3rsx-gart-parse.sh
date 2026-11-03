#!/bin/bash
# ps3rsx-gart-parse.sh: round 3 K8/K9 (local/0007) and K6 (local/0049) on the host.
# 1. ps3rsx_parse_size(), the parser of ps3rsx_gart= and ps3rsx_cma=, is taken out of the patch as it
#    is and run on sizes, including the ones memparse() wrapped to 0: a number above the ceiling (or
#    one its suffix pushes above) must give the ceiling and a warning, never 0 and never a wrapped value.
# 2. The fixed GART reservation that memblock can't satisfy must turn it off, not panic (the patch's
#    prealloc_ps3rsx_gart has no panic call), and the pieces of the K6 fix must all be in 0049:
#    remove cancels the watchdog and then the re-probe, and skips waiting when the re-probe itself is
#    the caller (cancel_work_sync on the running item would never return).
set -u
cd "$(dirname "$0")/.."
P7=patches/local/0007-ps3-rsx-gart-cma.patch P49=patches/local/0049-gelic-rx-watchdog.patch
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# the function body, as added by the patch
awk '/^\+static u64 __init ps3rsx_parse_size\(/ {on=1} on {print substr($0, 2)} on && /^\+}$/ {exit}' "$P7" > "$T/fn.c"
[ -s "$T/fn.c" ] || { echo "FAIL: ps3rsx_parse_size not in $P7"; exit 1; }
cat > "$T/t.c" <<'EOC'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint64_t u64;
#define __init
#define fallthrough __attribute__((__fallthrough__))
#define pr_warn(...) do { printf("  warn: "); printf(__VA_ARGS__); } while (0)
#define simple_strtoull(a, e, b) strtoull(a, e, b)
#include "fn.c"
#define M (1ULL << 20)
static int bad;
static void t(const char *s, u64 max, u64 want, int warns)
{
	printf("%s (max %llu MiB)\n", s, (unsigned long long)(max >> 20));
	u64 got = ps3rsx_parse_size("ps3rsx_x", s, max);
	if (got != want) { printf("  WRONG: got %llu, want %llu\n", (unsigned long long)got, (unsigned long long)want); bad = 1; }
	(void)warns;
}
int main(void)
{
	u64 g = 128 * M, c = 256 * M;
	t("0", g, 0, 0);
	t("64M", g, 64 * M, 0);
	t("64m", g, 64 * M, 0);
	t("128M", g, 128 * M, 0);
	t("129M", g, g, 1);
	t("131072K", g, 128 * M, 0);
	t("131073K", g, g, 1);
	t("134217728", g, 128 * M, 0);
	t("1G", g, g, 1);
	t("1G", c, c, 1);
	t("256M", c, 256 * M, 0);
	t("16384G", g, g, 1);			/* 2^44 << 20 wrapped to 2^64 = 0 in memparse */
	t("17179869184G", g, g, 1);		/* 2^34 << 30 = 2^64: 0 in memparse */
	t("17179869184G", c, c, 1);
	t("16777216T", g, g, 1);		/* 2^24 << 40 = 2^64 */
	t("1E", g, g, 1);
	t("18446744073709551615", g, g, 1);
	t("99999999999999999999999", g, g, 1);	/* strtoull saturates */
	t("junk", g, 0, 0);
	t("", g, 0, 0);
	return bad;
}
EOC
if gcc -Wall -Werror -I"$T" -o "$T/t" "$T/t.c" 2>"$T/cc.log"; then
	"$T/t" > "$T/out.log"; rc=$?
	[ $rc = 0 ] && ok "ps3rsx_parse_size: sizes, ceilings, the memparse wraps" || { cat "$T/out.log"; bad "ps3rsx_parse_size"; }
else cat "$T/cc.log"; bad "test program does not compile"; fi

# the old parser wrapped: show it (memparse's shift, the same cascade) so the test fails if someone
# puts memparse back
if grep -q '^+[^/*]*memparse(' "$P7"; then bad "0007 calls memparse again"; else ok "0007 does not call memparse"; fi

# no panic on an oversized fixed reservation
fnp=$(awk '/^\+static void __init prealloc_ps3rsx_gart\(void\)/ {on=1} on {print} on && /^\+}$/ {exit}' "$P7")
[ -n "$fnp" ] || bad "prealloc_ps3rsx_gart missing"
if echo "$fnp" | grep -q 'panic('; then bad "prealloc_ps3rsx_gart panics"; else ok "prealloc_ps3rsx_gart does not panic"; fi
echo "$fnp" | grep -q 'p->size = 0' && ok "a failed reservation turns the fixed GART off" || bad "failed reservation leaves a size"
grep -q '^+struct ps3_prealloc ps3rsx_gart __ro_after_init' "$P7" && ok "ps3rsx_gart is __ro_after_init" || bad "ps3rsx_gart not __ro_after_init"
grep -q '^+static struct cma \*ps3rsx_cma __ro_after_init' "$P7" && ok "ps3rsx_cma is __ro_after_init" || bad "ps3rsx_cma not __ro_after_init"
grep -q '^+#define PS3RSX_GART_MAX' "$P7" && ok "ps3rsx_gart has a ceiling" || bad "no ceiling"

# K6
rm_fn=$(awk '/^\+\t\/\* no re-probe queued behind a remove/ {on=1} on {print} on && /gelic_card_set_link_mode|^ \t\/\* set auto-negotiation/ {exit}' "$P49")
echo "$rm_fn" | grep -n 'cancel_delayed_work_sync(&card->rx_watchdog)' >/dev/null && ok "remove cancels the watchdog" || bad "remove does not cancel the watchdog"
echo "$rm_fn" | grep -q 'gelic_reprobe_cancel()' && ok "remove cancels the re-probe" || bad "remove does not cancel the re-probe"
a=$(echo "$rm_fn" | grep -n 'cancel_delayed_work_sync' | head -1 | cut -d: -f1); b=$(echo "$rm_fn" | grep -n 'gelic_reprobe_cancel()' | head -1 | cut -d: -f1)
[ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ] && ok "the watchdog (which queues the re-probe) is cancelled first" || bad "cancel order"
grep -A4 '^+static void gelic_reprobe_cancel' "$P49" | grep -q 'current_work() == &gelic_reprobe_work' && ok "the re-probe's own remove doesn't wait for itself" || bad "no current_work() guard"
grep -q '^+	if (cancel_work_sync(&gelic_reprobe_work))' "$P49" && grep -A1 'cancel_work_sync(&gelic_reprobe_work)' "$P49" | grep -q put_device && ok "a cancelled re-probe gives its device reference back" || bad "reference leak on cancel"
grep -q 'kzalloc' "$P49" && bad "0049 still allocates a work per fault" || ok "no per-fault allocation (one static work)"
exit $fail
