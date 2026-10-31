+++
component = "game-data-packager"
patches = ["patches/0004-Add-no-launchers-no-menu-entries-or-icons-in-the-Doo.patch", "patches/0014-no-launchers-no-icon-from-a-GOG-installer-either.patch", "patches/0018-no-launchers-no-menu-entry-or-icon-in-the-ScummVM-ga.patch"]
aliases = ["gdp 0004", "gdp 0014", "gdp 0018"]
category = "feature"
switch = "--no-launchers"
default = "off"
forwarded = "no (0004); not yet, goes with --no-launchers (0014, 0018)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager --no-launchers: data packages without menu entries or icons

Walnut ships its own launcher per game (one engine, chosen settings, a menu entry per game and edition), and
a second entry per data package would show up beside it. With `--no-launchers` the packages carry the data
files and the preinst only; without it nothing changes. The option grew in three steps:

- 0004: the Doom family's data packages (doom-wad, doom2-wad, heretic-wad, hexen-wad, ...) leave out their
  `.desktop` file, pixmap and hicolor icons.
- 0014: no icon from a GOG installer either. gdp converted the installer's `goggame-*.ico` on the fly, so
  nothing could know its checksum, and Walnut's import refused quake-rerelease from GOG's Quake Enhanced
  installer over it. Not covered by the host tests (it needs a real Inno Setup installer and python3-pil);
  the PS3 import is its check.
- 0018: the ScummVM games' packages leave out their menu entry, the link into game-data-packager-runtime and
  the icon, and the Recommends on the runtime.

One logical option, three patches: a candidate for folding into one ([MERGES.md](../MERGES.md)).
