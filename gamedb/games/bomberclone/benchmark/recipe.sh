# BomberClone 0.11.9 (our build, gamedb/games/bomberclone/packages/bomberclone/: closes audio and SDL at exit, fullscreen by default): a
# Bomberman clone, SDL 1.2 (sdl12-compat), up to 8 local or network players. Played sessions only.
COMM=bomberclone
CMD=(/usr/games/bomberclone)
# variant pad (a smoke run): our bomberclone 0003's menus through a virtual DualShock 3 (smoke.inc
# smoke_pad; the console's own DS3 stays connected: the game opens the first two pads and both work the
# menus), no keyboard: the D-pad down three times to Manual, Cross opens it, Start turns a page, Circle
# back, then up to Single Game and Cross into its menu. The shots from 20 s.
BC_PAD=0
setup() {
	[ "$BC_PAD" = 1 ] || return 0
	smoke_pad +12 down +1 down +1 down +2 cross +3 start +3 circle +2 up +1 up +1 up +2 cross
}
during() { [ "$BC_PAD" = 1 ] || return 0; sleep 20; shot_mark; }
variant_pad() { . "$BENCH_LIB/smoke.inc"; BC_PAD=1; NEEDS+=(evemu-play); SHOT_FROM=mark; }
