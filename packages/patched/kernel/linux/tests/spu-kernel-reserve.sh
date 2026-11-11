#!/bin/bash
# spu-kernel-reserve.sh: local/0058 (spu_kernel_reserve, spu_kernel_reset, spu_kernel_ls_decode,
# spu_kernel_release, the "reserved" sysfs attribute, the reboot notifier) on the host.
#  1. The patch: only spu_base.c and asm/spu.h; no hypervisor call added (no lv1_ in an added line, and
#     the block calls only the priv1 operations and spu_base helpers on its list); the four functions
#     exported GPL and nothing non-GPL; nothing in the block allocates or loads firmware;
#     no callback is ever set to NULL; init_spu_base calls spu_kernel_init; the series carries it.
#  2. The config: CONFIG_SPUISO_FS and CONFIG_SPULDR_FS stay unset (package.conf's REQUIRE_UNSET names
#     both, no fragment turns them on, and tools/linux-config-check.sh refuses a config that has them).
#  3. The block between the patch's "SPEs reserved for a kernel driver" markers, taken from the patch as
#     it is, compiled with spu-kernel-reserve-test.c (a model of six SPEs, PS3's priv1 operations,
#     spu_base's interrupt handlers and spufs's list walks): plain with -Werror, then ASan/UBSan.
#  4. Fix-offs: one of the reserve rules turned off at a time in a copy of the block (a literal edit
#     that must match exactly once); each build must fail with the FAIL line given for it.
# Output in build/tmp/tests/spu-kernel-reserve/.
# shellcheck disable=SC2015,SC2154  # ok and bad always return 0; the REQUIRE_* lists come from package.conf
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
KD=packages/patched/kernel/linux
P=$KD/patches/local/0058-spu-kernel-reserve.patch
TEST=$PWD/$KD/tests/spu-kernel-reserve-test.c
O=$PWD/build/tmp/tests/spu-kernel-reserve
command -v gcc >/dev/null || { echo "skip: no gcc"; exit 77; }
rm -rf "$O"; mkdir -p "$O"
fail=0 nok=0 noff=0
ok() { echo "ok: $1"; nok=$((nok + 1)); }
bad() { echo "FAIL: $1"; fail=1; }

# --- 1. the patch ---------------------------------------------------------------------------------
[ -f "$P" ] || { echo "FAIL: no $P"; exit 1; }
files=$(sed -n 's|^diff --git a/\([^ ]*\) b/.*|\1|p' "$P" | sort -u | tr '\n' ' ')
[ "$files" = "arch/powerpc/include/asm/spu.h arch/powerpc/platforms/cell/spu_base.c " ] \
	&& ok "the patch touches only asm/spu.h and spu_base.c" || bad "the patch touches: $files"
if grep -qE '^\+.*\blv1_' "$P"; then bad "an added line calls lv1: $(grep -E '^\+.*\blv1_' "$P" | head -1)"
else ok "no hypervisor call added"; fi
for f in spu_kernel_reserve spu_kernel_reset spu_kernel_ls_decode spu_kernel_release; do
	grep -qx "+EXPORT_SYMBOL_GPL($f);" "$P" || bad "$f not exported GPL"
done
grep -qE '^\+EXPORT_SYMBOL\(' "$P" && bad "a non-GPL export" || ok "the four functions exported GPL, nothing else"
grep -qx '+	spu_kernel_init();' "$P" && ok "init_spu_base calls spu_kernel_init" || bad "spu_kernel_init not called"
grep -qx 'local/0058-spu-kernel-reserve.patch' $KD/patches/series && ok "the series carries 0058" || bad "0058 not in the series"

