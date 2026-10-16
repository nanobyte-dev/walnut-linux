# X-Moto 0.6.3: replays the "credits" replay (shipped inside xmoto.bin, which --replay doesn't read:
# assets/xmoto-credits.rpl is it, extracted, MD5 05a7200d...) with --timedemo --benchmark
# (draws every frame, prints the average and exits), fullscreen 1920x1080, no network. It sets no
# swap interval itself: vblank_mode=0.
SUITE=1
COMM=xmoto
ENV=(vblank_mode=0)
BACKUP=("$HOME/.config/xmoto/config.dat")
setup() {
	mkdir -p "$HOME/.local/share/xmoto/Replays"
	cp "$ASSETS/xmoto-credits.rpl" "$HOME/.local/share/xmoto/Replays/credits.rpl"
}
CMD=(/usr/games/xmoto --nowww -fs -res 1920x1080 --replay credits --timedemo --benchmark)
FPS_RE='Average framerate: ([0-9.]+) fps'
SOUND_AT=20
