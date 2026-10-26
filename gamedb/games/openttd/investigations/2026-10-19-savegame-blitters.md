+++
id = "savegame-blitters"
games = ["openttd"]
driver_patches = []
game_patches = ["openttd/0003"]
sessions = []
batches = ["2026-10-19-ssdroot3-openttd-smoke", "2026-10-19-ssdroot3-openttd-sound", "2026-10-19-ssdroot3-openttd-buffers", "2026-10-19-ssdroot3-openttd-blitters"]
+++

# OpenTTD: the 1962 savegame, and why the 8bpp-optimized blitter became the default (2026-10-19)

**Symptom.** On the new savegame benchmark (820 vehicles, 30 towns, the camera on a city of 5000, sound
and music on, the GL driver) Debian's default 32bpp-anim blitter ran at 10-14 fps, mostly 11, with 22-54
s of gameplay stalls in the 120 s window, 3500-4700 underruns, 13-22 MB free.

**Evidence** (batch ssdroot3: SSD root, kernel .472, Mesa ps3.476, openttd ps3.297, three rounds per group):

| Variant | Steady fps | 1% low | Gameplay stalls | Underruns | Free RAM |
|---|---|---|---|---|---|
| `base` (32bpp-anim), 9 runs | 10-14, mostly 11 | 5.5-7.5 | 22-54 s | 700-4700 | 13-22 MB |
| `blit8` (8bpp-optimized) | 34, 35, 43 | 13-15 | 0.3-0.5 s | 2600-4000 | 41 MB |
| `blit32` (32bpp-optimized) | 13-14 | 6-6.5 | 26-35 s | 3400-4400 | 18-26 MB |
| `nosound` / `nomusic` / `quiet` | 14 / 13 / 14 | 7-8 | 19-21 / 24-28 / 19-21 s | 0 / 63-107 / 0 | |
| `hz22`, `buf2048`, `buf4096` | 11-14, as `base` | 5.7-7.5 | 21-46 s | 80-3500 | |
| `zoomout` | 7 | 4.0-4.3 | 115-120 s | 3200-4400 | 9-18 MB |
| `scroll` | 7-8 | 3.9-4.4 | 100-116 s | 2700-4600 | |
| `simplay` (the save's simulation only) | 6000 ticks in 82.7 s | | | | 51 MB |

32bpp-anim draws every sprite into a 32-bit buffer and keeps an 8-bit animation buffer next to it; the
8bpp-optimized blitter writes one byte a pixel and the GL driver applies the palette on the GPU. So the
frame rate triples and a frame no longer takes 100 ms or more. 32bpp-optimized (no palette animation)
gains only 2 fps. The picture is the same: webcam pairs of `base` and `blit8` at 60 s and 150 s show the
same sprites, town labels, bridges, water and date bar (the speckled water is the webcam catching the
palette animation mid-cycle, in both). OpenGFX is an 8bpp base set, so nothing the 8bpp blitter loses
shows; 32bpp-only NewGRFs would. Zooming out and scrolling were the worst case: the sprite count per
frame. The music costs about 3 fps (-20%) and 25-30 s of stalls.

**Fix.** openttd 0003: `support8bpp` defaults to `system`, so OpenTTD picks the 8bpp-optimized blitter for
an 8bpp base set; 32bpp NewGRFs still get a 32bpp blitter. The zoom and scroll cases turned out to be the
per-frame staging buffers ([the next investigation](2026-10-19-staging-buffer-per-frame.md)): with Mesa
0040 they ran at 23 and 31 fps.
