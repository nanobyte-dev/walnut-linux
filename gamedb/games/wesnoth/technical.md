# The Battle for Wesnoth: technical notes

## Status

Our ppc64 build of Wesnoth 1.18.8 (`wesnoth-1.18`, `wesnoth-1.18-server`, `1:1.18.8-1+ps3.N`,
[packages/wesnoth](packages/wesnoth/); Debian has none, Boost.Context #1146645): Debian's `wesnoth-1.18`
source built against our `boost-context`, with the build profile `pkg.wesnoth.archonly` (`packaging/`), so
only those two packages are ours and the data and campaigns are Debian's arch-all ones. Variant `ai` (two AIs on The Freelands, `--nodelay`, 20 turns) ran its whole 360 s on 2026-10-19
(gamedb:batch/2026-10-19-ssdroot8-smoke) with the right picture (units moving, "2 enemy units sighted"): 68 fps
steady, HUD median 114, 1% low 5.7, 98 frames over 100 ms with a 7.9 s load frame, peak RSS 110 MB, lowest
MemAvailable 15.3 MiB, CPU 179 s, 83 underruns; the 20 turns did not finish in that time. Expect 2-4 GB of
address space and about 130-215 MiB RSS. Not played yet.

- By default Wesnoth redirects its own stdout and stderr to `~/.config/wesnoth-1.18/logs/`, which the bench
  harness read as the game having ended (its first smoke, gamedb:batch/2026-10-19-ssdroot4-smoke, was killed
  after 1.7 s with no game.log); the recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) passes
  `--no-log-to-file`. Variants `nosound`, `window`, `menu`, `httt`, `ai`.
- game.log is full of `cannot open track '*.ogg'`: the Wesnoth music package is not installed, so there is
  only the effects' sound.
