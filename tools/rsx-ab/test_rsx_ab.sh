#!/bin/bash
# test_rsx_ab.sh: abba-batch.sh against stubs, no PS3: it runs the harness from BENCH/harness, loads
# the arms in ABBA order then the extra arms with their parameters, runs every case once per arm in
# order, and stops at a failed arm switch. desktop-cases.sh's mpv counter script on luajit with a
# fake mp: the drops of every loop add up (the counters start again at each loop), the totals are
# written to stderr at the end whatever mpv's message level.
set -u
here=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

mkdir -p "$T/bench/harness" "$T/bin"
cat > "$T/bench/harness/run.sh" <<'X'
#!/bin/bash
echo "run.sh $* in $PWD" >> "$LOG"
X
cat > "$T/arm" <<'X'
#!/bin/bash
echo "arm $*" >> "$LOG"
[ "$1" != X ]
X
cat > "$T/desk" <<'X'
#!/bin/bash
echo "desk $*" >> "$LOG"
X
printf '#!/bin/sh\n[ "$1" = dmesg ] && exit 0\n[ "$1" = cat ] && exit 0\nexec "$@"\n' > "$T/bin/sudo"
printf '#!/bin/sh\necho "ps3-bench.sh $*" >> "$LOG"\n' > "$T/bin/ps3-bench.sh"
chmod +x "$T/bench/harness/run.sh" "$T/arm" "$T/desk" "$T/bin/sudo" "$T/bin/ps3-bench.sh"

export LOG=$T/log
PATH=$T/bin:$PATH BENCH=$T/bench RSX_ARM=$T/arm DESK=$T/desk CASES="neverball mpv" \
	EXTRA="C:test_clear_off=1" bash "$here/abba-batch.sh" > "$T/out" 2>&1
got=$(grep -o '^arm .*' "$LOG" | tr '\n' ';')
check "arms in ABBA order, then the extra arm with its parameter" \
	'[ "$got" = "arm A;arm B;arm B;arm A;arm C test_clear_off=1;" ]'
check "the harness runs from BENCH/harness" \
	'grep -q "^run.sh bench neverball base 1 in $T/bench/harness$" "$LOG"'
check "every case once per arm" '[ "$(grep -c "^run.sh bench neverball" "$LOG")" = 5 ] && [ "$(grep -c "^desk mpv" "$LOG")" = 5 ]'
check "the desktop comes back at the end" '[ "$(tail -1 "$LOG")" = "ps3-bench.sh desktop" ]'

: > "$LOG"
PATH=$T/bin:$PATH BENCH=$T/bench RSX_ARM=$T/arm DESK=$T/desk CASES="neverball" ARMS="A X B" \
	bash "$here/abba-batch.sh" > "$T/out" 2>&1
rc=$?
check "a failed arm switch stops the batch" '[ $rc != 0 ] && ! grep -q "^arm B" "$LOG" && ! grep -q "ps3-bench.sh desktop" "$LOG"'
: > "$LOG"
PATH=$T/bin:$PATH BENCH=$T/nobench RSX_ARM=$T/arm DESK=$T/desk bash "$here/abba-batch.sh" > "$T/out" 2>&1
check "no harness: refused before any arm" '[ $? != 0 ] && [ ! -s "$LOG" ]'

# MESA_AB=1: the arms are Mesa builds, chosen per run through the environment, ps3rsx untouched
mkdir -p "$T/mbench/harness" "$T/mbin"
cat > "$T/mbench/harness/run.sh" <<'X'
#!/bin/bash
echo "run.sh $* LD=${LD_LIBRARY_PATH-} DRI=${LIBGL_DRIVERS_PATH-} GBM=${GBM_BACKENDS_PATH-}" >> "$LOG"
X
cat > "$T/marm" <<'X'
#!/bin/bash
case $1 in
select) echo "mesa select $2" >> "$LOG" ;;
env) if [ "$2" = off ]; then
		if [ -n "${MESA_BASE_LD:-}" ]; then echo "export LD_LIBRARY_PATH='$MESA_BASE_LD'"; else echo "unset LD_LIBRARY_PATH"; fi
		echo "unset LIBGL_DRIVERS_PATH GBM_BACKENDS_PATH"
	else
		echo "export LD_LIBRARY_PATH='/m/$2/lib${MESA_BASE_LD:+:$MESA_BASE_LD}'"
		echo "export LIBGL_DRIVERS_PATH='/m/$2/dri'"
		echo "export GBM_BACKENDS_PATH='/m/$2/lib/gbm'"
	fi ;;
