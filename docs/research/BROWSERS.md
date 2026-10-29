# A browser with JavaScript on the PS3 (research, 2026-10-28)

Walnut ships NetSurf 3.11 with JavaScript off. The user finds a browser without JavaScript of little use, and on
2026-10-28 picked WebKitGTK as Walnut's browser, then our own build of it (below). This note looks at what could run
JavaScript on Debian sid ppc64 (big-endian, debian-ports) with 256 MB of XDR, the in-order Cell PPE and a GL 2.1
GPU, and at the two ways to make one fit: slim WebKitGTK, or teach NetSurf to redraw pages that scripts change.
Ladybird was ruled out earlier (Skia has no big-endian support, Qt 6, memory) and is not repeated here.

## Where it stands

**Our own WebKitGTK 2.52.6 build, with the big-endian fixes, is built and waits for its PS3 test.** The user chose
it on 2026-10-28 over NetSurf with a relayout patch of ours ("I'd trust an unpatched WebKit more"), and asked for
the features we don't need to be left out where that saves memory or attack surface.

- **The engine:** `webkit2gtk 2.52.6-1+ps3.N` (`packages/patched/desktop/webkit2gtk/`): Debian's 2.52.6-1 source
  (fetched from the 2026-09-25 snapshot; sid has moved to 2.54), only the GTK 3 (4.1 API) flavour Badwolf needs,
  with the cuts in [Our build](#our-build) and seven patches of ours:
  1. CompactVariant keeps a small payload in the storage's low-order bytes on big-endian (the abort on nearly
     every page, WebKit [bug 300442](https://bugs.webkit.org/show_bug.cgi?id=300442); no patch upstream);
  2. the LLInt read the callee at `Callee + PayloadOffset` when WebAssembly is off: fine where PayloadOffset is
     0, but 4 on 64-bit big-endian, so every caught exception crashed (`try { null.x } catch {}` in `jsc`,
     Wikipedia in the browser). Found by running our ppc64 build under qemu-user (below);
  3. Cairo's ARGB32 pixels are native-endian words (A, R, G, B in memory on big-endian) but the canvas pixel
     conversions read them as BGRA bytes: `getImageData` gave opaque red as transparent black;
  4. the HTML fast parser's UTF-16 de-interleave (`SIMD::findInterleaved`) took the high bytes for the low ones on
     big-endian, so `innerHTML` from a 16-bit string read tags as text;
  5. `Uint8Array.fromHex`/`toHex` swapped nibbles on big-endian;
  6. no view transitions without accelerated compositing: `document.startViewTransition()` forced compositing
     mode, and a UI process without an accelerated backing store (Badwolf: acceleration NEVER, no DMA-BUF
     renderer) crashed. YouTube took Badwolf down in seconds on the PC, with Debian's build too;
  7. a missing include the smaller feature set exposes.
  Built on the dev PC (`BUILD=xcc`, [PACKAGING.md](../PACKAGING.md#built-on-the-dev-pc)), never by CI; published
  to testing from there once the PS3 test passes.
- **Tested so far:** on the PC (amd64) the whole build, Badwolf on it and every page below. On ppc64, without
  the PS3: fixes 1, 4 and 5 with standalone tests of the patched headers under qemu-user (the stock CompactVariant
  aborts, the patched one passes; the stock `findInterleaved` missed '<' in 31 of 33 positions); then the ppc64
  packages themselves in a ppc64 chroot under qemu-user with Xvfb, the way the PS3 runs them: `jsc` runs a set
  of checks (JSON, Intl, BigInt, typed arrays, the hex methods) on the C_LOOP interpreter; Badwolf draws the
  GameDB home (rounded corners, the filter's script running, "23 of 23 games"), the `?q=doom` page and the
  [self-test page](webkit/be-test.html) (13 of 13 with `2.52.6-1+ps3.1062`; its canvas lines failed until fix 3); Wikipedia crashed the web process
  until fix 2 (found from the qemu core: the faulting load was `convertJSCalleeToVM` on a half-pointer). How the
  bugs were found besides: Debian's crash report, and a read of WTF, JSC and WebCore for the same class of bug
  (a smaller value packed into the start of a wider word, byte-wise SIMD on 16-bit data, BGRA bytes).
- **Not tested yet: the PS3.** The first PS3 session (2026-10-28) only got as far as the video baseline in mpv,
  whose `--vo=gpu` wedged the RSX (graph exception 0x105, [RSX.md](../RSX.md#errors-and-what-wedges-the-gpu),
  being fixed separately). The browser itself draws without GL (acceleration NEVER, no DMA-BUF renderer, no
  GStreamer GL), so it does not use that path; video in the browser waits for that fix. The test plan is
  [below](#ps3-test-plan).
- **The shell:** `badwolf` 1.4.0-1+ps3.N (`packages/patched/desktop/badwolf/`, [DESKTOP.md](../DESKTOP.md#the-browser)),
  built for ppc64 by CI. Not the default and not in the image. Why Badwolf and what the patches do:
  [The shell](#the-shell-badwolf). It needs no rebuild for our engine (same API and soname).
- **No newer engine:** WebKitGTK 2.54 removed the Cairo renderer, and Skia, the only one left, does not build on
  big-endian. Debian closed its ppc64 crash report ([#1117148](https://bugs.debian.org/1117148)) with "WebKitGTK no
  longer supports big-endian architectures" and is removing webkit2gtk from the big-endian ports
  ([#1148038](https://bugs.debian.org/1148038)). Porting Skia is in the backlog
  ([platform.md](../backlog/platform.md#skia-on-big-endian)).

**Security: we carry 2.52 alone.** 2.52.6 is the last 2.52 release; WebKitGTK's security advisories now name
2.54.x, and their fixes will not be backported to 2.52 by anyone but us. Each advisory's CVEs need reading against
2.52, and the ones that apply (and are small enough) need porting into this package, for as long as we ship it.
What makes the gap smaller: no JIT and no WebAssembly on ppc64 (the C_LOOP interpreter), which rules out the JIT
bugs that make up much of WebKit's advisories; the features we leave out ([Our build](#our-build)); and our pages,
the GameDB, being the main target. What makes it bigger: no sandbox (the bubblewrap sandbox needs user namespaces
our kernel lacks, and the GTK 3 API never turns it on), so a web process runs with the user's rights; and the
media stack (GStreamer, libav) parses whatever a page plays. A browser for our own pages and the occasional site is
reasonable; daily browsing of arbitrary sites on an unmaintained engine is not, and the docs for users should say so.

The paths not taken (NetSurf with DOM-driven relayout, 1-2 weeks; NetSurf as it is; WPE with cog) stay below as
background.

| Path | What it gets us | Memory (PC, GameDB home) | Effort | Main risk |
|---|---|---|---|---|
| **A. Our WebKitGTK 2.52 build, the Badwolf shell** (chosen) | The real web: modern JavaScript, CSS grid, everything | 208 MB PSS, 55 MB of it anonymous (Debian's build: 216 and 69) | Built; the PS3 test is next | Big-endian WebKit is unmaintained upstream, no security fixes; CLoop speed on the PPE |
| B. NetSurf with DOM-driven relayout, minimal version (prototype below, works on the PC) | Our own pages and simple ES5 scripts: filters, tabs, menus | 84-90 MB PSS with JavaScript on | 1-2 weeks to ship as `netsurf 3.11-4+ps3.N` with the polyfills our site needs | ES5 only (Duktape): most of today's web still fails; a whole rebuild per change costs seconds on big pages on the PPE |
| B2. NetSurf with incremental restyle and relayout | B, fast on big pages | as B | 2-4 months, high risk | It is the dynamic layout engine NetSurf has planned since 2013 and never built |
| D. WPE + cog with an X11 platform | A without GTK in the UI process | not measured; perhaps 30-60 MB below A | 1 week (rebuild cog) | Same engine; no window manager integration |

The memory numbers in this note were measured on the PC (amd64, Debian sid in a container, Xvfb, Mesa llvmpipe
for GL): a guide to the order of magnitude, not PS3 numbers. ppc64 ELFv1 code is larger (libwebkit2gtk is 131 MB
installed on ppc64 against 101 MB on s390x), while the PS3 has no LLVM in its GL driver, so the tables give totals
with and without llvmpipe's mappings. The package facts come from the debian-ports and Debian indexes and buildd
pages of 2026-10-28.

Firefox is out: no ppc64 build is installable on current sid, and with every memory pref we know it took
474 MB PSS on the PC. Chromium has no big-endian port in Debian and no upstream support.

## The comparison

Memory: PSS of all the browser's processes, summed from `/proc/<pid>/smaps_rollup` about 30-40 s after
the load. "Home" is our GameDB home page (23 cards with screenshots), "busy" the English Wikipedia
article on the PlayStation 3.

| Option | ppc64 BE in debian-ports (2026-10-28) | JavaScript | Memory, home / busy (PC) | Rendering | Main risks |
|---|---|---|---|---|---|
| **WebKitGTK + surf** | libwebkit2gtk-4.1-0 2.52.6-1 and libwebkitgtk-6.0-4 2.52.6-1, installable; sid has 2.54.2, which is BD-Uninstallable on ppc64. surf 2.1+git20250419-2, vimb 4.0.0-2 (GTK 4), epiphany-browser 51.1-1 | Full modern JS on JavaScriptCore's C++ interpreter (CLoop): no JIT, no WebAssembly on big-endian | stock surf 351 / 422 MB; tuned shell 241 / 313 MB (200 / 272 without llvmpipe) | Cairo on big-endian (Skia is off there), GL compositing optional | Memory; CLoop speed on the PPE; ports lag a release behind; upstream means to drop Cairo, the only big-endian renderer |
| **NetSurf 3.11** (what we ship) | netsurf-gtk 3.11-4, current | Duktape 2.7 (ES5.1), built in, off by default; DOM changes after layout are not drawn (section 5) | 107 / 150 MB (JS off) | Own engine, CSS 2.1 plus flexbox, no grid | It is the ceiling: no dynamic pages |
| **NetSurf 3.11 + relayout patch** | our prototype | as above, and changes are drawn | 84-90 MB on home (JS on, 1000x740 window) | as above | Full rebuild per change; ES5 only |
| **Firefox ESR / Firefox** | firefox-esr 128.4.0esr-1, firefox 133.0-1, and 146.0.1-1+quickjs in `unreleased`: all three need libvpx9 or libvpx11, and ppc64 has only libvpx12, so none installs. 153.6.0esr and 158.0.1 are BD-Uninstallable (nodejs) | SpiderMonkey interpreters only in Debian builds; an out-of-tree big-endian JIT exists for 153+ | 474 / 556 MB with the low-memory profile below, JIT off | Software WebRender (our GL 2.1 is below the hardware path's GL 3 / GLES 3) | Memory alone rules it out; 133.0 is reported to segfault at start on big-endian |
| **Chromium, QtWebEngine** | Not built: Debian's chromium is amd64, arm64, armhf, i386, loong64, ppc64el; qt6-webengine is x86/ARM only | V8 | not measured; 267 MB for one tab on a Pi 4 (LinuxLinks) | Skia, patched | No upstream big-endian support; memory |
| **WPE WebKit + cog** | libwpewebkit-2.0-1 2.50.6-1 and cog 0.18.5-1, installable, but Debian's cog has only the drm, headless and wl platforms (no X11) | As WebKitGTK | not measured | As WebKitGTK | Same engine risks; no window manager integration |
| **Pale Moon (UXP)** | Not packaged; community big-endian ppc64 builds exist (forum, 2026) | Old SpiderMonkey, interpreter only on ppc64 | not measured; single-process by design | Own Gecko fork | Community builds that do not start for everyone; old web compatibility |
| **elinks 0.19** | 0.19.1-2.1+b1, built without any ECMAScript engine | none in Debian's build (upstream can use QuickJS, MuJS or SpiderMonkey) | small | Text | Terminal only; partial DOM |
| **Dillo 3.3** | 3.3.0-3 | none | small (58 MB of disk beyond the desktop) | Own engine | No JavaScript at all |
| **Servo** | Not packaged | SpiderMonkey | not measured | WebRender (GL 3-class GPU, or software) | No big-endian story, heavier than WebKit |

Disk beyond what the desktop already has (dependency closure from the ppc64 index): surf about 184 MB in
39 packages (WebKit, GStreamer, ICU), vimb 180 MB, epiphany 205 MB, cog with WPE 142 MB, Firefox 260 MB.
The SSD root image is already nearly full ([backlog/platform.md](../backlog/platform.md)).

## 1. Firefox

**Packages.** debian-ports ppc64 sid has firefox-esr 128.4.0esr-1 (the October 2024 release) and firefox
133.0-1 (December 2024). The `unreleased` suite has firefox 146.0.1-1+quickjs, built on 2026-01-02 by John
Paul Adrian Glaubitz with SPARC fixes and a `firefox-quickjs.patch` (QuickJS in place of nodejs at build
time; nodejs is not installable on ppc64). All three depend on libvpx9 or libvpx11, and ppc64 sid now has
only libvpx12, so none of them installs. The current 153.6.0esr-1 and 158.0.1-1 are BD-Uninstallable on
ppc64: nodejs needs libicu76, which is gone
([buildd firefox-esr](https://buildd.debian.org/status/package.php?p=firefox-esr&suite=sid),
[buildd firefox](https://buildd.debian.org/status/package.php?p=firefox&suite=sid)). On s390x, the other
big-endian Debian architecture, firefox-esr 153.5.1esr and firefox 157.0.1 are built.

**JavaScript on big-endian.** Mozilla's tree has no big-endian ppc64 JIT, so a Debian build runs the C++
interpreter. Trung Le's out-of-tree patches ([runlevel5/firefox-ppc64](https://github.com/runlevel5/firefox-ppc64),
[PR 2](https://github.com/runlevel5/firefox-ppc64/pull/2), pushed 2026-10-28) claim every tier on
big-endian for Firefox 153 ESR and 154+: baseline interpreter, baseline JIT, Ion, the regexp JIT and
WebAssembly, ELFv1 included, tested with the SpiderMonkey shell's jit-tests on a POWER9 running Debian
ppc64. A "pre-VSX" tier for the PowerPC 970 (ISA 2.01, AltiVec) passed jit-tests only when forced on a
POWER9 under ELFv2. The Cell PPE is ISA 2.02 with VMX and no VSX, so it would land in that tier, untested on
ELFv1 and on a Cell. No full browser run on big-endian is reported in the PR. The
[Raptor wiki](https://wiki.raptorcs.com/wiki/Porting/Firefox) still lists "Firefox 133.0 segfaults on
startup" on big-endian, backwards Skia colours in some cases (canvas), and an FFmpeg overlay assertion.
Mozilla's Skia byte-swaps around Skia's little-endian-only code
([bug 1504834](https://bugzilla.mozilla.org/show_bug.cgi?id=1504834)). Building it ourselves means GCC
with ld.bfd (lld cannot link ELFv1), about 20 GB of RAM for the libxul link, and a nodejs workaround.

**Graphics.** The non-WebRender layers backend was removed in Firefox 93
([WebRender wiki](https://wiki.mozilla.org/Platform/GFX/WebRender_Where)), so every Firefox draws through
WebRender. Its GL path needs a GL 3 / GLES 3 class driver (the blocklist reason is
`FEATURE_FAILURE_OPENGL_LESS_THAN_3`), so on our GL 2.1 it falls back to software WebRender (SWGL), which the
runlevel5 patches swizzle for big-endian at the X11 boundary.

**Memory.** With the profile below (one content process, no Fission, no preallocated processes, no
safe browsing or telemetry, small caches, JIT off), Firefox ESR 153.6 on the PC used 474 MB PSS for the
GameDB home and 556 MB for Wikipedia: the parent process alone was 354 MB, 196 MB of it anonymous memory
(the browser UI is itself a large JavaScript application), plus the content process (48 MB), a
WebExtensions process (58 MB), a utility process and the fork server. A Pi 4 measurement of 2020 gives
363 MB for one simple tab ([LinuxLinks](https://www.linuxlinks.com/raspberry-pi-4-chronicling-desktop-experience-firefox-revisited/2/));
512 MB Pi Zero users report crashes ([forum](https://forum.core-electronics.com.au/t/firefox-on-the-pi-zero-2/21314)).
Multi-process can no longer be switched off by pref in official builds since Firefox 68
([ghacks](https://www.ghacks.net/2019/05/17/going-forward-multi-process-cant-be-turned-off-anymore-in-firefox/));
`MOZ_FORCE_DISABLE_E10S=1` is the remaining environment switch and was not tried. Twice the PS3's RAM is
not something prefs can close.

The prefs, for the record (tested as a profile's `user.js`): `dom.ipc.processCount` 1,
`dom.ipc.processCount.webIsolated` 1, `fission.autostart` false, `dom.ipc.processPrelaunch.enabled` false,
`dom.ipc.keepProcessesAlive.web` 0, `network.process.enabled` false, `media.rdd-process.enabled` false,
`layers.gpu-process.enabled` false, `gfx.webrender.software` true, `browser.sessionhistory.max_total_viewers`
0, `browser.cache.memory.capacity` 4096, `image.mem.surfacecache.max_size_kb` 16384,
`javascript.options.baselinejit`/`ion`/`wasm` false, `javascript.options.mem.high_water_mark` 32, the
`browser.safebrowsing.*.enabled` trio false, telemetry and Normandy off, Pocket, new tab page, ML and
updates off, speculative connections and DNS prefetch off, `accessibility.force_disabled` 1. A package
would ship them as `/etc/firefox/*.js` or `/etc/firefox-esr/*.js` (Debian reads every file in that
directory, `pref(..., locked)` allowed) or as `/etc/firefox/policies/policies.json`
([policy templates](https://mozilla.github.io/policy-templates/)); autoconfig (`mozilla.cfg`) is not needed.

**Gecko forks.** Pale Moon (UXP) is single-process, which is where Firefox's memory goes. Community builds
of Pale Moon 34.3 for big-endian ppc64 Linux exist
([Pale Moon forum](https://forum.palemoon.org/viewtopic.php?p=275981): the default build uses POWER8 vector
code in NSS that breaks on older CPUs until patched). SeaLion, a Pale Moon derivative for 32-bit PowerPC,
has a report of a ppc64 rebuild that does not start
([Hyperion forum](https://forum.hyperion-entertainment.com/viewtopic.php?p=62003)). Worth a line in the
backlog, not a test now.

## 2. Chromium

Not supported on big-endian. V8 itself handles big-endian (s390x Linux, PPC on AIX), but Chromium's other
parts do not: Skia's config header stops a big-endian build with "The Skia team is not endian-savvy enough
to support big-endian CPUs" ([WebKit bug 274032](https://bugs.webkit.org/show_bug.cgi?id=274032) quotes
it), and Debian builds chromium only for i386, amd64, arm64, armhf, loong64 and ppc64el, and qt6-webengine
only for x86 and ARM. The one port, [zeldin/chromium_be](https://github.com/zeldin/chromium_be) (patches for
Chromium up to 146 and QtWebEngine up to 6.11.1, last push 2026-07-27), carries 69 patches over Chromium,
BoringSSL, Skia, Perfetto, WebRTC and FFmpeg; the [Raptor wiki](https://wiki.raptorcs.com/wiki/Porting/Chromium/BE)
lists libyuv, Perfetto's fixed 4096-byte page size and many test failures as open. Chromium on a Pi 4 used
267 MB for one tab ([LinuxLinks](https://www.linuxlinks.com/raspberry-pi-4-chronicling-the-desktop-experience-memory-usage/2/)).
No.

## 3. WebKitGTK

**Packages.** webkit2gtk 2.52.6-1 is built and installable on ppc64 (GTK 3 `4.1` API and GTK 4 `6.0`
API), and it crashes on most pages ([Where it stands](#where-it-stands)); sid's 2.54.2 has no Cairo renderer and
will never build on big-endian, and Debian is removing webkit2gtk from those architectures. Shells on ppc64: surf
(GTK 3, 4.1 API), vimb (GTK 4), epiphany-browser 51.1 (GTK 4 and libadwaita). luakit 2.3 is stuck on the removed
4.0 API and does not install. Badwolf is not in Debian; we package it ([The shell](#the-shell-badwolf)).

**How it is built on big-endian.** From WebKit's cmake at the webkitgtk-2.52.6 tag: `OptionsGTK.cmake` and
`OptionsWPE.cmake` turn Skia on only when `CMAKE_CXX_BYTE_ORDER` is little-endian, so ppc64 keeps the
Cairo renderer (since 2.46, [bug 274032](https://bugs.webkit.org/show_bug.cgi?id=274032));
`WebKitFeatures.cmake` gives every CPU other than x86-64, ARM64, ARMv7 Thumb-2 and RISC-V 64 the C_LOOP
interpreter with JIT and FTL off, `USE_SYSTEM_MALLOC` on (so bmalloc and libpas are compiled but not used),
no sampling profiler and no WebAssembly. Debian's
[rules](https://sources.debian.org/data/main/w/webkit2gtk/2.52.6-1/debian/rules) add nothing for ppc64: a
Release build with `-g1`, no LTO, `USE_LIBBACKTRACE=OFF`, `--reduce-memory-overheads` for the link, the GTK 3
flavour without documentation, and bubblewrap on wherever libseccomp exists. The big-endian non-Skia build
breaks now and then and gets fixed: Epiphany's tests on s390x needed 2D canvas acceleration off
([bug 279220](https://bugs.webkit.org/show_bug.cgi?id=279220)), and 2.52.2 failed to build without Skia
([bug 312160](https://bugs.webkit.org/show_bug.cgi?id=312160)). The CLoop has its own history of
big-endian bugs, found on s390x, the only big-endian machine WebKit tests
([bug 238956](https://bugs.webkit.org/show_bug.cgi?id=238956),
[bug 213307](https://bugs.webkit.org/show_bug.cgi?id=213307)). Upstream says Cairo is to be removed
"in the near future" ([Igalia](https://blogs.igalia.com/carlosgc/?p=1029)); when that happens big-endian
WebKit loses its renderer unless someone ports Skia.

**Process model.** Three processes at least: the UI (the shell), one WebKitWebProcess per tab (the only
model since 2.26: `webkit_web_context_set_process_model` is a no-op, see `WebKitWebContext.cpp` at the
tag) and one WebKitNetworkProcess, which cannot be folded in. `WEBKIT_USE_SINGLE_WEB_PROCESS` was a
temporary switch of 2.25.92 ([release notes](https://webkitgtk.org/2019/09/03/webkitgtk2.25.92-released.html))
that shared one web process between views; it is gone from 2.54 (not in the library's strings, and setting
it changed nothing), and `webkit_web_context_set_web_process_count_limit` only warns. Tabs can still share
one web process: a view created with `related-view` takes its relative's process (`WebProcessPool::createWebPage`),
and the GTK 3 API leaves process swapping on cross-site navigation off unless the shell asks for it
(`process-swap-on-cross-site-navigation-enabled`, construct-only, default false; the GTK 4 API forces it on), so
a related tab keeps that process whatever site it goes to. Badwolf does this: on the PC three tabs (GameDB,
Wikipedia, a third site) ran in one WebKitWebProcess, 296 MB PSS in all with llvmpipe in the UI. The price is
that one crash takes every tab's page with it. The UI and network processes stay separate.
`ENABLE_GPU_PROCESS` is on in the default build (it needs `USE_GBM`); no GPU process appeared in the PC runs.

### Measured on the PC: what run-time settings save

WebKitGTK 2.54.2-2 amd64 (so Skia, where the PS3 build has Cairo), Xvfb 1280x1024, surf from Debian and a
46-line GTK 3 shell (one `WebKitWebView`, settings switched by environment), `JSC_useJIT=0` and
`WEBKIT_DISABLE_COMPOSITING_MODE=1` in every run. "Tuned" is the shell with all of the following:
`webkit_settings_set_hardware_acceleration_policy(NEVER)`; `WebKitMemoryPressureSettings` with a 100 MB
limit, conservative 0.33, strict 0.5, kill threshold off, poll every 5 s, set with the static
`webkit_website_data_manager_set_memory_pressure_settings` (2.54 has no construct property on the web
context); `WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER`; an ephemeral website data manager (no disk cache); page
cache, media, WebAudio, WebGL, media stream, MSE, EME and developer extras off.

| Run | Page | Total MB | UI / network / web | Anonymous (all processes) | Total without llvmpipe |
|---|---|---:|---|---:|---:|
| stock surf | home | 351 | 95 / 39 / 218 | 162 | 287 |
| stock surf | Wikipedia | 422 | 97 / 51 / 275 | 219 | 356 |
| shell, nothing set | home | 396 | 100 / 38 / 257 | 196 | 330 |
| shell, tuned | home | 241 | 115 / 36 / 90 | 69 | 200 |
| shell, tuned | Wikipedia | 313 | 115 / 46 / 152 | 130 | 272 |
| tuned + `WEBKIT_DISABLE_DMABUF_RENDERER=1` | home | 217 | 93 / 35 / 89 | 52 | 182 |
| tuned + `WEBKIT_DISABLE_DMABUF_RENDERER=1` | Wikipedia | 287 | 90 / 45 / 151 | 114 | 254 |

A bisect on the home page puts nearly all of the gain on one setting. The acceleration policy alone gave
238 MB (web process 89 MB, its anonymous memory down from 144 to 19 MB); memory pressure settings with the
cache model alone gave 384 MB, the feature switches alone 386 MB. With the policy at NEVER the web process
maps no Mesa at all; its biggest mappings are then libwebkit2gtk (41 MB), JavaScriptCore (11 MB), 8 MB of
anonymous memory and a 7.5 MB `WebKitSharedMemory` buffer. `WEBKIT_DISABLE_COMPOSITING_MODE=1` did not
achieve this on 2.54: the web process still created a GL context. Our reading is that on amd64 Skia
rasterises on the GPU (here llvmpipe) unless the policy says NEVER. The PS3's Cairo build rasterises on the
CPU anyway, so part of this saving is already there on ppc64; the policy still guarantees that no GL
context is made. The GameDB filter worked in every tuned run (`#q=doom` showed "6 of 23 games").

What did not matter at these sizes: the memory pressure limit (the web process never reached it), the
cache model and the ephemeral manager (on a 217 MB machine WebKit's `CacheModel.cpp` already sizes the
memory cache at 8 MB and the back/forward cache at 0 for every model; the model mostly changes the disk
cache), `JSC_useConcurrentGC=0` and `JSC_gcMaxHeapSize=33554432` (within 1 MB). The UI process kept about
40 MB of libLLVM and gallium whatever we tried (`LIBGL_ALWAYS_SOFTWARE`, `GDK_DEBUG=gl-disable`; softpipe
made it 100 MB larger); that part does not exist on the PS3.

surf cannot be told any of this: it sets only the cache model (DOCUMENT_VIEWER when its `DiskCache` is off)
and leaves the acceleration policy at its default
([surf.c](https://git.suckless.org/surf/tree/surf.c)). Path A therefore needs a small patch to surf, or our
own shell.

**What that predicts for the PS3.** Tuned, the GameDB home needs about 70 MB of anonymous memory plus
whatever part of 110-150 MB of library code it touches (libwebkit2gtk, JavaScriptCore, GTK, ICU: shared,
file-backed, read back from the SSD when evicted rather than swapped). Wikipedia needs about 130 MB of
anonymous memory. With the desktop up there is about 100-130 MB free, so the home page might fit and a
busy page will lean on the swap tiers. Only the PS3 can say whether that is a usable browser.

### The shell: Badwolf

Picked from what runs on the GTK 3 API (the lighter one, and the one whose sandbox is opt-in): surf has no
browser chrome (no tabs bar, location bar or buttons), vimb is vim-modal and GTK 4, luakit does not install
(4.0 API), Midori is dead, Epiphany is GTK 4 and libadwaita with its own services. [Badwolf](https://hacktivis.me/projects/badwolf)
1.4.0 (2025-08-14) is C, about 1500 lines, GTK 3 on the 4.1 API, with tabs, a location bar, back and forward,
downloads (a tab of their own), find in page, bookmarks as completion from an XBEL file, and ordinary
shortcuts; BSD-3-Clause, the bookmarks code MPL-2.0, the icons CC-BY-SA-4.0. Not in Debian, so it is an upstream
package of ours (`badwolf 1.4.0-1+ps3.N`, the release tarball verified against upstream's signify key once).

Upstream's choices that did not fit, and the patches (`packages/patched/desktop/badwolf/patches/`):

1. Every new tab got its own ephemeral web context (a network and a web process per tab, nothing kept) with the
   bubblewrap sandbox on, which cannot start on our kernel. Now: one persistent context (cookies in SQLite,
   third-party ones refused, data and disk cache under the XDG directories), no sandbox, every tab related to an
   open one, so one web process for all.
2. Memory defaults: hardware acceleration NEVER, no page cache (back/forward reloads), no WebGL, WebRTC or media
   stream, no smooth scrolling, no developer extras, `WEBKIT_DISABLE_DMABUF_RENDERER=1`. For measuring:
   `WALNUT_BROWSER_ACCEL=never|ondemand|always`, `WALNUT_BROWSER_CACHE=browser|viewer` (default WEB_BROWSER: on a
   217 MB machine both models give the same memory cache, and the browser one keeps the disk cache),
   `WALNUT_BROWSER_MEMORY_LIMIT=<MB>`, `WALNUT_BROWSER_DEVTOOLS=1`.
3. JavaScript on (upstream starts with it off), "Walnut" at the end of WebKit's User-Agent, no autoplay.
4. Words in the location bar search (DuckDuckGo's HTML results); a local start page (`/usr/share/badwolf/start.html`:
   a search box and a few links) at start, in a new tab and on Home; Reload and Home buttons; the window maximized.
5. The usual shortcuts: Ctrl+W, Alt+Left/Right for back and forward (upstream: tabs), Ctrl+Tab, Alt+D and F6 for
   the location bar (upstream's Alt+D closed the tab), Alt+Home, Ctrl+plus/minus.
6. Links in other schemes (walnut://, mailto:) go to the desktop's handler through GIO instead of failing.

Checked on the PC (WebKitGTK 2.54, Xvfb): the start page, three tabs in one web process, "Open in Walnut Games" on
a GameDB game page from the User-Agent, walnut://game/openarena reaching a stand-in handler. On the PS3 it
starts, draws its window and loads pages, and then the engine aborts (below).

### Run-time knobs, for the shell

From the source at webkitgtk-2.52.6 (`Source/WTF/wtf/MemoryPressureHandler.cpp`, `RAMSize.cpp`,
`Source/WebKit/Shared/CacheModel.cpp`, `Source/JavaScriptCore/runtime/OptionsList.h`):

- `WebKitMemoryPressureSettings` (since 2.34): the default limit is the RAM size (capped at 3 GB), read
  with `sysinfo()`, so about 217 MB on the PS3; conservative 0.33 and strict 0.5 of it, kill threshold
  unset, polled every 30 s. Linux has no pressure source in this code (no PSI, no cgroup): only the
  process's own footprint is polled, so a lower limit (60-80 MB) and a shorter poll are the levers. The
  network process takes its own copy through the website data manager.
- JSC options read from `JSC_<name>` in release builds: `gcMaxHeapSize` (0, unlimited), `forceRAMSize`,
  `smallHeapRAMFraction` 0.25, `largeHeapSize` 32 MB, `useConcurrentGC` and `useGenerationalGC` (true),
  `numberOfGCMarkers`, `maxPerThreadStackUsage` 5 MB, `criticalGCMemoryThreshold` 0.80. Heap sizing is
  derived from the RAM size, so on 217 MB JSC is already in its small-heap mode; whether `gcMaxHeapSize`
  helps on a script-heavy page is untested.
- `hardware-acceleration-policy` NEVER (the measured win), `WEBKIT_DISABLE_DMABUF_RENDERER=1` (another 24 MB
  on the PC, mostly in the UI), `enable-page-cache` off (no effect at a capacity of 0, harmless).
- Fonts: not measured; fontconfig's cache is shared, mapped files.

### The sandbox

WebKitGTK's bubblewrap sandbox is opt-in in the GTK 3 API: `m_sandboxEnabled` starts false and only a shell
that calls `webkit_web_context_set_sandbox_enabled(TRUE)` gets it (Epiphany does; surf and our shell do not,
and no bwrap or xdg-dbus-proxy process appeared). The GTK 4 API turns it on at context creation. Where it is
on, there is no fallback: `ProcessLauncherGLib.cpp` spawns the web process through bwrap, which needs user,
PID, IPC, UTS and (often) network namespaces; in the container `WEBKIT_FORCE_SANDBOX=1` died at once with
"bwrap: Creating new namespace failed: Operation not permitted" and "Connection: failed to receive
credentials". So on our kernel the rule is simple: stay on the GTK 3 API and do not enable the sandbox, or
set `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` for a GTK 4 shell. Debian's package still depends on
bubblewrap and xdg-dbus-proxy (disk only).

### Our build

Every WebKit option is an ordinary cmake cache variable ("private" ones are only hidden from the GUI), so the
cuts are `-D` flags in `debian/rules` (`packages/patched/desktop/webkit2gtk/packaging/`). What each one is in
2.52's cmake (`Source/cmake/OptionsGTK.cmake`, `WebKitFeatures.cmake`, `GStreamerDefinitions.cmake`,
`GStreamerDependencies.cmake`), and why it goes:

| Left out | What it is in 2.52 | Memory | Attack surface it removes | Needed for ordinary browsing? |
|---|---|---|---|---|
| `ENABLE_WEBGL` (and `USE_ANGLE` with it; `ENABLE_WEBGPU` is off on GTK anyway) | WebGL 1 and 2 through ANGLE, linked into libwebkit2gtk; `HardwareAccelerationManager` turns acceleration off for good without it | nothing until a page uses WebGL | ANGLE's shader translator, and a path from any page to the GL driver: on the PS3, Mesa nv30 and the RSX, where one bad command wedges the GPU until a reboot | no: 3D games and demos |
| `ENABLE_WEB_RTC`, `USE_GSTREAMER_WEBRTC` (off on GTK by default; kept off), `ENABLE_MEDIA_STREAM`, `ENABLE_MEDIA_RECORDER` | peer connections, camera and microphone capture, recording | small | ICE/STUN networking and device access from script | video calls only |
| `ENABLE_WEB_AUDIO` | the audio graph API (needs GStreamer) | small | DSP and decoders driven from script; a fingerprinting source | some games and music sites; `<video>` sound does not need it |
| `ENABLE_WEB_CODECS` | decoders and encoders called from script | small | codec input straight from script | rare |
| `ENABLE_SPEECH_SYNTHESIS` (flite) | `speechSynthesis`, with flite's voices | **the biggest item:** flite and its voices are mapped into every web process (16 MB PSS on the PC) and set up at start (about 13 MB anonymous) | a C speech engine | no |
| `ENABLE_ENCRYPTED_MEDIA` (off on GTK by default) | EME (DRM) | - | - | Netflix and the like, impossible here anyway |
| `USE_GSTREAMER_GL` | GStreamer's GL sink for video | libgstgl and a GL context | GL again | no: frames go through the software path |
| `ENABLE_GAMEPAD` | the Gamepad API through libmanette (evdev) | small | input devices | browser games |
| `ENABLE_GEOLOCATION` | GeoClue over D-Bus | none until used | a D-Bus service | no |
| `ENABLE_SPELLCHECK` | enchant and hunspell dictionaries in text fields | dictionaries when a field has focus | dictionary parsing | no, on a console |
| `USE_LIBSECRET` | HTTP-auth passwords in the keyring over D-Bus | small | - | no keyring daemon on Walnut |
| `USE_AVIF`, `USE_JPEGXL` | libavif (dav1d, aom) and libjxl decoders | none until an image comes | two large image decoders | no: sites send AVIF or JPEG XL only when the Accept header offers them, and send WebP or JPEG otherwise |
| `ENABLE_PDFJS` | pdf.js, the PDF viewer in the library's resources | disk | a JavaScript application in the engine | no: PDFs download |
| `ENABLE_WEBDRIVER`, `ENABLE_MINIBROWSER` | separate programs (`WebKitWebDriver`, MiniBrowser) | disk | an automation server | no |
| `ENABLE_REMOTE_INSPECTOR` | the inspector server (`WEBKIT_INSPECTOR_SERVER`) | small | a debugging socket | no (whether Badwolf's local inspector, `WALNUT_BROWSER_DEVTOOLS`, still opens is not checked) |
| `ENABLE_WAYLAND_TARGET` | WebKit's Wayland code; GTK 3 still links libwayland | small | - | no: Walnut is X11 |
| `ENABLE_JOURNALD_LOG` | logging to journald instead of stderr | - | - | no |
| `ENABLE_INTROSPECTION`, `ENABLE_DOCUMENTATION` | GObject introspection data and API docs | disk, build time | - | no: Badwolf is C |
| `ENABLE_BUBBLEWRAP_SANDBOX` | the bwrap launcher for web processes | - | none: the sandbox needs user namespaces our kernel lacks and the GTK 3 API never turns it on, so it protected nothing here | drops the bubblewrap and xdg-dbus-proxy dependencies |

Kept: `<video>` and Media Source with GStreamer, the user's decision of 2026-10-28 (video in the browser is tried
on the PS3 before deciding), with `gstreamer1.0-libav` recommended for H.264, AAC, Opus, VP8 and VP9 and no
`gstreamer1.0-plugins-bad` (Debian's package recommends it) or `gstreamer1.0-gl`. Accessibility has no switch
(`USE_ATK` and `USE_ATSPI` are hard-wired for GTK 3); it maps 0.7 MB, the web process sets `NO_AT_BRIDGE` itself,
and the UI process talks to an AT-SPI bus only if one runs. The allocator stays glibc's: libpas is enabled only
on x86-64 and ARM64 Linux (`bmalloc/BPlatform.h`), every CPU without a JIT builds with `USE_SYSTEM_MALLOC`, and
the old bmalloc has never run on big-endian. On the PC, Debian's build with `Malloc=1` (glibc in place of libpas
at run time) took the same memory within the noise (216 against 218 MB on the GameDB home, 297 against 301 on
Wikipedia), so nothing is lost there. Code is scheduled for the PPE (`-mtune=cell`; the instruction set stays
the baseline, no AltiVec).

**Measured on the PC** (amd64, Debian sid at the 2026-09-25 snapshot in a container, Xvfb, our Badwolf build;
PSS of all processes 60 s after the start, anonymous in brackets; the variants interleaved per page). Debian's
2.52.6-1 binaries against two builds of ours: as shipped, and with GStreamer (and so video and MSE) left out too.

| Page | Debian 2.52.6-1 | Ours | Ours without GStreamer |
|---|---:|---:|---:|
| GameDB home | 216 (69) | 208 (55) | 190 (52) |
| GameDB game page (OpenArena) | 215 (65) | 197 (51) | 185 (50) |
| GameDB `?q=doom` | 224 (68) | 196 (54) | 159 (51) |
| Wikipedia, "PlayStation 3" | 297 (127) | 270 (113) | 259 (110) |
| BBC News | 402 (228) | 369 (221) | 373 (219) |
| YouTube watch page (the consent dialog over the player) | Badwolf crashed (view transitions) | 728 (521) | 485 (312), "Your browser can't play this video" |

The file-backed part moves by 10-30 MB between runs (how much of the library a run has touched: the `?q=doom`
row); the anonymous memory is the steadier number. Ours saves 13-14 MB of anonymous memory on simple pages and
7 MB on heavy ones, most of it flite's. Leaving out GStreamer saves about 3 MB of mapped libraries and 1-4 MB
anonymous on pages without media, and 210 MB on YouTube, whose player fills media buffers in the web process
(the PC's YouTube numbers include the x86 JIT; the PPE has none). YouTube on the PS3 is out of reach either way:
its page alone is several times the console's memory.

On disk (amd64) `libwebkit2gtk-4.1-0` installs 78.8 MB against Debian's 96.2 (74.7 MB without GStreamer);
JavaScriptCore is unchanged at 32 MB. Its dependencies lose flite, libavif, libjxl, enchant, libmanette,
libsecret, libseccomp, libsystemd's journald use, libgstreamer-gl, the Wayland libraries of its own,
bubblewrap, xdg-dbus-proxy, `gstreamer1.0-plugins-bad` and the portal.

### What the browser adds to the image

Against the 0.1.0 image's package list (its MANIFEST), resolved with apt against the pinned ppc64 index:

| | Packages | Installed |
|---|---:|---:|
| Badwolf on Debian's 2.52.6-1, no Recommends | 29 | 181 MB |
| Badwolf on Debian's 2.52.6-1, with Recommends (as an image build would install it) | 89 | 253 MB |
| Badwolf on ours, no Recommends | 24 | 154 MB |
| Badwolf on ours, with Recommends (`gstreamer1.0-libav` and the rest) | 30 | 157 MB |

`libwebkit2gtk-4.1-0` itself installs 105.5 MB on ppc64 against Debian's 131.1 MB; JavaScriptCore 23.5 MB
against 24.1. Of the 154 MB, GStreamer is about 24 MB: `gstreamer1.0-plugins-good` (12 MB), `-base` (4 MB) and
the libraries they pull in (TagLib, libcaca, v4l, libvisual, Theora, WavPack, AMR, DV, libshout, cdparanoia,
aalib). `gstreamer1.0-libav` adds one package of 0.6 MB: the image already has FFmpeg's libraries for the games.
Debian's Recommends that ours no longer brings: `gstreamer1.0-plugins-bad` and its closure (ImageMagick,
TensorFlow Lite through zxing), `xdg-desktop-portal` with its GTK backend (a resident service), `gstreamer1.0-gl`.

**Dropping video (a proposal for after the PS3 test):** 2.52 does not even build with `ENABLE_VIDEO` off
(`JSHTMLMediaElementCustom.cpp` lacks its guard: one `#if`), and then `<video>` and `<audio>` are unknown
elements: YouTube says "Your browser can't play this video", sites with a GIF-like `<video>` show nothing. It saves
little memory on ordinary pages (above) but the whole GStreamer closure on disk and the media parsers' attack
surface (demuxers, libav's decoders), the largest native parsing code left in the browser. If the PS3 cannot play
a 360p H.264 clip smoothly in the browser, drop it.

**Building it.** Debian's ppc64 buildd needed 3-5 h for both flavours on POWER hardware. Ours is one flavour,
built with `BUILD=xcc` ([PACKAGING.md](../PACKAGING.md#kinds-of-source-packages)): the ppc64 chroot under qemu,
the x86-64 cross compiler inside it. A heavy file compiles in 2 s that way against 22 s under qemu-user, so a
qemu-only build would have taken most of a day; amd64 with clang on 20 cores took 20 minutes. The ppc64 release
build took 73 minutes at 12 jobs under `nice` on a PC busy with other builds (a third of the objects from
ccache), 42 minutes when only a few files had changed; it runs on the overflow disk (`XCC_ROOT`). The packages: `libwebkit2gtk-4.1-0` 21 MB
(its `-dbgsym` 204 MB), `libjavascriptcoregtk-4.1-0` 4.6 MB.

**Testing without the PS3.** The ppc64 packages run in a ppc64 chroot under qemu-user with Xvfb, as the PS3 would
run them, just slowly (a page takes a few minutes). That is how fixes 2 and 3 were found: the web process's
qemu core and a copy of `/proc/<pid>/maps` taken while it ran give each frame's library and offset, and gdb in
the chroot with the `-dbgsym` packages names them. A build tree kept in such a chroot, where `ninja JavaScriptCore` alone
rebuilds the interpreter, turns a JSC fix around in about half an hour instead of a whole package build.

## 4. WPE WebKit and cog

WPE is WebKit's embedded port (no GTK), aimed at set-top boxes. libwpewebkit-2.0-1 2.50.6-1 and cog
0.18.5-1 install on ppc64, but Debian's cog ships only the `drm`, `headless` and `wl` platform modules.
`drm` would take the display from X; `wl` needs a Wayland compositor (cage or weston nested in X: more
memory). Cog's upstream X11 platform is not built by Debian, so WPE on our desktop means rebuilding cog
with it (about a week with testing). The web and network processes are the same engine as WebKitGTK; what
WPE saves is the UI process, which was 93-115 MB PSS in the tuned GTK runs (36 MB anonymous, 40 MB of it
llvmpipe that the PS3 does not have). Igalia and wpewebkit.org publish no memory figures; the slides say
"low memory and storage footprint" without a number
([Igalia slides](https://www.igalia.com/downloads/slides/MarioSanchezPrada-UnlockingtheFullPotentialofWPE.pdf)).
WPE's defaults differ little from GTK's (GPU process on, MiniBrowser only in developer mode). Worth it only
if path A nearly fits.

## 5. NetSurf's JavaScript

**Built, off.** Debian's netsurf (3.11-4) builds from netsurf-all with the defaults, and
`Makefile.defaults` has `NETSURF_USE_DUKTAPE := YES`; Debian even carries `fix-nsgenbind-be.patch` so the
bindings generate on big-endian. `desktop/options.h` declares `enable_javascript` with default false. It is
switched on in `~/.config/netsurf/Choices` with `enable_javascript:1`, or in the GUI's preferences.

**What 3.11 implements** (Duktape 2.7, ES5.1, with an `Array.from` polyfill; read from the `.bnd`
bindings and checked with probe pages): `getElementById`, `getElementsByTagName`, `createElement`,
`appendChild` and the other Node methods, `textContent`, `className`, `classList`, `setAttribute`,
`addEventListener`, `setTimeout`/`setInterval`, `location` (with `search`, `hash`, `pathname`),
`document.write`, `console`, cookies, canvas image data. Stubs that return `undefined` or throw:
`querySelector`/`querySelectorAll` (the stub on `Element.prototype` is non-writable), `innerHTML` (the
getter returns ""), `element.style` (an empty object: setting `style.display` does nothing),
`closest`, `URLSearchParams` ("Bad constructor"), `execCommand`. Missing: `dataset`, the `hidden`
property, `history`, `Promise`, `fetch`, `XMLHttpRequest`, `localStorage`, `scrollIntoView`. ES2015
syntax (`let`, arrow functions) is a SyntaxError. Of the DOM events, NetSurf itself fires only `load` and
`click`: no `input` or `change` from its form controls.

**Why changes are never drawn** (netsurf 3.11 source):

- `html_finish_conversion()` (html.c) builds the box tree once, from the whole DOM, after parsing; its own
  comment says "NetSurf has no concept of dynamically changing documents, so this would break badly" and
  it ignores a second call ("Ignoring style change: NS layout is static").
- `box_construct.c` (`dom_to_box`, `convert_xml_to_box`) walks the DOM in steps of ten elements, selects
  each element's style with libcss (`box_get_style`, which parses the `style` attribute as an inline sheet
  each time), creates the boxes, starts image fetches (`html_fetch_object`) and form gadgets, and stores
  the box on the DOM node as user data. Nothing ever rebuilds it.
- libdom does fire mutation events: `setAttribute` dispatches DOMAttrModified and DOMSubtreeModified,
  text changes DOMCharacterDataModified, insertions and removals DOMNodeInserted/DOMNodeRemoved plus
  DOMSubtreeModified on the parent. NetSurf's `dom_event.c` uses them only for side effects: a `<base>`,
  `<img>` (prefetch), `<link>`, `<meta refresh>`, `<style>` or `<script>` inserted, a `<title>` changed, an
  `<input>`'s gadget resynced. None of them touches the box tree.
- libcss keeps per-node data (a Bloom filter of ancestors and the computed style, for style sharing) on
  the DOM node, and NetSurf's `set_libcss_node_data` asserts the slot is empty, so a node cannot simply be
  selected a second time. libdom also has a bug of its own: its cached class list is rebuilt only when a
  `class` attribute is created, so changing an existing one (`el.className = "x"`) is invisible to CSS
  selectors.
- Relayout exists and is cheap to call: `html_reformat()` (layout.c's `layout_document` over the existing
  box tree), reached through `content__reformat()`, which broadcasts `CONTENT_MSG_REFORMAT`; the browser
  window then updates its extent and redraws, keeping the scroll position. Images use this path when they
  arrive. Layout itself has no dirty tracking: it always lays out from the root.

**Prior attempts.** None found. The NetSurf 3.0 release (2013) called libdom "a foundation that paves the
way for NetSurf to implement a fully dynamic layout engine in the future"; the 3.10 release news (2020)
still said there was no mechanism to update the affected parts of the box tree after a script change
([Iconbar](https://www.iconbar.co.uk/NetSurf_reaches_version_3.10/news1571.html)). The git history has a
test page for it from 2019 (`test/js/event-onclick-insert.html`, "Add simple JS test for dynamic
layout") and no code. NetSurf is not dead: master has 331 commits since 3.11 (2023-12-28), the newest on
2026-10-27, a Qt 6 frontend among them, but no release and nothing on dynamic layout.

### The prototype: rebuild the whole box tree on any change

Written and tested on the PC in a Debian sid container against Debian's 3.11-4 source; the patch is
[netsurf-relayout/netsurf-3.11-dom-relayout.patch](netsurf-relayout/netsurf-3.11-dom-relayout.patch)
(about 470 added lines in 17 files of netsurf and libdom). It took most of a day, including the bugs below.

- Any DOMSubtreeModified after the first layout (attributes, class, `style`, text, children) calls
  `html__dom_changed()`, which schedules one rebuild 10 ms later, so a script that changes fifty nodes
  costs one rebuild. It waits while a drag is running, the content is locked or a conversion is in
  progress, and it skips documents with frames or iframes.
- The rebuild clears each element's box pointer and libcss node data (`nscss_dom_node_restyle()`, through
  libcss's own `CSS_NODE_DELETED` path), builds a new tree synchronously into a new talloc context with
  `dom_to_box_sync()` while the old tree stays drawable, then frees the old one and calls
  `content__reformat()`. Scroll position is kept by the browser window's normal reformat path.
- What points into the old tree is moved over: images and background images are taken over by the new box
  for the same node and URL instead of being fetched again (the first version re-fetched them and lost
  every picture on the page); form gadgets keep their state, text fields keep their text and caret, the
  focused field stays focused; `<select>` options are not added twice; inputs outside a `<form>` keep their
  gadget through node user data.
- Fixed on the way: libdom's class cache (above), so `className` changes restyle.
- Added so the GameDB filter can be driven: `[hidden] { display: none }` in the UA sheet and a `hidden`
  property; `change` events from checkboxes, radio buttons and selects, `input` events from text fields.

Results (screenshots looked at for each step):

- The [probe page](netsurf-relayout/probe.html) (a timer changes `className`, sets `hidden`, a `style`
  attribute, `classList.add`, removes one item and appends another, changes text): stock 3.11 shows the
  page as first laid out; the patched build shows every change.
- The GameDB home, built unminified (Hugo's `catch{}` does not parse in Duktape) with a test-only shim for
  what NetSurf lacks ([gamedb-test-shim.js](netsurf-relayout/gamedb-test-shim.js): `querySelectorAll` for
  simple selectors, `URLSearchParams`, `history.replaceState`): `?q=doom` opens with "6 of 23 games";
  Clear shows 23 of 23; ticking "horror" gives 4 of 23; typing "quake" in the search box narrows to 1 of
  23 (QuakeSpasm, with its screenshot), with the page scrolled halfway down and the scroll kept through
  each keystroke's rebuild.
- Cost per change on the PC: GameDB 2-4 ms to rebuild and 2-12 ms to reflow; Wikipedia's PlayStation 3
  article, styled, with a script toggling `<body>`'s class: 105-115 ms rebuild plus 168-173 ms reflow,
  steady over 20 changes, pictures kept. Our guess is that the PPE is 10-20 times slower per thread than this PC
  for such pointer-chasing code, which would give 0.1-0.3 s per change on GameDB and 3-6 s on a Wikipedia-sized page.
- Memory: 84-90 MB PSS on the GameDB pages (JS on, 1000x740); on Wikipedia, PSS grew by about 80 KB per
  rebuild (146.4 to 147.9 MB over 20), a leak or fragmentation not yet chased.

What (1) still needs to ship, as `netsurf 3.11-4+ps3.N` (`packages/patched/`): the leak; the failure path
(a rebuild that runs out of memory hands objects back badly); selection and search highlights, which are
dropped on rebuild; frames and iframes (skipped now); stylesheets added by script (ignored, as before); a
PS3 measurement of the rebuild cost. For scripts beyond our own: real `querySelector`/`querySelectorAll`
(a small selector matcher in C or the bindings' polyfill, 1-2 days), `element.style` backed by the `style`
attribute (1-2 days), `URLSearchParams`, `history.replaceState` and `dataset` (a day). That is one to two
weeks with testing. Our site would also need its minifier told to target ES5 (`[minify.tdewolff.js]
version = 2015` in hugo.toml stops the `catch{}` rewrite) and its filter to fall back from
`querySelectorAll`, or the polyfills shipped in NetSurf.

What it will not do: ES2015 and later. Duktape is ES5.1, and most sites now ship ES2015+ bundles. A modern
engine (QuickJS) under NetSurf's bindings would be a separate project of weeks, since nsgenbind generates
Duktape-specific code.

### (2) Incremental restyle and relayout

The proper version restyles only the elements a change can affect, patches the box tree in place and
reflows only what moved. In NetSurf each part is missing, not just switched off:

- Restyle: libcss has no invalidation data, so a class change on an element must restyle its subtree and,
  with sibling combinators, its following siblings and their subtrees; the node data and style sharing
  have to be invalidated along the same lines.
- Box tree: boxes are built in one pass with anonymous inline containers, float wrappers, list markers,
  generated content and table normalisation (`box_normalise.c`); replacing one element's boxes means
  undoing and redoing those around it.
- Layout: `layout.c` (5,470 lines) and `layout_flex.c` lay out from the root with cached min/max widths and
  no dirty bits; tables, floats and shrink-to-fit widths depend on their children.

On the PC numbers the rebuild is 40% of the cost on Wikipedia and the reflow 60%, so incremental box
construction alone would not help much; dirty-bit layout is what pays, and it is the hardest part. This is
the dynamic layout engine NetSurf's developers have planned since 2013. Two to four months for someone who
knows the code, with a long tail of layout bugs; not a project for us. A cheaper middle step, rebuilding
only the box subtree under the nearest block ancestor of the changes and then reflowing everything, would
save part of the 40%.

## 6. The PS3's own browser, as a reference point

GameOS's browser moved from NetFront to a WebKit-based engine with firmware 4.10 in February 2012, with
better speed, layout and some HTML5
([Wikipedia](https://en.wikipedia.org/wiki/PlayStation_3_system_software),
[Engadget](https://www.engadget.com/2012-02-08-ps3-firmware-update-speeds-up-web-browsing.html)). Nothing
public gives its memory budget, its process model or its JavaScript engine (psdevwiki was not reachable);
what is known are user reports of "There is not enough memory" errors that blank the page on heavy sites
([Audioholics thread](https://forums.audioholics.com/forums/threads/ps3-webpage-error-not-enough-memory.42391/latest),
[HardForum](https://www.hardforum.com/threads/low-memory-message-comes-up-while-web-brwsing-on-ps3.1241268/)).
How Sony fitted it beside the XMB is undocumented; WebKit ports of 2012 commonly ran in one process (the
WebKit1 model), and this one still ran out of memory on the heavy pages of its day. Today's WebKitGTK
cannot be made single-process, and today's pages are heavier. The reference says a WebKit browser on this machine is
possible; it does not say today's one will be comfortable.

## 7. The rest, in a line each

- **elinks 0.19.1**: Debian builds it without an ECMAScript engine; a QuickJS rebuild would give a terminal
  browser partial DOM scripting, not the web.
- **Dillo 3.3.0**: installable, no JavaScript by design; lighter than NetSurf, no gain for this question.
- **links2, w3m, lynx**: installable, no JavaScript.
- **Servo**: not in Debian; draws through WebRender (GL 3-class, or software); no big-endian work known.
- **litehtml-based browsers**: HTML/CSS only, no JavaScript.
- **qutebrowser**: arch-all in the index, but needs PyQt6's QtWebEngine, which does not exist on ppc64.
- **Ladybird**: ruled out before (Skia big-endian, Qt 6, memory).

## What happened on the PS3

**Debian's build, 2026-10-28,** kernel .874, the desktop up (Openbox at 1920x1080, a terminal of the user's open), swap 127 MB used
in the VRAM tier at the start. Installed: `badwolf 1.4.0-1+ps3.931~dev` and the 28 Debian packages it lacked (183 MB,
WebKitGTK 2.52.6, JavaScriptCore, GStreamer base and good, fonts-liberation; from the pinned snapshot with
`dpkg -i`, since apt must not solve against Debian's full lists on the PS3). With Recommends, as an image build
would install them, it is 84 packages and 254 MB: GStreamer bad and libav, ImageMagick, TensorFlow Lite through
zxing, and `xdg-desktop-portal` with its GTK backend, a resident service; an image that ships WebKit should leave
those out.

One run: `badwolf https://walnut.nanobyte.dev/gamedb/` started in `ps3-desktop.slice` as the menu starts it,
caches dropped first, a 1 s CPU sample and an `xwd` every 2 s. The window was up and maximized in about 4 s, the
title "Walnut Games" arrived and the progress bar crept along, and the page area stayed white for the whole
minute. The web process had aborted at 29 s (systemd-coredump took three more minutes to write its 4 MB core, at
10-15% of the CPU, which is what kept it busy in the samples). Its backtrace, symbolised on the PC with Debian's
`libwebkit2gtk-4.1-0-dbgsym`: `WTFCrashWithInfo` from `CompactVariantOperations<Style::Length, Style::Percentage,
UnevaluatedCalculation>::encodedPayload` (CompactVariantOperations.h:109), from
`CSSValueConversion<LengthPercentage>` for `border-top-left-radius` (StyleBorderRadius.cpp:82), from
`Style::Builder::applyProperty`, `Resolver::applyMatchedProperties`, `TreeResolver::resolveComposedTree`.
At 90 s the three processes held 19 MB PSS and had 72 MB in swap, which says nothing about a working browser.
Closing the window left the crashed process until its core was written.

The measurements the plan below asks for (memory, load time, scrolling, the GameDB filter, game mode freezing it)
were not taken: every page with a rounded corner or similar length crashes the engine. The packages were removed
again; the PS3 is as before.

**Our build:** not on the PS3 yet. The session that was to test it started with the video baseline in mpv (the
bar the browser's video is judged against), and mpv's `--vo=gpu` wedged the RSX 7 frames into a 360p clip
(graph exception 0x105, [TESTS.md](../TESTS.md), [dump](../dumps/exception-0x105-mpv-vo-gpu.txt)); the user
rebooted the console and moved the browser and video tests to a session with them.

## PS3 test plan

The rules: not while the user plays or a benchmark or another agent's batch runs ([TESTS.md](../TESTS.md)
queue, the PS3 lock), and video only once the 0x105 fix is in. The desktop is up, since a browser lives there.
Nothing else runs: no game, no deploy, no ssh scans during a measured load. Swap empty at the start (`swapoff -a;
swapon -a`, as in the measurement protocol), the same boot for every candidate, one candidate at a time, and each
run checked by an `xwd` screenshot read by eye (a black or half-drawn page is not a result).

**Install.** `webkit2gtk` (our `libwebkit2gtk-4.1-0` and `libjavascriptcoregtk-4.1-0`) and `badwolf` by `make
deploy`, the Debian packages they lack from the pinned snapshot with `dpkg -i` (apt must not solve against
Debian's full lists on the PS3). The `-dbgsym` packages go on too, so a crash symbolises on the console. Against
the 0.1.0 image the closure is [the list below](#what-the-browser-adds-to-the-image).

**Candidates, in order.**

1. NetSurf 3.11 (baseline, JavaScript off), already installed.
2. Our WebKitGTK through `badwolf`: first [webkit/be-test.html](webkit/be-test.html) (every line green; canvas
   readback is the open question), then its defaults on the pages below, then `WALNUT_BROWSER_ACCEL=ondemand` and
   `WALNUT_BROWSER_CACHE=viewer` on the same pages to settle the defaults; three tabs to see the one web
   process; a game started through `ps3-game` with the browser open (game mode freezes it with the desktop
   slice; the game's fps against a run without it, the browser answering after). Watch for a WebKitGPUProcess
   in `ps`, and for an AT-SPI bus (`at-spi-bus-launcher`) started for it.
3. Video, after the 0x105 fix: the same H.264 clips (Big Buck Bunny at 360p, 480p and 720p) in mpv
   (`--vo=xv` first, then `gpu`) as the baseline, then opened in Badwolf from a file: plays or not, dropped
   frames (mpv's counters; for the browser, `xwd` sequences or GStreamer's stats), CPU and memory. A YouTube
   watch page if the page loads at all.
4. NetSurf with the relayout patch, JavaScript on, the GameDB home built for ES5 with the test shim: the
   rebuild time per change (from the log: "box tree rebuilt in N ms, reflowed in M ms") on GameDB and on
   the Wikipedia article with a class toggler.
5. Only if 2 nearly fits: cog with its X11 platform.

**Pages.** The GameDB home (`https://walnut.nanobyte.dev/gamedb/`, and the same page with `?q=doom`), one
game page (OpenArena), and a busy site (the English Wikipedia article "PlayStation 3"). If WebKit survives
those, one script-heavy site the user cares about.

**What to record per candidate and page.**

- Memory: PSS and anonymous memory of every process of the browser (`/proc/<pid>/smaps_rollup`, summed; for
  WebKit the UI, WebKitWebProcess and WebKitNetworkProcess), plus `MemAvailable` and swap used per tier
  (zram, VRAM swap, USB) before and 30 s after each load.
- Swap traffic: `pswpin`/`pswpout` from `/proc/vmstat` over the load and for 60 s after; major faults of the
  web process (library pages read back count here too).
- Time to load: from the command to the page drawn and the web process idle (CPU under 5% for 3 s), with an
  `xwd` every 2 s to see when the page is complete. NetSurf's status bar gives its own "Done (x s)".
- Scrolling the busy page by keyboard for 30 s: responsive or not, and stalls seen.
- The GameDB filter: bar visible, typing a word narrows the cards, the count changes, a tag box works, the
  link `?q=doom` opens filtered, and how long each change takes to show.
- The desktop afterwards: closing the browser gives memory back (`MemAvailable` within a few MB of the
  start), panel and file manager answer at once.

**Reading the result.** WebKit is a candidate to ship if the GameDB pages load in seconds, not tens of
seconds, the busy page scrolls without multi-second stalls, and the swap traffic settles once the page is
up. If it only works with the desktop frozen, it is not a desktop browser; then the patched NetSurf is the
browser for our own pages and simple sites, and the web at large stays out of reach on this machine.

## Sources

Indexes and builds: [debian-ports ppc64 Packages](https://deb.debian.org/debian-ports/dists/sid/main/binary-ppc64/),
[unreleased](https://deb.debian.org/debian-ports/dists/unreleased/main/binary-ppc64/),
buildd status for [webkit2gtk](https://buildd.debian.org/status/package.php?p=webkit2gtk&suite=sid) and
its [ppc64 build logs](https://buildd.debian.org/status/logs.php?pkg=webkit2gtk&arch=ppc64),
[wpewebkit](https://buildd.debian.org/status/package.php?p=wpewebkit&suite=sid),
[firefox-esr](https://buildd.debian.org/status/package.php?p=firefox-esr&suite=sid),
[firefox](https://buildd.debian.org/status/package.php?p=firefox&suite=sid),
[netsurf](https://buildd.debian.org/status/package.php?p=netsurf&suite=sid); Debian's
[webkit2gtk rules](https://sources.debian.org/data/main/w/webkit2gtk/2.52.6-1/debian/rules);
WebKit at the [webkitgtk-2.52.6](https://github.com/WebKit/WebKit/tree/webkitgtk-2.52.6) tag:
`Source/cmake/WebKitFeatures.cmake`, `OptionsGTK.cmake`, `OptionsWPE.cmake`, `OptionsCommon.cmake`,
`Source/WTF/wtf/MemoryPressureHandler.cpp` and `RAMSize.cpp`, `Source/WebKit/Shared/CacheModel.cpp`,
`Source/WebKit/UIProcess/API/glib/WebKitWebContext.cpp` and `WebKitMemoryPressureSettings.cpp`,
`Source/WebKit/UIProcess/Launcher/glib/ProcessLauncherGLib.cpp` and `BubblewrapLauncher.cpp`,
`Source/JavaScriptCore/runtime/OptionsList.h`; NetSurf: Debian's netsurf 3.11-4 source and the
[git history](https://github.com/netsurf-browser/netsurf) to 2026-10-27. Everything else is linked where
it is used. The PC measurements, the NetSurf builds and the probes were run in scratch containers and
deleted; the patch, the probe page and the shim are kept in [netsurf-relayout/](netsurf-relayout/).
