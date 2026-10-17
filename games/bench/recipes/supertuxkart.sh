# SuperTuxKart 1.5 (Debian; 839 MB with its data, deployed by name, not in ps3-games). Its profile
# mode (--profile-time) starts a race at once with every kart driven by the AI, drives for the given
# seconds, prints "Number of frames: N time T, Average FPS: F" and quits. GL 2.1 picks the legacy
# renderer by itself. RAM is the known risk (its FAQ wants 1 GB against our 217 MB): the first runs
# use a small track and 4 karts; the config (~/.config/supertuxkart/config-0.10/config.xml) is made
# by the game on its first run and kept between runs.
COMM=supertuxkart
STK_CFG=$HOME/.config/supertuxkart/config-0.10/config.xml
BACKUP=("$STK_CFG")
STK_TRACK=lighthouse STK_KARTS=4 STK_TIME=60
stk_cmd() {
	CMD=(/usr/games/supertuxkart --fullscreen --screensize=1920x1080 --track=$STK_TRACK
		--numkarts=$STK_KARTS --profile-time=$STK_TIME --no-start-screen)
}
stk_cmd
FPS_RE='Average FPS: ([0-9.]+)'
DURATION=240
