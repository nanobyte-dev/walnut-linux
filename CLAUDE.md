# PS3 / RSX tinkering

The docs are indexed in docs/README.md. The plan is in docs/PLAN.md, the PS3 test log in docs/TESTS.md, background facts in docs/research/RESEARCH.md, measured GPU facts in docs/RSX.md, packaging and the apt repository in docs/PACKAGING.md, layout and usage in docs/LAYOUT.md. Read them at the start of a session.

## Project hygiene
- Keep the layout in docs/LAYOUT.md. Sources/config are tracked; anything generated goes under build/.
- Never delete what we publish: official package versions on the repository server (distribution `ps3` and
  its image snapshots) are kept forever, and so is `build/aptly` (its database, backed up to the server).
  Everything else under build/ is ephemeral (local debs, images, chroots, source and work trees, build/old,
  test builds, debug symbols) and may be recreated or deleted. Exception: a patch work tree with unexported
  commits is unsaved source, never discarded.
- Workspace hygiene (/home is nearly full): heavy targets stop when the disk has less than 10% free;
  `make du` shows usage and `make clean-ephemeral` reclaims space. Experiments and agents work in
  `build/tmp/<name>` and delete their trees once the result is recorded.
- Overflow space (the user, 2026-10-18): `/mnt/data_linux/Temp/ps3` (about 150 GB free) may hold
  build output when /home runs short; move ephemeral things there rather than deleting them.
  `build/aptly` and its backups stay on /home. Caches are bounded (ccache
  max_size, apt cache for the current snapshot only, superseded builder images pruned).
- backups/ holds the private NOR dump: never commit or upload it.

## Priorities
- Target is the PS3 directly: get existing nouveau/Mesa nv30 running on the RSX with the least new code. The Core 2 + 7600 GT box is for upstreaming driver fixes (the user, 2026-10-18): our nv30 Mesa patches tested on a stock x86 nouveau system the way upstream wants them (piglit/deqp, little-endian); not for game testing or benchmarks, which stay on the PS3.
- Don't optimize or fix nouveau/Mesa bugs until the baseline runs. Park side projects (power-control rig, HEVC decoder, Core 2 + 7600 GT setup) unless asked.

## Memory trims (the user, 2026-10-17)
- Trim only what doesn't hurt quality of life: system services keep working and things a desktop
  user expects stay (wallpaper, applets, services in normal use). The memory comes from game mode:
  while a game runs, the rest gets out of the way (unloaded, frozen, paused) and comes back after,
  instead of being disabled for good. Careful work: an Opus agent, nothing risky shipped untested.
- Game mode turns on and off by itself (the user, 2026-10-17): any fullscreen 3D app is detected and
  gets it, with an env variable (PS3_GAME_MODE=off) to opt out; launchers aren't the only way in.

## PS3 safety rules (hard constraints)
- No permanent alterations to the PS3 beyond installing Linux (OtherOS++ / Petitboot / HDD region for Linux).
- Never leave it in a state that isn't recoverable. Back up NAND/NOR flash before anything touches flash, and verify the dump.
- GameOS (XMB) stays the default boot OS. The boot flag may be set to OtherOS temporarily (it is the only way into Petitboot), but must be set back to GameOS at the end of every session.
- No syscon writes, no fuse/eFuse or flash patches beyond the documented OtherOS++ install. Hardware mods only if explicitly asked.
- Cooling: nothing controls the fan under Linux (webMAN's control loop dies with GameOS). Before
  setting the OtherOS flag, webMAN fan control must be in SYSCON (automatic) mode, or a fixed
  speed of at least 60%. Keep Linux sessions supervised; stop on any "PS3 Thermal Alert".
- Fan policy from Linux is allowed (the user, 2026-10-18): only through the System Manager's fan-policy
  service, runtime only. Linux puts the fan in SYSCON automatic mode at boot and on every exit path
  (shutdown, reboot, panic), so we don't depend on what GameOS/webMAN left set. Manual mode only as a
  fixed floor at or above a safe duty (no control loop that needs the PPE to react in time). Test new
  fan code in the safe direction first (full speed, then back to auto). Other SYSCON writes stay forbidden.
- When unsure whether a step is reversible, stop and ask.
