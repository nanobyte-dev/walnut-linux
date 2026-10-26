# Image against the live PS3 (2026-10-16)

This is a dated record. On 2026-10-16 we built a fresh root filesystem image from the current packages and compared it with the root the PS3 runs from its USB disk, to see what a reimage would lose or drop. Nothing was reimaged and the PS3 was only read.

The two package fixes it found were made, and both are in the release image of 2026-10-18 (2.9 GiB, sha256 `65dae499...`, see [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md#the-image)). What a reimage would still lose is the live system's own state: old kernels, hand edits and a few files under "Elsewhere" and "Home" below. The image session on the PS3 has not run yet.

Key for the Action column: **a** = goes into a package or the image, **b** = dropped at reimage, **c** = the user keeps it.

## The image of 2026-10-16

- `build/rootfs/debian-ppc64.tar`, 2,179,409,920 bytes (2.0 GiB), sha256 `4882903b...f78234`. `build/rootfs/base.tar` (the Debian layer) was rebuilt too, since `system/image/packages.txt` had changed (kbd): 855 s. Our layer took 200 s. The previous outputs went to `build/old/20261016-194147-rootfs/`.
- Our packages: kernel `.178` (release and debug), ps3rsx 0.181, Mesa `+ps3.186`, ps3-desktop and ps3-apps 0.202, ps3-system 0.160, ps3-boot 0.152, ps3-gpu-tools 0.189, ps3-splash 0.181~dev, Plymouth `+ps3.177~dev`, classicube `+ps3.152`. ps3-spu, ps3-spu-programs and libspeq0 are 0.201 from the `ps3-dev` pool (sha256 checked), mounted over `build/debs/ps3-spu` for the build, because that directory was rebuilt as 0.204~dev while the base layer ran.
- The live system ran the same set, so the comparison was like for like.

## Summary

The live system was close to the image. 754 packages in the image, 766 live, no version differences between packages installed on both. Users, groups, diversions, `/usr/local` and `/opt` were identical. `dpkg --verify` on the PS3 flagged three files. Apart from the old kernels, no file under `/usr` or `/opt` was missing from both the image and every package.

## Before a reimage

Two findings needed a package fix. Both are fixed in the 2026-10-18 release image: ps3-system 0.489 enables systemd-networkd and ssh, and ps3-boot 0.484 gives `default=` the release kernel (`default=6.12.111+ps3.472`). The Petitboot in use ignores `default=` anyway ([PACKAGING.md](PACKAGING.md#kernels)); the image session checks which entry it preselects.

| What | Where | Action |
|---|---|---|
| systemd-networkd isn't enabled in the image. Live it runs and keeps eth0's DHCP lease; its links came from the old `system/image/customize.sh` (`systemctl enable systemd-networkd ssh`). ps3-system's postinst calls `deb-systemd-helper enable systemd-networkd.service`, but systemd's own postinst ran `deb-systemd-helper update-state` for the unit first, so the helper sees it as already installed with no links (left disabled by the admin) and creates none. The image booted with only the kernel's `ip=dhcp` address, with no renewal, and `20-ps3-wired.network` went unused. | `packages/walnut/system/ps3-system/debian/postinst` | **a**: enable with `systemctl enable systemd-networkd.service` (works in a chroot) or ship the `.wants` links; rebuild ps3-system. Fixed in 0.489 |
| The image's boot menu defaulted to the **debug** kernel (`default=6.12.111+ps3.178-debug202610161230`) despite `PS3_DEFAULT_FLAVOUR=release`. `build/debs/ps3-boot` was 0.152, from before ecfaa5d; the source (version 176) understands `release`/`debug`, the 0.152 script only `-<flavour>` suffixes. | `ps3-boot` | **a**: `make ps3-boot-deb`, then rebuild the image. Fixed in 0.484 |
| Live `kboot.conf` defaults to `6.12.112-ps3-debug-202610151803` (old pipeline kernel; it sorts above 6.12.111). Booting without picking in Petitboot gives that kernel, not `.178`. | live only | **b**: goes with the old kernels |
| `build/debs` was behind the source for ps3-gpu-tools (0.189, source 193) and ps3-splash/Plymouth (`~dev` builds, committed as 182). ps3-system and classicube differed only by tooling commits. | `build/debs` | **a**: rebuild before the image you install. Done for the release image |

## Packages

| Difference | Packages | Action |
|---|---|---|
| Only live: old kernels and their drivers | `linux-image-6.12.111+ps3.152-ps3{,-release}`, `...+ps3.163-ps3{,-release}`, `linux-image-ps3-release` (old meta), `ps3-kernel`, `ps3-kernel-6.12.112-ps3-debug-202610151608`, `...-202610151803`, `ps3rsx` (0.114), `ps3rsx-6.12.112-ps3-debug-202610151608`, `ps3rsx-modules-6.12.111+ps3.{152,163}-ps3{,-release}`, `ps3rsx-modules-6.12.112-ps3-debug-202610151803`, `ps3rsx-modules-ps3-release`; `linux-image-6.12.111+ps3.173.dev...` removed, config files left (`rc`) | **b** |
| Only live: installed by hand | `htop` | **a** if wanted: add to `system/image/packages.txt`; else **b** |
| Only in the image | `kbd` (added to `system/image/packages.txt` after this root was installed); `linux-image-ps3-debug`, `linux-image-6.12.111+ps3.178-debug202610161230`, `ps3rsx-modules-ps3-debug`, `ps3rsx-modules-6.12.111+ps3.178-debug202610161230` (the deploys left the debug kernel out) | image is right; nothing to do |
| Install marks | Live marks `hsetroot` manual (auto in the image, a ps3-desktop dependency) | **b** |
| Versions | none differ | |

## /etc

Every other file hashed the same on both sides (secrets excluded from hashing: shadow, gshadow, SSH host keys, `NetworkManager/system-connections`).

| File (live) | Difference | Action |
|---|---|---|
| `systemd/system/**/systemd-networkd*`, `dbus-org.freedesktop.network1.service`, `sysinit.target.wants/systemd-network-generator.service` | enable links only live | **a** (see above) |
| `default/ps3-boot` | live: no `PS3_DEFAULT_FLAVOUR`, an old `PS3_LEGACY_ARGS` (`ps3rsx_gart=32M`, for kernels without an `.args` file) | **b**: the image writes it |
| `kboot.conf` | live lists the old kernels | **b** |
| `ssh/sshd_config` | live: `PasswordAuthentication no` edited in (old customize.sh); the image has the same in ps3-system's `sshd_config.d/ps3.conf` | **b** |
| `X11/Xwrapper.config` | same two settings, live lacks Debian's comment header (old desktop-setup.sh) | **b** |
| `plymouth/plymouthd.conf`, `plymouthd.conf.dpkg-dist`, `plymouthd.confe` | same effective settings; live keeps the old conffile with Debian's new one beside it, and `plymouthd.confe` is a `sed -ie` leftover | **b** |
| `apt/sources.list.ps3-snapshot` | the build snapshot line set aside by ps3-system; the image build deletes it | **b** |
| `fstab` | live has the root and swap lines; the USB installer writes them | **b** (installer) |
| `machine-id`, `group-`, `.updated` | per installation / backup / systemd stamp | **b** |
| `ssh/ssh_host_*` (names only) | host keys | **c**: the USB installers copy them over unless `FRESH=1` |
| `bluetooth/input.conf` | modified, same hash both sides (ps3-system sets `ClassicBondedOnly=false`) | as designed |
| `NetworkManager/system-connections/` | empty live: no saved Wi-Fi | nothing |

`dpkg --verify` (PS3, niced, 4.5 min) flagged only `plymouthd.conf` and `bluetooth/input.conf` (both by design) and `/usr/lib/ps3/boot/6.12.111+ps3.163-ps3-release.args` (`plymouth.debug` added for a test boot: **b**, goes with that kernel).

## Elsewhere on the live disk

| What | Action |
|---|---|
| `/.old-20261015-150236`: the previous root, kept by the USB installer, 2.35 GB, 143,715 entries; `/.old-20261015-150353` is empty. A reimage adds another `.old-<date>` | **b**: delete by hand once the new root works (or `FORMAT=1`) |
| `/boot`: 7 old vmlinux/System.map/config sets (`.152`, `.163`, `6.12.112-*`) | **b** |
| `/usr/lib/modules/<old releases>`: `modules.*` files of the old kernels (no package owns them) | **b** |
| `/var/backups`: `ps3-legacy-{system,desktop}-*.tar`, `pre-ps3-system-*.tar.gz`, `dpkg.status.pre-*`, `apt.extended_states.pre-debmesa-*` (the 2026-10-15/16 migration) | **b**, or copy to the PC first |
| `/var/lib/bluetooth/<adapter>/<device>`: the DualShock 3 pairing | **c**: carried over unless `FRESH=1` |
| `/root/.debug` (perf build-id cache), `/root/suite.log` | **b** |

## Home directory (`/home/tibi`, names only)

The image ships only `.bashrc`, `.bash_logout`, `.profile`, `.face` and `.ssh/authorized_keys`. The installers don't carry `/home` over: it stays in the `.old-<date>` directory on the disk.

| What | Action |
|---|---|
| `.local/share/classicube/`, `.openarena/baseoa/` (`q3config.cfg`, `challenges.dat`, demos) | **c**: game data and settings |
| `.config/tint2/tint2rc` (differs from `/etc/xdg/ps3/tint2/tint2rc`), `.config/libfm/libfm.conf`, `.config/pcmanfm/default/`, `.config/dconf/user`, `.config/pnmixer/`, `.config/procps/` | **c** if they're deliberate; a tint2rc change worth keeping belongs in ps3-desktop (**a**) |
| `.config/autostart/blueman.desktop` (`Hidden=true`) | **b**: ps3-session starts no XDG autostart entries |
| `cc-test-backup/default.zip{,.2}`, `suite.log`, `.xsession.log`, `.bash_profile.bak-20261015-150643`, `.local/share/ps3-legacy/` (empty), `.dbus/`, `.fontconfig/`, `.cache/` | **b** |
| `.bash_history`, `.local/share/recently-used.xbel` | **c**/**b**, the user's choice |

## Method

Image side: `dpkg` status, `extended_states`, diversions and `/etc` read from the tar; the file list from `tar -t`. Live side, read-only over SSH: `dpkg-query`, the dpkg status and `*.list` files streamed through gzip, `find` of `/etc`, `/usr`, `/opt`, `/etc/systemd`, sha256 of `/etc` files (secrets left out), `systemctl list-unit-files`, and `dpkg --verify` under `nice -n 19 ionice -c3`. Only the 11 differing small config files were fetched for a diff. "Unowned" means not in any package's file list or a diversion target, with `/lib`, `/bin`, `/sbin` mapped to `/usr`.

Older versions and the dated history: [archive/IMAGE-DIFF.md](archive/IMAGE-DIFF.md).
