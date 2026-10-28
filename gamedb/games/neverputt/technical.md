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


## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": "Neverball 1.6 (+Neverputt)": neverball 112 MB, with neverputt 138 MB installed; builds on ppc64 (tests disabled); GL fixed function, VBO and point parameters required, GLSL/FBO optional: tier A.
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Neverball / Neverputt" a strong candidate: light, uses a gamepad well, packaged in Debian.
