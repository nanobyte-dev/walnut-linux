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
2.6.30.10 build of 2011 (its version string), with the `ps3stormgr` driver and GPT support in the
kernel. **[unverified]** Whether its initramfs has `ps3stor_region` and `ps3hvc_hvcall`: I couldn't
unpack it here; the user checks it read-only before anything is wiped (checklist step U3). Our
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
| A Linux write to the wrong region: our ps3/0010 exposes `ps3da` (whole disk, VFLASH included) and `ps3db` read-write, one letter from `ps3dd` | `mkfs` on `ps3da` wipes VFLASH (firmware reinstall from Recovery) and the region table; on `ps3db` it destroys GameOS | Before root moves: make every region except the OtherOS one read-only in the kernel (setsid's policy, our change 1); installer guards (change 3) |
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

1. **Kernel:** a local patch (or an 0010 change) so ps3disk marks every region read-only except
   the OtherOS one, with an override parameter, before anything writes `ps3dd`. setsid's rule: the
   highest region that is neither region 0 nor the whole drive.
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
   stays as the fallback: Petitboot lists the `kboot.conf` of both disks.
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
- **Kernel guard:** `ps3da` and `ps3dc` read-only; writes to `ps3db` allowed only inside the
  registered extents (a small ps3disk check), so a mistyped mkfs can't reach GameOS's data.
- **Boot:** Petitboot can't see a device-mapper disk, so the kernel and kboot.conf stay readable to
  it. Petitboot's 2.6.30 kernel has vfat, ext3, iso9660 and ufs built in (strings of
  firmware/petitboot/dtbImage.ps3.bin). Options: (1) `/boot` on the USB disk as today, read only at
  boot, plus a small initramfs that builds the mapping (too many extents for `dm-mod.create=`);
  (2) `/boot` on dev_hdd1 (vfat, GameOS's cache partition, which GameOS may clear: keep (1) as the
  fallback); (3) the kernel as a file on dev_hdd0, only if Petitboot mounts UFS2 (`ufstype=ufs2`)
  and finds kboot.conf there, to check read-only from its shell; kernel updates would then go
  through GameOS. (1) first.
- **Risk:** deleting the file in GameOS hands its blocks back to GameOS; the boot check stops Linux
  from writing there, but the file must be treated as "do not touch". Rollback: delete the file.

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
| U3 | U | Petitboot shell, read-only: `which ps3hvc_hvcall ps3stor_region`, `ls -l /dev/ps3hvc /dev/ps3stormgr`, `grep ps3d /proc/partitions` | **Both tools present, or stop** (then W builds them before going on) | 5 min |
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
- Ours: `firmware/petitboot/dtbImage.ps3.bin` strings, `linux/patches/ps3/0010` and `0150`, the
  `.457` kernel config (`EFI_PARTITION=y`, `MSDOS_PARTITION=y`, `EXT4_FS=y`, `PS3_STRGMNGR` off),
  docs/OTHEROS-PATCHES.md, docs/FLASH-SYSCON.md.
