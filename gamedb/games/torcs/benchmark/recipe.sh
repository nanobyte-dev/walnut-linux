# TORCS 1.3.7 (Debian ports ppc64: torcs, torcs-data 326 MB download): smoke test, not benchmarked.
# Menu only: `torcs -r` is the console mode (no window, no OpenGL), so the player starts the race
# by hand: Race > Quick Race > New Race (Enter and the arrow keys; Quick Race already has the
# human driver, 4 computer drivers and the Forza road track; steer with the arrow keys). The menu
# and the race are plib/freeglut under GLUT: fullscreen goes through glutGameModeString and XRandR
# (a 1920x1080 mode that X already has), which may fail on this X; variant window leaves
# fullscreen off (a 1920x1080 window under Openbox, probably taller than the screen).
# What it exercises: texture compression, the game's own "texture compression ARB" option
# (Configure > Display > Graphic in the menu), which makes plib upload GL_COMPRESSED_RGB(A)_ARB:
# Mesa makes DXT1/DXT5 of them (libspeq does DXT1 on the SPEs; DXT5 on the PPE until Mesa patch
# 0032 is built into a Mesa deb). setup() writes config/graph.xml with it enabled and a texture
# size limit of 1024, and config/screen.xml for 1920x1080 fullscreen; both are copies of the
# system files with those keys changed. The key names (section "OpenGL Features") are read from the
# binary's strings and unverified: if the menu shows the option disabled, set it there. Variant notc
# turns it off.
# Risks: plib's fixed-function drawing with many state changes and big textures (RAM: the
# cars and tracks are 526 MB of data, loaded per race), the simulation and 4 robots on the PPE
# (frame rate may drop with the car count), OpenAL through ALUT for the engine sound.
COMM=torcs-bin
. "$BENCH_LIB/smoke.inc"
CMD=(/usr/games/torcs)
TORCS_HOME=$HOME/.torcs
TORCS_DATA=/usr/share/games/torcs
TORCS_TC=enabled TORCS_FS=yes
BACKUP=("$TORCS_HOME/config/screen.xml" "$TORCS_HOME/config/graph.xml")
setup() {
	mkdir -p "$TORCS_HOME/config"
	sed -E -e 's|(name="x" val=)"[0-9]+"|\1"1920"|' -e 's|(name="y" val=)"[0-9]+"|\1"1080"|' \
		-e 's|(name="fullscreen" in="yes,no" val=)"[a-z]+"|\1"'"$TORCS_FS"'"|' \
		"$TORCS_DATA/config/screen.xml" > "$TORCS_HOME/config/screen.xml"
	sed -e '/<\/params>/i\  <section name="OpenGL Features">\n    <attstr name="texture compression ARB" in="enabled,disabled" val="'"$TORCS_TC"'"/>\n    <attnum name="user texture sizelimit" val="1024"/>\n  </section>' \
		"$TORCS_DATA/config/graph.xml" > "$TORCS_HOME/config/graph.xml"
}
variant_notc() { TORCS_TC=disabled; }
variant_window() { TORCS_FS=no; }
# no multitexturing (torcs -s), the fallback for old cards: the second texture unit is not used
variant_single() { CMD+=(-s); }
