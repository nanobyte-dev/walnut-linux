# Red Eclipse: technical notes

## Status

Red Eclipse 1.6.0 is Debian's (`redeclipse`, `redeclipse-common`, `redeclipse-data`: 842 MB download, 959
MB installed), the Cube 2 engine family without our patches. It is smoke-tested, not benchmarked: it
runs in a match on torus as a spectator at lean settings and 11 kHz mono sound, a player name set, no
OOM, 86 fps HUD peak (2026-10-18). The first run was OOM-killed loading torus: the memory is the sound
([the investigation](investigations/2026-10-18-oom-the-memory-is-the-sound.md)). Not played yet.

- Recipe [benchmark/recipe.sh](benchmark/recipe.sh) (`SUITE=0`): `-x'dm <map>'` (a deathmatch with bots
  on the engine's loopback server); `RE_MAP` or the variants `singularity`, `disco`, `cyanide` pick the map
  (torus, 43 KB, is the smallest real one). Variants `tex512`, `fullsound`, `bots0`, `bots3`, `defaults`,
  `menu`.
- Its settings, written by setup(): `soundmono 1` and `soundfreq 11025` into `init.cfg` (they count only
  before the audio device opens); autoexec.cfg sets `vsync 0`, `maxfps 0`, `texreduce 3`, `maxtexsize
  256`, `shaderdetail 1`, and turns off water reflection and refraction, env maps, caustics, glare, grass,
  shadow maps, depth effects, motion blur, dynamic lights, bump- and env-mapped models, GPU skinning,
  decals, anisotropy, gore, ragdolls and mumble (every name checked in the 1.6 source), and ends with
  `playername RE_NAME` (the first start's "Welcome to Red Eclipse" profile menu stays up while the name is
  empty and blocks spawning). 1.6 reads `localinit.cfg` for the loopback server (not `localexec.cfg`), and
  the bot count is `sv_botlimit` (1 bot).
- `usetexcompress` is read-only in 1.6 (min 1 > max 0), so texture compression needs cube2's 0001, which
  applies to Red Eclipse's tree.
- Next: cube2's memory patches carried over, samples loaded when first played (the game's todo).

## Investigations

- [2026-10-18 the OOM: the memory is the sound](investigations/2026-10-18-oom-the-memory-is-the-sound.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "Red Eclipse 2 (Tesseract-derived engine, redeclipse/base master 2.0.9)": Red Eclipse 2 is XL: a deferred-only renderer (G-buffer with 4 MRT, shadow atlas, radiance-hint GI, volumetrics, SMAA), no forward path. It accepts GL 2.0 contexts and EXT_framebuffer_object and nv30 meets its hard limits, so it may even start, but every light is a fullscreen-class pass over a 1080p G-buffer, far beyond the RSX. Debian's 1.6 (cube2) is the right Red Eclipse for the PS3. Scores: popularity 3, novelty 5, playability on 256 MB 1.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": Red Eclipse 2: out (fill rate); 1.6 already runs.
