# A browser with JavaScript on the PS3 (research, 2026-10-28)

Walnut ships NetSurf 3.11 with JavaScript off. The user finds a browser without JavaScript of little use.
This note looks at what could run JavaScript on Debian sid ppc64 (big-endian, debian-ports) with 256 MB of
XDR, the in-order Cell PPE and a GL 2.1 GPU. Ladybird was ruled out earlier (Skia has no big-endian
support, Qt 6, memory) and is not repeated here.

Nothing was run on the PS3. The package facts come from the debian-ports and Debian indexes and buildd pages
of 2026-10-28. The memory numbers were measured on the PC: amd64, Debian sid in a container, Xvfb at
1280x1024, Mesa llvmpipe for GL. They are a guide to the order of magnitude, not PS3 numbers: ppc64 ELFv1
code is larger (libwebkit2gtk is 131 MB installed on ppc64 against 101 MB on s390x), while the PS3 has no
LLVM in its GL driver (llvmpipe's libLLVM accounted for 20-30 MB of PSS in the browsers below).

## Verdict

**Test WebKitGTK through surf on the PS3. It is the only engine with modern JavaScript that is built and
installable for ppc64 today, and the GameDB filter works in it.** Whether it fits is open: on the PC, surf
on the GameDB home took 352 MB PSS, of which perhaps 200-250 MB would remain on the PS3 once llvmpipe and
x86/Skia differences are taken out (a guess). With the desktop up that is more than the free RAM, so the
VRAM swap and zram would carry part of it. Only the PS3 can say whether that is usable or a swap storm.

Keep NetSurf as the default browser for the static pages it renders well. Its JavaScript is not an answer:
3.11 runs scripts, timers and click handlers, but changes to the page after it is laid out never reach the
screen (tested below), and there has been no NetSurf release or JavaScript work since 2023.

Firefox is out for now: no ppc64 build is installable on current sid, and with every memory pref we know it
took 474 MB PSS on the PC for the GameDB home. Chromium has no big-endian port in Debian and no upstream
support. WPE with cog is a possible later step if WebKitGTK nearly fits, since it drops GTK.

## The comparison

Memory: PSS of all the browser's processes, summed from `/proc/<pid>/smaps_rollup`, about 20-40 s after the
load, on the PC (amd64, see above). "Home" is our GameDB home page (23 cards with screenshots), "busy" is
the English Wikipedia article on the PlayStation 3.

| Option | ppc64 BE in debian-ports (2026-10-28) | JavaScript | Memory, home / busy (PC) | Rendering | Effort | Main risks |
|---|---|---|---|---|---|---|
| **WebKitGTK + surf** | libwebkit2gtk-4.1-0 2.52.6-1 and libwebkitgtk-6.0-4 2.52.6-1, installable; sid has 2.54.2, which is BD-Uninstallable on ppc64. surf 2.1+git20250419-2, vimb 4.0.0-2 (GTK 4), epiphany-browser 51.1-1 | Full modern JS on JavaScriptCore's C++ interpreter (CLoop): no JIT, no WebAssembly on big-endian | 352 / 418 MB with `JSC_useJIT=0` | Cairo on big-endian (Skia is off by default there), GL compositing optional | S to install, M for a trimmed shell | Memory; CLoop speed on the PPE; ports lag a release behind; upstream means to drop Cairo, the only big-endian renderer |
| **NetSurf 3.11** (what we ship) | netsurf-gtk 3.11-4, current | Duktape 2.7 (ES5.1), built in, off by default; DOM changes after layout are not drawn | 107 / 150 MB (JS off) | Own engine, CSS 2.1 plus flexbox, no grid | none | It is the ceiling: no dynamic pages |
| **Firefox ESR / Firefox** | firefox-esr 128.4.0esr-1, firefox 133.0-1, and 146.0.1-1+quickjs in `unreleased`: all three need libvpx9 or libvpx11, and ppc64 has only libvpx12, so none installs. 153.6.0esr and 158.0.1 are BD-Uninstallable (nodejs) | SpiderMonkey interpreters only in Debian builds; an out-of-tree big-endian JIT exists for 153+ (below) | 474 / 556 MB with the low-memory profile below, JIT off | Software WebRender (our GL 2.1 is below the hardware path's GL 3 / GLES 3) | XL: our own build of a 20 GB-link package | Memory alone rules it out; 133.0 is reported to segfault at start on big-endian |
| **Chromium, QtWebEngine** | Not built: Debian's chromium is amd64, arm64, armhf, i386, loong64, ppc64el; qt6-webengine is x86/ARM only | V8 | not measured; 267 MB for one tab on a Pi 4 (LinuxLinks) | Skia, patched | XL+: a 69-patch out-of-tree port | No upstream big-endian support; memory |
| **WPE WebKit + cog** | libwpewebkit-2.0-1 2.50.6-1 and cog 0.18.5-1, installable, but Debian's cog has only the drm, headless and wl platforms (no X11) | As WebKitGTK | not measured; expect WebKitGTK minus GTK | As WebKitGTK | M: rebuild cog with its X11 platform, or run it in a Wayland compositor | Same engine risks; no window manager integration |
| **Pale Moon (UXP)** | Not packaged; community big-endian ppc64 builds exist (forum, 2026) | Old SpiderMonkey, interpreter only on ppc64 | not measured; single-process by design | Own Gecko fork | L: our own build | Community builds that do not start for everyone; old web compatibility |
| **elinks 0.19** | 0.19.1-2.1+b1, built without any ECMAScript engine | none in Debian's build (upstream can use QuickJS, MuJS or SpiderMonkey) | small | Text | M to rebuild with QuickJS | Terminal only; partial DOM |
| **Dillo 3.3** | 3.3.0-3 | none | small (58 MB of disk beyond the desktop) | Own engine | none | No JavaScript at all |
| **Servo** | Not packaged | SpiderMonkey | not measured | WebRender (GL 3-class GPU, or software) | XL | No big-endian story, heavier than WebKit |

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
API); sid has moved to 2.54.2-2, which is BD-Uninstallable on ppc64 and s390x for now
([buildd](https://buildd.debian.org/status/package.php?p=webkit2gtk&suite=sid)), so the port runs one
release behind. Shells on ppc64: surf (GTK 3, 4.1 API), vimb (GTK 4), epiphany-browser 51.1 (GTK 4 and
libadwaita). luakit 2.3 is stuck on the removed 4.0 API and does not install. badwolf is not packaged.

**How it is built on big-endian.** From WebKit's own cmake at the 2.52.6 tag: `OptionsGTK.cmake` and
`OptionsWPE.cmake` turn Skia on only when `CMAKE_CXX_BYTE_ORDER` is little-endian, so ppc64 keeps the
Cairo renderer (the change came with 2.46, [bug 274032](https://bugs.webkit.org/show_bug.cgi?id=274032));
`WebKitFeatures.cmake` gives every CPU other than x86-64, ARM64, ARMv7 Thumb-2 and RISC-V 64 the C_LOOP
interpreter with JIT off, the system malloc and no WebAssembly. Debian's rules add nothing for ppc64
beyond that. The big-endian non-Skia build breaks now and then and gets fixed: Epiphany's tests on s390x
needed 2D canvas acceleration off ([bug 279220](https://bugs.webkit.org/show_bug.cgi?id=279220)), and
2.52.2 failed to build without Skia ([bug 312160](https://bugs.webkit.org/show_bug.cgi?id=312160)).
The CLoop has its own history of big-endian bugs, found on s390x, the only big-endian machine WebKit tests
([bug 238956](https://bugs.webkit.org/show_bug.cgi?id=238956),
[bug 213307](https://bugs.webkit.org/show_bug.cgi?id=213307)). Upstream says Cairo is to be removed
"in the near future" ([Igalia](https://blogs.igalia.com/carlosgc/?p=1029)); when that happens big-endian
WebKit loses its renderer unless someone ports Skia.

**Memory knobs.** `WEBKIT_DISABLE_COMPOSITING_MODE=1` still exists (a crash with it was fixed in February
2026, [bug 306993](https://bugs.webkit.org/show_bug.cgi?id=306993)); the settings API has
`hardware-acceleration-policy` `NEVER` ([docs](https://webkitgtk.org/reference/webkit2gtk/stable/enum.HardwareAccelerationPolicy.html)).
`WebKitMemoryPressureSettings` (since 2.34) sets a memory limit, conservative and strict thresholds, a
kill threshold and the poll interval for web processes, and separately for the network process
([docs](https://webkitgtk.org/reference/webkit2gtk/stable/struct.MemoryPressureSettings.html)); a shell has
to set it, surf does not. The cache model `WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER` drops the memory cache; surf
picks it when its `DiskCache` setting is off. Media, WebAudio and WebGL are settings too (`enable-media`,
`enable-webaudio`, `enable-webgl`). `JSC_useJIT=0` changes nothing on ppc64, where there is no JIT. A
web process per tab is the model since 2.26; one tab means one web process, plus the network process and
the UI. Since WebKit links GStreamer, media stays loadable unless the shell turns it off.

**Measured on the PC** (WebKitGTK 2.54.2 amd64, so Skia and llvmpipe, `JSC_useJIT=0`,
`WEBKIT_DISABLE_COMPOSITING_MODE=1`, stock surf): GameDB home 352 MB PSS (UI 106, network 34, web process
221), a game page 340 MB, Wikipedia 418 MB. The web process on the home page held 58 MB of heap, 40 MB of
other anonymous memory, 43 MB of libwebkit2gtk, 31 MB of libLLVM and 11 MB of JavaScriptCore; the UI held 19
MB of libLLVM. Taking llvmpipe out and adding ppc64's larger code gives the 200-250 MB guess above. We
found no published figure for WebKitGTK on 512 MB boards; a Pi user asking why WebKitWebProcess uses so
much memory gives none either ([Pimoroni forum](https://forums.pimoroni.com/t/browser-requirements/1483)).

**The GameDB filter in surf works**: `index.html#q=doom` showed "6 of 23 games" with the filter bar
visible.

**GL.** With compositing on, WebKitGTK draws layers through EGL and GLES 2 class calls (TextureMapper),
which our nv30 driver should manage (EGL works, [DRIVER.md](../DRIVER.md)), but it is a GL client the
validator has never seen. The first PS3 runs keep compositing off; turning it on is a separate step, with
VRAM use watched since VRAM is the swap's first tier.

## 4. WPE WebKit and cog

WPE is WebKit's embedded port (no GTK). libwpewebkit-2.0-1 2.50.6-1 and cog 0.18.5-1 install on ppc64, but
Debian's cog ships only the `drm`, `headless` and `wl` platform modules. `drm` would take the display from
X; `wl` needs a Wayland compositor (cage or weston nested in X: more memory). Cog's upstream X11 platform is
not built by Debian. So WPE on our desktop means rebuilding cog with its X11 platform (M effort: a small
package). It saves GTK in the UI process; the web and network processes are the same engine, so the gain
is probably tens of MB, not a different class. Igalia publishes no Pi Zero-class numbers we could find
([Igalia WPE](https://igalia.com/wpe)). Worth it only if surf measures close to fitting.

## 5. NetSurf's JavaScript

**Built, off.** Debian's netsurf (3.11-4) builds from netsurf-all with the defaults, and
`Makefile.defaults` has `NETSURF_USE_DUKTAPE := YES`; Debian even carries `fix-nsgenbind-be.patch` so the
bindings generate on big-endian. `desktop/options.h` declares `enable_javascript` with default false. It is
switched on in `~/.config/netsurf/Choices` with `enable_javascript:1`, or in the GUI's preferences.

**What 3.11 implements** (Duktape 2.7, ES5.1, with an `Array.from` polyfill; read from the `.bnd`
bindings and checked with a probe page in netsurf-gtk 3.11-4 on the PC): `getElementById`,
`getElementsByTagName`, `createElement`, `appendChild` and the other Node methods, `textContent`,
`innerHTML`, `className`, `classList`, `style`, `addEventListener`, `setTimeout`/`setInterval`, `location`,
`document.write`, `console`, cookies, canvas image data. Not implemented (missing, or a stub that returns
`undefined`): `querySelector`/`querySelectorAll`, `dataset`, the `hidden` property, `history`, `Promise`,
`fetch`, `XMLHttpRequest`, `localStorage`, `navigator.clipboard`, `scrollIntoView`; `closest`,
`execCommand` and `URLSearchParams` exist only as stubs (`new URLSearchParams("a=1")` throws "Bad
constructor"). ES2015 syntax (`let`, arrow functions) is a SyntaxError.

**The decisive test.** Scripts run while the page parses (a `textContent` change before layout shows), and
timers and click handlers fire (they changed `location.href`, which the URL bar showed). But the text and
class changes those same handlers made were never drawn: after load, the page on screen stays as first laid
out. Any filter, tab or menu that changes the page after load cannot work in 3.11, whatever we polyfill.

**Development.** No release after 3.11 (2023-12-28); the newest commits under
`content/handlers/javascript` are warning fixes of 2024, the last feature work (DOMTokenList) is from
November 2022 ([GitHub mirror](https://github.com/netsurf-browser/netsurf/commits/master/content/handlers/javascript)).

**The GameDB filter in NetSurf.** With JavaScript on, the home and game pages look exactly as with it off
(the home screenshots are pixel-identical) and the log has `Uncaught error in JS: SyntaxError: parse error
(line 1)`. The cause is Hugo's minifier: it rewrites `catch (e) {}` in `assets/gamedb.js` to `catch{}`
(ES2019 optional catch binding), which no ES5 engine parses. Had it parsed, line 1's
`document.querySelectorAll` would throw a TypeError, and the `URLSearchParams`/`history.replaceState` guard
keeps the bar hidden anyway. So turning JavaScript on gains nothing on our site and costs parse time on
others; it stays off. (Hugo's JS minifier targets ES2022 unless told otherwise; `[minify.tdewolff.js]
version = 2015` in hugo.toml should stop the `catch{}` rewrite if we ever target an ES5 engine. Not needed
for NetSurf.)

## 6. The rest, in a line each

- **elinks 0.19.1**: Debian builds it without an ECMAScript engine (no QuickJS, MuJS or SpiderMonkey in
  `debian/rules` or the dependencies); a QuickJS rebuild would give a terminal browser partial DOM
  scripting, not the web.
- **Dillo 3.3.0**: installable, no JavaScript by design; lighter than NetSurf, no gain for this question.
- **links2, w3m, lynx**: installable, no JavaScript.
- **Servo**: not in Debian; draws through WebRender (GL 3-class, or software); no big-endian work known.
- **litehtml-based browsers**: HTML/CSS only, no JavaScript.
- **qutebrowser**: arch-all in the index, but needs PyQt6's QtWebEngine, which does not exist on ppc64.
- **Ladybird**: ruled out before (Skia big-endian, Qt 6, memory).

## PS3 test plan

The rules: not while the user plays or a benchmark or another agent's batch runs ([TESTS.md](../TESTS.md)
queue; check nobody is at the console first). The desktop is up, since a browser lives there. Nothing else
runs: no game, no deploy, no ssh scans during a measured load. Swap empty at the start (`swapoff -a; swapon
-a`, as in the measurement protocol), the same boot for every candidate, one candidate at a time, and each
run checked by an `xwd` screenshot read by eye (a black or half-drawn page is not a result). Packages go on
with apt from debian-ports; nothing is changed in our packages until a result says so.

**Candidates, in order.**

1. NetSurf 3.11 (baseline, JavaScript off), already installed.
2. surf on WebKitGTK 2.52.6: `apt install surf` (pulls libwebkit2gtk-4.1-0 and GStreamer). Runs: (a)
   `WEBKIT_DISABLE_COMPOSITING_MODE=1`, stock surf; (b) the same with GStreamer's media off if surf allows,
   else noted; (c) compositing on, only if (a) is usable, watching `dmesg` for ps3rsx validator rejections
   and the VRAM census. If bubblewrap cannot create its namespaces under our kernel, note it and run once
   with `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` to get the numbers.
3. Only if surf nearly fits: a trimmed shell (surf's `config.h`: DiskCache off for the document-viewer
   cache model, media, WebGL and accelerated canvas off, plus `WebKitMemoryPressureSettings` with a
   limit), or cog with its X11 platform. Firefox is not tested: no package installs, and its PC floor is
   twice the RAM.

**Pages.** The GameDB home (`https://walnut.nanobyte.dev/gamedb/`, and the same page with `#q=doom`), one
game page (OpenArena), and a busy site (the English Wikipedia article "PlayStation 3"). If surf survives
those, one script-heavy site the user cares about.

**What to record per candidate and page.**

- Memory: RSS and PSS of every process of the browser (`/proc/<pid>/smaps_rollup`, summed; for WebKit the
  UI, WebKitWebProcess and WebKitNetworkProcess), plus `MemAvailable`, swap used per tier (zram, VRAM swap,
  USB) before and 30 s after each load.
- Swap traffic: `pswpin`/`pswpout` from `/proc/vmstat` over the load and for 60 s after; major faults of the
  web process.
- Time to load: from the command to the page drawn and the web process idle (CPU under 5% for 3 s), with an
  `xwd` every 2 s to see when the page is complete. NetSurf's status bar gives its own "Done (x s)".
- Scrolling the busy page by keyboard for 30 s: responsive or not, and stalls seen.
- The GameDB filter: bar visible, typing a word narrows the cards, the count changes, a category select
  works, the hash link `#q=doom` opens filtered.
- The desktop afterwards: closing the browser gives memory back (`MemAvailable` within a few MB of the
  start), panel and file manager answer at once.

**Reading the result.** surf is a candidate to ship if the GameDB pages load in seconds, not tens of
seconds, the busy page scrolls without multi-second stalls, and the swap traffic settles once the page is
up. If it only works with the desktop frozen, it is not a desktop browser. Then the next step is cog with
X11 (no GTK) or living with NetSurf.

## Sources

Indexes and builds: [debian-ports ppc64 Packages](https://deb.debian.org/debian-ports/dists/sid/main/binary-ppc64/),
[unreleased](https://deb.debian.org/debian-ports/dists/unreleased/main/binary-ppc64/),
buildd status for [webkit2gtk](https://buildd.debian.org/status/package.php?p=webkit2gtk&suite=sid),
[wpewebkit](https://buildd.debian.org/status/package.php?p=wpewebkit&suite=sid),
[firefox-esr](https://buildd.debian.org/status/package.php?p=firefox-esr&suite=sid),
[firefox](https://buildd.debian.org/status/package.php?p=firefox&suite=sid),
[netsurf](https://buildd.debian.org/status/package.php?p=netsurf&suite=sid); Debian's
[webkit2gtk rules](https://salsa.debian.org/webkit-team/webkit/-/blob/debian/unstable/debian/rules);
WebKit's [WebKitFeatures.cmake](https://github.com/WebKit/WebKit/blob/webkitgtk-2.52.6/Source/cmake/WebKitFeatures.cmake)
and [OptionsGTK.cmake](https://github.com/WebKit/WebKit/blob/webkitgtk-2.52.6/Source/cmake/OptionsGTK.cmake)
at webkitgtk-2.52.6; the netsurf 3.11 source and Debian packaging from deb.debian.org. Everything else is
linked where it is used. The PC measurements and NetSurf probes were run in a scratch container and
deleted; the probe pages and the `user.js` are described above.
