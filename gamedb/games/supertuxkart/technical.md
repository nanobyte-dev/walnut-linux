# SuperTuxKart: technical notes

SuperTuxKart 1.5 runs on its legacy GL 2.1 renderer with our big-endian fixes; without them the game quits
while loading its track models. It is the heaviest game we play: at the `lean` settings, now the
package's default for a fresh configuration, it races at 19-22 fps by the Gallium HUD in the benchmark,
and its played session on Mesa ps3.850 (2026-10-26) had a 24 fps median over five tracks with no crash
and no glitches. Light tracks play well, heavy ones (Ravenbridge Mansion) are slow, and a track's load
takes about 20-30 s.

## Packages and patches

`supertuxkart`, `supertuxkart-data` (`1.5-2+ps3.N`, [packages/supertuxkart](packages/supertuxkart/)),
replacing Debian's. `supertuxkart-data` is Debian's data, rebuilt because `supertuxkart` needs it at its
own version. 839 MB with the data (752 MB of it data), so it deploys by name and is in no suite.

| Patch | What |
|---|---|
| [0001](packages/supertuxkart/patches/0001-Load-.spm-meshes-on-big-endian-hosts.patch) | the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run (upstream #5578 was closed as not planned); all 1268 .spm files parse identically on amd64 and ppc64 |
| [0002](packages/supertuxkart/patches/0002-Right-colours-on-big-endian-with-the-OpenGL-driver.patch) | textures and vertex colours in the right channel order on the OpenGL driver |
| [0003](packages/supertuxkart/patches/0003-Legacy-renderer-minimap-render-target-power-of-two-s.patch) | legacy renderer: no crash when render-to-texture fails, a power-of-two (512x512) minimap render target |
| [0004](packages/supertuxkart/patches/0004-OpenGL-driver-texture-compression-for-opaque-power-o.patch) | texture compression reaches the legacy renderer: opaque power-of-two textures as DXT1, compressed by Mesa at upload |
| [0005](packages/supertuxkart/patches/0005-Profile-mode-STK_PROFILE_WARMUP-S-restarts-the-frame.patch) | `STK_PROFILE_WARMUP=S`: profile mode restarts its frame count and clock once the race passes S seconds and ends the race at S + the profile time |
| [0006](packages/supertuxkart/patches/0006-Console-defaults-for-a-fresh-configuration-the-lean-.patch) | defaults of a fresh configuration (a key already in `config.xml` keeps its value): the `lean` graphics (HD textures off, important particles only, drivers not animated, no minimap), texture compression on, the old-driver popup off, fullscreen at the desktop's resolution (`real_width`/`real_height` 0, resolved in `IrrDriver::initDevice`) |
| [0007](packages/supertuxkart/patches/0007-Game-controllers-Walnut-s-standard-scheme-second-bin.patch) | game controllers like a console game: a fixed scheme as the default bindings of any pad with the usual buttons, a second binding per action, a hot-plug scan |

**Controls (0007).** A pad's bindings come from its SDL game controller mapping
(`GamepadConfig::initSDLMapping`), once, when STK first sees that pad; 0007 rewrites that table. A pad
that already has an entry in `~/.config/supertuxkart/config-0.10/input.xml` keeps it (delete the entry,
or the file, to get the new scheme). R2 or R1 accelerate, L2 or L1 brake and reverse, the left stick or
d-pad steer (and move in menus), Cross skids (selects), Square fires the item, Circle nitro (back),
Triangle looks back, Select rescues, Start pauses. STK allows one binding per action, so 0007 adds a
second one (`DeviceConfig::setAltBinding`, saved as a second `<action ... alt="true">`; setting a binding
by hand clears it). A pad with fewer buttons gets the game's older default. Hot-plug:
`InputManager::update` looks every 2 s for a pad SDL lists that isn't open.

## How it performs

| Configuration | Steady fps (HUD) | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| base (the game's own defaults) | 12.6 | 13.5 s / 8.4 / 676 ms | 24 / 130 MB | gamedb:batch/2026-10-18-batch8 |
| `lean` | 21.4 | 2.4 s / 12.2 / 663 ms | 19 / 96 MB | gamedb:batch/2026-10-18-batch8 |
| `lean` (6 runs) | 19.1-21.0 | 1.3-2.7 s / 11.4-12.1 / 430-690 ms | 22-27 / 95-96 MB | gamedb:batch/2026-10-18-batch16 |
| `lean`, SSD root (n=3) | 20 (20-22) | 2.0 s (0.2-3.6) / 11.1 / 448 ms | 26 / 97 MB | gamedb:batch/2026-10-19-ssdroot1-baseline, gamedb:batch/2026-10-19-ssdroot2-force-dxt |
| `lean`, Mesa 0044 `NV30_MINMAX_VMX=1` (x5 against unset) | 22 against 20 | 0.6 against 1.2 s / 12.3 against 11.6 | | gamedb:batch/2026-10-20-ssdroot9-minmax-vmx |

- **Two frame rates.** The game's own "Average FPS" (105-111 for `lean`) is its physics tick rate, not a
  frame rate; the HUD's 19-22 is ([the investigation](investigations/2026-10-19-two-frame-rates.md)).
- **The load** is the suite's longest: 54.3 s on the USB root, 31.1 s on the SSD root, where the main
  thread's 23.8 s of CPU is 53% decoders (png 4.2, zlib 3.9, audio 2.2, jpeg 2.1 s)
  ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)). Our libpng16
  +ps3.560 took the png part from 4.0 to 1.6 s and the load from 30.3 to 27.6 s
  (gamedb:batch/2026-10-19-ssdroot6-libpng); our libvorbis -0.7 s, zlib-ng -0.9 s, and libjpeg-turbo's
  AltiVec path is in use (`JSIMD=none` +1 s) (gamedb:batch/2026-10-19-ssdroot5-load-libraries).
