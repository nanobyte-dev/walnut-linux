# Quake III Arena on ioquake3 (the player's data): a played-session recipe only (no benchmark): the session harness starts the game from its
# menu launcher (rating/test.toml names the process and the settings set aside); CMD is the fallback.
COMM=ioquake3
CMD=(/usr/lib/ioquake3/ioquake3 +set fs_basepath /usr/lib/quake3/base +set sv_pure 0 +set vm_cgame 0 +set vm_game 0 +set vm_ui 0)
