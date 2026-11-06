#!/bin/bash
# play-test.sh: a played test, both sides from one command (docs/games/SUITE.md "Played sessions").
#   play-test.sh check                    the PS3 is up and idle; the harness pushed and the logger in place
#   play-test.sh start GAME [LAUNCHER]    the recorded session on the PS3 (bench/harness/session.sh, which
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
. tools/lib/ps3.sh	# the PS3's address, key and options; every ps3_ssh call checks the lease first
ps3_check || exit 1	# someone else holds the PS3 lease (tools/ps3 lock)
ps3() { PS3_TIMEOUT=${T:-60} ps3_ssh "$@"; }
cmd=${1:-}; G=${2:-}; L=${3:-}
# GAME is a Walnut Games entry, whose recipe is its own (gamedb/games/GAME/benchmark/recipe.sh,
# bench/harness/games.sh), or an old recipe name (sauerbraten-edition: Sauerbraten's)
E=$G; R=$G
BENCH=$PWD/bench; . bench/harness/games.sh
if [ -n "$G" ] && r=$(bench_id "$G"); then
	R=$r; [ -e "gamedb/games/$G/index.md" ] || E=$r
fi

case $cmd in
check)
	ps3 'printf "up: %s, kernel %s\n" "$(uptime -p)" "$(uname -r)"
		systemctl --user is-active -q ps3-game-mode.service && echo "!! game mode is on: a game is running"
		for u in $(systemctl --user list-units --plain --no-legend "walnut-session-*" | cut -d" " -f1); do echo "!! $u is running"; done
		[ -x /var/tmp/bench/harness/session.sh ] && echo "harness: /var/tmp/bench" || echo "!! no harness (make bench-push)"
		[ -r /usr/lib/ps3/lib/ps3-xrunlog.conf ] && echo "sound dropout logger: installed" || echo "!! no sound dropout logger (ps3-gpu-tools)"
		df -h /var/tmp | awk "NR == 2 { print \"disk: \" \$4 \" free\" }"' ;;
start)
	[ -n "$G" ] || { echo "usage: play-test.sh start GAME [LAUNCHER]" >&2; exit 2; }
	bench_id "$R" > /dev/null || { echo "!! no recipe gamedb/games/$R/benchmark/recipe.sh" >&2; exit 2; }
	# the PS3 first: the load time counts from here
	ps3 "[ -S /tmp/.X11-unix/X0 ] || { echo '!! the desktop is down (a benchmark leaves it so: ps3-bench.sh desktop)'; exit 1; }
		systemctl --user is-active -q walnut-session-$R && { echo '!! a $R session is still open (its form?)'; exit 1; }
		systemctl --user is-active -q ps3-game-mode.service && { echo '!! a game is running'; exit 1; }
		systemctl --user reset-failed walnut-session-$R 2>/dev/null
		systemd-run --user --unit=walnut-session-$R -q -E DISPLAY=:0 -E XAUTHORITY=\$HOME/.Xauthority \
			/var/tmp/bench/harness/session.sh $R $(printf %q "$L") && echo '>> PS3: $R started (session.sh)'"
	if [ "${PC:-1}" = 1 ]; then
		mkdir -p build/pc-play
		setsid nohup tools/play-pc.sh "$E" ${L:+"$L"} > "build/pc-play/$E.log" 2>&1 < /dev/null &
		echo ">> PC: $G starting (build/pc-play/$E.log)"
	fi ;;
stop)
	[ -n "$G" ] || { echo "usage: play-test.sh stop GAME" >&2; exit 2; }
	# the game's process: its played test's (rating/test.toml through playdef.py; bench-push carries it),
	# else the recipe's COMM (a harness pushed before the test.toml files)
	T=120 ps3 "c=; eval \"\$(python3 -I /var/tmp/bench/harness/playdef.py /var/tmp/bench/games/$R/rating/test.toml 2>/dev/null)\"; c=\${TEST_PROCESS:-}
		[ -n \"\$c\" ] || { c=\$(sed -n 's/^COMM=//p' /var/tmp/bench/games/$R/recipe.sh | head -1); c=\${c%%[[:space:]]*}; }
		c=\${c:0:15}; [ -n \"\$c\" ] && pkill -TERM -x \"\$c\"
		for i in \$(seq 30); do sleep 1
			# a game can hang in its own SIGTERM handler (ioquake3, 2026-10-27: on a futex, SIGTERM blocked)
			[ \$i = 10 ] && [ -n \"\$c\" ] && pkill -KILL -x \"\$c\"
			f=\$(pgrep -a -x python3 | grep session-form.py | cut -d' ' -f1); [ -n \"\$f\" ] && kill \$f
			systemctl --user is-active -q walnut-session-$R || break; done
		systemctl --user is-active -q walnut-session-$R && echo '!! still running' || echo '>> PS3: $R session ended'"
	docker rm -f "play-pc-$E" > /dev/null 2>&1 && echo ">> PC: $E closed" || true ;;
collect)
	[ -n "$G" ] || { echo "usage: play-test.sh collect GAME" >&2; exit 2; }
	make -s bench-pull > /dev/null 2>&1 || echo "!! the pull failed (is the PS3 up?): the runs already here"
	python3 -I tools/play-collect.py "$R" "$E"
	echo ">> import: look at the shots, write REVIEW.md, then python3 -I tools/gamedb/import-run.py build/bench/<run> --game $E" ;;
*)
	sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2 ;;
esac
