# Duke Nukem 3D: technical notes

## Engine options

- EDuke32 (the pick: classic Build software renderer by default, Polymost/Polymer GL optional; the most-used Build port; voidpoint.io GitLab)
- jfduke3d (classic port, PPC Mac history)
- icculus/duke3d (the original Linux port)
- chocolate_duke3D (a readable port, archived)
- NBlood's tree (EDuke32-based; also Blood and Redneck Rampage)
- Raze (Build games on GZDoom's renderer: GL 3.3, or a GLES2-class `gles` backend that is unproven on nv30; M-L big-endian; not worth it: EDuke32 covers Duke with a better renderer for this GPU)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": EDuke32 (Duke3D shareware) on the "try now" list; not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": Duke 3D (with a console version): EDuke32, playable, Polymost GL plus software, PPC Mac history (?), Duke shareware, effort S; EDuke32 is the pick (software mode suffices on the PPE).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "eduke32": no hard no: the classic Build software renderer (8-bit palette, default) is always there; Polymost (OpenGL) and Polymer are options, `USE_OPENGL := 0` turns GL off. Not in Debian: a straight `make` (SDL2, SDL2_mixer). Big-endian: `B_BIG_ENDIAN`/`B_SWAP*` in compat.h, a DEVKITPPC (Wii/GameCube) platform; BE cost S. RAM estimate 40-100 MB classic. Data: the Duke Nukem 3D shareware (`DUKE3D.GRP` v1.3d, episode 1, freely distributable, about 11 MB); the Atomic Edition from GOG. Effort S. Verdict: TRY NOW, the best FPS candidate.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "rednukem, NBlood, Raze": Raze: GL backend requires 3.3; a `gles` backend picks `GLES_MODE_OGL2` below 3.3 with shader version "100" (unproven on nv30); 300-600 MB; M. Verdict: Raze NOT WORTH IT (EDuke32 covers Duke).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Addendum: GameOS port exists?": partial: EDuke32 has Wii/PSP ports, no known PS3 one.
