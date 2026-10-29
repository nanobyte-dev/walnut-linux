# Neverputt: technical notes

## Status

Our `neverputt` and `neverputt-data` from the neverball source package (gamedb:neverball's packages,
`1.6.0+git20180603-3+ps3.N`; dev build `ps3.931~dev` installed with `dpkg -i`, Neverball's packages with it).
Smoke run on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap on; `ps3-bench-20261028T230058Z`): without a
`~/.neverball/neverputtrc` (the recipe sets the player's aside and restores it), Return three times by XTEST
(Play, the first course, the first hole): hole 1 with the flag, the course, the score and par, fullscreen
1920x1080 from the package's new default (neverball 0004). 129 fps steady on the Gallium HUD, 1% low 88, sound
running with 6 audible dropouts, 47 MB peak RSS. Not checked: a putt, the pad, the sound by ear. The recipe:
[benchmark/recipe.sh](benchmark/recipe.sh).

Played session `ps3-bench-20261029T212316Z` (ps3.931~dev, 2026-10-29): 5.7 min, quit, 328 s of play, fullscreen 1920x1080
(five shots, three courses of par 2, 3 and 9, in proportion, no bars, the pointer hidden). Median 60.0 fps, 1% low 44.5,
longest frame 194.6 ms, 0.33 s a minute of stutter, load 15 s, 44 MB RSS, 0 audible sound dropouts (17 xruns of 15 ms).
The form: sound and music heard, controls full (the DualShock opened, 22171 pad events in play, one key press), pad menus
worked, play seamless, no glitches. No PC reference sheet. Rated Platinum 96, provisional: stability is scored from 5
minutes of play (90, the start of a crash-free game).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": "Neverball 1.6 (+Neverputt)": neverball 112 MB, with neverputt 138 MB installed; builds on ppc64 (tests disabled); GL fixed function, VBO and point parameters required, GLSL/FBO optional: tier A.
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Neverball / Neverputt" a strong candidate: light, uses a gamepad well, packaged in Debian.
