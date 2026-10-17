# Sauerbraten (our cube2 build: textures compressed at upload on Mesa too) with cube2-data 1.3's
# maps (the BananaBread content). No timedemo: -l loads map "cave" (its only player start, so every
# run looks from the same spot) and the player stands still; a CubeScript (-x) counts frames for 30 s
# from 10 s after the first frame (a sleep re-armed every frame, wall time from getmillis 1), prints
# "ps3bench: <n> frames in <ms> ms: <fps> fps, longest <ms> ms" and quits. getfps itself averages
# only the last 60 frames. Settings go into ~/.cube2/autoexec.cfg (run after config.cfg, so the
# player's saved fullscreen or vsync can't override them); both files are restored after the run.
# GPU skinning (gpuskel 1, the default; the hud gun is a skeletal model) indexes a bone array in the
# vertex shader, which nv30 compiles to if/else trees: variant cpuskel turns it off.
SUITE=1
COMM=cube2_client
SB_HOME=$HOME/.cube2
BACKUP=("$SB_HOME/autoexec.cfg" "$SB_HOME/config.cfg" "$SB_HOME/restore.cfg")
SB_MAP=cave
SB_SET=()	# extra CubeScript lines for autoexec.cfg (variants)
setup() {
	mkdir -p "$SB_HOME"
	printf '%s\n' "fullscreen 1" "vsync 0" "maxfps 0" "${SB_SET[@]}" > "$SB_HOME/autoexec.cfg"
}
SB_SCRIPT='ps3f = 0; ps3max = 0
ps3count = [ps3f = (+ $ps3f 1); ps3now = (getmillis 1); ps3max = (max $ps3max (- $ps3now $ps3last)); ps3last = $ps3now; sleep 1 ps3count]
sleep 1 [echo "ps3bench: first frame"; sleep 10000 [echo "ps3bench: start"; ps3t = (getmillis 1); ps3last = $ps3t; ps3count; sleep 30000 [ps3ms = (- (getmillis 1) $ps3t); echo (format "ps3bench: %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3f $ps3ms (divf (*f $ps3f 1000) $ps3ms) $ps3max); quit]]]'
sb_cmd() { CMD=(/usr/games/cube2 -w1920 -h1080 -t1 "-l$SB_MAP" "-x$SB_SCRIPT"); }
sb_cmd
FPS_RE='^ps3bench: [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
MAXMS_RE='^ps3bench: .* longest ([0-9]+) ms'
LOAD_MARK='^read map '
# a safety net: loading, 10 s, 30 s measured; quits by itself
DURATION=170
# skeletal models skinned on the CPU (the GPU path's bone indexing is the nv30 risk)
variant_cpuskel() { SB_SET+=("gpuskel 0"); }
# no texture compression at all (as upstream on Mesa): VRAM and load time against base
variant_notc() { SB_SET+=("texcompress 0"); }
# the biggest map with one player start (66 lights, an 8192 world)
variant_towers() { SB_MAP=two_towers; sb_cmd; }
