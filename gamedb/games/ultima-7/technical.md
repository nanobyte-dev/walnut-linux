# Ultima VII (Exult): technical notes

Also known as: Exult (the engine); The Black Gate; Serpent Isle.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": "with the user's own data: Exult"; not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "The user's favourites / Alternatives": Ultima VII (SNES cut-down version): Exult, complete, old BE-friendly code (?), effort S; the pick (694 stars, last push 2026-10-12).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "exult (Ultima VII engine)": no hard no: SDL2 software image buffers, no GL renderer in the tree. Debian contrib `exult` 1.12.1-3, no ppc64 binary; build the client alone (S-M). Big-endian: a PPC OS X history in the ChangeLog; data read through Read2/Read4 helpers; BE cost S. RAM estimate 60-150 MB plus the sound font if MIDI goes through fluidsynth. Data: Black Gate / Serpent Isle are proprietary (GOG "Ultima 7 Complete", about 200 MB), no free set. Verdict: WORTH A BUILD, only if the user owns or buys the data.
