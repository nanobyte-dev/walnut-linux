# Freeciv: technical notes

Also known as: freeciv-sdl2 (the SDL2 client, merged here).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Strategy / Alternatives and popularity": freeciv ships rulesets for Civilization I and II and is the pick for those games (gamedb:civilization, gamedb:civilization-2).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": the Freeciv SDL2 client (`freeciv-client-sdl`, in main): not a hard no; the only cost is its Depends on fonts-noto-cjk, fonts-ipafont-gothic and fonts-arphic-uming (+174 MB): edit the control file of the .deb or rebuild with them moved to Recommends; the client falls back to DejaVu for non-CJK text, no code change. RAM like the GTK3 client, small (not measured). Effort S; verdict "try now (low value: GTK3 client already covers Freeciv)".
