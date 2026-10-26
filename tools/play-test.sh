#!/bin/bash
# play-test.sh: a played test, both sides from one command (docs/games/SUITE.md "Played sessions").
#   play-test.sh check                    the PS3 is up and idle; the harness pushed and the logger in place
#   play-test.sh start GAME [LAUNCHER]    the recorded session on the PS3 (bench/session.sh, which
#                                         counts the load from here) and the same game and launcher on this
#                                         PC (tools/play-pc.sh, detached; its log in build/pc-play/GAME.log)
#       PC=0 play-test.sh start ...       the PS3 only (the PC copy is still open, or a game with no PC side)
#   play-test.sh stop GAME                ends the PS3 session's game and its form (a false start: the run
#                                         is kept on the PS3 and simply not imported) and the PC copy
#   play-test.sh collect GAME             once the player is done: the runs pulled (make bench-pull), the
#                                         game's sessions listed with how they ended, play time, launcher,
#                                         sound dropouts, the form's answers, and whether they're imported
# The player's game settings are never touched; nothing runs on the PS3 between start and the player's
# "done" (the session is being measured).
set -euo pipefail
cd "$(dirname "$0")/.."
conf() { sed -n "s/^$1 *[:?]*= *//p" config.local.mk config.mk 2>/dev/null | grep -v '^$' | head -1; }
IP=$(conf PS3_IP); USER_=$(conf PS3_USER); KEY=$(conf PS3_SSH_KEY); KEY=${KEY//\$(HOME)/$HOME}
[ -n "$IP" ] && [ -n "$USER_" ] || { echo "!! PS3_IP / PS3_USER: set them in config.local.mk" >&2; exit 2; }
ps3() { timeout "${T:-60}" ssh -i "$KEY" -o UserKnownHostsFile="$PWD/build/ps3_known_hosts" -o BatchMode=yes \
	-o ConnectTimeout=10 "$USER_@$IP" "$@"; }
cmd=${1:-}; G=${2:-}; L=${3:-}
# GAME is a Walnut Games entry, whose recipe is its own (gamedb/games/GAME/benchmark/recipe.sh,
# bench/games.sh), or an old recipe name (sauerbraten-edition: Sauerbraten's)
E=$G; R=$G
here=$PWD/bench; . bench/games.sh
if [ -n "$G" ] && r=$(bench_id "$G"); then
	R=$r; [ -e "gamedb/games/$G/index.md" ] || E=$r
fi

case $cmd in
check)
	ps3 'printf "up: %s, kernel %s\n" "$(uptime -p)" "$(uname -r)"
		systemctl --user is-active -q ps3-game-mode.service && echo "!! game mode is on: a game is running"
		for u in $(systemctl --user list-units --plain --no-legend "walnut-session-*" | cut -d" " -f1); do echo "!! $u is running"; done
		[ -x /var/tmp/bench/session.sh ] && echo "harness: /var/tmp/bench" || echo "!! no harness (make bench-push)"
		[ -r /usr/lib/ps3/lib/ps3-xrunlog.conf ] && echo "sound dropout logger: installed" || echo "!! no sound dropout logger (ps3-gpu-tools)"
		df -h /var/tmp | awk "NR == 2 { print \"disk: \" \$4 \" free\" }"' ;;
start)
	[ -n "$G" ] || { echo "usage: play-test.sh start GAME [LAUNCHER]" >&2; exit 2; }
	bench_id "$R" > /dev/null || { echo "!! no recipe gamedb/games/$R/benchmark/recipe.sh" >&2; exit 2; }
	# the PS3 first: the load time counts from here
	ps3 "systemctl --user is-active -q walnut-session-$R && { echo '!! a $R session is still open (its form?)'; exit 1; }
		systemctl --user is-active -q ps3-game-mode.service && { echo '!! a game is running'; exit 1; }
		systemctl --user reset-failed walnut-session-$R 2>/dev/null
		systemd-run --user --unit=walnut-session-$R -q -E DISPLAY=:0 -E XAUTHORITY=\$HOME/.Xauthority \
			/var/tmp/bench/session.sh $R $(printf %q "$L") && echo '>> PS3: $R started (session.sh)'"
	if [ "${PC:-1}" = 1 ]; then
		mkdir -p build/pc-play
		setsid nohup tools/play-pc.sh "$E" ${L:+"$L"} > "build/pc-play/$E.log" 2>&1 < /dev/null &
		echo ">> PC: $G starting (build/pc-play/$E.log)"
	fi ;;
