# Mr.Boom: technical notes

Mr.Boom 5.5, a Bomberman clone for up to eight players (SDL2, pads first). Debian's ppc64 binary crashed
within 15 s of every start on the PS3: both played sessions of 2026-10-26 ended in a segfault in the game's
own code on the welcome screen (Scrap). The cause is a big-endian bug in the SDL2 build, now fixed in our
package; the fix passes every recorded demo under qemu, and the PS3 check is still to come.

## Packages

- `mrboom` (`5.5+ds-1+ps3.N`, [packages/mrboom](packages/mrboom/), Debian's source with one patch),
  replacing Debian's. In no suite. It has its CI workflow and goes through testing first.
- [0001](packages/mrboom/patches/0001-mrboom.h-big-endian-layout-for-the-SDL-front-ends.patch) defines
  `MSB_FIRST` from the compiler's byte order when the libretro header isn't used, so the emulated 8086
  registers get their big-endian layout in the SDL2 front end too
  ([the investigation](investigations/2026-10-26-big-endian-registers.md)).
- Fullscreen is already the default: rendered at 1920x1080 through sdl2-compat, the 320x200 picture stretched to
  16:9.

## How it performs

The two sessions on Debian's 5.5+ds-1 (Mesa ps3.860, kernel .786) reached the welcome screen at 52-55 fps
median before the crash, 13-14 s of play, a 15 s load, 58 MB RSS. Nothing about play is measured yet. The
played test ([rating/test.toml](rating/test.toml)) starts `/usr/games/mrboom` (no menu launcher of ours); the
recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) is for sessions only, no benchmark.

## Known problems

- Until our package is on the console: the crash within half a minute.
- Esc on the welcome screen quits the game outright (upstream behaviour, unchanged).

What the next session checks (docs/TESTS.md, batch mrboom-be): the welcome screen's text and the attract
demo for a minute, the DualShock 3 joining as player 1 (any button), Space adding bots, a full match on 2-3
levels and back to the menu, no segfault in dmesg, the picture against the PC (palette, sprites, no garbage),
the music and effects by ear; then `mrboom -e -z` from a terminal, which should end `SUCCESS check demos`.

## Investigations

- [2026-10-26 the 8086 registers in little-endian layout](investigations/2026-10-26-big-endian-registers.md).