esac
X
printf '#!/bin/sh\necho "ps3-bench.sh $* LD=${LD_LIBRARY_PATH-} DRI=${LIBGL_DRIVERS_PATH-}" >> "$LOG"\n' > "$T/mbin/ps3-bench.sh"
cp "$T/bin/sudo" "$T/mbin/sudo"
chmod +x "$T/mbench/harness/run.sh" "$T/marm" "$T/mbin/ps3-bench.sh" "$T/mbin/sudo"
: > "$LOG"
LD_LIBRARY_PATH=/sys0 PATH=$T/mbin:$PATH BENCH=$T/mbench RSX_ARM=$T/arm MESA_ARM=$T/marm DESK=$T/desk \
	MESA_AB=1 CASES="neverball" bash "$here/abba-batch.sh" > "$T/out" 2>&1
check "Mesa A/B: no ps3rsx module is reloaded" '! grep -q "^arm " "$LOG"'
got=$(grep -o '^mesa select .*' "$LOG" | tr '\n' ';')
check "Mesa A/B: the arms are selected in ABBA order" '[ "$got" = "mesa select A;mesa select B;mesa select B;mesa select A;" ]'
check "Mesa A/B: each run has its arm's libraries, driver path and gbm path, the system's LD_LIBRARY_PATH behind" \
	'[ "$(grep "^run.sh" "$LOG" | sed "s/^run.sh bench neverball base 1 //" | tr "\n" ";")" = "LD=/m/A/lib:/sys0 DRI=/m/A/dri GBM=/m/A/lib/gbm;LD=/m/B/lib:/sys0 DRI=/m/B/dri GBM=/m/B/lib/gbm;LD=/m/B/lib:/sys0 DRI=/m/B/dri GBM=/m/B/lib/gbm;LD=/m/A/lib:/sys0 DRI=/m/A/dri GBM=/m/A/lib/gbm;" ]'
check "Mesa A/B: the desktop comes back on the system's Mesa" '[ "$(tail -1 "$LOG")" = "ps3-bench.sh desktop LD=/sys0 DRI=" ]'
: > "$LOG"
PATH=$T/mbin:$PATH BENCH=$T/mbench RSX_ARM=$T/arm MESA_ARM=$T/marm DESK=$T/desk MESA_AB=1 EXTRA="C:test_clear_off=1" \
	CASES="neverball" bash "$here/abba-batch.sh" > "$T/out" 2>&1
check "Mesa A/B: ps3rsx extra arms are refused before any arm" '[ $? != 0 ] || ! [ -s "$LOG" ]'
cat > "$T/marm-bad" <<'X'
#!/bin/bash
[ "$1" = select ] && [ "$2" = B ] && exit 1
exec "$(dirname "$0")/marm" "$@"
X
chmod +x "$T/marm-bad"
: > "$LOG"
PATH=$T/mbin:$PATH BENCH=$T/mbench RSX_ARM=$T/arm MESA_ARM=$T/marm-bad DESK=$T/desk MESA_AB=1 CASES="neverball" \
	bash "$here/abba-batch.sh" > "$T/out" 2>&1
rc=$?
check "Mesa A/B: an unusable arm stops the batch" '[ $rc != 0 ] && [ "$(grep -c "^run.sh" "$LOG")" = 1 ] && ! grep -q "ps3-bench.sh desktop" "$LOG"'

