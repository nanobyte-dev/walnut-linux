+++
id = "flight-smoke"
games = ["endless-sky"]
game_patches = ["endless-sky/0011", "endless-sky/0012"]
+++

# Endless Sky: the first flights without an OOM (2026-10-20)

Moved on 2026-10-26 from docs/TESTS.md's history (two rows of 2026-10-20: kernel .472 with the
snd_ps3 0047 test module, SSD root). The load-phase memory work before it (patches 0009-0011, the
OOMs of ssdroot7, 9 and 11) is [memory-and-renderer](2026-10-19-memory-and-renderer.md).

## ps3.602: no OOM, but no HUD (11:18-11:33, 13:20-13:29)

Batch ssdroot13 and es-flight: ps3.602 (sound samples read at their first play, patch 0011) as a
smoke flight and a menu run, then the flight again with photos at 300 and 420 s.

- No OOM any more: the flight's 480 s and the menu's 360 s ran out (high-water mark 92-108 MB; DXT
  took the textures from 373 to 91 MB; the game's buffers held 390 MiB of VRAM including the VRAM swap).
- The keys reach the game at about 240 s: Enter Ship and Depart work and the ship is in space at 300 s.
  A help dialog ("Lost 1") stayed up because Return came 10 s after Depart, too early on the PS3; the
  recipe now presses Return three times, 15 s apart.
- In flight there was no HUD, starfield or planet, and game.log had "out of temps!!" 36 times: the
  full outline shader is more than nv30's temporaries allow.

## ps3.624: playable at smoke level (14:18-14:27)

Patch 0012 makes the cheap ship outlines the default. In flight the starfield, asteroids, other
ships, the radar and the HUD text ("no target") show at 280 s; no "out of temps" in game.log; 480 s,
high-water mark 100 MB, no OOM. What is left (fps, stalls, the full HUD, the load's memory with
`MEM=1`) is in the game's todo.toml.
