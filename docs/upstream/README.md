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
| 6 | Boost.Context ppc64 fcontext (ELFv2/ELFv1 on big-endian ppc64) | Debian BTS #1146645 (boost1.92) follow-up; GitHub boostorg/context issue + PR | `boost-context.md`, `boost-context/packaging/0001-ppc64-fcontext.patch` | draft, not sent. Wesnoth runs on the PS3 with it (batch ssdroot8) |

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