stop)
	[ -n "$G" ] || { echo "usage: play-test.sh stop GAME" >&2; exit 2; }
	T=120 ps3 "c=\$(sed -n 's/^COMM=//p' /var/tmp/bench/games/$R/recipe.sh | head -1); c=\${c%%[[:space:]]*}; c=\${c:0:15}; [ -n \"\$c\" ] && pkill -TERM -x \"\$c\"
		for i in \$(seq 30); do sleep 1
			f=\$(pgrep -a -x python3 | grep session-form.py | cut -d' ' -f1); [ -n \"\$f\" ] && kill \$f
			systemctl --user is-active -q walnut-session-$R || break; done
		systemctl --user is-active -q walnut-session-$R && echo '!! still running' || echo '>> PS3: $R session ended'"
	docker rm -f "play-pc-$E" > /dev/null 2>&1 && echo ">> PC: $E closed" || true ;;
collect)
	[ -n "$G" ] || { echo "usage: play-test.sh collect GAME" >&2; exit 2; }
	make -s bench-pull > /dev/null 2>&1 || echo "!! the pull failed (is the PS3 up?): the runs already here"
	python3 -I - "$R" "$E" <<'PY'
import glob, os, sys, tomllib
sys.path.insert(0, "bench")
import games    # bench/games.py: a run's game (runs from before the move name the old recipe)
g, entry = sys.argv[1], sys.argv[2]
kv = lambda f: dict(l.split("=", 1) for l in open(f, errors="replace").read().splitlines() if "=" in l)
done = set()
for r in glob.glob("gamedb/games/%s/rating/sessions/*/result.toml" % entry):
    done.add(tomllib.loads(open(r).read()).get("run"))
def game_of(d):
    f = os.path.join(d, "bench.txt")
    if os.path.exists(f):
        return games.run_game(kv(f))
    return games.resolve(os.path.basename(d).split("-session-", 1)[1].rsplit("-", 1)[0], old=True)
runs = sorted(d for d in glob.glob("build/bench/ps3-bench-*-session-*") if game_of(d) == g)
if not runs:
    print("no sessions of %s pulled" % g)
for d in runs:
    b = kv(os.path.join(d, "bench.txt")) if os.path.exists(os.path.join(d, "bench.txt")) else {}
    f = os.path.join(d, "session.toml")
    form = tomllib.loads(open(f).read()) if os.path.exists(f) else {}
    play = float(b.get("play_seconds") or 0)
    print("%s  %s  %4.1f min  %-6s launcher=%s  dropouts=%s/%s  form=%s%s" % (
        os.path.basename(d)[10:26], "imported" if os.path.basename(d) in done else "new     ", play / 60,
        b.get("ended", "?"), b.get("launcher", "-"), b.get("xruns_audible", "-"), b.get("xruns_logged", b.get("underruns", "-")),
        ("sound=%s music=%s controls=%s" % (form.get("sound"), form.get("music"), form.get("controls")) +
         (" glitches=%s played_with=%s menus=%s saves=%s rules=2" % (form.get("glitches"), form.get("played_with"),
          form.get("menus_modes"), form.get("saves_load")) if form.get("form_version", 1) >= 2 else "")) if form else "none",
        ("  notes: " + " ".join(form.get("notes", "").split())[:100]) if form.get("notes") else ""))
PY
	echo ">> import: look at the shots, write REVIEW.md, then python3 -I tools/gamedb/import-run.py build/bench/<run> --game $E" ;;
*)
	sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2 ;;
esac
