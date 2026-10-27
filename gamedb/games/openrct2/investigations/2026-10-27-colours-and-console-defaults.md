+++
id = "colours-and-console-defaults"
games = ["openrct2"]
driver_patches = []
game_patches = ["openrct2/0007"]
sessions = []
batches = []
+++

# OpenRCT2: the blue screenshots and the console defaults (2026-10-27)

**Report.** In the data-pilot run (docs/TESTS.md, 10-27 17:43) OpenRCT2 0.5.6+ds-1+ps3.889 with
`roller-coaster-tycoon2-data` 89 (game-data-packager's import of the user's CD) showed the title, the windows
and Electric Fields in blue and purple in every xwd screenshot, while the 10-19 smoke test (the old
`rct2-data`, build ps3.574) had looked right. It also started in a 1280x720 window with the changelog open.

## The colours: the game draws right, the screenshot is wrong

Nothing was wrong with the game, the data or the renderer. Same build, same data, same config
(`drawing_engine = SOFTWARE_HWD`, the default a fresh config gets), started windowed:

- the **webcam photo of the TV** shows the right colours (turquoise water, green buttons, red-and-yellow
  helter-skelters), while the xwd taken at the same moment is blue and purple;
- **fullscreen** (`fullscreen_mode = 2`, scale 2) the xwd is right too. The 10-19 smoke test ran
  fullscreen and was judged on the webcam, so it never saw the problem. That's why it "looked right".

The cause is the window's X visual. SDL's GL window gets visual 0x8d, a **DirectColor** GLX visual whose
masks are byte-swapped: red 0xff00, green 0xff0000, blue 0xff000000. The root's TrueColor visual has
0xff0000/0xff00/0xff. Twelve DirectColor and eighteen TrueColor visuals the X server lists (`xdpyinfo`)
carry the swapped masks: these are the GLX visuals made from Mesa's configs on this big-endian host.
The pixels in the window are in the root's layout anyway (X server and scanout copy them unchanged
between same-depth drawables, so the TV is right). Only a client that reads the window and decodes it
through the visual gets it wrong. xwd does exactly that, `-root` included (it reads each visual's region
through its own masks and colormap):

- an `xwd -id <window>` dump decoded with the visual's masks: pixel `ff63bbbb` becomes (0xbb, 0x63, 0xff),
  purple; with the root's masks it is (0x63, 0xbb, 0xbb), the water's turquoise. The whole dump decoded with
  the root's masks is the right picture;
- the earlier gdp-step1 note "windowed games' xwd captures came out blue (R/B), fullscreen ones right" is
  the same thing, for every windowed GL game.

So the data (g1.dat matches gdp's checksum; it draws right) and the build are fine. No game patch.
Consequences:
- screenshots of **windowed** GL games on the PS3 lie. Judge them on the webcam or capture fullscreen.
  The harness's shots (`xwd -root`, bench/harness/client.sh) are right for our fullscreen recipes;
- the masks are a big-endian bug of the GLX visuals (Mesa's configs as the X server turns them into
  visuals: a candidate for the driver track, not fixed here).

## The console defaults: patch 0007

On a console the game is started from the menu on a TV. Upstream's defaults made a fresh configuration
start in a 1280x720 window with the changelog over the title screen (`last_run_version` is empty on a
fresh config, which upstream treats as an upgrade). [0007](../packages/openrct2/patches/0007-console-defaults-fullscreen-desktop-no-changelog.patch)
changes only what a configuration without these settings gets:

- `fullscreen_mode` defaults to 2, "Fullscreen (desktop)": SDL's fullscreen at the desktop's resolution, read
  at run time (720p TV 720p, 1080p TV 1080p);
- the changelog opens only after an upgrade (`last_run_version` set and older), not on the first start;
- the first start's display check (`InferDisplayDPI`, once, then saved) also picks the window scale for a
  TV: desktop height / 540 on a desktop of 1080 lines or more (2 at 1080p: a 960x540 canvas that SDL
  scales up), else 1. The software renderer paints the whole canvas on the PPE each frame; scale 2 is the
  setting the benchmark recipe measured (21-24 fps in Electric Fields) and a quarter of 1080p's pixels.

A configuration that already has the settings keeps them. The player's Options choices are never overridden.
Controllers are not part of this patch: OpenRCT2 0.5.6 has its own gamepad support (scrolling with a
stick), not checked yet.

## Verified on the PS3

openrct2 0.5.6+ds-1+ps3.940~dev (0001-0007) with roller-coaster-tycoon2-data 89, a fresh user directory
(`--user-data-path`, the object and scenario indexes copied in, no config.ini), started from the desktop's
slice: the window came up 1920x1080 fullscreen, the config it wrote says `fullscreen_mode = 2`,
`window_scale = 2.000000`, `last_run_version = "0.5.6"`, and no changelog opened. xwd and webcam agree on
the colours: the title (turquoise water, green buttons), RCT2's red scenario list, Electric Fields with its
objective window, the Financial Summary and Research & Development windows. The player's
`~/.config/OpenRCT2/config.ini` (written by the data-pilot run's windowed start) was set aside as
`config.ini.data-pilot-20261027`, so the next start from the menu gets the new defaults.
