# BZFlag 2.4.30 (Debian ports ppc64: bzflag-client, bzflag-data, and bzflag-server for the local
# game): smoke test, not benchmarked. The client joins a bzfs that during() starts on the same
# machine (a generated world, no list server registration) as "smoke" on the command line, which
# makes it connect at once (startupInfo.autoConnect), with `-solo 3`: three robot tanks the client
# drives itself, so there is something to shoot at. The client is fullscreen unless `-window` is
# given (bzflag.cxx: only `_window` makes a window), at the screen's resolution.
# Arrow keys or the mouse drive, the left button or Space... fires, Esc the menu.
# Settings: ~/.bzf/ (config.cfg, written at exit), restored after the run.
# What it exercises: GL 1.x fixed function through GLEW on SDL2, display lists, the radar's
# stencil; its own sound mixer on SDL audio. Risks: big-endian network code (the server and client
# on one machine at least agree), the robots' CPU on the PPE.
COMM=bzflag
REF_PKGS="bzflag-client bzflag-server"	# the PC reference (tools/bench-ref.sh): Debian's bzflag
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.bzf")
BZ_PORT=5154
BZ_SOLO=3
# -time: the sky follows the clock by default (a night sky at night); noon for a picture to compare
bz_cmd() { CMD=(/usr/games/bzflag -nolist -nomotd -time 12:00:00 -solo "$BZ_SOLO" "smoke@127.0.0.1:$BZ_PORT"); }
bz_cmd
# the local server; it dies with the run. SHOT_FROM=mark: the screenshots after the spawn
during() {
	[ -x /usr/games/bzfs ] || { echo "no bzfs: apt install bzflag-server"; return 1; }
	[ "$BZ_SOLO" -gt 0 ] || { echo "menu variant: no server"; return 0; }
	/usr/games/bzfs -p "$BZ_PORT" -ms 3 -j -t +r -a 50 38 2>/dev/null &
	local srv=$!
	trap 'kill $srv 2>/dev/null' TERM EXIT
	# after joining the tank waits dead ("Press Right Mouse or "i" to start"): i spawns it, Up drives
	smoke_xkeys BZFlag 10 i +3 Up Up Up; shot_mark
	wait $srv
}
SHOT_FROM=mark