# mesa-arms-prep.sh and mesa-arm.sh on fake debs: the layout, the hashes, the arm check
mkdeb() {	# mkdeb DIR PKG VERSION: a deb with PKG's files of the fake build VERSION
	local d=$1 p=$2 v=$3 w=$T/mk-$2-$3 t=usr/lib/powerpc64-linux-gnu
	rm -rf "$w"; mkdir -p "$w/data/$t" "$w/ctl" "$d"
	case $p in
	mesa-libgallium) echo "gallium $v" > "$w/data/$t/libgallium-$v.so" ;;
	libgl1-mesa-dri) mkdir -p "$w/data/$t/dri"; echo "NEEDED libgallium-$v.so" > "$w/data/$t/dri/libdril_dri.so"
		ln -s libdril_dri.so "$w/data/$t/dri/nouveau_dri.so"; ln -s libdril_dri.so "$w/data/$t/dri/swrast_dri.so" ;;
	libegl-mesa0) echo egl > "$w/data/$t/libEGL_mesa.so.0.0.0"; ln -s libEGL_mesa.so.0.0.0 "$w/data/$t/libEGL_mesa.so.0" ;;
	libglx-mesa0) echo glx > "$w/data/$t/libGLX_mesa.so.0.0.0"; ln -s libGLX_mesa.so.0.0.0 "$w/data/$t/libGLX_mesa.so.0" ;;
	libgbm1) echo gbm > "$w/data/$t/libgbm.so.1.0.0"; ln -s libgbm.so.1.0.0 "$w/data/$t/libgbm.so.1"
		mkdir -p "$w/data/$t/gbm"; echo dri_gbm > "$w/data/$t/gbm/dri_gbm.so" ;;
	esac
	printf 'Package: %s\nVersion: %s\nArchitecture: ppc64\n' "$p" "$v" > "$w/ctl/control"
	echo 2.0 > "$w/debian-binary"
	tar -cJf "$w/control.tar.xz" -C "$w/ctl" ./control
	tar -cJf "$w/data.tar.xz" -C "$w/data" .
	(cd "$w" && ar rc "$d/${p}_${v}_ppc64.deb" debian-binary control.tar.xz data.tar.xz)
}
for p in mesa-libgallium libgl1-mesa-dri libegl-mesa0 libglx-mesa0 libgbm1; do
	mkdeb "$T/debsA" $p 26.2.4-1+ps3.1; mkdeb "$T/debsB" $p 26.2.4-1+ps3.2
done
mkdeb "$T/debsB" mesa-libgallium-dbgsym 26.2.4-1+ps3.2	# a dbgsym deb beside them is not an arm's
(cd "$here/../.." && MESA_ARMS_OUT=$T/mesa-out bash "$here/mesa-arms-prep.sh" "$T/debsB" "$T/debsA" > "$T/prep.out" 2>&1)
check "mesa-arms-prep: both arms in the ladder layout" \
	'[ -e "$T/mesa-out/A/lib/libgallium-26.2.4-1+ps3.1.so" ] && [ -e "$T/mesa-out/B/lib/libgallium-26.2.4-1+ps3.2.so" ] && [ -e "$T/mesa-out/B/lib/libGLX_mesa.so.0" ] && [ -e "$T/mesa-out/B/lib/libEGL_mesa.so.0" ] && [ -e "$T/mesa-out/A/lib/libgbm.so.1" ] && [ -e "$T/mesa-out/A/lib/gbm/dri_gbm.so" ] && [ -L "$T/mesa-out/B/dri/nouveau_dri.so" ] && [ -e "$T/mesa-out/B/dri/nouveau_dri.so" ]'
check "mesa-arms-prep: mesa-arms.txt names the versions" \
	'[ "$(cut -f1,2 "$T/mesa-out/mesa-arms.txt" | tr "\t\n" " :")" = "A 26.2.4-1+ps3.1:B 26.2.4-1+ps3.2:" ]'
check "mesa-arms-prep: the same build twice is called out" \
	'(cd "$here/../.." && MESA_ARMS_OUT=$T/mesa-out2 bash "$here/mesa-arms-prep.sh" "$T/debsB" "$T/debsB" 2>&1 | grep -q "same Mesa version")'
