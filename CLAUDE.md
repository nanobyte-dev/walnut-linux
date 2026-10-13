# PS3 / RSX tinkering

The plan is in docs/PLAN.md, background facts in docs/RESEARCH.md, layout and usage in README.md. Read them at the start of a session.

## Project hygiene
- Keep the layout in README.md. Sources/config are tracked; anything generated goes under build/.
- Never delete build outputs: `make clean` moves them to build/old/.
- backups/ holds the private NOR dump: never commit or upload it.

## Priorities
- Target is the PS3 directly: get existing nouveau/Mesa nv30 running on the RSX with the least new code. The Core 2 + 7600 GT is only a side reference.
- Don't optimize or fix nouveau/Mesa bugs until the baseline runs. Park side projects (power-control rig, HEVC decoder, Core 2 + 7600 GT setup) unless asked.

## PS3 safety rules (hard constraints)
- No permanent alterations to the PS3 beyond installing Linux (OtherOS++ / Petitboot / HDD region for Linux).
- Never leave it in a state that isn't recoverable. Back up NAND/NOR flash before anything touches flash, and verify the dump.
- GameOS (XMB) stays the default boot OS. The boot flag may be set to OtherOS temporarily (it is the only way into Petitboot), but must be set back to GameOS at the end of every session.
- No syscon writes, no fuse/eFuse or flash patches beyond the documented OtherOS++ install. Hardware mods only if explicitly asked.
- When unsure whether a step is reversible, stop and ask.
