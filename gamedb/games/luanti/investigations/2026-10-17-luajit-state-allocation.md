+++
id = "luajit-state-allocation"
games = ["luanti"]
driver_patches = ["luajit/0001"]
game_patches = []
sessions = []
batches = []
+++

# Luanti: LuaJIT's state allocated where it refuses it (2026-10-16/17)

**Symptom.** Debian has no ppc64 Luanti (its control lists only LuaJIT architectures; s390x builds
5.15.2 and passes the unit tests, a 2025 ppc64 attempt at 5.10.0 aborted in TestSAO). Our build started,
but `luaL_newstate()` failed about 40% of the time, and with it the game.

**Cause.** Debian's LuaJIT on ppc64 big-endian allocated its state between 2 and 4 GB, which LuaJIT itself
then refuses: it checks that the state lies in the low 2 GB.

**Fix.** Our luajit's 0001 (packages/patched/libs/luajit/, commit 331c539) makes ppc64 big-endian allocate
in the low 2 GB. Luanti's 330 unit tests pass with it. The library serves every consumer of LuaJIT on the
image, not only Luanti. A second big-endian bug in the same port was the sound: Luanti decoded Ogg Vorbis
in little-endian order, which played as noise (Luanti 0003, commit 7b5b968).
