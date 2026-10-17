# Luanti 5.15.2 (our build, LuaJIT, ppc64) with Minetest Game (luanti-game-minetest): a fixed
# flight over terrain generated from a fixed seed. A server-side mod can't count frames, so the
# world mod ps3bench (assets/luanti-ps3bench) only makes every run the same: it holds the player at
# the start for 15 s while the first blocks are generated and meshed, logs "ps3bench: path start",
# flies 45 s (240 nodes north looking down, then a full turn), logs "ps3bench: path end" and shuts
# the game down. fps from the Gallium HUD from the "path start" line (the load time includes the
# 15 s hold; parse prints joined=, Luanti's own stamps from its first log line to the join).
# World and config live in /var/tmp/ps3-bench/luanti (the player's ~/.minetest worlds and
# minetest.conf are untouched). The world is made new for every run, so each run generates the
# same terrain (mapgen CPU time counts, as for a player exploring); variant saved keeps the last map.
SUITE=1
COMM=luanti
LU_DIR=/var/tmp/ps3-bench/luanti
LU_WORLD=$LU_DIR/world
LU_CONF=$LU_DIR/bench.conf
LU_FRESH=1
LU_SET=()	# extra "key = value" lines for bench.conf (variants)
setup() {
	[ "$LU_FRESH" = 1 ] && rm -rf -- "${LU_WORLD:?}"
	mkdir -p "$LU_WORLD/worldmods"
	rm -rf -- "${LU_WORLD:?}/worldmods/ps3bench"
	cp -r "$ASSETS/luanti-ps3bench" "$LU_WORLD/worldmods/ps3bench"
	printf '%s\n' "gameid = minetest" "world_name = ps3bench" "backend = sqlite3" \
		"player_backend = sqlite3" "auth_backend = sqlite3" "mod_storage_backend = sqlite3" \
		> "$LU_WORLD/world.mt"
	# Luanti writes its settings back into this file on exit: made new every run
	printf '%s\n' "video_driver = opengl" "fullscreen = true" "screen_w = 1920" "screen_h = 1080" \
		"autosave_screensize = false" "viewing_range = 60" "vsync = false" "fps_max = 1000" \
		"fps_max_unfocused = 1000" "mg_name = v7" "fixed_map_seed = 20261017" "creative_mode = true" \
		"enable_damage = false" "disable_anticheat = true" "server_announce = false" \
		"ps3bench_warmup = 15" "ps3bench_seconds = 45" "${LU_SET[@]}" > "$LU_CONF"
}
CMD=(/usr/games/luanti --go --world "$LU_WORLD" --config "$LU_CONF")
LOAD_MARK='ps3bench: path start'
END_RE='ps3bench: path end'
# a safety net: startup, 15 s hold, 45 s path
DURATION=170
parse() {
	local l first= join=
	while IFS= read -r l; do
		[[ $l =~ ^([0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9:]{8}): ]] || continue
		[ -n "$first" ] || first=${BASH_REMATCH[1]}
		[[ $l == *"ps3bench: joined"* ]] && { join=${BASH_REMATCH[1]}; break; }
	done < "$D/game.log"
	[ -n "$join" ] && echo "joined=$(( $(date -d "$join" +%s) - $(date -d "$first" +%s) ))"
}
# the map from the previous run: loaded from disk instead of generated
variant_saved() { LU_FRESH=0; }
# post-processing off (on by default: the scene goes through an FBO)
variant_nopp() { LU_SET+=("enable_post_processing = false"); }
# mipmapping on (off by default): Mesa generates the levels on the GPU
variant_mip() { LU_SET+=("mip_map = true"); }
