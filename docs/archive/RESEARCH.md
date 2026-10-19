> Superseded on 2026-10-19 by docs/research/RESEARCH.md; kept unchanged for its history.

# Background research (2026-10-13)

## Linux on the PS3

- Boot path: CFW with OtherOS++, then Petitboot, which kexecs a ppc64 kernel.
- Mainline still supports the PS3. Cell blades were dropped in 6.15 (March 2025), PS3 was kept.
- Distros: Gentoo (Damian Dudycz's installer, PS3 kernel overlay, Cell binhost), T2 SDE (René Rebe), Debian ppc64 via debian-ports (unofficial).
- Limits: 256 MB XDR (about 210 MB usable), 256 MB GDDR3 only usable as ps3vram swap/block device.

## RSX (GPU)

- NVIDIA G70/NV47-derived, same family as GeForce 7. OpenGL 2.1 at best. No usable video decode block.
- Stock OtherOS gated the RSX. OtherOS++ (graf_chokolo's lv1 patches) was meant to give Linux GameOS-level access including the RSX. On a CFW fat PS3 no new exploit is needed; the missing piece is the driver.
- Prior work: René Rebe's xf86-video-ps3 (partial 2D EXA, last update Oct 2020), glevand's FreeBSD RSX driver attempt.
- Driver basis: kernel nouveau (NV40 family) and Mesa nv30. Reusable: pushbuffer format, 3D methods, shader compiler. Replace: device bring-up, memory management, interrupts, display (ps3av).
- References: envytools docs, RPCS3 (RSX command emulation), PSL1GHT librsx, psdevwiki lv1 hypercalls. Avoid Sony's leaked SDK.
- Nouveau on GeForce 6/7 in general: display and 2D solid, 3D (nv30) buggy and poorly maintained. NVIDIA's legacy 304 driver is dead.

## Jellyfin / video (parked)

- No hardware video decoder. GameOS decodes H.264 on the SPUs via Sony's VDEC.
- Existing clients: MontyMcK/JellyFin-PS3 (native, H.264 1080p via VDEC) and FloStyle/m7-jellyfin-neo (Movian plugin). HEVC/AV1 means server transcode.
- An SPU HEVC decoder looks feasible for 8-bit 1080p30 (x265 enables WPP by default), but it's months of work. AV1 is much harder.

## Spare hardware

- Gigabyte GeForce 7600 GT (GV-NX76T256D-RH), PCIe: same family as the RSX. Possible PC-side reference with nouveau.
- MSI MS-V117, PCIe: GeForce 8800 GT or 9600 GT, unconfirmed. Not RSX family.
- Palit Radeon 9550 and an ATI Radeon 9600-family card, both AGP.
- Core 2 Duo PC (fits the 7600 GT) and a Pentium 4 PC (likely AGP).

## Sources

- https://wiki.gentoo.org/wiki/User:Damiandudycz/PlayStation3
- https://t2linux.com/hardware/console/Sony/PS3/
- https://www.psdevwiki.com/ps3/OtherOS++
- https://archive.org/details/petitboot-ps3
- https://lkml.rescloud.iu.edu/2503.3/03947.html
- https://www.psx-place.com/threads/update-october-06th-20-hw-acceleration-rsx-project-update-by-renerebetm.21370/
- https://www.elotrolado.net/hilo_otheros-de-graf-chokolo-y-otros-loaders-de-gnu-linux-09-02-2013_1589945_s200
- https://github.com/MontyMcK/JellyFin-PS3
- https://github.com/FloStyle/m7-jellyfin-neo
