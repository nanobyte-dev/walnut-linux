# Scorched3D: technical notes

## Status

Debian's `scorched3d` and `scorched3d-data` 44+dfsg-8 (plus wxWidgets 3.2 for the launcher, about 135 MB
installed). Smoke runs on 2026-10-28 (kernel .874, Mesa ps3.924). The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) starts the client `scorched3dc -startcustom` with a
`~/.scorched3d/singlecustom.xml` made from the tutorial's (four computer tanks, "Random" AIs, nobody else: the
match plays itself) and `display.xml` with fullscreen 1920x1080.

- On the game's default display settings, the VRAM swap off (`run.sh smoke`'s default): OOM-killed 34 s in
  while the land was made (`ps3-bench-20261028T210926Z`, 490 MB virtual, the swap filling).
- With the game's own `fastestdisplay.xml` preset (no shadows, sky layers, waves, detail texture, model
  lighting; small textures) and the VRAM swap on (`NOVSWAP=0`, as game mode has it;
  `ps3-bench-20261028T211050Z`): the four bots play, the island, water, sky, HUD and kill messages drawn right
  (xwd at 30 and 90 s, webcam at 60 and 90 s), fullscreen (`geom=1920x1080+0+0 fs=1`). 17 fps steady on the
  Gallium HUD, 1% low 4, 66 frames over 100 ms (19 s of 90), the longest 4.3 s; 121 MB peak RSS; sound
  running, 3 audible dropouts. Runs, but memory-bound: the stalls are the swap.

- On the game's default display settings with the VRAM swap on (recipe variant `default`,
  `ps3-bench-20261028T231236Z`): no OOM, but 9 fps, 1% low 1.8, 61 s of 90 in frames over 100 ms, 133 MB RSS.
  So the defaults play but are unplayable: the fastest preset is the console's default (below).

Not checked: the wxWidgets launcher (`scorched3d`, the menu entry), a human player's turn, the sound by ear.

## Our package

`scorched3d` 44+dfsg-8+ps3.N ([packages/scorched3d](packages/scorched3d/)),
[0001](packages/scorched3d/patches/0001-OptionsDisplay-a-fresh-configuration-on-the-fastest-.patch): when
`~/.scorched3d/display.xml` doesn't exist (a first start, the menu's launcher too), the options start from
the game's own `fastestdisplay.xml` preset, fullscreen at the desktop's size (SDL's current video mode);
the launcher and the client both write the file back at once, so a player's settings decide after that.
On the PC (amd64 build, a fresh home, Xvfb 1280x720) the written display.xml has `FullScreen on`,
1280x720, `NoShadows on`, `TexureSize 0`, `NoGLExt on`, `DetailTexture off`. State: pending its PS3 test.


## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": Scorched3D 44, Debian `scorched3d` (125 MB); builds on ppc64; fixed function with optional GLSL/FBO/shadows: tier A; RAM UNVERIFIED; no benchmark mode (`-startclient` AI game, UNVERIFIED); OpenAL + Ogg, wxWidgets. Verdict for the suite: no.
