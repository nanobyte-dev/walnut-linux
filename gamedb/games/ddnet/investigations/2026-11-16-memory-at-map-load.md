+++
id = "memory-at-map-load"
games = ["ddnet"]
driver_patches = []
game_patches = ["ddnet/0002"]
sessions = []
batches = []
+++

# Map images and fonts held in RAM at map load (2026-11-16)

Kernel 6.12.115+ps3.1554, ps3rsx 0.1589, Mesa 26.2.4-1+ps3.1508, our ddnet 20.1.1+ds-1+ps3.874, the
recipe's base (OpenGL 2.1, 1920x1080, the local server's default map Sunny Side Up). Unattended smoke
runs (`s1114b-batch` DDN, DDN2, DDN3, DDM) and one heaptrack run in the harness's bare X.

## Symptom

- VRAM swap off (`run.sh smoke`'s default, `NOVSWAP=1`; zram's 384 MB the only swap): DDNet is
  OOM-killed in every run, 5 of 5, at about 60 s or later. Glibc's malloc presets (`MALLOC=mmap` twice,
  `MALLOC=trim` once) didn't save it either.
- VRAM swap on (`NOVSWAP=0`, as on the desktop): it lives, with 84 fps steady but 20 `ps3rsx: out of
  VRAM` lines, 35.1 s of stalls in the window, a 29.0 s longest frame and a white rectangle in the sky,
  as on 11-14 (26 lines, 25.7 s).

## Evidence

- A sampler of `/proc/<pid>/smaps` every 3 s (DDN2, swap off): the main heap (`[heap]`) grows to 147 MB
  and the thread arenas to about 70 MB within a minute, nearly all of it swapped out, while the 64 MB
  CMA GART is full (CmaFree 332 kB) and the client holds about 190 MB of VRAM.
- heaptrack (`heaptrack` 1.5.0 from Debian, 50 s, the swap on): a peak heap of 209 MB. The callers,
  named with the dbgsym of a local rebuild (`make ddnet-deb`, ps3.1484: the same build id, bff70651,
  as the installed ps3.874, so the build is reproducible):

  | Peak | Caller |
  |---|---|
  | 73.6 MB, 34 calls | `ConvertToRgbaAlloc` in `CGraphics_Threaded::LoadTextureRaw` from `CMapImages::OnMapLoadImpl`: a copy of each embedded map image (already RGBA) for the texture command |
  | 31.1 MB, 4 calls | `io_read_all` in `CTextRender::LoadFonts`: the font files read whole |
  | 27.3 MB, 6 calls | `CImageLoader::LoadPng` from `OnMapLoadImpl`: the external map images (mapres) decoded |
  | 14.7 MB, 25 calls | `CLayers::InitTilemapSkip`: the map's tile layers |
  | 8.6 MB | `CSound::RateConvert`: the sounds resampled |

  Mesa's own allocations are small (1.2 MB under `glGenTextures`).
- Why the map images pile up: `LoadTextureRaw` and `LoadTextureRawMove` only queue a texture-create
  command with the pixel data in DDNet's command buffer. The render thread uploads and frees it when the
  buffer is run, at the next frame, and `OnMapLoadImpl` loads every image of the map before any frame.
  So all of the map's images, about 100 MB here, are in RAM at once, plus the map's own copy of each
  embedded image until `UnloadData`.
- At the OOM with the swap off, 110-124 MB of zram was still free: the RAM was zram's compressed store,
  the CMA GART (64 MB), about 25 MB of slab and 13 MB free; DDNet itself had about 5 MB resident. With
  zram the only swap, a peak this size doesn't fit, whatever malloc does with the freed blocks.

## Patch 0002

`packages/ddnet/patches/0002`: `LoadTextureRaw` and `LoadTextureRawMove` count the pixel bytes they
queue and run the command buffer once 16 MB wait (`AddCmd` kicks it the same way when a buffer is full;
the threaded backend waits for the previous buffer first, so about two batches at most are in RAM).
Tested on the PS3 the same afternoon (ps3.1485~dev, a local build, against the installed ps3.874):

- heaptrack, the same 50 s: peak heap 147 MB against 209 MB.
- VRAM swap off: still OOM-killed, 2 of 2 (the fonts, the map data and zram's own RAM are still too much).
- VRAM swap on, same boot, U P P U: stalls in the window 22.1 and 31.7 s against 34.8 and 36.6; longest
  frame 16.3 and 24.7 s against 27.9 and 27.3; 1% low 34.4 and 31.9 against 26.2 and 31.0; steady 95 and
  86 fps against 87 and 84; load 63.8 and 82.2 s against 86.2 and 88.0. Out-of-VRAM lines 87 and 21
  against 21 and 25. The pictures alike; the white rectangle is the map's keyboard tutorial image, drawn
  in one run of each arm and white in the other (a texture that didn't get VRAM: not the patch).

## Next

- The embedded images are copied once more (`ConvertToRgbaAlloc` on data already RGBA): handing the map's
  buffer over would save that copy too.
- The fonts: 31 MB read whole (`LoadFonts`); which four files, and whether FreeType could open them from
  the file instead.
- The out-of-VRAM lines and the white keyboard image with the swap on: VRAM, not RAM (about 190 MB held by
  the client); which textures fail to allocate.