# the two parts, as the patch adds them
awk -v want=arch/powerpc/platforms/cell/spu_base.c '
	/^diff --git / { file = $3; sub(/^a\//, "", file); next }
	file == want && /^\+\/\* --- SPEs reserved for a kernel driver --- \*\/$/ { on = 1 }
	on { print substr($0, 2) }
	on && /^\+\/\* --- end of SPEs reserved for a kernel driver --- \*\/$/ { on = 0 }' "$P" > "$O/block.c"
awk -v want=arch/powerpc/include/asm/spu.h '
	/^diff --git / { file = $3; sub(/^a\//, "", file); next }
	file == want && /^\+\/\* --- SPEs reserved for a kernel driver \(spu_base.c\) --- \*\/$/ { on = 1 }
	on { print substr($0, 2) }
	on && /^\+\/\* --- end of SPEs reserved for a kernel driver --- \*\/$/ { on = 0 }' "$P" > "$O/spu-h.h"
[ -s "$O/block.c" ] && [ -s "$O/spu-h.h" ] || { echo "FAIL: the marked parts are not in $P"; exit 1; }
grep -q 'end of SPEs reserved' "$O/block.c" || { echo "FAIL: the block's end marker is missing"; exit 1; }
if grep -nE '\b(kmalloc|kzalloc|kcalloc|vmalloc|kvmalloc|devm_[a-z]+|alloc_pages?|__get_free_pages?|request_firmware[a-z_]*)\(|GFP_' "$O/block.c"; then
	bad "the block allocates or loads firmware (the reset path must not)"
else ok "nothing in the block allocates or loads firmware"; fi
if grep -nE '_callback = NULL' "$O/block.c"; then bad "a callback set to NULL"
else ok "no callback is ever set to NULL"; fi
# the spu_* calls the block makes outside itself: only these (the priv1 operations spufs uses, and
# spu_base.c's own helpers); a new one must be added here on purpose
allowed=" spu_add_dev_attr spu_associate_mm spu_init_channels spu_int_mask_set spu_int_stat_clear spu_invalidate_slbs spu_mfc_sr1_get spu_mfc_sr1_set spu_mfc_tclass_id_set "
defined=$(sed -nE 's/^(static )?[a-z_ ]+[ *]+(spu_[a-z0-9_]+)\(.*/\2/p' "$O/block.c" | sort -u | tr '\n' ' ')
other=
for f in $(grep -oE '\bspu_[a-z0-9_]+\(' "$O/block.c" | tr -d '(' | sort -u); do
	case " $defined " in *" $f "*) continue ;; esac
	case "$allowed" in *" $f "*) ;; *) other="$other $f" ;; esac
done
[ -z "$other" ] && ok "the block calls only the listed priv1 operations and spu_base helpers" || bad "calls not on the list:$other"
if grep -nE 'spu_priv1_ops|spu_management_ops' "$O/block.c"; then bad "the block uses the ops tables directly"; fi

# --- 2. the config --------------------------------------------------------------------------------
# shellcheck source=/dev/null
. $KD/package.conf
for o in CONFIG_SPUISO_FS CONFIG_SPULDR_FS; do
	case " $REQUIRE_UNSET " in *" $o "*) ok "REQUIRE_UNSET names $o" ;; *) bad "REQUIRE_UNSET lacks $o" ;; esac
	if grep -qE "^$o=(y|m)" $KD/config/*.config; then bad "a config fragment turns $o on"; else ok "no fragment turns $o on"; fi
done
{	# a config that meets every REQUIRE_SET list, then each isolated-SPU option turned on
	for o in $REQUIRE_SET $REQUIRE_SET_release; do echo "${o%%|*}"; done
} > "$O/good.config"
: > "$O/empty.config"	# no fragment (the check reads stdin without one)
PKGCONF=$PWD/$KD/package.conf tools/linux-config-check.sh release "$O/good.config" "$O/empty.config" </dev/null >"$O/cfg.out" 2>&1 \
	&& ok "linux-config-check passes a config without them" || { bad "linux-config-check refuses the good config"; sed 's/^/    /' "$O/cfg.out"; }
for o in CONFIG_SPUISO_FS=y CONFIG_SPULDR_FS=m; do
	{ cat "$O/good.config"; echo "$o"; } > "$O/bad.config"
	if PKGCONF=$PWD/$KD/package.conf tools/linux-config-check.sh release "$O/bad.config" "$O/empty.config" </dev/null >"$O/cfg.out" 2>&1; then
		bad "linux-config-check passes a config with $o"
	else ok "linux-config-check refuses $o"; fi
done
# a built kernel's config, if one is given (LINUX_CONFIGS: paths, e.g. a deb's /boot/config-*)
for c in ${LINUX_CONFIGS:-}; do
	if grep -qE '^CONFIG_SPU(ISO|LDR)_FS=(y|m)' "$c"; then bad "$c has an isolated-SPU file system on"
	else ok "$c: SPUISO_FS and SPULDR_FS unset"; fi
done

# --- 3. the model ---------------------------------------------------------------------------------
CF=(-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I "$O")
if gcc "${CF[@]}" -Werror -o "$O/test" "$TEST" 2>"$O/cc.log"; then
	if "$O/test" > "$O/run.log"; then ok "the model: $(tail -1 "$O/run.log" | sed 's/^ok: //')"
	else sed 's/^/    /' "$O/run.log"; bad "the model"; fi
else sed 's/^/    /' "$O/cc.log"; bad "the model does not compile"; fi
if echo 'int main(void){return 0;}' | gcc -fsanitize=address,undefined -x c -o "$O/probe" - 2>/dev/null; then
	if gcc "${CF[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all -o "$O/test-san" "$TEST" 2>"$O/cc-san.log" &&
	   "$O/test-san" > "$O/run-san.log" 2>&1; then ok "the model under ASan/UBSan"
	else sed 's/^/    /' "$O/cc-san.log" "$O/run-san.log"; bad "the model under ASan/UBSan"; fi
else echo "SKIP ${TEST_SUITE:-spu-kernel-reserve}:sanitizers gcc has no ASan/UBSan here"; fi

# --- 4. the fix-offs ------------------------------------------------------------------------------
# off NAME 'FAIL line it must print' 'FROM' 'TO' ['FROM' 'TO']...
off() {
	local name=$1 expect=$2 d=$O/off-$1
	shift 2
	mkdir -p "$d"; cp "$O/spu-h.h" "$d/"; cp "$O/block.c" "$d/block.c"
	while [ $# -gt 0 ]; do
		FROM=$1 TO=$2 NAME=$name perl -0777 -i -pe \
			'my $n = () = /\Q$ENV{FROM}\E/g;
			 $n == 1 or die "spu-kernel-reserve.sh: fix-off $ENV{NAME}: found $n times: $ENV{FROM}\n";
			 s/\Q$ENV{FROM}\E/$ENV{TO}/' "$d/block.c" || { bad "fix-off $name: the edit doesn't apply"; return; }
		shift 2
	done
	noff=$((noff + 1))
	if ! gcc -std=gnu11 -O1 -w -I "$d" -o "$d/test" "$TEST" 2>"$d/cc.log"; then
		sed 's/^/    /' "$d/cc.log"; bad "fix-off $name does not compile"; return
	fi
	if timeout 60 "$d/test" > "$d/run.log" 2>&1; then bad "fix-off $name: the model passes with the rule off"
	elif grep -qF -- "$expect" "$d/run.log"; then ok "fix-off $name fails as expected"
	else bad "fix-off $name failed, but without: $expect"; sed 's/^/    /' "$d/run.log" | head -5; fi
}
T=$'\t' N=$'\n'
# taking the SPE off spufs's list
off list-del "FAIL: the reserved SPE is still on the node list" \
	"$T$T${T}list_del_rcu(&s->cbe_list);$N" ''
off list-del-init "spusched_thread's walk doesn't end" \
	"$T$T${T}list_del_rcu(&s->cbe_list);" "$T$T${T}list_del_init(&s->cbe_list);"
off spu-used "the affinity path (ctx_location, then the alloc_state test) takes the reserved SPE" \
	"$T$T${T}s->alloc_state = SPU_USED;$N" ''
off take-unbound-used "spu5, kept SPU_USED by find_victim" \
	's->alloc_state != SPU_FREE || s->ctx ||' 's->ctx ||'
off take-with-context "spu4, which has a context" \
	's->alloc_state != SPU_FREE || s->ctx ||' 's->alloc_state != SPU_FREE ||'
off take-isolated "spu3, isolated" \
	'(in_be32(&s->problem->spu_status_R) & SPU_KERNEL_ISOLATED))' '0)'
# the callbacks
off callbacks-before-reset "FAIL: a driver callback ran during reserve" \
	"${T}spu_kernel_mask_all(spu);$N${T}spu_kernel_set_callbacks(spu, NULL);$N${T}ret = __spu_kernel_reset(spu);" \
	"${T}spu_kernel_set_callbacks(spu, ops);$N${T}spu_kernel_mask_all(spu);$N${T}ret = __spu_kernel_reset(spu);"
off no-callbacks "FAIL: the driver's callbacks are not installed" \
	"${T}spu_kernel_set_callbacks(spu, ops);$N" ''
off release-null-callbacks "FAIL: release left a callback that isn't spu_base's stub" \
	"$T${T}spu->stop_callback = spu_kernel_stub_stop;" "$T${T}spu->stop_callback = (void *)0;"
off release-keeps-driver-callbacks "FAIL: a late interrupt after release reached the driver's callback" \
	"${T}spu_kernel_set_callbacks(spu, NULL);$N${T}spu_kernel_clear_ls(spu);$N" "${T}spu_kernel_clear_ls(spu);$N"
off bury-keeps-driver-callbacks "a callback that isn't spu_base's stub" \
	"${T}spu_kernel_grace(spu);$N${T}spu_kernel_set_callbacks(spu, NULL);$N${T}spu_kernel_stop_hard(spu);" \
	"${T}spu_kernel_grace(spu);$N${T}spu_kernel_stop_hard(spu);"
# the grace periods
off no-grace-reserve "FAIL: reserve: no grace period" \
	"${T}spu_kernel_grace(spu);$T$T/* nothing of spufs's last context reaches the driver */" "${T}spu_kernel_mask_all(spu);"
