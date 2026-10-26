# Upstream submissions (drafts)

Everything here is a draft. Nothing has been sent, opened or posted; the decision and the sending are yours.
Patches are `git format-patch` output with author Tiberiu Chibici <chibicitiberiu@gmail.com>, no
Co-authored-by or other attribution lines. The kernel patches carry your Signed-off-by (required there).
Bases: the pristine upstream tarballs we package (teeworlds 0.7.5, yquake2 8.70, Linux 6.12.111, Mesa 26.2.4).
Rebase onto each project's current head before sending.

| # | Item | Where to submit | Files | Status |
|---|---|---|---|---|
| 1 | Teeworlds FIONBIO int (big-endian 64-bit) | GitHub teeworlds/teeworlds issue + PR; Debian BTS for `teeworlds` | `teeworlds.md`, `teeworlds-0001-ioctl-fionbio-int.patch` | draft, not sent. Master has the same code; tested on PS3 and qemu |
| 2 | Linux ps3_power_save() lost decrementer | mail: geoff@infradead.org, linuxppc-dev | `linux-ps3-idle.md`, `linux-ps3-0001-idle-lost-decrementer.patch` | draft, not sent. RFC: problem measured; the fix booted on 2026-10-18 (kernel .419: worst idle wakeup 4 ms with it, 1103 ms without), numbers not yet in the commit message |
| 3 | Linux ps3_sys_manager_fin() hang | mail: same recipients | `linux-ps3-sys-manager.md`, `linux-ps3-0002-sys-manager-fin-wait-for-command.patch` | draft, not sent. RFC: compiled only; in the booted kernel .433 since 2026-10-18, but its power-off and button checks have not run |
| 4 | Yamagi Quake II KISS table | GitHub yquake2/yquake2 issue + PR | `yquake2.md`, `yquake2-0001-kiss-table-size.patch` | draft, not sent. Smallest form of our fix; measured with the heap variant |
| 5 | Mesa translate_generic 8-bit LUT | gitlab.freedesktop.org/mesa/mesa MR | `mesa.md`, `mesa-0001-translate-8bit-lut.patch` | draft, not sent. Generic (all u_vbuf/draw users); upstream form not compiled |
| 6 | Boost.Context ppc64 fcontext (ELFv2/ELFv1 on big-endian ppc64) | Debian BTS #1146645 (boost1.92) follow-up; GitHub boostorg/context issue + PR | `boost-context.md`, `packages/patched/libs/boost-context/packaging/0001-ppc64-fcontext.patch` | draft, not sent. Wesnoth runs on the PS3 with it (batch ssdroot8) |

## Candidates, not drafted

Upstreaming happens only when it is justified (the user, 2026-10-20): most games don't target big-endian, and
the gain is mainly fewer patches for us to carry. Noted here, not worked on:

- **Warzone 2100** (we stay on 4.3.5, the last GL 2.1 release): 0001 `<cstdint>` for GCC 16 (a plain build fix),
  0003 terrain decal colour, 0005 radar colours, 0008 Opus sample order (byte-order neutral rewrites), 0004 the
  bundled etcpak's DXT blocks on big-endian (also wolfpld/etcpak). Whether master still has the code: not checked.
- **sdl2-compat: SDL_CloseAudioDevice() with the device lock held deadlocks** (`packages/patched/graphics/sdl2-compat/patches/0001`): not
  big-endian, it hangs on amd64 too (stock Debian 2.32.74 on SDL3 3.4.18, dummy and ALSA drivers, through SDL2's
  API or SDL 1.2's via sdl12-compat; Armagetron Advanced hangs on quit). Not reported upstream (no issue found on
  2026-10-25; #499/#503 fixed the same lock order for pausing). A good candidate: a 20-line test program shows it.
- **nv30: query report slots freed while their query still uses them** (`packages/patched/graphics/mesa/patches/0046`): stock nv30 code, not
  PS3-specific. Past 124 outstanding reports the oldest object is freed under its query, and the next result check is a
  use after free (wrong occlusion results, then heap corruption; real Sauerbraten crashed on it). A strong candidate:
  a crash fix for every NV3x/NV4x user, with a host test (`packages/patched/graphics/mesa/tests/query-steal-test.c`) and the Core 2 + 7600 GT box
  to run piglit's occlusion query tests on. The upstream form drops nothing (the patch has no PS3 switch), but sits on
  our 0006/0035/0042 query changes: rebase onto stock nv30_query.c first.
- **hid-sony: an inactive DS3 reads as both sticks pushed up and left** (`packages/patched/kernel/linux/patches/local/0050`): not PS3-specific,
  any Linux PC with a DualShock 3 on the cable before its PS button. 6.12 has it (mainline not checked); SDL (evdev),
  joydev and raw evdev games all see it (Quakespasm's menu runs up by itself). A good candidate: a 20-line helper, a QEMU/uhid test
  that shows 0 before and 128 after (`packages/patched/kernel/linux/tests/sixaxis-idle-axes.sh`); rebase onto hid.git and check whether
  hid-playstation has taken the DS3 over first.
- **GCC 16.2 wrong code** (Warzone patch 0007, PACKAGING.md): the loop vectorizer on AltiVec without VSX at `-O3`.
  Worth a GCC report only with a minimal reproducer and a check of GCC 14/15 (a regression gets triaged);
  the reproducer would also show which of our other `-O3 -mcpu=cell` builds could be hit.

## Order I would send in

1. Teeworlds (small, tested, clearly right). Debian bug after the upstream issue exists, so it can link it.
2. Yamagi Quake II (small, measured). Open the issue first and wait for a reply.
3. Mesa MR, after a compile check and, ideally, the host comparison test as a unit test.
4. The kernel patches last: boot the test kernel first (idle patch) and rebase onto powerpc `next`.

## Open points

- Boot session: idle patch numbers with the fix (see `linux-ps3-idle.md`; measured 2026-10-18, to be written into the message), sys-manager patch sanity (still open).
- Mesa: the PC-side test harness is not saved in the repo; recreate before offering a test.
- Yamagi: nobody checked whether demos or savegames depend on the random sequence.
- All patches were cut against our tarballs, not against each project's head. Upstream-head checks done:
  teeworlds `src/base/system.c` and yquake2 `src/common/shared/rand.c` read on GitHub master (same code).
  Linux and Mesa heads could not be fetched.
- Email sending needs `git send-email` configured; GitHub needs forks. None of that was touched.
