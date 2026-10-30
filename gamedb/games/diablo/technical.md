# Diablo (DevilutionX): technical notes

Also known as: DevilutionX (the engine); Hellfire (the expansion, also played by DevilutionX).

## Status

DevilutionX 1.5.5 built by us (`devilutionx` `1.5.5-1+ps3.N`, built from
`gamedb/games/diablo/packages/devilutionx/`: DevilutionX is under the Sustainable Use License 1.0, non-commercial
and not DFSG-free; the user accepted that on 2026-10-29 ("I'm not using it commercially"), so it is public in the
testing suite, marked non-free in its description, README.Debian and the GameDB page, Section still `games`). Two patches:
0001 opens the audio device in the host byte order (`AUDIO_S16SYS`; `AUDIO_S16` is little-endian, so the PPE
swapped every sample twice); 0002 fixes the see-through mouse cursor (below). Upstream's defaults are already
console-like: fullscreen at the screen's mode (`SDL_WINDOW_FULLSCREEN_DESKTOP`, the picture widened to 16:9 and
scaled up), SDL game controllers on. The package links game-data-packager's `diablo-data` files
(`/usr/share/games/diablo/DIABDAT.MPQ`, Hellfire's `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, `hfvoice.mpq`)
into `/usr/share/diasurgical/devilutionx/`, a folder DevilutionX searches (it never looks in /usr/share/games).
The player's 1.09 DIABDAT.MPQ (517,501,242 bytes) is not the GOG file GDP knows (517,501,282), so GDP may
not take it as it is.

**The cursor (patch 0002, 2026-10-30).** DevilutionX draws a hardware cursor: `Source/hwcursor.cpp` hands SDL a
colour cursor (`SDL_CreateColorCursor`; sdl2-compat on SDL3, whose X11 driver copies the ARGB8888 surface into
an XcursorImage in host order: correct on big-endian). When the renderer scales the picture up (1920x1080 for
the game's 854x480: 2.25), it first converts the 8-bit sprite to `SDL_PIXELFORMAT_ARGB8888` and scales it with
its own `BilinearScale32` (`Source/utils/sdl_bilinear_scale.cpp`), which takes byte 3 of each pixel as the
alpha. ARGB8888 is a packed format: byte 3 is alpha on little-endian, blue on big-endian. On the PPE the
scaler used the blue channel as alpha, so Diablo's browns (no blue) went transparent and the rest blended
wrong. Upstream master still has it. 0002 uses `SDL_PIXELFORMAT_BGRA32` for that path (ARGB8888 on x86, so
nothing changes there; BGRA8888 on big-endian, alpha in byte 3). Checked on the PC with the X server's cursor
read back (XFixesGetCursorImage) under Xvfb at 1920x1080, the main menu's 80x67 cursor: the shipped ppc64
build (qemu-user) had 268 opaque, 1711 partly transparent and 3381 clear pixels; the patched ppc64 build and
the amd64 build both 1733, 665 and 2962. The host test
[packages/devilutionx/tests/hwcursor-alpha-test.cpp](packages/devilutionx/tests/hwcursor-alpha-test.cpp) runs
the same path on a test sprite: on ppc64 1322 of 2401 inner pixels lose alpha with ARGB8888, none with BGRA32;
none either way on amd64. Not seen on the PS3 yet (TESTS.md Next).

**The PC side.** DevilutionX isn't in Debian, so `tools/play-pc.sh diablo` plays our amd64 build: the recipe's
`REF_IMAGE=devilutionx-amd64`, made by `tools/ref-image.sh devilutionx` (after `make devilutionx-src`; a local
image, rebuilt the same way when the package changes). The data: the `diablo-data` .deb if one is in build/,
else `PLAY_PC_DATA_DIR=<folder with DIABDAT.MPQ>` mounted at `/usr/share/games/diablo` (the recipe's
`REF_DATA_DIR`). Tried 2026-10-30 under Xvfb: the intro, then the main menu at 1920x1080 with the player's 1.09
file.

The data is the player's: Diablo 1.09's `DIABDAT.MPQ` (517,501,242 bytes, sha256
`6bc120a603fdb383ff91f374e7db599765d6230d81e5a4c1b33f8254911774e7`, md5 `68f049866b44688a7af65ba766bef75a`),
in `~/.local/share/diasurgical/devilution/` (SDL's pref path for "diasurgical", "devilution": not
`devilutionx`; diablo.ini and the saves go there too; `--data-dir` names another folder).

Smoke runs on 2026-10-28 (kernel .874, Mesa ps3.924, the VRAM swap on; the last one
`ps3-bench-20261028T223638Z`): the recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) starts `devilutionx
--diablo -n` with a save folder of the run's own, then Return through the menus and a typed name (DevilutionX
fills one in only for a pad) into Tristram. The title menu, the hero creation, the difficulty and the town are
drawn right (fire, pentagrams, the Warrior, the panel's orbs; xwd and webcam), fullscreen 1920x1080
(`geom fs=1`). In town: 58 fps steady on the Gallium HUD, 1% low 25, 10 frames over 100 ms in 90 s (2.3 s,
the longest 425 ms), 48 MB peak RSS; the sound running with 8 audible dropouts (to be judged by ear). The hero
was saved (`single_0.sv`, an MPQ the game writes). DevilutionX found the DualShock 3 ("PS3 Controller"
mapping); the pad is not tried. Its `--demo`/`--timedemo` options would make a benchmark once a demo is
recorded.

Played session `ps3-bench-20261029T213401Z` (ps3.931~dev, 2026-10-29): 21.5 min, quit, 1273 s of play, fullscreen
1920x1080 (21 shots: Tristram at dusk, then the cathedral levels, the inventory open; in proportion, no bars). Median
59.3 fps, 1% low 23.9, longest frame 2.3 s, 1.8 s a minute of stutter, load 15 s, 47 MB RSS, 11 xruns, 3 audible dropouts
by the form's count. The form: sound and music heard, controls full, pad menus worked, play seamless, no glitches; its
note: "Very minor issue, cursor is semitransparent". The cursor is an X pointer (XFixes saw it in 16 of 21 shots), and
X cursors are not in the xwd captures, so the shots can't show it; reviewed as wrong colours or blending (-25, the
only category near it); the cause is found and fixed (patch 0002, above). No PC reference sheet: play-pc.sh had no
PC side for DevilutionX then (it has now) and upstream's x86_64 release was run by hand. Rated Gold 80, provisional (0.35 h of play): smoothness 62 is the weakest aspect.

