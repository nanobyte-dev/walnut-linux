# games.sh: where a game's benchmark is (gamedb/README.md "games/<id>/benchmark/"). Sourced by the
# harness (client.sh, run.sh, session.sh, ssd-arm.sh) and by tools/bench-ref.sh, tools/play-test.sh,
# tools/play-pc.sh, with $here set to the bench directory. bench/games.py is the same for Python.
# A game's benchmark is a directory: recipe.sh (code, sourced by client.sh), maybe analysis.toml
# (data for the analysis tools, bench/gamedata.py), lib/ (its helpers), assets/ (its files).
#   the repository:  gamedb/games/<id>/benchmark/
#   the PS3's tree:  /var/tmp/bench/games/<id>/ (make bench-push copies each benchmark there)
# Its played test is data beside it, rating/test.toml (gamedb/games/<id>/rating/, on the PS3
# /var/tmp/bench/games/<id>/rating/): bench_playdef reads it through playdef.py (session.sh, client.sh).
# A recipe sees GAME_DIR (that directory), ASSETS ($GAME_DIR/assets) and BENCH_LIB (the generic
# helpers: smoke.inc, openal.inc, xkeys.py, keyboard.evemu; bench/ for now, bench/lib/ later).
# Names: a game is named by its gamedb id. A recipe renamed by the move into gamedb (step 1c,
# 2026-10-26: sauerbraten.sh became bananabread's, sauerbraten-edition.sh sauerbraten's) keeps its
# old name in its analysis.toml ([legacy] recipe_names): bench_id takes an old name that is no id
# (sauerbraten-edition -> sauerbraten); runs from before the move are read through those names first
# (bench/games.py: bench.txt without recipe=).
# Overrides: BENCH_GAMES, a root of <id>/recipe.sh or <id>/benchmark/recipe.sh; BENCH_RECIPES (tests),
# a directory of <name>.sh files, GAME_DIR then <that directory>/<name>.
BENCH_LIB=${BENCH_LIB:-$here}

bench_root() {	# the root of the game directories
	if [ -n "${BENCH_GAMES:-}" ]; then echo "$BENCH_GAMES"
	elif [ -d "$here/games" ]; then echo "$here/games"
	else echo "$(cd "$here/.." && pwd)/gamedb/games"; fi
}

bench_ids() {	# every game with a recipe, one id a line, sorted
	local r f
	if [ -n "${BENCH_RECIPES:-}" ]; then
		for f in "$BENCH_RECIPES"/*.sh; do [ -e "$f" ] && basename "$f" .sh; done
		return 0
	fi
	r=$(bench_root)
	for f in "$r"/*/recipe.sh "$r"/*/benchmark/recipe.sh; do
		[ -e "$f" ] || continue
		f=${f%/recipe.sh}; f=${f%/benchmark}; echo "${f##*/}"
	done | LC_ALL=C sort -u
}

bench_id() {	# bench_id NAME: the game's id (an id, else an old recipe name in [legacy]), or fail
	local n=$1 r f
	[[ $n =~ ^[a-z0-9][a-z0-9-]*$ ]] || return 1
	if [ -n "${BENCH_RECIPES:-}" ]; then [ -e "$BENCH_RECIPES/$n.sh" ] && echo "$n"; return; fi
	r=$(bench_root)
	if [ -e "$r/$n/recipe.sh" ] || [ -e "$r/$n/benchmark/recipe.sh" ]; then echo "$n"; return 0; fi
	for f in "$r"/*/analysis.toml "$r"/*/benchmark/analysis.toml; do
		[ -e "$f" ] || continue
		if grep -qE "^recipe_names *= *\[.*\"$n\"" "$f"; then
			f=${f%/analysis.toml}; f=${f%/benchmark}; echo "${f##*/}"; return 0
		fi
	done
	return 1
}

bench_game_dir() {	# bench_game_dir ID: the game's benchmark directory (it holds recipe.sh)
	local r
	if [ -n "${BENCH_RECIPES:-}" ]; then echo "$BENCH_RECIPES/$1"; return; fi
	r=$(bench_root)
	if [ -e "$r/$1/recipe.sh" ]; then echo "$r/$1"; else echo "$r/$1/benchmark"; fi
}

bench_recipe() {	# bench_recipe ID: the recipe file
	if [ -n "${BENCH_RECIPES:-}" ]; then echo "$BENCH_RECIPES/$1.sh"; else echo "$(bench_game_dir "$1")/recipe.sh"; fi
}

bench_use() {	# bench_use ID: GAME_DIR and ASSETS for sourcing its recipe (they may be used at source time)
	GAME_DIR=$(bench_game_dir "$1"); ASSETS=$GAME_DIR/assets
}

bench_test() {	# bench_test ID: the game's played test, rating/test.toml beside its benchmark (it may not exist)
	if [ -n "${BENCH_RECIPES:-}" ]; then echo "$BENCH_RECIPES/$1.test.toml"; else echo "$(bench_root)/$1/rating/test.toml"; fi
}

bench_playdef() {	# bench_playdef ID: TEST_* from the game's test.toml (playdef.py; TEST_FILE empty when it has
	# none, the others empty, TEST_HUD 1); fails (2) on a file that doesn't parse or breaks the schema
	local f out
	TEST_FILE=; TEST_PROCESS=; TEST_HOME=(); TEST_PLAY_MARK=; TEST_LOAD_MARK=; TEST_COMMAND=(); TEST_ENV=()
	TEST_SOUND_AT=; TEST_HUD=1
	f=$(bench_test "$1"); [ -e "$f" ] || return 0
	out=$(python3 -I "$here/playdef.py" "$f") || return 2
	eval "$out"
}