off no-grace-release "FAIL: release: no grace period" \
	"${T}spu_kernel_grace(spu);$T$T/* nothing of the driver's reaches the next context */" "${T}spu_kernel_mask_all(spu);"
off grace-no-sleep "no grace period when given up" \
	"${T}msleep(SPU_KERNEL_GRACE_MS);$N" ''
# the reset
off sr1-not-written "FAIL: SR1 is 0x3b, not 0x33" \
	"if (spu_kernel_sr1(spu, true))$N$T${T}return -ENXIO;" "if (0)$N$T${T}return -ENXIO;"
off sr1-unchecked "SR1 0x13: an SR1 write ps3/spu.c BUG()s on" \
	"${T}if (sr1 != want)$N$T${T}return -EIO;$N" "${T}sr1 = want;$N"
off sr1-buries-quiet "FAIL: SR1 0x13, quiet: the SPE isn't given back to spufs" \
	'if (ret == -ENXIO) {' 'if (0) {'
off no-tclass "FAIL: TCLASS" \
	"${T}spu_mfc_tclass_id_set(spu, 0x10000000);$N" ''
off no-slb-invalidate "FAIL: the SLBs were not invalidated" \
	"${T}spu_associate_mm(spu, NULL);$N${T}spu_invalidate_slbs(spu);$N" "${T}spu_associate_mm(spu, NULL);$N"
