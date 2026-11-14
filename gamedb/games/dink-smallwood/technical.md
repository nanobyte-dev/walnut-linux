# Dink Smallwood (GNU FreeDink): technical notes

Also known as: GNU FreeDink (the engine).

## Status

Debian's GNU FreeDink 109.6 (`freedink`, `freedink-engine`, `freedink-data` 1.08.20190120, about 95 MB with
`freepats`) installs on the PS3 from Debian ports with `--no-install-recommends`; no patch of ours. Smoke run on
2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114-smoke): the title screen
with Start, Continue and Quit, fullscreen 1920x1080 (the 640x480 picture stretched, no bars), the same as the PC
reference, both on the default display (the 8-bit frame drawn in software, shown through OpenGL) and on SDL's
software renderer (variant `sw`). The menu's hand pointer follows relative mouse motion: an XTEST click on
Start and the arrow keys with Ctrl did not move it onto Start on the PC, so the recipe stops at the title
([benchmark/recipe.sh](benchmark/recipe.sh)). Not played yet; the sound not heard.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "RPG": Dink Smallwood (1997): FreeDink (?), complete (?), SDL2, freeware, S.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": GNU FreeDink (Savannah; only a dead GitHub mirror): complete, GPL, the only option.
