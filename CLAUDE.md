# PS3 / RSX tinkering

The plan is in docs/PLAN.md, background facts in docs/RESEARCH.md, measured GPU facts in docs/RSX.md, packaging and the apt repository in docs/PACKAGING.md, layout and usage in README.md. Read them at the start of a session.

## Project hygiene
- Keep the layout in README.md. Sources/config are tracked; anything generated goes under build/.
- Never delete what we publish: official package versions on the repository server (distribution `ps3` and
  its image snapshots) are kept forever, and so is `build/aptly` (its database, backed up to the server).
  Everything else under build/ is ephemeral (local debs, images, chroots, source and work trees, build/old,
  test builds, debug symbols) and may be recreated or deleted. Exception: a patch work tree with unexported
  commits is unsaved source, never discarded.
- Workspace hygiene (/home is nearly full): heavy targets stop when the disk has less than 10% free;
  `make du` shows usage and `make clean-ephemeral` reclaims space. Experiments and agents work in
  `build/tmp/<name>` and delete their trees once the result is recorded. Caches are bounded (ccache
  max_size, apt cache for the current snapshot only, superseded builder images pruned).
- backups/ holds the private NOR dump: never commit or upload it.

## Priorities
- Target is the PS3 directly: get existing nouveau/Mesa nv30 running on the RSX with the least new code. The Core 2 + 7600 GT is only a side reference.
- Don't optimize or fix nouveau/Mesa bugs until the baseline runs. Park side projects (power-control rig, HEVC decoder, Core 2 + 7600 GT setup) unless asked.

## Memory trims (the user, 2026-10-17)
- Trim only what doesn't hurt quality of life: system services keep working and things a desktop
  user expects stay (wallpaper, applets, services in normal use). The memory comes from game mode:
  while a game runs, the rest gets out of the way (unloaded, frozen, paused) and comes back after,
  instead of being disabled for good. Careful work: an Opus agent, nothing risky shipped untested.

## PS3 safety rules (hard constraints)
- No permanent alterations to the PS3 beyond installing Linux (OtherOS++ / Petitboot / HDD region for Linux).
- Never leave it in a state that isn't recoverable. Back up NAND/NOR flash before anything touches flash, and verify the dump.
- GameOS (XMB) stays the default boot OS. The boot flag may be set to OtherOS temporarily (it is the only way into Petitboot), but must be set back to GameOS at the end of every session.
- No syscon writes, no fuse/eFuse or flash patches beyond the documented OtherOS++ install. Hardware mods only if explicitly asked.
- Cooling: nothing controls the fan under Linux (webMAN's control loop dies with GameOS). Before
  setting the OtherOS flag, webMAN fan control must be in SYSCON (automatic) mode, or a fixed
  speed of at least 60%. Keep Linux sessions supervised; stop on any "PS3 Thermal Alert".
- When unsure whether a step is reversible, stop and ask.