## Engine options

- DevilutionX (the pick: complete, very active, 9.9k stars, BE prior art)
- devilution (the matching decompilation, Windows exe: DevilutionX's source)
- freeablo (stalled since 2023)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (shareware `spawn.mpq`); not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "RPG": Diablo + Hellfire (1996, PS1 too): DevilutionX, complete, SDL software; a Mac OS X Tiger PPC build (45 fps on an iBook G4), Amiga 68k, a BE network fix; shareware free; effort S.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "devilutionx (Diablo 1)": no hard no: the game renders an 8-bit palette surface itself, `SDL_CreateRenderer` only presents it; SDL1.2, SDL2 or SDL3. Not in Debian: own build (CMake, bundled libs). Big-endian good: explicit LE loads/stores (`Source/utils/endian_*.hpp`); changelog "Added support for 32bit PowerPc", "macOS 10.4 PowerPC", "Correct networking support on big-endian systems"; Amiga M68K port. RAM estimate 60-120 MB (runs on the 3DS in under 100 MB). Data: `spawn.mpq` from the Blizzard shareware (first dungeon levels; tolerated, not an open licence); full game `DIABDAT.MPQ` (GOG). Effort S. Verdict: TRY NOW, the best 2D candidate of part A.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Addendum: GameOS port exists?": no known PS3 port (its README's platform list has no PS3).
