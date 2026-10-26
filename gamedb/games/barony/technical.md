# Barony: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, the user, "the rule of 2026-10-20 (no API veto)": GL 3 is no longer a veto: a renderer port to GL 2 is an effort estimate.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "barony": hard no then: `src/init.cpp:1563` asks for a GL 3.2 core context; GLSL `#version 150 core`; glew; no other renderer. Not in Debian. Needs the purchased data (README: "you still need a purchased copy of Barony"). Verdict: DROP (hard no).