off no-purge "FAIL: reserve failed" \
	"${T}out_be64(&priv2->mfc_control_RW, MFC_CNTL_PURGE_DMA_REQUEST | MFC_CNTL_SUSPEND_MASK);$N${T}eieio();$N${T}if (!spu_kernel_poll(" \
	"${T}if (!spu_kernel_poll("
off no-mailbox-drain "outbound mailboxes full" \
	"${T}for (i = 0; i < 4 && (in_be32(&prob->mb_stat_R) & 0xff); i++)$N$T${T}in_be32(&prob->pu_mb_R);$N" '' \
	"${T}for (i = 0; i < 4 && (in_be32(&prob->mb_stat_R) & 0xff0000); i++)$N$T${T}in_be64(&priv2->puint_mb_R);$N" ''
off no-status-clear "FAIL: reset left interrupt status" \
	"${T}for (i = 0; i < 3; i++)$N$T${T}spu_int_stat_clear(spu, i, stat[i]);$N" ''
off reset-allocates "FAIL: allocated on the reset path" \
	"${T}spu_kernel_mask_all(spu);$N${T}if (!spu_kernel_quiesce(spu))$N$T${T}return -EIO;" \
	"${T}(void)kmalloc(64, GFP_NOIO);$N${T}spu_kernel_mask_all(spu);$N${T}if (!spu_kernel_quiesce(spu))$N$T${T}return -EIO;"
