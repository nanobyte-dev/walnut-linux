+++
id = "memory-at-map-load"
games = ["ddnet"]
driver_patches = []
game_patches = []
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

## Next

- A ddnet patch so the map's images don't all wait for the first frame: run the command buffer after
  each image (or every few MB) in `OnMapLoadImpl`, and hand the embedded images' data over without the
  `ConvertToRgbaAlloc` copy when it is already RGBA. Measured by the same heaptrack run (the peak) and
  the swap-on smoke (stalls, out-of-VRAM lines).
- The fonts: 31 MB read whole (`LoadFonts`); which four files, and whether FreeType could open them from
  the file instead.
- The white rectangle and the out-of-VRAM lines with the swap on: VRAM, not RAM (about 190 MB held by the
  client); which textures fail to allocate.