mkdir -p "$T/debsC"; cp "$T/debsA"/*.deb "$T/debsC/"; rm "$T/debsC/mesa-libgallium_"*; mkdeb "$T/debsC" mesa-libgallium 26.2.4-1+ps3.9
check "mesa-arms-prep: debs of two builds mixed in one arm are refused" \
	'! (cd "$here/../.." && MESA_ARMS_OUT=$T/mesa-out3 bash "$here/mesa-arms-prep.sh" "$T/debsB" "$T/debsC" > "$T/mix.out" 2>&1)'
check "mesa-arms-prep: ...with the reason" 'grep -q "debs are not of one build" "$T/mix.out"'
export MESA_DIR=$T/mesa-out RSX_ARMS_LOG=$T/arms.log
check "mesa-arm select: logs the switch like rsx-arm.sh" \
	'bash "$here/mesa-arm.sh" select B > /dev/null && [ "$(cut -f2- "$T/arms.log")" = "$(printf "B\t26.2.4-1+ps3.2\tmesa")" ]'
check "mesa-arm env: the three variables, the arm's directories first" \
	'[ "$(MESA_BASE_LD=/sys0 bash "$here/mesa-arm.sh" env A | tr "\n" ";")" = "export LD_LIBRARY_PATH='"'$T/mesa-out/A/lib:/sys0'"';export LIBGL_DRIVERS_PATH='"'$T/mesa-out/A/dri'"';export GBM_BACKENDS_PATH='"'$T/mesa-out/A/lib/gbm'"';" ]'
check "mesa-arm env off: the caller's path back, the others unset" \
	'[ "$(MESA_BASE_LD=/sys0 bash "$here/mesa-arm.sh" env off | tr "\n" ";")" = "export LD_LIBRARY_PATH='"'/sys0'"';unset LIBGL_DRIVERS_PATH GBM_BACKENDS_PATH;" ]'
echo changed >> "$T/mesa-out/B/lib/libgallium-26.2.4-1+ps3.2.so"
check "mesa-arm select: a libgallium that isn't the one prepared is refused" '! bash "$here/mesa-arm.sh" select B 2>/dev/null'
check "mesa-arm select: a missing arm is refused" '! bash "$here/mesa-arm.sh" select C 2>/dev/null'
unset MESA_DIR RSX_ARMS_LOG

# desktop-cases.sh's mpv script, extracted from the file, on a fake mp
lua=$(sed -n "/cat > \"\$lua\" <<'EOF'$/,/^EOF$/p" "$here/desktop-cases.sh" | sed '1d;$d')
check "desktop-cases.sh has the mpv script" '[ -n "$lua" ]'
if command -v luajit > /dev/null; then
	cat > "$T/fake.lua" <<'X'
-- a fake mp: properties from a table, the timers run by hand, shutdown at the end
local props = { ["frame-drop-count"] = 0, ["vo-delayed-frame-count"] = 0 }
local timers, events, quit_at, quit = {}, {}, nil, false
mp = { get_property_number = function(k, d) return props[k] or d end,
       add_periodic_timer = function(t, f) timers[#timers + 1] = f end,
       register_event = function(e, f) events[e] = f end,
       get_opt = function(k) return k == "rsxab-secs" and "42" or nil end,
       add_timeout = function(t, f) quit_at = t; f() end,
       command = function(c) quit = c == "quit" end }
local script = loadfile(arg[1]); script()
-- two loops: drops 3 then 5 in the first, the counters start again, 4 in the second
for _, v in ipairs({ 1, 3, 5, 0, 2, 4 }) do
	props["frame-drop-count"] = v; props["vo-delayed-frame-count"] = v * 2
	for _, f in ipairs(timers) do f() end
end
events["shutdown"]()
io.stderr:write(string.format("quit_at=%s quit=%s\n", tostring(quit_at), tostring(quit)))
X
	printf '%s\n' "$lua" > "$T/mpv.lua"
	out=$(luajit "$T/fake.lua" "$T/mpv.lua" 2>&1 >/dev/null)
	check "mpv counters summed over loops, on stderr ($(tr '\n' ' ' <<<"$out"))" \
		'grep -q "rsxab: mpv drops=9 delayed=18" <<<"$out"'
	check "mpv quits after SECS (script-opts), not at a file position" 'grep -q "quit_at=42 quit=true" <<<"$out"'
else
	echo "SKIP rsx-ab:mpv-lua no luajit"
fi
[ $fails -eq 0 ] && echo "rsx-ab: all passed"
exit $((fails > 0))
