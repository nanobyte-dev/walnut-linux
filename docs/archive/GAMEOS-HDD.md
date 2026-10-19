> Superseded on 2026-10-19 by docs/GAMEOS-HDD.md; kept unchanged for its history.

# GameOS's disk from Linux (read-only)

Under OtherOS++ Linux sees more of the PS3's internal disk than the usual OtherOS region
(measured 2026-10-18):

| device | size | `blkid` | GameOS name |
|---|---|---|---|
| `/dev/ps3db` | 957 GB | UFS (UFS2) | `dev_hdd0`: games, game data, saves, user data |
| `/dev/ps3dc` | 2 GB | vfat | `dev_hdd1`: install cache, temporary data |

`ps3-gameos-mount` (package `ps3-system`) mounts them read-only on demand:

```
sudo ps3-gameos-mount        # /mnt/gameos/hdd0 (ufs2) and /mnt/gameos/hdd1 (vfat), ro,noatime,nodev,nosuid,noexec
sudo ps3-gameos-mount -u     # unmounts both
```

It checks `blkid` first and leaves a device alone if its type isn't the expected one, refuses any
read-write request, and is not run at boot.

## Why no key is needed

GameOS encrypts its disk regions (AES, keys in the console's secure storage). Under OtherOS++ the
region is exposed through the hypervisor's storage path, which decrypts transparently: Linux
reads plain UFS2 and FAT, the same way it sees its own regions. Nothing in this project touches a
key.

## Read-only, on purpose

The GameOS partition must never be written from Linux (PS3 safety rules: nothing beyond installing
Linux). Three layers:

- the kernel is built with `CONFIG_UFS_FS=m` and `CONFIG_UFS_FS_WRITE` not set
  (`linux/config/ps3.config`), so the module has no write code at all; Linux's UFS2 write support
  is experimental and deliberately not built;
- the helper mounts with `ro` and refuses to mount otherwise;
- the vfat part is mounted `ro` too (the kernel can write vfat, so that one relies on the mount
  option and the helper).

A read-only mount of a filesystem that GameOS left dirty doesn't replay or repair anything. If
GameOS was powered off hard, files may be inconsistent: copy what you need, don't trust it.

## What's there

Game data and installed games under `game/` and `home/` of `dev_hdd0` (PS3 games, saves in
`home/<user>/savedata`, trophies, `photo`, `video`, `music` the user put there). Saves and game
data are in GameOS's own formats, some encrypted per console; copying them out is possible, using
them from Linux is not part of the project.

## Not verified yet

The mount itself needs a kernel with the `ufs` module booted on the PS3 (6.12.111+ps3.419 or
later, the first such kernel that booted; this said .403 before 2026-10-18 evening). (2026-10-18 evening: `dev_hdd0` did mount read-only on the 6.12.111+ps3.419 boot: an 864 GB
file system, 22 games listed under it. So hdd0 works; only hdd1 is left.) The first try on
6.12.111+ps3.433 (2026-10-18): hdd0 not tried, hdd1 failed because
`mount -o iocharset=utf8` found no `nls_utf8` ("missing codepage or helper program"). Fixed
2026-10-18: the kernel config has `CONFIG_NLS_UTF8=y`, `NLS_CODEPAGE_437=y` and
`NLS_ISO8859_1=y` (vfat's defaults) built in from 6.12.111+ps3.457, and `ps3-gameos-mount` retries
a failed vfat mount without `iocharset` (file names with non-ASCII characters then show as `?`).
Still to check on 6.12.111+ps3.457 (built, for the next boot): `sudo ps3-gameos-mount` mounts both, `ls /mnt/gameos/hdd0` and
`/mnt/gameos/hdd1` list files, `dmesg | tail` shows no UFS/FAT errors, `sudo ps3-gameos-mount -u`
unmounts both.

## A Linux region on the internal SSD

Research notes, 2026-10-18. Nothing here was run on the console. The question: the user wants
50-100 GB of the internal SSD for Linux (root, swap, games), because it measured 100 MB/s
sequential and 0.1 ms median random 4K reads against the USB root disk's 27.8 MB/s and 20 ms.
How to read it: **[fact]** lines carry a source (list at the end of this section), **[inference]**
is my reasoning and needs checking, **[unverified]** came from search snippets or forum posts I
couldn't open (psx-place blocks scripts and its Wayback captures are challenge pages).

### Short answer

- **[fact]** On Evilnat's CFW the documented way is: Recovery Menu, "Restore PS3 System" (formats
  the whole disk and leaves a gap after `dev_hdd1`), then glevand's `create_hdd_region.sh` from the
  Petitboot shell, which turns the gap into a new region that Linux sees as `/dev/ps3dd`.
- **[fact]** The gap Evilnat leaves is fixed: 46137320 sectors, 22 GiB, whatever the drive size
  (setsid measured it on Evilnat 4.93 with a 320 GB HDD and a 960 GB SSD; kostirez1 reported
  22 GB on 4.90). **So stock Evilnat gives 22 GiB, not 50-100 GB.** [inference] 4.92 sits between
  the two and has the same patch.
- **[fact]** There is no lossless shrink. The GameOS region has to be reformatted: everything on
  `dev_hdd0` and `dev_hdd1` goes, plus users and settings. Backup Utility first, restore after.
- More than 22 GiB needs a firmware whose `emer_init.self` reserves more (a self-built PUP, a write
  to the NOR's CoreOS bank). That's outside what the safety rules allow without the owner's
  explicit decision. My recommendation is no-go (below, "Getting more than 22 GiB").

### 1. How regions work on a NOR console

- **[fact]** lv1 splits the internal disk into storage regions and gives each LPAR an access list
  per region. Linux's `ps3stor_probe_access()` reads sector 0 of every region and keeps the ones
  that answer. Sony's OtherOS exposed one region; OtherOS++ (Evilnat's lv1 included) skips the ACL
  check, so every region is reachable (setsid region-handling.md; psdevwiki OtherOS++ features:
  "Ability to deactivate/activate HDD/VFLASH encryption", storage ACL skip).
- **[fact]** The layout under OtherOS++ (psdevwiki Talk:OtherOS++, setsid's boot log):

  | Linux | region | contents | ours today (sectors of 512 B) | ours after Evilnat's Restore [inference] |
  |---|---|---|---|---|
  | `ps3da` | 0 | the whole drive | 1875385008 | same |
  | `ps3db` | 1 | `dev_hdd0`, UFS2 | 1870666384 (957.8 GB) | 1824529056 (934.2 GB) |
  | `ps3dc` | 2 | `dev_hdd1`, FAT, 2047 MiB | 4194296 | same |
  | `ps3dd` | 3 | the OtherOS region | none | 46137320 (22 GiB) |

  setsid's console has the same drive size as ours (1875385008 sectors) and shows region 1 at start
  524320, region 2 right after it, and region 3 at 1829247680 with 46137320 sectors. Our `ps3db`
  is exactly 46137328 sectors bigger than his, which is the 22 GiB gap plus the 8-sector spacer.
- **[fact]** On a NOR console VFLASH (`dev_flash`, `dev_flash2` with xRegistry, `dev_flash3`, the
  OS area with Petitboot and the boot flag) lives on this disk too, starting at sector 8
  (psdevwiki HDD Encryption; FLASH-SYSCON.md 1). It's not one of the HDD regions above; it sits
  before region 1 (sectors 8 to about 524320, 256 MiB).
- **[fact]** Every sector is encrypted with the console's ATA keys (XTS-AES-128 on Slims); VFLASH is
  encrypted twice. lv1 decrypts on the way, so Linux sees plaintext in every region
  (psdevwiki Harddrive, HDD Encryption). A disk moved to another console or a PC is unreadable
  without those keys, the Linux region included.
- **[inference] Why our disk has no gap today.** If Evilnat's Restore always leaves 22 GiB, our
  full-size `ps3db` means the SSD was formatted before Evilnat was installed (OFW or the 4.75 that's
  still in ros1). Nothing in the current layout can be reused.

**How the gap is made.** **[fact]** The GameOS region size is computed in `emer_init.self` (CoreOS,
the Recovery Menu's program) when it formats. PS3MFW's `patch_gameos.tcl` has
`--patch-gameos-hdd-region-size` (eighth, quarter, half, or 10-1000 GB smaller) and psdevwiki's
Talk page lists the replaced instruction: 22 GB smaller is `\x3c\xe9\xfd\x40`, 400 GB is
`\x3c\xe9\xce\x00`, i.e. `addis r7,r9,-N` with N = 0x2C0 and 0x3200. **[inference]** N is
GiB × 32 (units of 65536 sectors): 0x2C0 × 65536 = 46137344 sectors = 22 GiB, matching what
Evilnat leaves, so Evilnat ships the "22 GB smaller" patch. **[fact]** Evilnat's own changelog
(4.91.2 and 4.93): "If you want to install OtherOS on PS3's internal HDD you first need to go to
Recovery Menu and select [Restore PS3 System], this will format the HDD and create a partition for
OtherOS. Remember to make a backup of all your data before."

**How the region is created.** **[fact]** `create_hdd_region.sh` (glevand 2011, GPL-2, mirrored in
dblsaiko/PS3-OtherOS) reads the disk's repository nodes with `ps3hvc_hvcall /dev/ps3hvc
get_repo_node_val` (number of regions, the last region's start and size, the disk's block count),
then calls `ps3stor_region /dev/ps3stormgr create <dev_id> <start> <count> 0x1070000002000001`,
start = last region's end + 8, count = all the rest - 8, access for the OtherOS LPAR's auth ID. It
has no checks of its own: it takes whatever is free after the last region. setsid and kostirez1
ran it from Petitboot's shell on Evilnat consoles; setsid's note: over SSH set
`PATH=/bin:/sbin:/usr/bin:/usr/sbin` first or `ps3hvc_hvcall` isn't found.

What was asked about and doesn't exist: no `ps3-hdd-region` in any ps3-utils I found; xai_plugin
has no HDD size option ("Install OtherOS" resizes VFLASH and installs Petitboot; "Toggle HDD
Space" is the UFS2 reserve, not regions); the XMB Format Utility has no size choice on 4.x (Sony's
manual lists only quick and full format); the Rebug "size" was the same PS3MFW `emer_init` patch
built into their 3.41/3.55 OtherOS++ PUPs, and Rebug REX 4.84.2 just detects an existing region.

**Our tools.** **[fact]** Our Petitboot (`firmware/petitboot/dtbImage.ps3.bin`) is glevand's
2.6.30.10 build of Jun 27 2011 (its version string), with the `ps3stormgr` and `ps3hvc` drivers
and GPT support in the kernel. **[fact]** Its initramfs has `/usr/sbin/ps3stor_region` and
`/usr/sbin/ps3hvc_hvcall` (unpacked 2026-10-18, see "Petitboot's behaviour" below); the user still
confirms them from the shell before anything is wiped (checklist step U3). Our
kernel carries the storage-manager patch (ps3/0150, `/dev/ps3strgmngr`, different ioctls) but
builds it off on purpose (OTHEROS-PATCHES.md: persistent region table); it stays off, Petitboot
is the tool for this one-off.

### 2. What the reformat erases, what stays

Erased (Recovery Menu option 5; **[fact]** psdevwiki Recovery Menu: "formatting and erasing all of
the data on the HD and returning all system settings to default", same as the XMB's Restore PS3
System):

- all of `dev_hdd0`: installed games and game data, DLC, saves (`home/<user>/savedata`),
  trophies (`home/<user>/trophy`), licences (`home/<user>/exdata`: `act.dat` and the `.rif`s;
  `/dev_hdd0/exdata` `.rap`s), photo/music/video, the users themselves, and everything the CFW
  put there: webMAN-MOD (`game/UPDWEBMOD`, `plugins/`), `boot_plugins.txt`, `PS3ISO`, `GAMES`,
  `PS2ISO`, `packages`, `tmp/` (xai's `cfw_settings.log` too);
- `dev_hdd1` (cache, nothing to keep);
- system settings in xRegistry (`dev_flash2`): network, video, users, PSN sign-in, region
  settings. **[fact]** On NOR, xRegistry lives in VFLASH and is regenerated, the first new user
  becomes 00000001 again (psdevwiki Harddrive talk notes).

Kept:

- **[fact]** System software: "System software is not deleted when the system storage is
  formatted" (Sony, Format Utility), so Evilnat 4.92, Cobra and xai_plugin stay (they're in
  `dev_flash`).
- **[inference]** The NOR (both CoreOS banks, bootldr, eEID, the per-console data) and the SYSCON:
  neither a Restore nor a region create writes them. Our NOR dump of 2026-10-13 stays valid since no
  PUP is installed in plan A.
- **Unknown:** VFLASH region 5 (Petitboot, boot flag) and xai's resized regions 6/7 after a Restore.
  Evilnat and kostirez1 both put "Restore PS3 System" before "Install OtherOS", which only says the
  order they used. Plan for either: if Petitboot doesn't come up afterwards, run xai "Install
  OtherOS" again (the step already done on 2026-10-13). If the XMB asks for a system software
  reinstall, the same Evilnat 4.92 PUP from Recovery option 6 is the fix (a same-version reinstall,
  but that's a PUP write: take a fresh NOR dump after it, FLASH-SYSCON.md 4).

### 3. Backup and restore of GameOS's data

Three copies, in this order of importance:

1. **XMB Backup Utility** (Settings > System Settings > Backup Utility) to an external USB drive
   formatted FAT32 with room for 174 GB (a 256 GB or bigger drive; not our Linux USB disk, which is
   ext4). **[fact]** psdevwiki Harddrive (HDD swap procedure): it carries "installed games, savedata,
   DLC/PSN, /Photo, /Music, /Video, bookmarks etc." into `\PS3\EXPORT\BACKUP\<date>\` as split
   `archive*.dat` files. **[fact]** Sony's manual: "You cannot back up your trophy information using
   the backup utility feature" and "Copy-protected data cannot be restored" (the page lists this
   among the restrictions; the HDD-swap guides restore to the same console, which is our case).
   **[unverified]** Whether it carries the CFW's own folders (`PS3ISO`, `GAMES`, `plugins`...): assume
   not.
2. **Licences and keys with xai_plugin** to a USB stick (the 4.93 xai list): "Backup activation file"
   (`act.dat`), "Export Licenses from rap.bin" (RAPs to `/dev_usb/exdata`), "Backup xRegistry.sys",
   and "Dump Flash" (a second NOR dump, compared with the first: FLASH-SYSCON.md 4 gap list). Trophies:
   sync with PSN if the account is signed in; otherwise the raw copy in 3.
3. **A raw read-only copy from Linux** (ours): `sudo ps3-gameos-mount`, then copy
   `/mnt/gameos/hdd0/home`, `/mnt/gameos/hdd0/exdata` and the CFW folders (whatever `du -sh
   /mnt/gameos/hdd0/*` shows) onto the USB root disk if it has the room (check with `df`), or at
   least `home` and `exdata` (small) onto a FAT32 stick. Hash the copy (`sha256sum` list) so it can
   be checked later. This is the fallback for what Backup Utility refuses (trophies, copy-protected
   saves, CFW folders). It goes back in through GameOS, not Linux (we build no UFS write): from a
   FAT32 stick with webMAN-MOD's file manager or multiMAN, or over FTP to webMAN-MOD. **[inference]**
   Raw files put back on the same console under the same user number work, since the per-console
   keys don't change; Evilnat allows unsigned `act.dat`/`.rif` (its changelog) and xai has
   "Convert savedata" for saves from elsewhere.

Restore after the reformat: create the first user (it gets 00000001 again), then Backup
Utility > Restore (brings the users and their data back), reinstall webMAN-MOD from its `.pkg`,
put back the CFW folders, check a few games and saves, re-sign in to PSN if used (purchases may
need the account re-activated: xai "Activate account" or the backed-up `act.dat`).

### 4. Rollback: giving the space back

- **Stop using it:** nothing to do on GameOS's side. GameOS doesn't see region 3 ("Don't mess with
  GameOS HDD region and GameOS won't mess with your OtherOS HDD region", glevand). Wipe it from
  Linux if wanted.
- **Give the 22 GiB to GameOS:** **[inference]** there's no grow for GameOS's UFS2 in place, so it's
  the same dance again: backup, Restore PS3 System. **[fact]** A GameOS reformat removes the Linux
  region ("if you damage your GameOS HDD region, GameOS will reformat HDD and remove your Linux HDD
  region in the process", glevand). **[inference]** But Evilnat's Restore leaves its 22 GiB gap
  again, so a full-size `dev_hdd0` would need a format under a firmware with an unpatched
  `emer_init` (OFW 4.92 from Recovery, then Evilnat again). Not worth it for 2.4% of the disk:
  the realistic rollback is to leave the gap empty.

### 5. Risks

| Risk | What happens | How likely / mitigation |
|---|---|---|
| Restore PS3 System itself | Data loss, which is the point; Petitboot or the OtherOS VFLASH layout may need xai "Install OtherOS" again | Backups 1-3 first; Petitboot reinstall is a documented step |
| `create_hdd_region.sh` on the wrong layout (no gap) | It computes a negative or tiny size; lv1 should refuse, the script won't | Run it only after checking: 3 regions, about 46137344 free sectors (step U7) |
| A region op corrupts the table | GameOS reformats the disk and the Linux region is gone (glevand) | Class 2 in FLASH-SYSCON.md 3: Recovery restores GameOS; Linux comes back from our image |
| GameOS later reformats for any reason (a "file system corrupted" screen answered with format, Format Utility, Restore PS3 System) | Linux region deleted | Never use those without a Linux backup; "Restore File System" and "Rebuild Database" are fine |
| A Linux write to the wrong region: our ps3/0010 exposes `ps3da` (whole disk, VFLASH included) and `ps3db` read-write, one letter from `ps3dd` | `mkfs` on `ps3da` wipes VFLASH (firmware reinstall from Recovery) and the region table; on `ps3db` it destroys GameOS | The kernel guard (local/0045, section "The kernel guard"): `ps3da` read-only, `ps3db` writable only inside the ranges a boot tool registered, `ps3dc` and `ps3dd` writable; installer guards (change 3) |
| Encryption | The SSD's Linux data is readable only in this console | Linux is rebuilt from our image; back up `/home` and the machine's state to the PC |
| The PS3's storage path | One command at a time per disk (one bounce buffer); setsid found a 2021 ps3disk offset bug that corrupts multi-segment I/O | Ours already has that fix (OTHEROS-PATCHES.md, "T2 1000-ps3disk-fix-bvec-memcpy"); our 0010 serialises regions with a busy flag; the hdd0 read-only mount shows it works |
| GameOS's 1-2 TB format issues (psdevwiki Harddrive) | | Not us: 960 GB |
| Cooling | webMAN-MOD is gone after the Restore, GameOS runs the fan on SYSCON automatic | That already meets the rule; after webMAN is back, set its fan mode to SYSCON (or ≥60%) before the OtherOS flag, as before |

Not a risk: re-pairing. **[fact]** A reformat sets the disk to the console's own keys and is the
normal path for any new disk (psdevwiki Harddrive: another console "will just ask to reformat
it"); the Blu-ray pairing is in eEID and the drive, untouched.

### 6. What changes on our side

**[inference]** Region 3 is appended, so `ps3da`, `ps3db`, `ps3dc` keep their names (our 0010
names a disk after its region index, like Petitboot and setsid's patch) and `ps3-gameos-mount`
needs no change. The new disk is `/dev/ps3dd`; check `/sys/block/ps3dd/size` = 46137320 on the
first boot.

1. **Kernel:** the guard (local/0045, section "The kernel guard"), before anything writes `ps3dd`:
   `ps3da` read-only, `ps3db` writable only inside registered ranges (none in this route, so
   read-only), `ps3dc` and `ps3dd` writable, by region index; no override parameter. setsid marks
   every region read-only except the highest one that is neither region 0 nor the whole drive.
2. **ps3-system:** a udev rule for `ps3d*`. **[fact]** systemd's `60-persistent-storage.rules` skips
   `ps3d*`, so no `/dev/disk/by-*` links appear (setsid: the swap unit by label timed out after
   90 s). **[inference]** Our root by `PARTUUID=` is resolved by the kernel (no initramfs), but
   systemd's fsck and remount units for `/` wait for the by-partuuid device unit; and
   `ps3-vswap` finds swap with `findfs`, which works without the links. The rule (blkid builtin plus
   by-uuid, by-label, by-partuuid links) makes both safe.
3. **Installer:** an `ssd-root` twin of `tools/usb-root-remote.sh`, run from the USB-root Linux
   (modern sfdisk/mke2fs; setsid: Petitboot's 2010 `mke2fs` makes ext4 that 6.x rejects, and its
   `parted` gets GiB and % wrong). Guards: target `/dev/ps3dd` only, its size between 8 and 128 GiB,
   region 3, no UFS/vfat signature, refuse `ps3da`/`ps3db`/`ps3dc` by name and by size. Layout:
   GPT with a fixed disk GUID and partition UUIDs (like our `50533301` MBR id), root ext4 18 GiB
   with the Petitboot-safe features we use on USB (`^metadata_csum,^metadata_csum_seed,^64bit,
   ^orphan_file`, so Petitboot's 2.6.30 can read `/etc/kboot.conf`), swap 4 GiB. Then unpack the
   image (or copy the running root with the machine's state, as `make usb-root` does), write fstab,
   set `PS3_ROOT_ARGS="root=PARTUUID=<ps3dd1> rootfstype=ext4 ro"` in `/etc/default/ps3-boot` and
   run `ps3-update-kboot` in the new root.
4. **config.mk / Makefile:** `ROOT_ARGS_ssd`, `make ssd-root`, `netboot-deb ROOT=ssd`. The USB root
   stays as the fallback: Petitboot lists the `kboot.conf` of both disks. The two need different
   entry labels: Petitboot's stored default is keyed on the label ("Petitboot's behaviour" below).
5. **Swap:** fstab's swap entry points at the SSD partition, so `ps3-vswap` backs VRAM swap with it
   instead of the USB partition. The USB swap partition stays unused.
6. **Measurements:** root location changes need a reboot, so SSD vs USB can't be a same-boot A/B;
   compare game loads and stalls across two boots and say so in the result.
7. **Docs:** README, PACKAGING.md, this file's table (`ps3db` becomes 934 GB), FLASH-SYSCON.md row F,
   PLAN 4.12.

**What fits in 22 GiB:** the image tar is 2.1 GB today; setsid's Debian was 875 MB used of 4 GiB.
Root 18 GiB plus 4 GiB swap leaves room for the suite. If the games outgrow it, the USB disk can
stay mounted for bulky game data while root and swap live on the SSD.

### Alternative: a Linux disk image inside dev_hdd0 (proposed 2026-10-18, not tried)

No reformat, no region table change. GameOS writes an ordinary file of 50-100 GB on dev_hdd0 (several
4 GiB files if a tool limits the size); Linux mounts dev_hdd0 read-only, reads the file's block list
(FIBMAP: our 6.12 UFS driver has `ufs_bmap`, fs/ufs/inode.c), and device-mapper (dm-linear) turns
exactly those blocks of `/dev/ps3db` into a disk for ext4. Linux writes only inside the file's data
blocks, never UFS metadata; GameOS sees an unchanged file. The two OSes never run at the same time.
- **Checks:** the uploaded file carries its own offset and a magic in every 4 KiB block; before the
  first write Linux reads every block through the mapping and checks each one. A header block (magic,
  UUID, a hash of the block list) is checked at every boot: if the list changed (file deleted,
  rewritten, cleared by a GameOS fsck), the disk isn't activated. The file is a whole number of UFS
  blocks (no tail fragment) and has no holes.
- **Kernel guard:** `ps3da` read-only; writes to `ps3db` allowed only inside the registered extents
  (a small ps3disk check), so a mistyped mkfs can't reach GameOS's data; `ps3dc` writable for
  Linux's boot files. Built as local/0045, section "The kernel guard" below.
- **Boot:** Petitboot can't see a device-mapper disk (its kernel has no device-mapper at all), so
  the kernel and kboot.conf stay readable to it. Petitboot's 2.6.30 kernel has ext2, ext3, ext4,
  vfat, msdos, iso9660, udf, ntfs and ufs built in, plus loop (strings of
  firmware/petitboot/dtbImage.ps3.bin; earlier versions of this note listed only four). Options:
  (1) `/boot` on the USB disk as today, read only at boot, plus a small initramfs that builds the
  mapping (too many extents for `dm-mod.create=`); (2) `/boot` on dev_hdd1 (vfat, GameOS's cache
  partition, which GameOS may clear: keep (1) as the fallback); (3) the kernel as a file on
  dev_hdd0. The analysis of the installed image (next subsection) rules out (3) as an automatic
  boot: Petitboot mounts every device without `ufstype=`, so UFS2 never mounts. (1) it is.
- **Risk:** deleting the file in GameOS hands its blocks back to GameOS; the boot check stops Linux
  from writing there, but the file must be treated as "do not touch". Rollback: delete the file.

### Petitboot's behaviour (from the installed image)

PC-side analysis, 2026-10-18: the image's initramfs unpacked and read, nothing from it run, nothing
done on the PS3. Tags as above: **[fact]** has a file, string or source line behind it,
**[inference]** is my reading and wants a check from Petitboot's shell.

**Which image.** The installed one is `firmware/petitboot/dtbImage.ps3.bin` (the user,
2026-10-18: Evilnat's instructions expect that version). **[fact]** Kernel "2.6.30.10
(glevand@7track.fastsh.it) ... Mon Jun 27 15:59:15 UTC 2011"; userland "pb-discover (petitboot)
-11.06.27-15.34", BusyBox 1.17.3, kexec-tools 2.0.2, util-linux-ng 2.13.0.1 `mount`, e2fsprogs
1.41.12, uClibc 0.9.32-rc2, all from glevand's OpenWrt tree
(`/home/glevand/petitboot/openwrt/build_dir/linux-ps3_petitboot`). The initramfs is an LZMA stream
with OpenWrt's settings (properties byte 0x6d) at offset 0x428000, 15.25 MB of newc cpio. The
kernel has no embedded config (no `IKCFG_ST`), so kernel options below come from its strings.
Petitboot's own source is upstream petitboot at 17d63c9 (2009-08-26, the last commit before
October 2011 in github.com/open-power/petitboot); the binaries' strings match that tree (config
file lists, ignored keywords, format strings), so the file names below refer to it.

**The other build.** `~/Downloads/Petitboot FULL (NOR).zip` has the Jun 4 2011 build
(glevand@bastion; the same file as `firmware/petitboot/dtbImage.ps3.bin.archive-org-0f161587`,
sha256 fdf625a3...). **[fact]** Same size and layout. The kernel differs only in build host, date
and link order. The initramfs has the same files plus, in the Jun 27 one, three symlinks
(`/etc/fstab`, `/etc/resolv.conf`, `/etc/TZ` into `/tmp`), a newer debootstrap (`force_md5`,
updated suite scripts) and a rebuilt libcrypto; pb-discover, pb-cui, kexec and busybox are rebuilds
of the same sources (stamps 11.05.29 and 11.06.27). Nothing in this subsection differs between them.

**1. Which devices it scans.**
- **[fact]** `/etc/udev/rules.d/99-petitboot.rules` is `SUBSYSTEM=="block",
  RUN+="socket:/tmp/petitboot.udev"`, and pb-discover starts with
  `udevadm trigger --subsystem-match=block` (discover/udev.c). There is no name filter: every block
  device, whole disks and partitions, gets a mount attempt. That covers `ps3da`-`ps3dc` (and
  `ps3dd` once a region exists), `sd*`, `sr*`, and the VFLASH regions (`ps3vflash*`).
- **[fact]** The kernel's ps3disk is a multi-region one: it prints "%s:%u: %lu accessible regions
  found" and "%s is a %s (%llu MiB total, %lu MiB region)", where mainline 2.6.30 has "...
  accessible regions found. Only the first one will be used" and "... MiB for OtherOS"
  (drivers/ps3/ps3stor_lib.c, drivers/block/ps3disk.c at v2.6.30). It also has ps3vflash,
  ps3nflash, ps3rom, ps3stormgr and ps3hvc. **[inference]** Whether regions 1 and 2 answer is lv1's
  decision (the OtherOS++ ACL skip): the kernel just reads sector 0 of each. Same lv1 as our 6.12,
  which sees `ps3db` and `ps3dc`, so Petitboot should list them too (U3 checks).

**2. How it mounts.**
- **[fact]** `mount_device()` in discover/device-handler.c runs
  `/bin/mount <DEVNAME> /var/petitboot/mnt/<name> -o ro`: no `-t`, no other option. On failure it
  removes the directory and forgets the device. `/bin/mount` is util-linux-ng's, typing the device
  with libblkid (which knows ufs, vfat, ext2/3/4, iso9660, udf, ntfs, swap and a few more).
- **[fact]** File systems in the kernel: ext2, ext3, ext4, vfat/msdos, iso9660, udf, ntfs (driver
  2.1.29, R/W), ufs; the loop driver; no device-mapper (no `dm_*` symbols).
- **UFS2 (`dev_hdd0`).** **[fact]** 2.6.30's fs/ufs/super.c: without `ufstype=` it prints "You
  didn't specify the type of your ufs filesystem ... default is ufstype=old" and reads the
  superblock at 8 KiB; UFS2's is at 64 KiB (`SBLOCK_UFS2`), read only with `ufstype=ufs2`, which
  2.6.30 has (both byte orders). **[inference]** pb-discover's mount of `ps3db` fails with
  "ufs_read_super: bad magic number", so dev_hdd0 is never in the menu and a kboot.conf on it is
  never read. Check: `dmesg | grep -i ufs` and `grep petitboot /proc/mounts` in the shell.
- **UFS write.** **[fact]** The image lacks "ufs was compiled with read-only support", which
  2.6.30 prints for a read-write mount when `CONFIG_UFS_FS_WRITE` is off, and 2.6.30 forces only
  ufstype old, hp, nextstep and openstep to read-only (those "supported read-only" strings are
  there); UFS2 isn't forced. **[inference]** `CONFIG_UFS_FS_WRITE=y`: in Petitboot's shell,
  `mount -t ufs -o ufstype=ufs2 /dev/ps3db /mnt` without `ro` would mount dev_hdd0 read-write.
  Always `-o ro,ufstype=ufs2` there.
- **vfat (`dev_hdd1`).** **[inference]** blkid says vfat, pb-discover mounts it read-only at
  `/var/petitboot/mnt/ps3dc` and reads a kboot.conf on it. Not verified that 2.6.30's vfat takes
  GameOS's FAT (our 6.12 mount of hdd1 isn't verified either, see "Not verified yet"). Check:
  `ls /var/petitboot/mnt/ps3dc`.
- **ext3/ext4 journals.** **[fact]** A read-only mount still replays a journal that needs recovery:
  "EXT4-fs: INFO: recovery required on readonly filesystem." / "write access will be enabled
  during recovery." (the same for EXT3), plus "orphan cleanup on readonly fs". After an unclean
  Linux shutdown, Petitboot's 2011 ext4 code writes to our USB root before we boot. That is the
  case today already.

**3. Config files and kboot.conf syntax.**
- **[fact]** Per mounted device, the kboot parser (discover/kboot-parser.c) reads the first of
  `/kboot.conf`, `/kboot.cnf`, `/etc/kboot.conf`, `/etc/kboot.cnf` and the upper-case
  `/KBOOT.CONF`, `/KBOOT.CNF`, `/ETC/KBOOT.CONF`, `/ETC/KBOOT.CNF` that opens. **`/boot/kboot.conf`
  is not on the list**, so a separate /boot partition needs `/kboot.conf` at its root. The yaboot
  parser reads `/yaboot.conf`, `/yaboot.cnf`, `/etc/yaboot.conf`, `/etc/yaboot.cnf`,
  `/suseboot/yaboot.cnf` (and upper case). No grub parser; `native-parser.c` is in the tree but not
  built (rules.mk: `parsers = event kboot yaboot`).
- **[fact]** Syntax (discover/parser-conf.c): one `label=image args` per line, `#` comments, the
  first `=` ends the label, surrounding quotes are stripped. Globals: `initrd=`, `root=`, `video=`
  (the last is stored and never used). `default`, `delay`, `message` and `timeout` are ignored. In an
  entry, `initrd=` and `root=` are taken out; the kernel gets `root=<value>` first, then the other
  words as written.
- **[fact]** Paths (`resolve_path()` in discover/paths.c): no `:` means relative to the root of the
  device that holds the kboot.conf. `dev:path` names another device: `ps3dc:/vmlinux` becomes
  `/var/petitboot/mnt/ps3dc/vmlinux`; `uuid=X:/p` and `label=X:/p` go through
  `/var/petitboot/mnt/disk/by-uuid/X` and `by-label`, links pb-discover makes from udev's
  `ID_FS_UUID` and `ID_FS_LABEL`; `scheme://` URLs are fetched (wget for http; the image has no tftp
  client). It never mounts anything for a path: the other device must be one pb-discover mounted.
- **[fact]** The udev rule that sets `ID_FS_*` (lib/udev/rules.d/60-persistent-storage.rules) runs
  `/sbin/blkid -o udev -p`; the image only has e2fsprogs' `/usr/sbin/blkid`, which has neither
  option. **[inference]** `ID_FS_UUID`/`ID_FS_LABEL` stay empty, so `uuid=` and `label=` references
  don't resolve; only kernel names (`ps3dc:`, `sda1:`) work, and `sd*` names follow plug order.
- **Default and timeout.** **[fact]** Neither comes from kboot.conf. pb-cui keeps them in the OS
  area's database on `/dev/ps3flash` (the `ps3vflashf` link from 70-persistent-ps3flash.rules),
  owner "petitboot" (ui/common/ps3.c): key 1 the default item, a hash of device name plus entry
  label (`pb_opt_hash()`), key 2 the video mode, key 4 the timeout (read-only for pb-cui, set with
  `ps3-bl-option -O`, default forever). The countdown starts only when an entry with the stored
  hash shows up. Booting any other entry stores its hash as the new default
  (`ps3_kexec_cb()`). **[inference]** The kboot parser never sets the device name, so in practice
  the hash is the label alone: the same label on two disks matches on both. **Correction:** the
  `default=` line `ps3-update-kboot` writes is ignored by this Petitboot; what boots after the
  countdown is the entry last picked by hand, if its label still exists. PACKAGING.md and PLAN.md
  still say kboot.conf sets the default (not changed here).

**4. Tools in the shell, and what writes by itself.**
- **[fact]** BusyBox applets: ash/sh, the usual file and text tools, dd, df, du, find, grep, sed,
  awk, vi, ps, top, kill, insmod/lsmod/rmmod, ifconfig, route, udhcpc, ping, telnet, telnetd,
  nslookup, switch_root, pivot_root, reboot, poweroff, halt, dmesg, hexdump, strings. Not in
  BusyBox but present: util-linux `mount`/`umount` (with `-o loop`), `kexec`, `blkid`, `fdisk`,
  `parted`, `partprobe`, `e2fsck`, `mke2fs`/`mkfs.ext2/3/4`, `tune2fs`, `resize2fs`, `badblocks`,
  `dosfsck`, `mkdosfs`, FreeBSD's UFS tools (`mkfs.ufs`, `fsck.ufs`, `tunefs.ufs`, `growfs.ufs`,
  `fsdb.ufs`, `dumpfs.ufs`, `ffsinfo`, `badsect.ufs`), `debootstrap`, `ps3stor_region`,
  `ps3hvc_hvcall`, `ps3-flash-util`, `ps3-bl-option`, `ps3-boot-game-os`, `ps3-dump-bootloader`,
  `ps3-video-mode`, dropbear (ssh, scp), wget, git, vim, nano, screen, tcpdump. Not present:
  `dmsetup`, `losetup`, LVM, mdadm, a tftp client.
- **[fact]** Writes it makes without being asked: (1) pb-cui's database write to the OS area when
  an entry other than the stored default is booted or a value changed, and at exit when the
  database couldn't be read; it formats the database when it can't read one ("formating db");
  (2) the ext3/ext4 journal replay above. Nothing else: every mount is `-o ro`, no fsck runs at
  boot (lib/functions/fsck/e2fsck.sh is OpenWrt glue that nothing in the image calls; there is no
  fstab init script and no hotplug2), `/etc/rc.local` is empty.

**5. What a kernel booted by it receives.**
- **[fact]** pb-cui runs `kexec -l --initrd=<initrd> --append=<args> <image>`
  (ui/common/ui-system.c), so the initrd is passed. `<args>` is `root=<root> <rest>`, plus
  ` video=ps3fb:mode:<N>` when it has no `video=` (N is the video mode in the database;
  `ps3_kexec_cb()`). Then it runs `/sbin/shutdown -r now`, which the image doesn't have, so it falls
  back to `kexec -e`: the init stop scripts never run and the read-only mounts are simply dropped.
- **[fact]** kexec-tools 2.0.2 builds the new device tree from Petitboot's `/proc/device-tree`
  (lv1's PS3 tree), with the command line in `/chosen/bootargs` and the initrd in
  `linux,initrd-start`/`linux,initrd-end`. Petitboot's own dtb (at 0x946000 in the image) has no
  `bootargs`.

**Where /boot goes, given all that.**

| option | works with this Petitboot | risks |
|---|---|---|
| (a) USB disk, as today | **[fact]** yes, it's what boots now | the USB disk stays plugged in; journal replay after a crash writes to it (our disk, harmless) |
| (b) dev_hdd1 (vfat) | **[inference]** likely: mounted ro, `/kboot.conf` or `/etc/kboot.conf` found, paths relative to it | kernel updates write GameOS's FAT from Linux (against "never written from Linux" unless the owner decides, or copied in from GameOS by hand); GameOS treats it as a cache and may clear it **[unverified]**; 2.6.30 vfat on GameOS's FAT not checked |
| (c) a file on dev_hdd0 (UFS2) | **[inference]** no automatic boot: the mount without `ufstype=ufs2` fails | only by hand from the shell every boot (`mount -o ro,ufstype=ufs2`, then `kexec`); forgetting `ro` mounts dev_hdd0 read-write; changing Petitboot to do it means rewriting the OS area (flash), out |
| (d1) a small /boot partition on the USB disk (ext2 or vfat, no journal) | **[inference]** yes, needs `/kboot.conf` at its root and paths relative to that partition | none new; removes the journal-replay write |
| (d2) `http://` paths in kboot.conf | **[fact]** wget is there | boot depends on tibi-server and the network; no |
| (d3) ps3dd after the reformat | **[inference]** yes, like the USB disk | the whole reformat route above |

**Recommendation:** (a). Keep kernel, initramfs and kboot.conf on the USB disk; the initramfs
builds the dm-linear disk on `ps3db` and mounts root from it. (d1) is a small later improvement.
(b) buys nothing over (a) and makes Linux write a GameOS region; (c) can't boot by itself. Two
consequences for the image plan: the entry labels should be unique per root (the default is keyed
on the label), and the initramfs has to carry everything (Petitboot can't help with dm).

Checks for the user at Petitboot's shell, all read-only: `grep ps3d /proc/partitions`,
`grep petitboot /proc/mounts`, `ls /var/petitboot/mnt/ps3dc`, `dmesg | grep -i -e ufs -e fat`,
`ps3-bl-option -o` and `-p` (timeout and stored default).

### /boot on dev_hdd1 kept alive by webMAN (proposed 2026-10-18, not tried)

GameOS gets control before every Linux session (the boot flag is set from GameOS and every boot goes
through it), so a GameOS-side script can put the boot files back on dev_hdd1 even if GameOS clears that
cache partition. No custom plugin needed: webMAN MOD runs `/dev_hdd0/autoexec.bat` (or
`/dev_hdd0/boot_init.txt`) at startup and `/dev_hdd0/onxmb.bat` when the XMB is up (its README;
`include/feat/script.h`: `if exist`/`if not exist`, `md`, `copy <src>=<dst>`, `del`, `wait`). Sketch:
- Master copy on dev_hdd0 (`/dev_hdd0/linux/boot/`: kboot.conf, kernel, initramfs), plain files
  GameOS can read and write; Linux never writes UFS.
- Linux writes new kernels to dev_hdd1 (vfat, `/linux/` plus `/kboot.conf` at its root, which is
  where Petitboot looks) and drops a flag file; at the next GameOS start the script copies dev_hdd1 to
  the master and deletes the flag. If dev_hdd1's files are missing, the script copies the master back.
- As drafted (the user, 2026-10-18; ps3-boot/webman/autoexec.bat): Linux installs every kernel and
  its initramfs to `/dev_hdd1/linux/boot/` and rewrites `/dev_hdd1/kboot.conf`; at every GameOS start
  the script mirrors dev_hdd1 to `/dev_hdd0/linux/boot/` when kboot.conf is there, and copies it back
  when it isn't. No rescue kernel: the backup is at most one Linux session old, and a GameOS start
  always comes between a Linux session and any game that could clear dev_hdd1. To check on the
  user's webMAN: that `copy` takes a directory, and how long the mirror takes.
- Petitboot finds `ps3dc:/kboot.conf`; entries name `ps3dc:/linux/vmlinux-...`. The USB disk's
  kboot.conf stays as the fallback, with different labels.
- What clears dev_hdd1 (researched 2026-10-18): games get it through the system cache API
  (`cellSysCacheMount`), which mounts the game's view of `/dev_hdd1` onto its own directory
  `/dev_hdd1/caches/<TITLEID>_<cache name>/` and clears the previous game's directory when a game with
  a different cache id mounts (RPCS3's reimplementation, rpcs3/Emu/Cell/Modules/cellSysCache.cpp; an
  RPCS3 admin: "on a real PS3 that path is mounted as an empty path whenever you boot a game",
  forums.rpcs3.net thread-200213). Nothing in that model touches files outside `caches/`, so a
  `/kboot.conf` and `/linux/` at the root should survive [inference: RPCS3 models observed
  behaviour, not Sony's code]. Not covered: the Recovery Menu's Restore/format and a dev_hdd1 GameOS
  finds corrupt, which recreate it; the webMAN restore covers both. psdevwiki's Harddrive page only
  calls dev_hdd1 the "system cache"; "Files on the PS3" adds "system cache: 2GB fixed size FAT32"
  and lists path mounts (`CELL_FS_PATH:`, e.g. a directory mounted over `/dev_bdvd/PS3_GAME`), the
  mechanism that lets a game's `/dev_hdd1` be a directory. Neither page says when it is cleared.
- Open: whether GameOS ever clears dev_hdd1's root in practice (a file left there through a few GameOS sessions and
  games tells), whether Petitboot sees ps3dc (its shell's `/proc/partitions`), and the user's webMAN
  version running autoexec.bat. The kernel guard (local/0045) leaves ps3dc writable for this (all of
  it: a block driver can't tell Linux's files from GameOS's) and keeps ps3da read-only.

### The GameOS installer (design, the user, 2026-10-18; nothing built)

A GameOS app (PKG, PSL1GHT) so users never need a PC shell. Flow:
- **Welcome:** install, install a new instance (several Linux installs side by side, each its own
  image file and boot entries), check installation (prerequisites plus the boot files on dev_hdd1 and
  the dev_hdd0 backup, with a repair).
- **Prerequisites:** CFW with OtherOS++ (Petitboot installed), dev_hdd1 present, webMAN MOD (for the
  autoexec backup), free space; the fan reminder before any reboot into Linux (CLAUDE.md).
- **1. The image from a USB stick.** FAT32 limits files to 4 GiB, so the root file system ships as
  chunks (compressed, zstd, to cut the USB 2.0 read time); the installer writes them straight into
  the target, never as a second copy.
- **2. Where:** a USB disk, or a file on the internal disk.
  - *USB disk:* warnings, then the whole disk is erased and partitioned. Open: whether a GameOS app
    can write a USB disk raw (lv2 storage syscalls); if not, the PC route (write the `.img.xz` with
    any image writer) stays, and the USB disk boots through Petitboot as today.
  - *Internal file:* ask the size, offer at most ~90% of dev_hdd0's free space.
- **3. Install.** The rest of the file must be really written (a hole has no blocks to map). Instead of
  zeros, each 4 KiB block gets a 16-byte stamp (magic, instance UUID, its offset), same cost, and it's
  what lets Linux verify the mapping before its first write. Then the boot files go to
  `/dev_hdd1/linux/<instance>/`, `/dev_hdd1/kboot.conf` gets the instance's entries, and the
  autoexec lines are added to webMAN's `autoexec.bat`.
- **4. First boot:** the initramfs checks the image header and the stamps, ext4 grows to the file
  (`resize2fs`), a swap file is made (behind zram and the VRAM swap).
- **5. Finish:** "reboot into Linux" = what xai_plugin's OtherOS boot does (boot flag for one boot;
  reboots land in GameOS again); call xai_plugin's or webMAN's command if one can be triggered from an
  app, otherwise show the steps.
- **Copies of the boot files:** the dev_hdd0 mirror (autoexec, automatic) and a boot bundle inside the
  image's own header area (plain bytes at a known offset, readable by the installer without ext4;
  Linux keeps it current through the mapping), the source for "check installation"'s repair.
- **Several instances:** each instance writes only `/dev_hdd1/linux/<instance>/` and its fragment of
  kboot.conf; `/kboot.conf` is rebuilt from all fragments; labels carry the instance name (Petitboot's
  default is the last label booted).
- **Toolchain:** PSL1GHT has its own PPU GCC, an exception to "one GCC everywhere" that the user
  decides (it builds only the GameOS app).

### Test drive, Phase A (built 2026-10-18; on the PS3 since 2026-10-19)

A first, reversible use of the "disk image inside dev_hdd0" route above: a 16 GiB stamped file uploaded
to dev_hdd0 from the PC over webMAN MOD's FTP, mapped and verified from Linux, then used as an ext4
disk for game data and as the VRAM swap's spill, with root still on the USB disk. The point (the user,
2026-10-18): run the benchmarks from the internal SSD and see how much faster loads get and how many
stutters go. Nothing is written into GameOS's file system: Linux writes only inside the file's own
blocks, and only after every block was read back with its stamp.

**Tools** (`tools/ssdimg/`):
- `stamp.py` (PC): streams the image to `/dev_hdd0/linux/ssdtest.img` with FTP `STOR` from a
  generator (nothing on the PC's disk), prints MB/s, checks the remote size with `SIZE` at the end;
  `dry-run` and `check` for tests. Refuses to overwrite an existing file without `--force`.
- `ps3-imgmap` (the PS3, root): `map`, `verify`, `recheck`, `create`, `remove` (its `--help` has
  the details). Everything up to `create` only reads `/dev/ps3db`; the tool never opens `ps3db` or
  `ps3da` for writing.
- `test_ssdimg.py` (`make test`, suite `ssdimg`) and `vm-e2e.sh` (`make test-slow`, suite `ssdimg-vm`).

**Stamp format** (version 1, stamp.py's docstring): one stamp at the start of every 4 KiB block, the
rest of the block zero: 8 bytes magic `PS3IMGv1`, 16 bytes instance UUID, the block's offset in the
image as 8 bytes big-endian, a CRC-32 of those 32 bytes as 4 bytes big-endian. The first MiB is the
image's header area: stamped too for now, never mapped into the Linux disk, so its stamps stay and
`create` reads them back through the extents at every activation (proof that the list still points at
our file). Phase B puts the real header there.

**FIBMAP's unit** (from build/src/linux/linux-6.12.111): `ufs_fill_super` ends with
`sb_set_blocksize(sb, uspi->s_fsize)`, so the super block's block size (FIGETBSZ, statvfs `f_bsize`) is
the fragment size; FIBMAP calls `ufs_bmap` = `generic_block_bmap(mapping, block, ufs_getfrag_block)`,
whose argument is a logical fragment number (`fragment >> s_fpbshift` is the block, `fragment &
s_fpbmask` the fragment in it) and whose result is the block's address from the inode or indirect block
plus that fragment, plus `s_sbbase` (0 for UFS2): a physical fragment number. So input and output are in
units of fs_fsize, and a sector of `/dev/ps3db` is fragment x fs_fsize / 512 (UFS starts at sector 0 of
the region). `map` checks it from three sides before it trusts it: FIGETBSZ = statvfs = the super
block's fs_fsize (read from 64 KiB of ps3db), each block's first and last fragment are contiguous, and
the file is whole blocks without holes. FIBMAP's int return limits it to 2^31 fragments: dev_hdd0 has
1.87 G sectors, fine for any fragment size.

**Tested on the PC** (2026-10-18): unit tests of the format, the stream, extent merging, the unit
conversion, every refusal (hole, tail fragment, read-write mount, no `ufstype=ufs2`, another file
system, another device, the unit cross-check, an unstamped file, an edited map, create without verify,
without a recheck in this boot, with ps3db mounted, a stale map whose header area no longer reads back),
on a fake FIBMAP layer with a file standing in for the disk (real O_DIRECT reads). Then the real kernel:
`vm-e2e.sh` makes big-endian UFS2 images with NetBSD's makefs (Debian's source rebuilt with the super
block at 64 KiB: as packaged it puts UFS2's at 8 KiB, `#if 0 /* XXX makefs is used for small
filesystems. */` in ffs/mkfs.c, where Linux doesn't look), boots the PC's own kernel (7.2.10, x86_64)
in a KVM guest and runs map, verify, recheck, create, mkfs.ext4, writes on both mapped devices, then
compares every sector outside the file's mapped blocks with the original image (identical: UFS
metadata, the other files, the header area), mounts the UFS again (other files unchanged) and rechecks
(unchanged). Both geometries passed (block 16384 / fragment 2048 and 32768 / fragment 4096). Not tested:
our 6.12 ppc64 kernel's ufs (same `ufs_bmap` code path; the PC reads the big-endian image through the
same fs32/fs64 byte swapping), GameOS's real geometry (map prints it), the PS3's storage path under
O_DIRECT and device-mapper, webMAN's FTP with a 16 GiB file.

**A kernel fact the tools build on** (seen in the guest): device-mapper claims its disks exclusively,
and so does a mounted file system, so `dmsetup create` over a mounted `/dev/ps3db` fails with EBUSY.
`create` therefore wants dev_hdd0 unmounted, and while `/dev/mapper/ps3ssd` exists
`ps3-gameos-mount` can't mount dev_hdd0 (EBUSY too): `ps3-imgmap remove ps3ssd` first. Phase B's
initramfs does the same: mount read-only, recheck, unmount, create.

**Safety points.**
- Nothing writes before `verify` passed; `create` refuses without a recorded pass of that exact list
  (sha256), without a `recheck` in the same boot, with dev_hdd0 mounted, or when the header area's
  stamps don't read back through the list. A failed verify or a changed recheck voids the map.
- `create` runs `blockdev --setro /dev/ps3da` first (the whole disk, VFLASH included, read-only
  until the next boot) and checks it took.
- `create` and everything after it only with the user's explicit go; it asks unless `--yes`.
- Before that go: the XMB Backup Utility (saves at least) to a FAT32 USB drive, section 3 above. The
  mapping writes only inside the file's blocks, the backup is for a bug in that.
- In GameOS the file is "do not touch": deleting or rewriting it hands its blocks back to GameOS;
  `recheck` catches that before any write (a different inode, size or block list), and the header
  check catches a list that went stale anyway. Rollback: delete the file in GameOS (webMAN's file
  manager or FTP). Never answer a GameOS "restore the file system" prompt without a recheck after it.
- The state (`/var/lib/ps3-imgmap/`: the map, the verify and recheck records) is on the USB root in
  Phase A; Phase B puts the list's hash into the image's header.

#### The GameOS session (the user)

0. Before it (us, Linux, after the running batch, the user not at the console): `sudo apt install
   dmsetup` (not in our image); ps3-system with the new `ps3-vswap` deployed (`make ps3-system-deb`,
   `make deploy`) and `echo PS3_VSWAP_DM=1 | sudo tee /etc/default/ps3-vswap` (the VRAM swap's backing
   becomes the device-mapper device `ps3vswap-back` over the USB swap partition's first GiB at the next
   boot: what lets the swap arm switch in the same boot; without it everything is as before);
   `modinfo -k 6.12.111+ps3.433 dm-mod` and `modinfo -k 6.12.111+ps3.433 ufs` (both modules there for
   the kernel that will boot; `BLK_DEV_DM=m`, `UFS_FS=m`); `make bench-push`.
1. GameOS: Backup Utility (above). In webMAN MOD's setup page (`http://10.0.0.77/setup.ps3`) the FTP
   server must not be disabled. 16 GiB plus a margin free on the internal disk (the XMB's System
   Information, or webMAN's file manager on `/dev_hdd0`).
2. On the PC:

       python3 tools/ssdimg/stamp.py upload --size 16G
       # defaults: --path /dev_hdd0/linux/ssdtest.img --host 10.0.0.77 --port 21 (any login)

   It prints the UUID, progress and MB/s, and ends with `remote size 17179869184: complete`. The time
   depends on webMAN's FTP write speed, which nobody measured here: about 14 min at 20 MB/s. A broken
   upload leaves a short file: run it again with `--force`.
3. As always before Linux: webMAN's fan in SYSCON mode (or fixed at 60% or more), the OtherOS flag,
   Petitboot, the usual USB-root entry.

#### On Linux (us, with the user's go where it says so)

    scp tools/ssdimg/ps3-imgmap tibi@10.0.0.77:/var/tmp/ps3-imgmap       # from the PC
    cat /sys/block/ps3vswap0/backing          # /dev/mapper/ps3vswap-back (step 0 worked)
    sudo ps3-gameos-mount
    ls -l /mnt/gameos/hdd0/linux/ssdtest.img  # 17179869184 bytes
    sudo python3 /var/tmp/ps3-imgmap map /mnt/gameos/hdd0/linux/ssdtest.img
        # prints the UFS2 block and fragment size, the extents; map in /var/lib/ps3-imgmap/ssdtest.map.json
    M=/var/lib/ps3-imgmap/ssdtest.map.json
    sudo python3 /var/tmp/ps3-imgmap verify $M          # every 4 KiB block; ~3 min at 100 MB/s
    sudo python3 /var/tmp/ps3-imgmap recheck $M
    sudo ps3-gameos-mount -u

Only with the user's explicit go:

    sudo python3 /var/tmp/ps3-imgmap create $M ps3ssd --swap-mib 1024
        # ps3da read-only; /dev/mapper/ps3ssd (15 GiB) and /dev/mapper/ps3ssd-swap (1 GiB)
    sudo dd if=/dev/mapper/ps3ssd of=/dev/null bs=1M count=1024 iflag=direct    # ~100 MB/s expected
    sudo mkfs.ext4 -L ps3ssd /dev/mapper/ps3ssd
    sudo mkdir -p /mnt/ps3ssd && sudo mount -o noatime /dev/mapper/ps3ssd /mnt/ps3ssd
    sudo mkdir -p /mnt/ps3ssd/games
    for g in "sauerbraten base" "openarena gl2min" "luanti base" "supertuxkart lean" "classicube base"; do
        /var/tmp/bench/ssd-arm.sh copy $g; done      # cp -a, then diff -rq against the original
    dpkg -L luanti-game-minetest | head -3     # under /usr/share/luanti? else add its dir to DATA_DIRS

Every later boot (the map is reused; GameOS may have run in between): `sudo ps3-gameos-mount`,
`recheck $M`, `sudo ps3-gameos-mount -u`, `create $M ps3ssd --swap-mib 1024 --yes`, mount. At the
end of a session: `ssd-arm.sh reset` (run.sh does it when it ends), `sudo umount /mnt/ps3ssd`, `sudo
python3 /var/tmp/ps3-imgmap remove ps3ssd`. On a kernel with the write guard (local/0045, section "The
kernel guard") create also registers and seals the two devices' ranges of ps3db, once per boot: a
`remove` and a second `create` in the same boot is refused (sealed earlier in this boot), the way
back is a reboot. Before create every write to ps3db fails, after it every write outside the image's
blocks.

#### The A/B in the bench (same boot)

Two arms, switched between runs by `games/bench/ssd-arm.sh` (run.sh calls it before each run's quiet)
and recorded by client.sh in bench.txt (`gamedata=`, `gamedata_dirs=`, `swap=`, `swaps=`; report.py:
`base@gdssd`, `base@gdssd+swssd`):
- `GAMEDATA=ssd`: the recipe's `DATA_DIRS` (Sauerbraten `/usr/share/games/cube2`, OpenArena
  `/usr/share/games/openarena`, Luanti `/usr/share/luanti`, SuperTuxKart
  `/usr/share/games/supertuxkart`, ClassiCube `~/.local/share/classicube/{texpacks,audio}`) are
  bind-mounted from `/mnt/ps3ssd/games/<dir>` for the run and unmounted for runs without it; the quiet
  drops the page cache after the bind as before every run. The binary, the libraries, Mesa's shader
  cache (`~/.cache`), Luanti's world (`/var/tmp`) stay on the USB disk.
- `SWAP=ssd`: the VRAM swap's spill goes to the SSD. Today the USB swap partition is not a swap of its
  own: it is the VRAM swap's backing (`ps3-vswap`: `ps3vswap0` at priority 300, zram at 100), written
  when VRAM is wanted back and read on swap-in, and the backing is set once per boot. So the arm swaps
  the table under the device-mapper backing `ps3vswap-back` (step 0): VRAM swap off (pages back in),
  `dmsetup suspend/load/resume` onto `/dev/mapper/ps3ssd-swap`, `mkswap`, on again at its priority.
  zram and the VRAM swap keep their places; a third, lower-priority SSD swap would only be used once
  both are full, i.e. it would measure nothing. A switch stops the desktop and empties the swaps first
  (`ps3-bench.sh quiet`). client.sh refuses a run whose arm isn't in place, and a plain run with binds
  or the SSD swap left over.

The batch (PS3, as tibi, the SSD mounted and the copies made; `tools/bench-cam.sh` as for every bench
run; 5 games x 3 arms x 3 rounds, the arms in this order inside each round, 45 runs, about 3-4 hours):

    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench supertuxkart lean 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench classicube base 3
    ps3-bench.sh desktop
    python3 /var/tmp/bench/report.py --load --stalls /var/tmp/ps3-bench/<run dir>...

What to compare (docs/PROFILING.md "Start-up profiling", docs/STALLS.md): `load`, `load_disk_s`,
loadprof's split of the load; `stalls_warm`, `warm_disk_s`; in the window `stall_disk_s`,
`stall_swap_s`, 1% lows and stalls first (smoothness over fps).

**Expected, from batch 16** (2026-10-18, kernel 433, USB disk, same-boot medians of 3, recorder on;
`build/tmp/batch16-runs.txt`). The USB disk measured 27.8 MB/s sequential and 20 ms median random 4K
reads, the internal SSD 100 MB/s and 0.1 ms, so its wait on the same reads is 3.6x shorter when they
are sequential and up to 200x when they are seeks. The load's disk wait then shrinks by roughly 72% to
nearly all of it, less what stays on USB (binaries, libraries, shader cache):

| game | load (s) | load_disk_s | warm_disk_s | in the window | load with GAMEDATA=ssd [estimate] |
|---|---|---|---|---|---|
| Sauerbraten base | 16.8 | 5.2 | 3.5 | stall_disk_s 0.1 | 11.7-13.1 (-22% to -30%) |
| OpenArena gl2min | 12.0 | 6.2 | 6.4 | stall_disk_s 6.8, stall_swap_s 20.1 | 5.9-7.5 (-37% to -51%) |
| Luanti base | 40.8 | 9.8 | 0.7 | stall_disk_s 0.5 | 31.1-33.7 (-17% to -24%) |
| SuperTuxKart lean | 53.6 | 10.9 | 5.7 | stall_disk_s 0.4 | 42.8-45.8 (-15% to -20%) |
| ClassiCube base | 14.2 | - | - | no recorder in batch 16 | small: its data is one texture pack and the sounds |

So loads become faster but stay mostly CPU-bound: what isn't disk wait today (5.8 s of OpenArena's
load, 11.6 s of Sauerbraten's, 31 s of Luanti's, 42.7 s of SuperTuxKart's) stays.
The warm-up's disk stalls (3.5-6.4 s in three games) should mostly go. `SWAP=ssd` matters where the
window swaps: OpenArena gl2min's 20.1 s of swap stalls, as far as they are spills read back from the
USB disk rather than zram or VRAM hits (the recorder's swap class counts both swaps). The batch will
say how much of each estimate holds.

**Result, batch ssd1** (2026-10-18 22:10-23:25Z, kernel 6.12.111+ps3.472, USB root, same boot, `STALLS=1 ROTATE=1`,
arms rotated each round; row "batch ssd1" in games/bench/results; only Sauerbraten (9 runs) and part of
OpenArena ran, the other three games are still to do). Medians with [min..max]:

| Sauerbraten base, n=3 | USB | GAMEDATA=ssd | ssd + SWAP=ssd |
|---|---|---|---|
| load (s) | 16.9 [15.4..19.3] | 15.9 [15.4..16.7] | 16.4 [15.3..17.5] |
| load_disk_s (sda wait in the load) | 5.5 [4.7..5.6] | 4.9 [4.8..5.5] | 5.4 [5.0..5.4] |
| window stalls (count / seconds) | 4 / 1.6 s [0.7..2.4] | 3 / 1.0 s [0.4..1.0] | 5 / 1.6 s [0.8..1.7] |
| longest frame (ms) | 459 [388..665] | 407 [394..443] | 454 [391..460] |
| 1% low (fps) | 24.1 [23.1..25.9] | 26.4 [24.8..26.5] | 24.1 [23.6..28.2] |
| steady median (fps) | 38.5 [38.4..38.9] | 39.3 [39.1..40.0] | 38.5 [38.4..39.8] |
| warm-up stall seconds / sda wait | 16.0 / 3.2 s | 11.9 / 2.9 s | 12.2 / 3.2 s |
| swapped out pages, whole run | 21.4k | 20.8k | 19.8k |

| OpenArena gl2min | USB (n=2) | GAMEDATA=ssd (n=1) | ssd + SWAP=ssd (n=3) |
|---|---|---|---|
| load (s, the game's own) | 10.7 [9.8..11.6] | 7.7 | 7.6 [7.5..7.6] |
| load_disk_s | 6.3 [6.2..6.4] | 6.5 | 6.4 [6.4..6.7] |
| window stall seconds (3 map reloads included) | 52 [50..54] | 41 | 43 [42..45] |
| gameplay-only stalls / seconds | 3 / 0.3 s | 0 / 0 | 1 / 0.1 s [0..0.4] |
| 1% low, gameplay only (fps) | 23.1 [22.9..23.3] | 23.5 | 24.4 [24.1..25.3] |
| longest gameplay frame (ms) | 111 [109..113] | 96 | 105 [88..394] |
| steady median (fps) | 55.9 | 56.1 | 56.1 [56.1..56.6] |
| warm-up stall seconds / sda wait | 28.6 / 5.6 s | 22.1 / 5.2 s | 20.5 / 4.3 s |

What it shows:
- The SSD did not turn the loads CPU-bound. The recorder's disk class counts the USB disk (sda) only, and
  that wait did not move: Sauerbraten 5.5 -> 4.9-5.4 s, OpenArena 6.3 -> 6.4-6.5 s. The game data was
  only a part of the load's reads from sda (Sauerbraten 93 -> 76 MB, OpenArena 144 -> 93 MB in one run
  each); the rest is the binary, libraries, Mesa, the shader cache and Xorg, which the arm leaves on
  USB, and those reads cost the same 5-6 s. The estimate above (-22% to -51%) assumed the load's disk
  wait was data; it is mostly not. The next arm to try is the whole system on the SSD (Phase B).
- Sauerbraten: load -6% (SSD) and -3% (SSD + swap) in the medians, but the spread is the first round: with
  the arm order rotated, round 1 had USB first and cold (19.3 s) and round 3 reads 15.4 / 15.4 / 15.3 s
  for USB / SSD / SSD + swap. Stalls: SSD 1.0 s against 1.6 s and 1% low 26.4 against 24.1 look better but
  sit inside the USB runs' own spread (0.7-2.4 s, 23.1-25.9); SSD + swap equals USB. No effect on the
  steady fps (38.5-39.3). Verdict: no proven gain, a possible small one for the game-data arm; n=3.
- OpenArena: the game's load is shorter by about 3 s (-28%) in all four SSD runs, against 9.8 and 11.6 s
  on USB, and the map reloads' stall seconds drop 52 -> 41-45 (-17%). The load's start to first frame
  (`load_end`) improves by about 3.6 s (28.1 s on USB, 23.5-25.7 s on SSD). The gameplay itself was clean
  in every arm (stalls 0-3, 1% low 23-25), so there is nothing for the SSD to fix there. n=2/1/3:
  indicative only. SSD + swap against SSD alone cannot be told apart (swap stall seconds 0.0-0.6 in all arms;
  the spill is rarely read back in these two games).
- Memory and sound: swap-outs the same in all arms; ALSA underruns in Sauerbraten
  (81-187 per run) are the known pattern, not arm-related (SSD 113-165, USB 153-187, SSD + swap 81-168).
- Excluded: `ps3-bench-20261018T230542Z-openarena-gl2min` (SSD arm, a key press at 195 s: interfered,
  its window 94 s), and a last OpenArena run at about 23:26Z (killed midway, its arm reset under it;
  no run directory was pulled). The 16 other runs have a picture (bench-cam photos at 60 and 150 s).

#### Phase B

Built 2026-10-19: the next section, "Phase B: the whole system on the SSD".

#### Open questions

- GameOS's UFS2 geometry: answered on the PS3 (2026-10-19), block 16384, fragment 4096; the 16 GiB
  file is 513 extents (one per 32 MiB, where UFS2 puts an indirect block).
- webMAN's FTP: write speed, and whether its `STOR`/`SIZE` handle a 16 GiB file (64-bit sizes).
- `dmsetup`: the image has it now; Phase B's initramfs doesn't use it (ps3-imgmap-boot makes the
  devices with the device-mapper ioctls itself).
- The table switch under the VRAM swap (`ps3vswap-back`) is tested only as commands on the PC
  (selftest's dry run, ps3-vswap's stubs): the first `SWAP=ssd` switch on the PS3 is its first real run.
- Whether GameOS leaves a file's blocks in place for good (no defragmenter is known on the PS3; a
  GameOS fsck after an unclean power-off might move things): `recheck` at every boot is the answer
  either way.
- Order bias: inside a round the arms always run base, then GAMEDATA=ssd, then both (run.sh
  `ROTATE=1` rotates them since 4954f90).

### Phase B: the whole system on the SSD (built 2026-10-19, not run on the PS3 yet)

The user's decision (2026-10-19): skip a game-data-only stage and put the whole root on the SSD image.
Petitboot loads the kernel and a small initramfs from the USB disk as today; the initramfs rechecks
the image's block list from dev_hdd0 (read-only), turns the file's blocks into
`/dev/mapper/ps3root` and `/dev/mapper/ps3swap`, and starts the system from there. The USB entries
stay as they are: picking one in Petitboot boots the USB root exactly as before, which is the
fallback for everything below.

**What was built** (all PC-side; tested on the PC, nothing run on the PS3):
- `tools/ssdimg/ps3-imgmap-boot.c`: the boot half of `ps3-imgmap` in C, static (glibc, 0.9 MB for
  ppc64), no Python, no dmsetup, no udev. `check MAP` (the boot map's own sha256 line, the extent
  list's sha256 by the same digest ps3-imgmap keeps, extents in order, no overlap, past the UFS2
  super block, the layout), `tables MAP root|swap` (the dm-linear lines, exactly as `ps3-imgmap
  create` builds them), `recheck MAP MOUNTPOINT` (the mount is ufs, `ro`, `ufstype=ufs2`, from
  `/dev/ps3db`; the super block's block and fragment size, FIGETBSZ and statvfs agree with the map;
  the device's size, the file's size and inode, its first block still the stamp of offset 0; then
  FIBMAP of the first and last fragment of every block, merged, compared extent by extent with the
  map), `create MAP` (needs this boot's recheck of this map and `/dev/ps3db` unmounted;
  `BLKROSET` on `/dev/ps3da` and its sysfs `ro` read back; the header area's 256 stamps read back
  through the extents with O_DIRECT; then DM_DEV_CREATE, DM_TABLE_LOAD, resume, and DM_TABLE_STATUS:
  the kernel's live table compared target by target with what was loaded; the size checked;
  `/dev/mapper/<name>` made; anything half made is removed again). Any difference refuses, exit 2.
  The device names are fixed in the code (no test seams); FIBMAP, BLKROSET and the dm ioctl numbers
  come from the kernel headers of the target (ppc64's FIBMAP is 0x20000001, checked).
- The kernel guard's registration (2026-10-19, section "The kernel guard"): after the devices are
  made, `create` registers the device side of their live tables in `/sys/block/ps3db/write_guard/`,
  seals and checks the result; a kernel without the guard gets a log line (or a refusal with
  `--guard required`, which the initramfs passes when it was built for a guard kernel).
- Why the dm ioctls and not dmsetup: dmsetup is dynamically linked (libdevmapper, libudev,
  libselinux, libpcre2 and the loader into the initramfs) and wants udev cookies or
  `--noudevsync`; the ioctls are about 150 lines, the table goes from the verified extents into the
  kernel without a text round trip, and the read-back of the live table checks the result in the
  same program. The KVM test compares the kernel's tables with those `ps3-imgmap create` (dmsetup)
  makes.
- `ps3-imgmap export-boot MAP --out FILE`: the boot map, a text file of "key value" lines (image
  path on dev_hdd0, device, its size, inode, file size, geometry, byte order, UUID, the extent
  list's sha256, `swap_sectors` from the created record so the boot makes the same two devices Phase
  A made), the extents, and a closing sha256 line over all of it. Refuses a map without a verify
  pass. 12 KB for the PS3's 513 extents.
- The initramfs (`make initrd-ssd IMGMAP=<copy of the PS3's map.json>`, `tools/ssdimg/build-initrd.sh`
  in the builder): `/init` (`tools/ssdimg/initrd/init`), Debian's busybox-static 1.38.0-3 (pinned by
  sha256 in config.mk; links only for the applets the init and a rescue shell use, the destructive
  ones like mke2fs only as `busybox mke2fs`), the static tool, `ufs.ko` and `dm-mod.ko` from the
  release's linux-image deb (vermagic checked, debug info stripped), the boot maps in
  `/ps3ssd/maps`, `/etc/ps3ssd-initrd`. A reproducible newc cpio (`tools/ssdimg/mkcpio.py`, owner
  root, fixed mtimes, `/dev/console` included), xz with CRC32 and a 1 MiB dictionary as the kernel
  wants. **Size: 1,273,608 bytes (3,879,704 unpacked: busybox 2.57 MB, the tool 0.93 MB, the
  modules 0.35 MB)**; two builds give the same sha256. Next to it: `.manifest` (sha256 of every
  file and of the deb's module files), `maps/`, `ps3-imgmap-boot.ppc64`.
- `/init`: proc, sys, devtmpfs, /run; checks its kernel release; finds the boot map whose image is
  `ps3ssd.image=`; loads ufs and dm-mod; waits for `/dev/ps3da` and `/dev/ps3db`
  (`ps3ssd.timeout`, 30 s); `blockdev --setro` on ps3da and ps3dc (checked in sysfs); mounts
  ps3db `ro,ufstype=ufs2`, `recheck`, unmounts; `create` with the name from `root=/dev/mapper/<name>`
  and `ps3ssd.swap` (ps3swap); mounts root with `rootfstype=`, `ro`/`rw`, `rootflags=`; moves /dev,
  /proc, /sys, /run into it; `switch_root` to `init=` (/sbin/init). The log stays readable as
  `/run/ps3ssd/init.log` (with the recheck's time and the tables in `/run/ps3ssd/*.table`). The
  root's fsck is systemd's, as on the USB disk (systemd-fsck-root on the read-only root, then
  systemd-remount-fs). Any failure: a framed message on tty1 and every active console (what failed,
  that nothing was written, `reboot -f` and the USB entry to go back), the log's tail, and a busybox
  shell on each. `ps3ssd.break=1` gives a shell after the devices are made, before root is mounted.
- `ps3-boot` 0.498 (in ps3-dev, not stable): `ps3-update-kboot` appends the entries of
  `/etc/kboot.d/*.conf` as written, after the kernels' entries, so a kernel install or removal keeps
  them. Left out, as a comment in kboot.conf and a message: a label already taken, a line that isn't
  an entry or is a global (`default=` and the like), an entry whose image or `initrd=` is missing on
  this disk (it comes back by itself once the files are there). Host tests: suite `ps3-boot`.
- `tools/ssdimg/ssd-install-image.sh` (the PS3, root, from the USB root): `check` (read-only: the
  USB root is `/`; ps3ssd and ps3ssd-swap exist and their live tables are the ones `ps3-imgmap
  create` wrote from a verified map rechecked this boot; the initramfs matches its manifest, its
  boot map is that map and gives the same two tables; the kernel release is in `/boot` with the
  same ufs and dm-mod files), `install` (a new ext4 labelled ps3root on `/dev/mapper/ps3ssd`, inode
  tables and journal written at mkfs, no lazy init later; it asks before erasing a file system that
  is there, e.g. Phase A's game copies; `--reuse` keeps an existing ps3root and moves its contents to
  `.old-<date>`; the image tar extracted with owners, modes and xattrs; then fstab, hostname, hosts,
  SSH host keys, Bluetooth pairings, NetworkManager connections, `/home/tibi`, `/var/tmp/bench`,
  `/var/tmp/ps3-bench`'s state without its run directories, `/var/lib/ps3-imgmap`, the
  `ps3ssd-udev` unit, and the package difference list), `boot` (`/boot/initrd-ssd-<release>.img`,
  `/etc/kboot.d/ssd.conf`, `ps3-update-kboot`). `--dry-run` everywhere. It unmounts the new root
  again if anything fails midway.
- The SSD root's fstab: `/dev/mapper/ps3root / ext4 defaults,noatime 0 1`,
  `/dev/mapper/ps3swap none swap sw,pri=200,noauto 0 0` (ps3-vswap takes the first swap line as the
  VRAM swap's backing, so it needs no new setting; `/etc/default/ps3-vswap` is not copied:
  `PS3_VSWAP_DM` belongs to Phase A's swap arm), tmpfs `/tmp`, and the USB root at `/mnt/usbroot`
  `noauto`. No USB swap.
- `ps3ssd-udev.service` (in the SSD root, enabled in sysinit, only when `/run/ps3ssd/init.log`
  exists): devices made without udev get no `/dev/mapper` link from udev and, with dmsetup's
  55-dm.rules, the coldplug "add" marks them not ready for systemd (SYSTEMD_READY=0, 99-systemd.rules).
  Nothing in the boot waits on them (systemd adds no device dependency for `/`, the swap line is
  noauto, ps3-vswap uses the node), so this is for consistency: a "change" event per dm device after
  the coldplug, which 55-dm.rules treats as the activation, then `udevadm settle`.
- bench.txt has `root=` now (client.sh): the run's root device, USB or `/dev/mapper/ps3root`.

**Boot time of the recheck.** FIBMAP twice per 16 KiB block, about 2.1 million ioctls for the 16 GiB
file. In the KVM guest on a 640 MiB file (81,920 FIBMAPs) the Python recheck took 0.49-0.53 s and the C
tool 0.043 s, about 11x less. The PS3's Python map took 79 s; at the same ratio the C recheck is
[estimate] 5-10 s per boot. The init log prints the measured time. If it matters, FIBMAP of the first
fragment only would halve it (UFS keeps a full block's fragments together; the last-fragment check is
the unit cross-check that `map` already did).

**Tested on the PC** (2026-10-19): `make test` suite `ssdimg` (export-boot and its refusals; the C
tool's `tables` equal `ps3-imgmap create`'s table files on the fake layer, also across several extents
and a 3 MiB swap; every refusal of a damaged map: edited byte, truncated, extent moved, overlap,
inside the super block, swap too big, bad path, field order, bad UUID, trailing lines; recheck and
create refuse a map for another device), suite `ps3-boot`. `make test-slow` suite `ssdimg-vm` adds, per
geometry (the PS3's 16384/4096, and 4096/512 where the file splits every 2 MiB: 15 and 2 targets): a
guest runs the C tool against the real ufs and dm (its tables are dmsetup's for the same map; recheck
on tmpfs refused; create without a recheck, with ps3db mounted, with another boot's recheck, with the
devices there: refused; BLKROSET set by the tool itself); a second guest runs `ssd-install-image.sh`
(check before and after create, a dry run that writes nothing, the question before erasing Phase A's
ext4, refusals while mounted and for `--reuse` on a foreign file system, a real install, a `--reuse`
install, `boot` into a fake USB root with the repository's ps3-update-kboot); then the guest boots
with the initramfs `boot` installed and the kernel line from the generated kboot.conf entry (as
Petitboot passes it, plus `rdinit=` for a wrapper that gives vda and vdb the PS3's names), and the
installed test root checks the result: root `/dev/mapper/ps3root` ext4 read-only, the swap device,
ps3da read-only, dev_hdd0 unmounted, the log and tables in /run/ps3ssd, /dev moved, the install's
fstab, hostname, host key, bench state without run directories, the udev unit, then 3 MiB written.
Every sector outside the image's mapped part is still the original's, and the writes landed inside.
Three refused boots (no map for `ps3ssd.image`, a header block zeroed, the file moved by a rebuilt
UFS) each stop in the rescue shell with their reason and leave the disk image byte for byte as it was.
The ppc64 build runs `check` and `tables` under qemu-user. Not tested: the PS3 itself (its kernel's dm
and ufs are the same code; Petitboot passes `initrd=` to `kexec --initrd`, from its source), the
timing, systemd on the SSD root (the test root is busybox), ps3ssd-udev.

#### The steps on the PS3 (after the batch; the user not at the console)

Phase A's devices stay: `/dev/mapper/ps3ssd` and `ps3ssd-swap`, made this boot. If the PS3 was
rebooted since, make them again first (Phase A's "every later boot" steps).

1. On the PS3, Phase A's arms off and the game copies unmounted:

       /var/tmp/bench/ssd-arm.sh reset
       sudo umount /mnt/ps3ssd

2. On the PC, the map and its records, then the initramfs for them (this replaces any
   build/initrd made from another map):

       rm -rf build/tmp/ssdb/imgmap && mkdir -p build/tmp/ssdb
       scp -r tibi@10.0.0.77:/var/lib/ps3-imgmap build/tmp/ssdb/imgmap
       make initrd-ssd IMGMAP=build/tmp/ssdb/imgmap/ssdtest.map.json
       # prints the root as 15359 MiB and the swap as 1024 MiB, 513 extents

3. The files and the new ps3-boot (0.498, the deb published to ps3-dev: sha256 001760a4...), from
   the PC (not `make deploy`, which would publish and upgrade everything built locally):

       ssh tibi@10.0.0.77 'rm -rf /var/tmp/ssdb && mkdir -p /var/tmp/ssdb'
       scp -r build/initrd tools/ssdimg/ssd-install-image.sh build/debs/ps3-boot/ps3-boot_0.498_all.deb \
           tibi@10.0.0.77:/var/tmp/ssdb/
       ssh tibi@10.0.0.77 'sha256sum /var/tmp/ssdb/ps3-boot_0.498_all.deb && sudo dpkg -i /var/tmp/ssdb/ps3-boot_0.498_all.deb'

4. On the PS3, the read-only check, then a dry run:

       sudo bash /var/tmp/ssdb/ssd-install-image.sh check --from /var/tmp/ssdb/initrd
       sudo bash /var/tmp/ssdb/ssd-install-image.sh install --from /var/tmp/ssdb/initrd --tar - --dry-run

5. With the user's go (it erases Phase A's game copies on ps3ssd): the fresh release image
   (`make rootfs`; build/rootfs/debian-ppc64.tar, 3 GB) streamed from the PC into the install:

       ssh tibi@10.0.0.77 'sudo bash /var/tmp/ssdb/ssd-install-image.sh install \
           --from /var/tmp/ssdb/initrd --tar - --yes' < build/rootfs/debian-ppc64.tar

   It prints what it copied and how many packages differ from the USB root.
6. The boot entry on the USB root:

       sudo bash /var/tmp/ssdb/ssd-install-image.sh boot --from /var/tmp/ssdb/initrd
       # kboot.conf now ends with ssd-6.12.111+ps3.472='/boot/vmlinux-6.12.111+ps3.472
       #   initrd=/boot/initrd-ssd-6.12.111+ps3.472.img root=/dev/mapper/ps3root rootfstype=ext4 ro
       #   ps3ssd.image=/linux/ssdtest.img ip=dhcp netconsole=... <the release's arguments>'

7. The user: fan in SYSCON mode (or 60% or more) as before every Linux session, reboot, Petitboot,
   the entry `ssd-6.12.111+ps3.472`; the boot flag back on GameOS at the end of the session, as
   always. Petitboot's countdown default is the label picked last (section "Petitboot's
   behaviour", from its code), so after this an unattended boot goes to the SSD root; picking a USB
   entry by hand switches back.
   With `quiet` the initramfs prints nothing on success; a failure is on the screen and, at kernel
   level 2, on netconsole (`make listen` on the PC) if the screen stays black.
8. Checks after the first SSD boot (the webcam first: the desktop must show):

       findmnt / ; cat /run/ps3ssd/init.log     # /dev/mapper/ps3root ext4 rw; the recheck's seconds
       cat /sys/block/ps3vswap0/backing; swapon --show   # /dev/mapper/ps3swap; ps3vswap0 300, zram 100
       cat /sys/block/ps3da/ro                  # 1
       systemctl --failed; systemctl status ps3ssd-udev dev-mapper-ps3root.device
       ls -l /dev/mapper; sudo dmsetup table ps3root | wc -l    # 480 lines
       dmesg | grep -i -e ext4 -e device-mapper -e ufs
       ip -4 addr show eth0                     # 10.0.0.77 (the DHCP client ID is the MAC)

9. The bench prerequisites: the packages the USB root has that the fresh image lacks or has in
   another version, from the list the install wrote (kernels apart), simulated first:

       sudo apt update
       xargs -a /var/lib/ps3ssd/packages-from-usb.txt sudo apt install -s --allow-downgrades | tail
       xargs -a /var/lib/ps3ssd/packages-from-usb.txt sudo apt install --allow-downgrades

   A version no longer in any suite (ps3-dev keeps 5) fails by name: install that package's current
   version on both roots instead. Then `make bench-push` from the PC if games/bench changed since
   the copy. The same package versions on both roots are what makes their numbers comparable.

**Falling back.** Any initramfs failure ends in its rescue shell with the reason: `reboot -f`, then a
USB entry. A broken SSD root: a USB entry, nothing on the USB root changed except `/boot` (one more
file) and `/etc/kboot.d/ssd.conf`. Removing the entry: `sudo rm /etc/kboot.d/ssd.conf
/boot/initrd-ssd-*.img && sudo ps3-update-kboot` on the USB root. The image file itself is untouched
by all of this (Linux writes only inside its blocks); deleting it in GameOS is the rollback of
everything, as in Phase A. While the SSD root runs, `ps3-gameos-mount` can't mount dev_hdd0 (device
mapper holds ps3db, EBUSY), and kernel updates must go to both roots: the USB root's `/boot` has the
kernel Petitboot loads, the SSD root has its modules. Install a new kernel on the USB root, build its
initramfs (`make initrd-ssd INITRD_RELEASE=<release> ...`), run `boot` again, and install the same
kernel package on the SSD root.

#### The benchmark: USB root against SSD root

Root can't move inside one boot, so this compares across boots (memory "same-boot baselines": the
exception, made as small as possible). Boots alternate USB, SSD, USB, SSD (two of each), the same
commands in the same order in every boot, `tools/bench-cam.sh` as always, the measurement protocol
(desktop stopped, swap emptied, nothing deployed between the boots), the same package versions on
both roots (step 9) and the same shader cache and bench state (copied by the install). In each boot:

    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench supertuxkart lean 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench classicube base 3
    STALLS=1 /var/tmp/bench/run.sh bench openarena gl2min 3       # gameplay, without the load profile
    STALLS=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    ps3-bench.sh desktop

The first five are the start-up set of batch 16/17 (`load`, `load_disk_s`, loadprof's split,
`stalls_warm`, `warm_disk_s`); the last two are gameplay, judged by stalls and 1% lows first
(`stall_disk_s`, `stall_swap_s`; with the SSD root the VRAM swap spills to the SSD too, so this is
"GAMEDATA=ssd+SWAP=ssd" plus binaries, libraries and the shader cache on the SSD). bench.txt's `root=`
tells the boots apart in the results; report.py over the run directories of each root gives the
medians to compare, a difference counting when both SSD boots agree against both USB boots.

#### Later (for a release, not needed for the benchmark)

- The header in the image's first MiB (magic, UUID, the list's sha256, room for a boot bundle;
  `ps3-imgmap header-write`), so the boot map can be checked against the image itself and the state
  needn't live on the USB root. Designed, not built: section "The image header" (writing it collides
  with the kernel guard).
- `/boot` on dev_hdd1 with the webMAN autoexec mirror (sections above), so the USB disk can go.
- The kernel guard: built (local/0045, next section; ps3da read-only, ps3db only inside the
  registered ranges, ps3dc writable for the boot files); since 2026-10-19 both create commands
  register and seal their tables' ranges and the patch is in the series. To do: its checks on the PS3
  (the steps there).
- Kernel updates in one step for both roots (a kernel hook that rebuilds the initramfs and the entry).
- The GameOS installer (design above).

### The kernel guard (local/0045; built 2026-10-19, in the series, not booted yet)

`linux/patches/local/0045-ps3disk-write-guard.patch`. A kernel with it refuses every write to `ps3db`
that no boot tool registered: Phase A's and Phase B's devices work only once their tool has
registered and sealed their ranges, which both tools do since 2026-10-19 (below). In
`linux/patches/series` since then, so the next kernel build has it; nothing built or installed for
the PS3 yet. **With a guard kernel on the PS3, Phase A needs the new `ps3-imgmap`** (copy it to
`/var/tmp/ps3-imgmap` again): an older copy makes ps3ssd and ps3ssd-swap without sealing, every write
to them fails with EIO (GameOS's data stays untouched, but ext4 on ps3ssd and the SWAP=ssd arm break)
until a reboot and a create with the new one.

**Policy**, by region index, fixed at build time (no parameter, no command-line switch):

| disk | region | policy | why |
|---|---|---|---|
| `ps3da` | 0 | read-only | the whole drive: VFLASH, the region table, every OS |
| `ps3db` | 1 | writes only inside the ranges registered and sealed in this boot | GameOS's dev_hdd0; our image file's blocks are the only part Linux may write |
| `ps3dc` | 2 | writable | GameOS's dev_hdd1. Linux keeps its boot files there (`kboot.conf`, kernels, initramfs; the webMAN autoexec mirrors them to dev_hdd0), so it must stay writable (decision 2026-10-19). The risk: a mistyped mkfs on `ps3dc` wipes GameOS's cache partition. **[inference]** GameOS recreates a dev_hdd1 it finds broken (the cache section above), so what is lost is caches and our boot files, which the autoexec restores from dev_hdd0 |
| `ps3dd` and up | 3+ | writable, as before | the OtherOS++ region |

`ps3da` is marked read-only at probe (`/sys/block/ps3da/ro` is 1, mounts are read-only, `write(2)`
gets EPERM), `blockdev --setrw` (BLKROSET 0) is refused with EPERM, and the request check below
refuses its writes too, since the block layer only warns about a bio sent to a read-only disk and
device-mapper can still send one. `blockdev --setro` (what Phase A's `create` and `ps3-imgmap-boot`
do) still succeeds. Nothing turns it writable short of a rebuilt kernel.

**The check** runs in ps3disk's `queue_rq` for every request, under the driver's lock, before the
data is copied to the bounce buffer and before anything goes to lv1. Reads and flushes without data
always pass. Every other operation counts as a write (fail closed): writes, the data part of a
PREFLUSH/FUA write (blk-mq sends the flush as its own request and the data as a plain write; the
queue has no FUA), discard and write-zeroes (not advertised by ps3disk, refused here too). A write's
[start, start + sectors), what lv1 is given, and every bio of the request have to lie inside one of
the merged ranges; a merged request that is only partly inside is refused whole. Bios arrive with
absolute sectors of the region: partitions and dm-linear are remapped before ps3disk sees them.
Whitelist writes also need 512-byte device blocks (ps3disk scales positions by the block size). A refused request fails with EIO and one kernel line, rate-limited to 10 per 5 s per region:

    ps3disk <device>: ps3db: write guard refused WRITE of 8 sectors at 123456: outside the registered ranges

(the reasons: `read-only region`, `no ranges sealed`, `outside the registered ranges`, `block size
is not 512`). `write_guard/refused` counts them. At boot each disk logs its region and policy
(`ps3db (region 1 id N, start S, L sectors): write guard: writes only inside the ranges sealed in
write_guard/`): the first boot shows whether index 1 really is dev_hdd0's region.

**The interface**, `/sys/block/ps3dX/write_guard/`; writing needs root (CAP_SYS_ADMIN):

| file | disks | mode | content |
|---|---|---|---|
| `policy` | all | r | `read-only`, `whitelist` or `writable` |
| `refused` | ps3da, ps3db | r | writes refused since boot |
| `ranges` | ps3db | w (0200) | lines of `start sectors`: decimal, 512-byte sectors of ps3db (the units of a dm-linear table's device offset), blanks between, one range per line, every line ending with a newline (the last one too). Appended until the seal. Each write(2) is taken whole or not at all. EINVAL: a malformed line, hex, a sign, an empty range, one reaching past the end of ps3db, no final newline; ENOSPC: more than 65536 ranges in all; EPERM: after the seal. sysfs cuts a write longer than 4096 bytes at 4096 without telling the driver: a cut inside a line gives EINVAL (the newline rule), a cut at a line end a short count, so a writer sends at most 4096 bytes of whole lines per write(2) and stops at the first error or short count |
| `sealed` | ps3db | rw (0644) | `0` or `1`. Writing `1` sorts the ranges, merges those that overlap or touch, and turns them on; from then on `ranges` and `sealed` refuse with EPERM until the next boot. Writing `0`: EINVAL |
| `nr_ranges` | ps3db | r | ranges registered so far; after the seal, the merged ranges |
| `nr_sectors` | ps3db | r | sectors the merged ranges cover; 0 before the seal |

Until the seal every write to `ps3db` is refused; sealing with no ranges keeps it read-only for the
rest of the boot. A new probe would forget the seal, so ps3disk has no sysfs bind/unbind
(`suppress_bind_attrs`), and the seal takes a reference on the module (ours is built in,
`CONFIG_PS3_DISK=y`, but a modular build can't be unloaded once sealed).

**What the boot tools do** (built 2026-10-19: `ps3-imgmap-boot create` in the initramfs, Phase A's
`ps3-imgmap create` the same steps), in this order:

1. The recheck as before (dev_hdd0 mounted read-only, then unmounted). Only reads.
2. Before anything is made: `write_guard/policy` must read `whitelist`, `sealed` `0` and `nr_ranges`
   `0` (ranges someone registered without sealing would be sealed along with ours). Sealed already:
   refused ("sealed earlier in this boot ... Reboot"), nothing made. No `write_guard/` directory (a
   kernel without 0045, the user's choice 2026-10-19): the log line `kernel has no write guard (no
   /sys/block/ps3db/write_guard): ps3db's writes are not limited to the image's blocks` and on as
   before, so today's kernels keep booting. Except with `--guard required`, which the initramfs
   passes when `/etc/ps3ssd-initrd` says `guard=required`: `build-initrd.sh` sets that when the
   release's `System.map` in the kernel deb has the guard (`ps3disk_guard_group` or
   `ps3disk_set_read_only`; `GUARD=required|auto` overrides). Such an initramfs on a kernel without the
   guard refuses (the release check already stops a mismatched pair; this is the second lock).
   Phase A has no required mode (it runs on whatever kernel the USB root booted).
3. The dm-linear devices (root and swap) made, read back and compared, as before. Still no writes.
   More than 65536 targets in all: refused before this step.
4. The device side of every target of both tables as the kernel reads it back (`DM_TABLE_STATUS` in
   C, `dmsetup table` in Python; Phase A's create now also compares that live table with what it
   loaded): `<offset> <len>` per target. Not the header area (never mapped). In chunks of whole lines
   of at most 4096 bytes, each one `write(2)` that must return its whole count; an error or a short
   count stops it. Then `nr_ranges` must equal the number of lines sent (before the seal it counts
   what was taken).
5. `1` written to `sealed` (one `write(2)` of `1\n`).
6. `sealed` reads `1`, `nr_ranges` equals the tool's own count after sorting by offset and merging
   ranges that overlap or touch (the kernel's rule), `nr_sectors` their sum (the tool first checks
   that the targets don't overlap). The log line: `write guard sealed: N ranges registered in W
   writes, M after merging, S sectors (X MiB) of ps3db writable, nothing else`.
7. Only now root is mounted (ext4's journal replay writes) and later swapon.

Any failure in steps 4-6 removes the devices made in step 3 (and their table records) and refuses:
`write guard: <what>. ps3db's guard can't be reset in this boot (ranges taken stay, a seal stays):
reboot`. The initramfs then stops in its rescue shell (nothing was written: ps3db refused every write
until then); `reboot -f` and the USB entry. Phase A's create says the same. Boots without the SSD root
(the USB root) register nothing until Phase A's create, so `ps3db` refuses every write before it; a
unit that seals an empty list early in those boots would also stop a later process from registering,
if we want that.

`ssd-install-image.sh check` (and so `install` and `boot`) reports the guard on the USB root: none
(`kernel without the write guard`), or it must be `whitelist`, sealed, with `nr_ranges` and
`nr_sectors` exactly those of ps3ssd's and ps3ssd-swap's tables merged (`write guard sealed: N
ranges, S sectors of ps3db writable, exactly ps3ssd and ps3ssd-swap's; R writes refused since boot`);
unsealed or different: refused (reboot, recheck, create).

**Not covered**: root making raw lv1 storage calls through `/dev/ps3lv1call` (`CONFIG_PS3_LV1CALL=m`,
loaded by hand for GPU research) goes around the block layer; kernels without the patch (older ones,
a kexec'd foreign kernel) have no guard (`write_guard/` absent). A buffered write to `/dev/ps3db`
that the guard refused leaves the changed data in the page cache although the disk is unchanged:
`blockdev --flushbufs /dev/ps3db` drops it. A refused write through a dm device shows as EIO in
ext4 (its `errors=` policy then applies). A stray write to `/dev/ps3db` that the block layer merges
with a dm write fails both (closed, but it would hit ps3root); `CONFIG_BLK_DEV_WRITE_MOUNTED=n` would
stop anyone opening ps3db for writing while device-mapper holds it, but it changes that for every
mounted disk and isn't set (fsck tools that open a mounted device read-write would get EBUSY).

**No debug switch.** A command-line or runtime switch that turns the check off is not built: anything
that needs writes elsewhere on ps3db can register those ranges for its boot, and a switch would make
one mistyped kernel argument (Petitboot's editable command line, a kboot.conf entry) enough to expose
dev_hdd0 again. A different region layout needs a changed patch, not a switch.

**Tested on the PC** (2026-10-19): `linux/tests/ps3disk-guard.sh` takes `ps3disk_guard.h` (the
parser, the seal's sort and merge, the check, the policy decision; no kernel APIs) straight from the
patch and runs `ps3disk-guard-test.c` on it, plain with `-Werror` and under ASan/UBSan: about 50 fixed
cases (malformed lines, a write cut inside a line, overflow at 2^64, the region's end, alignment, the
4096-byte page full of ranges, ranges that touch or overlap, runs across a merged boundary or into a gap, empty and wrapping
runs, every policy sealed and unsealed), 65536 reversed ranges, and 2000 seeded random rounds of
several writes each checked sector by sector against a bitmap. Five deliberate off-by-one mutations of
the header each fail it. The patch applies on the series as of 0044, `ps3disk.o` compiles without
warnings with `W=1` (also as a module), and a full `vmlinux` links, with both `ps3.config` and
`ps3.config` + `ps3-release.config` on `ps3_defconfig` (the host's powerpc64-linux-gnu-gcc 16.2.1). A
second agent (Opus) reviewed the request path against the 6.12 block layer (merging, the flush
machine, requeue, partitions, dm, sysfs, BLKROSET); its should-fixes are in. Not tested: the PS3,
ps3disk's request path in a running kernel.

**The boot tools' side, tested on the PC** (2026-10-19):
- `make test` suite `ssdimg`: the C tool's test build (`-DPS3_GUARD_FAKE`: `tools/ssdimg/guard-fake.h`
  replaces the sysfs I/O with a model built on the patch's own `ps3disk_guard.h`, which
  `linux/tests/guard-header.sh` takes from the patch; the 4096-byte cut, whole-or-nothing writes, the
  seal's merge, EPERM after it) runs create's guard steps on the boot map's tables (`guard-test`):
  exactly the device side of both tables, then `1` to `sealed`; a fragmented 16 MiB layout goes in
  several writes, each at most 4096 bytes of whole lines and filled before the next; refusals for no
  guard with `--guard required`, sealed already, policy `writable` and `read-only`, pending ranges,
  a short count, EINVAL on the first and on the second write, the seal failing, `nr_ranges` or
  `nr_sectors` off, ranges past the disk's end; no guard without `required`: the log line, nothing
  written. Phase A's `ps3-imgmap create` against a Python model of the same: the same lines as the C
  tool, the header area in no range, refusals before anything is made (no setro, no dmsetup) and
  after (both devices removed again, "reboot" in the message).
- `make test-slow` suite `ssdimg-vm`: `ps3guard_sim` (`tools/ssdimg/guard-sim`, built against the
  host kernel by vm-e2e.sh) gives the KVM guest disks named ps3da and ps3db over its virtual disks,
  running the patch's own code unchanged (`extract.sh` takes struct ps3disk_guard, the region policy,
  BLKROSET's hook and the whole sysfs interface out of the patch; only the per-write check is the
  same decision on each bio instead of each request). The second Phase B geometry (block 4096 /
  fragment 512: 15 + 2 targets, 16 ranges after merging) runs behind it: Phase A's create registers
  and seals, `ssd-install-image.sh check` reports the seal, mkfs, the tar and the fix-ups write
  through it with nothing refused; the initramfs (built with `guard=required`) registers and seals
  from the kernel's tables, root mounts and writes with nothing refused, the test root's writes to
  ps3db sector 0 and to the header area are refused (EIO, counted), `ranges` after the seal and
  BLKROSET 0 on ps3da refused; without the module the same initramfs stops in the rescue shell (the
  kernel has no guard), the disk unchanged. Two more guests: Phase A behind the guard (the files and
  policies, a write refused before the seal, map/verify/recheck, create sealing, ext4 and swap on the
  devices, writes outside refused and one inside straight to ps3db allowed, `ranges` and `sealed`
  EPERM, `sealed 0` EINVAL, the C tool's create refused because of the seal, nothing made, the module
  pinned; every sector outside the image's mapped part unchanged) and sysfs's edges (six malformed
  writes refused with nothing taken, a 4100-byte write taken as 4096 with a short count, a write cut
  inside a line EINVAL, the C tool refusing pending ranges, 512 equal ranges sealed into one of 8
  sectors, the sectors just inside allowed and one past either end refused; the disk byte for byte
  the original). The first geometry runs without the module: the initramfs and both tools log
  "kernel has no write guard" and boot as before. And the test build against the real
  device-mapper: what it registers is exactly `dmsetup table`'s device side, and a short count, a
  failed seal or wrong sectors remove both devices again.

**On the PS3** (not done yet; after the batch, the user not at the console, nothing published).
Every write test below writes back data that is already there, so even a broken guard changes
nothing.

1. PC: the kernel with 0045 (`make linux-deb`, then `make ps3rsx-modules`), the new tools to the PS3
   (`scp tools/ssdimg/ps3-imgmap tibi@10.0.0.77:/var/tmp/ps3-imgmap`; `ssd-install-image.sh` with the
   step 3 copy below). First boot of the new kernel without installing it: `make netboot-deb
   FLAVOUR=release ROOT=usb` (Petitboot, the user), or install it (`make deploy KERNELS=1
   PKGS="linux-image-ps3 ps3rsx-modules-ps3"`, which publishes to ps3-dev: only with the user's go).
2. The USB root on the guard kernel, before Phase A's create:

       dmesg | grep 'write guard'          # ps3da read-only, ps3db "writes only inside ...", ps3dc writable
       grep . /sys/block/ps3d?/write_guard/policy     # read-only, whitelist, writable (ps3dd too)
       grep . /sys/block/ps3db/write_guard/*          # sealed 0, nr_ranges 0, nr_sectors 0, refused 0
       cat /sys/block/ps3da/ro; sudo blockdev --setrw /dev/ps3da   # 1; EPERM
       M=/var/lib/ps3-imgmap/ssdtest.map.json
       S=$(python3 -c 'import json,sys; m=json.load(open(sys.argv[1]))
       print(next(p + 40 - l for l, p, n in m["extents"] if l <= 40 < l + n))' $M)   # header block 5
       sudo dd if=/dev/ps3db of=/tmp/hdr5 bs=512 skip=$S count=8 iflag=direct
       head -c 8 /tmp/hdr5; echo                       # PS3IMGv1: its stamp
       sudo dd if=/tmp/hdr5 of=/dev/ps3db bs=512 seek=$S count=8 oflag=direct conv=notrunc
           # expect "Input/output error"; refused 1; dmesg: "refused WRITE of 8 sectors at $S: no ranges sealed"

3. Phase A as every boot (`ps3-gameos-mount`, `recheck`, `-u`), then `sudo python3
   /var/tmp/ps3-imgmap create $M ps3ssd --swap-mib 1024 --yes`: it prints `write guard sealed: 513
   ranges ...` or so (513 extents; the root's and the swap's targets merge where they touch). Then
   `grep . /sys/block/ps3db/write_guard/*` (sealed 1, nr_sectors 33552384 = 16 GiB minus the 1 MiB
   header), the same `dd` write-back of header block 5 (EIO, refused + 1, "outside the registered
   ranges"), `echo '0 8' | sudo tee /sys/block/ps3db/write_guard/ranges` (EPERM), mount ps3ssd and
   run the bench's SSD arms as usual: `refused` must not move.
4. The initramfs for the guard kernel from the PS3's real map (step 2 of Phase B's PS3 steps, with
   `INITRD_RELEASE=<the new release>`): it must print `guard=required`. `ssd-install-image.sh check`
   then reports the seal; `boot` adds the entry `ssd-<new release>`.
5. The first SSD boot on it (the user): `cat /run/ps3ssd/init.log` has `write guard sealed: ...`;
   `grep . /sys/block/ps3db/write_guard/*` sealed 1 and `refused` 0 after the desktop is up and a
   bench run; the header block 5 write-back once more (EIO). If anything refuses at boot, the rescue
   shell says why: `reboot -f`, the USB entry (an older kernel is still installed).

### The image header (design, 2026-10-19; not built)

What "Later" asked for: a header in the image's first MiB (magic, UUID, the extent list's sha256,
room for a boot bundle) and `ps3-imgmap header-write`. Designed here and stopped, because writing it
collides with the kernel guard, and building it is bigger than the guard work it would ride on.

**Why it is wanted.** The initramfs carries one boot map per image, made on the PC from a copy of
the PS3's `/var/lib/ps3-imgmap`: the state lives on the USB root and in the initramfs. With a header
the image describes itself: the boot tool FIBMAPs the file (the recheck does that already), reads
the header through the read-only UFS mount and compares its extent sha256 with what FIBMAP found. A
boot map would no longer be needed to find the file's blocks, only to say which image to use.

**Layout** (image bytes [0, 1 MiB), 256 blocks of 4 KiB, never mapped into a dm device):
- block 0 stays the stamp of offset 0 for good. `recheck` reads it through UFS as the file's identity
  (magic, UUID) and every tool refuses an image whose block 0 isn't that stamp, so leaving it alone
  keeps all of today's checks.
- block 1, the header: `PS3IMGH1`, a format version, the UUID (the stamps'), the file size, the
  extent list's sha256 (the digest `ps3-imgmap` already keeps: "ps3-imgmap 1", size, uuid, the
  extents), `swap_sectors`, a generation counter, the time written, the length and sha256 of the
  bundle, then a sha256 of the block's first part. Big-endian fixed fields, zero padded.
- blocks 2-255, the bundle (1016 KiB): the boot map text and the kernel arguments the image wants.
  Not the initramfs (1.27 MB) or a kernel: too small, and Petitboot reads neither from here.

**Compatibility.** The installed initramfs checks that "all 256 stamps read back" and would refuse
an image with a header. The new rule in `ps3-imgmap-boot` and `ps3-imgmap create`: block 0 must be
its stamp; block 1 its stamp (no header, today's image) or a valid header of this UUID whose extent
sha256 is the map's; blocks 2-255 their stamps, or covered by the header's bundle sha256. An image
without a header (the one on the PS3) passes unchanged, so nothing has to be rewritten. The order
matters: every initramfs and Phase A copy has to know the new rule before a header is written, or
the next boot refuses (safely: the rescue shell, USB fallback).

**The conflict with the guard.** The header area is never registered, so on a kernel with 0045 no
write can reach it: that is the guard's point ("Not the image's header area: it stays unwritable").
`header-write` would need one of:
1. a kernel without the guard (today's .472), once, before the first guard kernel. Simple, but only
   once: later writes (a new generation) need option 2.
2. a boot spent on it: `ps3-imgmap header-write MAP` registers only the header's blocks (device
   sectors through the verified, rechecked map), seals, writes them with O_DIRECT, reads them back
   and verifies; the seal leaves nothing else writable, so the image's devices need the next boot.
   Safe and simple to reason about, one reboot per write.
3. registering the header blocks together with the dm targets in every boot: no extra boot, but the
   header becomes writable whenever the image is in use, which is what the guard was meant to stop.
Option 2 is the one that keeps the guard's promise. It also changes this tool's own rule ("never
opens /dev/ps3db for writing"): `header-write` would be the one command that does, for at most 255
blocks resolved through the map, before any dm device exists, and only after a recheck in the same
boot.

**What it would take.** The format and its checks in both tools (Python and C), the relaxed header
rule above with tests for every combination (stamp, header, damaged header, header of another UUID or
list), `header-write` with the guard sequence of option 2 (the fake guard and the VM simulation
cover it), a boot path that finds the extents from FIBMAP plus the header without a boot map, and
the PS3 order (new initramfs and Phase A tool first, then one header-write boot). Not started: the
benchmark doesn't need it, and the guard should first prove itself on the PS3.

### Getting more than 22 GiB (no-go for now)

- **Custom `emer_init`:** **[inference]** For 64 GiB the instruction would be `addis r7,r9,-0x800`
  (`3c e9 f8 00`), for 96 GiB `-0xC00` (`3c e9 f4 00`), in place of Evilnat's `fd 40`. It means
  unpacking Evilnat 4.92's PUP, decrypting and re-signing `emer_init.self` (PS3MFW Builder's task,
  whose listed offsets stop at 4.46), repacking and installing the PUP: a CoreOS write to the NOR's
  other bank (class C in FLASH-SYSCON.md, recovery 2 or 3, and we have no hardware flasher). A
  broken `emer_init` breaks the Recovery Menu, our safety net. Nobody has published this for 4.92.
  It needs the owner's explicit decision under the flash rule; I'd say no.
- **Rebuilding the regions by hand** after the Restore (delete regions 1 and 2, recreate them
  smaller, then a bigger region 3): undocumented. **[inference]** GameOS would find a UFS2 bigger
  than its region and reformat, which brings back Evilnat's layout; a wrong start sector could hit
  VFLASH. Not recommended.
- **Pragmatic:** 22 GiB on the SSD for root and swap, the USB disk for bulk data.

### Checklist

U = the user at the console, W = us (read-only on the PS3 unless the step says otherwise).
Every Linux session ends with the boot flag back on GameOS, as always.

| # | Who | Step | Go/no-go | Time |
|---|---|---|---|---|
| W1 | W | Linux, read-only: `ps3-gameos-mount`, `du -sh` of `hdd0/*` and `home/*/{savedata,trophy,exdata}`, `cat /sys/block/ps3d?/size`, `df` of the USB root | User agrees what must survive | 15 min |
| W2 | W | Raw copy (section 3, copy 3) with a hash list | Copy hashes check | 30 min (home/exdata) to 2 h (all 174 GB at USB speed) |
| U1 | U | xai: Backup activation file, Export Licenses, Backup xRegistry, Dump Flash (second dump), Show ROS Bank | W: `cmp` the new dump with the first, PyPS3checker clean | 15 min |
| U2 | U | Backup Utility to a FAT32 USB drive; trophies synced if PSN is used | Backup finishes without errors | 2-3 h [estimate] |
| U3 | U | Petitboot shell, read-only: `which ps3hvc_hvcall ps3stor_region`, `ls -l /dev/ps3hvc /dev/ps3stormgr`, `grep ps3d /proc/partitions` | **Both tools present, or stop** (then W builds them before going on; the installed image has both in `/usr/sbin`) | 5 min |
| U4 | U | Back to GameOS (flag), put webMAN fan to SYSCON; Recovery Menu (hold power until two beeps), option 5 Restore PS3 System | Point of no return | 10-20 min |
| U5 | U | First setup: language, user, network; Petitboot test with xai "Set OtherOS Boot Flag"; if it doesn't start, xai "Install OtherOS" and try again | Petitboot comes up | 15 min |
| U6 | U | Petitboot shell: rerun U3's checks; `grep ps3d /proc/partitions` shows ps3da-ps3dc only, `ps3db` 912264528 blocks (1824529056 sectors) | ps3db shrank by about 22 GiB, else stop | 5 min |
| U7 | U | `sh create_hdd_region.sh` from a USB stick (W provides it, sha256 checked, plus a wrapper that refuses unless there are 3 regions and 8-128 GiB free); reboot to Petitboot | `ps3dd` appears with 46137320 sectors | 10 min |
| W3 | W | Kernel change 1 and the udev rule (change 2) deployed and booted from USB first | GameOS regions show `ro 1` in `lsblk` | before U7's reboot into Linux; host work 1-2 h |
| W4 | W | `make ssd-root`: partition, mkfs, root and state, fstab, kboot.conf (writes only `ps3dd`) | Guards pass | 30-45 min |
| U8 | U | Petitboot: boot the SSD entry | Desktop up, picture on the webcam | 10 min |
| W5 | W | Checks: `findmnt /` on `ps3dd1`, swap on `ps3dd2` behind VRAM swap, `dmesg` clean, `ps3-gameos-mount` still mounts hdd0 read-only | | 15 min |
| U9 | U | Back to GameOS: Backup Utility > Restore, webMAN-MOD reinstall (fan mode SYSCON), CFW folders back, a few games and saves tried | Saves and licences work | 2-3 h [estimate] |

Total: about 7-9 hours of wall time, most of it unattended copying (U2, U9, W2); the user's hands-on
time is about 1.5 hours, ours 3-4 hours of host work (W3, W4 tooling) plus the checks.

Go/no-go summary: (1) the user accepts 22 GiB, or decides on the custom `emer_init` (I recommend
not); (2) all three backups are done and checked; (3) Petitboot has `ps3hvc_hvcall` and
`ps3stor_region`; (4) after the Restore, `ps3db` really shrank by 22 GiB; (5) the read-only kernel
change is booted before root moves.

### Sources

- psdevwiki (Wayback captures, 2025): [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B)
  (install on HDD, glevand's warnings, firmware list with half/eighth/22 GB variants),
  [Talk:OtherOS++](https://www.psdevwiki.com/ps3/Talk:OtherOS%2B%2B) (ps3da-ps3dd table,
  `emer_init` values), [Harddrive](https://www.psdevwiki.com/ps3/Harddrive) (encryption, HDD swap
  with Backup Utility, capacity limits, xRegistry on NOR),
  [HDD Encryption](https://www.psdevwiki.com/ps3/HDD_Encryption) (XTS, VFLASH at sector 8),
  [Recovery Menu](https://www.psdevwiki.com/ps3/Recovery_Menu).
- glevand's tools, mirrored at [dblsaiko/PS3-OtherOS](https://github.com/dblsaiko/PS3-OtherOS):
  `create_hdd_region.sh`, `INSTALL_OTHEROS_ON_HDD`.
- [PS3MFW `tasks/patch_gameos.tcl`](https://github.com/anonymous-ElmerFudd/PS3MFW/blob/master/tasks/patch_gameos.tcl)
  (`--patch-gameos-hdd-region-size` and its byte values);
  [Rebug: OtherOS-enabled FW](https://rebug.me/create-otheros-enabled-rebug-fw/).
- Evilnat changelogs at ps3.nz ([4.91.2](https://ps3.nz/en/changelog.html),
  [4.93](https://ps3.nz/en/changelog-cfw-evilnat-4.93.html), Wayback 2026) and the
  [4.93 psx-place thread](https://www.psx-place.com/threads/4-93-evilnat-w-cobra-v8-5-cex-pex-d-pex.39743/)
  (Wayback 2026-10-04: xai option list).
- [setsid/ps3-linux-internal-ssd](https://github.com/setsid/ps3-linux-internal-ssd) (commit
  c9892f1, 2026-08-18): Debian from the OtherOS region on Evilnat 4.93, CECH-2503B, a 960 GB
  SSD; region log, 22 GiB on 320 GB and 960 GB drives, udev rule, Petitboot tool caveats,
  read-only policy.
- kostirez1, [HDD OtherOS region reuse](https://www.psx-place.com/threads/hdd-otheros-region-reuse-for-un-official-filesystems-expansion-past-1tb-of-internal-space.39979/)
  (search snippets only: Evilnat 4.90, Restore leaves 22 GB, region made from Petitboot).
- Sony manuals: [Backup Utility](https://manuals.playstation.net/document/en/ps3/current/settings/backuputility.html),
  [Format Utility](https://manuals.playstation.net/document/en/ps3/current/settings/formatutility.html).
- Ours: `firmware/petitboot/dtbImage.ps3.bin` strings and its unpacked initramfs, upstream
  petitboot at 17d63c9 and Linux v2.6.30 (fs/ufs/super.c, drivers/ps3/ps3stor_lib.c,
  drivers/block/ps3disk.c), `linux/patches/ps3/0010` and `0150`, the
  `.457` kernel config (`EFI_PARTITION=y`, `MSDOS_PARTITION=y`, `EXT4_FS=y`, `PS3_STRGMNGR` off),
  docs/OTHEROS-PATCHES.md, docs/FLASH-SYSCON.md.