- **Gameplay CPU** (gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles): the game 36.5% (the main thread:
  Irrlicht's driver, Bullet), Mesa 35.5%, libc 15.1%, the kernel 5.1%, ps3rsx 2.6%, ALSA and OpenAL about
  3% ([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).
- **Played** (rules 2.0): on Mesa ps3.827 a crash on Ravenbridge Mansion after GART ran out, with garbage
  textures from dropped uploads before it; Mesa 0049 (ps3.850) fixed both, and the next session played
  five tracks with a restart, textures and kart shadows as on the PC
  ([the investigation](investigations/2026-10-26-fence-segfault.md)).

The benchmark ([recipe](benchmark/recipe.sh)): profile mode (`--profile-time`) starts a race at once with
every kart AI-driven, on lighthouse with 4 karts (`STK_TRACK`, `STK_KARTS`), fullscreen 1920x1080,
`--no-start-screen`; with `STK_PROFILE_WARMUP=60` and `--profile-time=180` it drives 240 s. The load ends
at the "Track: Overall scene complexity" line; GL 2.1 picks the legacy renderer by itself ("Using the
fixed pipeline"). setup() writes the whole `config.xml` every run; base is the game's defaults with HD
textures on. The race is AI-driven (`rand()` seeded from the clock, frame-time physics): close between
runs, not identical. Its GPU timers are garbage on the RSX (timer queries return a pixel count). The
sound state is read at 70 s: at 15 s on the slow USB root the game hadn't opened its audio device yet,
so the runs of 2026-10-17 and 10-18 logged `sound=none`.

## Known problems

- Slow on the heavy tracks (the legacy fixed pipeline on the PPE); objects popped in and out on
  Ravenbridge Mansion in the ps3.827 session (the dropped uploads, since fixed).
- OpenAL logs "Wait timeout... buffer size too low?" in about half the runs on the stock kernel: the
  sound card's DMA stopping, fixed by kernel local/0047 (gamedb:batch/2026-10-20-ssdroot12-dma-watchdog).
- Log noise: `irr_driver: Using the fixed pipeline (old GPU, or shaders disabled)` and `OpenGL: OpenGL
  version is too old!` in every run (the modern renderer needs GL 3.3, by design); 30 `User Config:
  Unknown value for X; expected true or false` and `Couldn't find parameter group ...` lines in 25 runs,
  because the recipe's `config.xml` holds only the attributes it sets (all keep their defaults); `SFXBuffer:
  Positional audio is not supported with stereo files` for WilhelmScream.ogg and a car sound; "out of
  VRAM" lines. Writing a full default config once would clear the config noise.
- The RSX wedged after the ps3.827 crash: X kept running but nothing drew, and a fresh X couldn't draw
  either; why the FIFO stopped is open (docs/TESTS.md).

## What was tried

| Change | Result | Verdict |
|---|---|---|
| Mesa 0038 `NV30_FORCE_DXT=opaque` on `lean` | forced 0 textures (182 kept for alpha, 173 for their format, 22 small, 6 non-power-of-two: the game's own compression already takes its opaque ones), fps and stalls equal (gamedb:batch/2026-10-19-ssdroot2-force-dxt) | nothing to gain; `=all` not run |
| Mesa 0044 `NV30_MINMAX_VMX=1` | +10% (22 against 20), stalls 0.6 against 1.2 s (gamedb:batch/2026-10-20-ssdroot9-minmax-vmx); +5% in gamedb:batch/2026-10-19-ssdroot7-minmax-vmx | a gain, candidate default |
| our openal-soft, Debian's, and `OAL=f32` | wedged 2, 3 and 0 of 5 runs: the wedge is the sound card, not OpenAL (gamedb:batch/2026-10-20-ssdroot10-openal-wedge) | the kernel fix instead |
| game prefetch | the USB load unchanged (gamedb:batch/2026-10-18-batch16) | off |

## Investigations

- [2026-10-17 the big-endian port](investigations/2026-10-17-big-endian-port.md): the .spm loader, the
  colours, the minimap target, VRAM 210 to 132 MB.
- [2026-10-19 the two frame rates](investigations/2026-10-19-two-frame-rates.md): the "Average FPS" is a
  tick rate.
- [2026-10-26 the GART crash and the garbage textures](investigations/2026-10-26-fence-segfault.md) (Mesa 0049).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a stretch goal (may work, but RAM or CPU will hurt), in its legacy GL 2.1 renderer mode; the assets are big, so memory will be the problem.