# release and giving up
off release-not-quiet "FAIL: the SPU doesn't stop: release gave the SPE back" \
	"if (!spu_kernel_quiesce(spu) || spu_kernel_sr1(spu, true)) {" "if ((spu_kernel_quiesce(spu), 0) || spu_kernel_sr1(spu, true)) {"
off queue-empty-unchecked "FAIL: the queue stays full after the purge: release gave the SPE back" \
	'return in_be64(&priv2->mfc_control_RW) & MFC_CNTL_DMA_QUEUES_EMPTY;' 'return true;'
off release-ls-kept "FAIL: the LS was not cleared at release" \
	"${T}spu_kernel_clear_ls(spu);$N${T}spu_kernel_unreserve(spu);" "${T}spu_kernel_unreserve(spu);"
off no-module-pin "the driver's module not pinned" \
	"${T}if (spu->kernel_owner)$N$T${T}__module_get(spu->kernel_owner);$N" ''
off bury-slbs-kept "the SLBs still valid when given up" \
	"${T}eieio();$N${T}spu_invalidate_slbs(spu);$N${T}out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE);" \
	"${T}eieio();$N${T}out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE);"
off bury-no-last-purge "no last suspend and purge when given up" \
	"${T}out_be64(&priv2->mfc_control_RW, MFC_CNTL_SUSPEND_DMA_QUEUE);$N${T}eieio();$N${T}out_be64(&priv2->mfc_control_RW, MFC_CNTL_PURGE_DMA_REQUEST | MFC_CNTL_SUSPEND_MASK);$N${T}eieio();$N${T}spu_kernel_wipe_ls(spu);" \
	"${T}spu_kernel_wipe_ls(spu);"
# reboot and sysfs
off reboot-ls-kept "FAIL: reboot: the LS was not cleared" \
	"$T${T}spu_kernel_stop_hard(spu);$N$T${T}WRITE_ONCE(spu->kernel_state, SPU_KERNEL_DEAD);$N$T}" \
	"$T${T}WRITE_ONCE(spu->kernel_state, SPU_KERNEL_DEAD);$N$T}"
off reboot-skips-dead "the reboot notifier skipped the given-up SPE" \
	"if (spu->kernel_state == SPU_KERNEL_NONE)$N$T$T${T}continue;" "if (spu->kernel_state != SPU_KERNEL_RESERVED)$N$T$T${T}continue;"
off reboot-silent "the notifier didn't log the failed quiesce" \
	"$T${T}if (!spu_kernel_quiesce(spu))$N$T$T${T}pr_warn(\"spu%d: not quiet at shutdown\\n\", spu->number);$N" "$T${T}spu_kernel_quiesce(spu);$N"
off reboot-not-final "FAIL: reserve after the reboot notifier" \
	"${T}spu_kernel_down = true;$N" ''
off sysfs-zero "FAIL: sysfs reserved reads" \
	'READ_ONCE(spu->kernel_state) != SPU_KERNEL_NONE' '0'

echo "$nok passed, $noff fix-offs"
exit $fail
