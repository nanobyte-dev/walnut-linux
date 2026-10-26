# GameOS's disk from Linux, and the SSD image

Linux on our OtherOS++ console sees GameOS's whole internal disk. We read GameOS's partitions
read-only (`ps3-gameos-mount`), and we run Linux from a 16 GiB file that GameOS keeps on its own
partition `dev_hdd0`: since 2026-10-19 the PS3 boots its root from that file (Phase B, kernel
`.472`), with the USB disk as the fallback. The kernel guard that limits Linux's writes to the
file's blocks (local/0045) is built and in the series but not booted yet; its PS3 checks are in
["On the PS3"](#on-the-ps3). The other route, a Linux region carved out of the disk by a reformat,
was researched and not taken: [research/HDD-REGION.md](research/HDD-REGION.md).

Terms like lv1, OtherOS++, Petitboot, webMAN and dev_hdd0 are in the [glossary](README.md#glossary).

## The disks Linux sees

Measured 2026-10-18 on kernel `.419` (sizes in 512-byte sectors where it matters):

| device | lv1 region | size | `blkid` | GameOS name |
|---|---|---|---|---|
| `/dev/ps3da` | 0 | the whole drive, 1875385008 sectors (960 GB) | (none) | everything: VFLASH, the region table, every OS |
| `/dev/ps3db` | 1 | 957 GB (1870666384 sectors) | UFS (UFS2) | `dev_hdd0`: games, game data, saves, user data |
| `/dev/ps3dc` | 2 | 2 GB (4194296 sectors, 2047 MiB) | vfat | `dev_hdd1`: install cache, temporary data |
| `/dev/ps3dd` | 3 | not present on this console | | would be an OtherOS region ([research/HDD-REGION.md](research/HDD-REGION.md)) |

On a NOR console like ours VFLASH (`dev_flash`, `dev_flash2` with xRegistry, `dev_flash3`, and
the OS area with Petitboot and the boot flag) also lives on this disk, from sector 8 to about
sector 524320 (256 MiB), before region 1. It is not a region of its own; only `ps3da` covers it.
Our kernel's ps3/0010 (from the psx-place set) names each disk after its region index.

**The internal disk is an SSD.** The console's stock 2.5" HDD was replaced by a 960 GB SSD. We
measured it at 100 MB/s sequential and 0.1 ms median random 4K reads, against the USB root disk's
27.8 MB/s and 20 ms (2026-10-18). A stock PS3 has the HDD, so results on the SSD root
(`root=/dev/mapper/ps3root` in bench.txt) are better on disk-bound phases than a stock console
would see; [TESTS.md](TESTS.md) says the same at its top.

## Why no key is needed

GameOS encrypts its disk regions (XTS-AES-128 on Slims, keys in the console's secure storage).
Under OtherOS++ the regions are reached through the hypervisor's storage path, which decrypts on
the way: Linux reads plain UFS2 and FAT, the same way it reads its own regions. Nothing in this
project touches a key. The other side of it: the disk is unreadable in another console or a PC,
our image file included.

## Read-only, on purpose

GameOS's partitions must never be written from Linux (PS3 safety rules: nothing beyond installing
Linux). The layers:

- The kernel is built with `CONFIG_UFS_FS=m` and `CONFIG_UFS_FS_WRITE` not set
  (`packages/patched/kernel/linux/config/ps3.config`), so the module has no write code at all. Linux's UFS2 write support
  is experimental and deliberately not built.
- `ps3-gameos-mount` mounts with `ro` and refuses to mount otherwise.
- The vfat partition is mounted `ro` too. The kernel can write vfat, so there it rests on the mount
  option and the helper.
- The kernel guard (local/0045, [below](#the-kernel-guard)) makes `ps3da` read-only for good and
  lets `ps3db` be written only inside the image file's blocks. Not booted yet.

A read-only mount of a filesystem that GameOS left dirty replays and repairs nothing. If GameOS was
powered off hard, files may be inconsistent: copy what you need, don't trust it.

## `ps3-gameos-mount`

In package `ps3-system`. It mounts both partitions read-only on demand and is not run at boot:

```
sudo ps3-gameos-mount        # /mnt/gameos/hdd0 (ufs2) and /mnt/gameos/hdd1 (vfat), ro,noatime,nodev,nosuid,noexec
sudo ps3-gameos-mount -u     # unmounts both
```

It checks `blkid` first and leaves a device alone if its type isn't the expected one, and refuses
any read-write request. A vfat mount that fails with `iocharset=utf8` is retried without it (file
names with non-ASCII characters then show as `?`).

While the SSD image's device-mapper disks exist (Phase A's `ps3ssd`, or the SSD root itself), it
can't mount `dev_hdd0`: device-mapper holds `/dev/ps3db` exclusively and the mount gets EBUSY. On
the SSD root that means no `dev_hdd0` mount at all; on the USB root, `ps3-imgmap remove ps3ssd`
first.

Whether to mount at boot (read-only) was asked on 2026-10-18 and is still open (PLAN D9, 4.5).

## What's there

Installed games and game data under `game/` and `home/` of `dev_hdd0`: PS3 games, saves in
`home/<user>/savedata`, trophies, and the `photo`, `video` and `music` the user put there. On our
console the file system is 864 GB with 22 games. Saves and game data are in GameOS's own formats,
some encrypted per console. Copying them out works; using them from Linux is not part of the
project.

## What was verified, and what not yet

- `dev_hdd0`: mounted read-only on the 6.12.111+ps3.419 boot (2026-10-18), an 864 GB file system
  with 22 games. Phase A's map on the `.472` boot read the image file through the same mount.
- `dev_hdd1`: the first try, on 6.12.111+ps3.433, failed: `mount -o iocharset=utf8` found no
  `nls_utf8` ("missing codepage or helper program"). Fixed from 6.12.111+ps3.457: the kernel has
  `CONFIG_NLS_UTF8=y`, `NLS_CODEPAGE_437=y` and `NLS_ISO8859_1=y` (vfat's defaults) built in, and
  the helper retries without `iocharset`. Not checked yet on a kernel with the fix (`.457` or
  later, so `.472` qualifies): `sudo ps3-gameos-mount` mounts both, `ls /mnt/gameos/hdd0` and
  `/mnt/gameos/hdd1` list files, `dmesg | tail` shows no UFS/FAT errors, `sudo ps3-gameos-mount -u`
  unmounts both. It has to run from the USB root, with no `ps3ssd` device made.

## The disk image inside dev_hdd0

The idea: GameOS holds an ordinary file on `dev_hdd0`, and Linux uses the file's blocks as a disk.
No reformat, no change to the region table, nothing of GameOS's lost, and the size is ours to
pick. Rolling back is deleting the file in GameOS.

How it works:

1. The file is written from GameOS's side (over webMAN MOD's FTP). Every 4 KiB block carries a
   stamp with its own offset, so Linux can tell the file's blocks from anything else.
2. Linux mounts `dev_hdd0` read-only, asks the UFS driver for the file's block list (FIBMAP: our
   6.12 UFS driver has `ufs_bmap`, fs/ufs/inode.c), and reads every block back through that list
   from `/dev/ps3db`, checking each stamp.
3. Device-mapper (dm-linear) turns exactly those blocks of `/dev/ps3db` into a disk for ext4 and a
   swap device. Linux writes only inside the file's data blocks, never UFS metadata; GameOS sees an
   unchanged file of the same size. The two OSes never run at the same time.
4. At every boot the list is checked again ("recheck"): if the file was deleted, rewritten or moved
   by a GameOS fsck, the disk isn't made.

The file must be a whole number of UFS blocks (no tail fragment) and have no holes: a hole has no
blocks to map. Deleting or rewriting the file in GameOS hands its blocks back to GameOS; the recheck
stops Linux from writing there afterwards, but in GameOS the file is "do not touch".

It went in two steps. Phase A (2026-10-18/19) kept root on USB and put game data and the VRAM
swap's spill on the image; Phase B (2026-10-19) moved the whole root onto it.

## Test drive, Phase A

A 16 GiB stamped file uploaded to `dev_hdd0` from the PC over webMAN MOD's FTP, mapped and verified
from Linux, then used as an ext4 disk for game data and as the VRAM swap's spill, with root still on
the USB disk. The point (the user, 2026-10-18): run the benchmarks from the internal SSD and see how
much faster loads get and how many stutters go. It ran on 2026-10-19 (batch ssd1, below). The Phase B
install has since reformatted the `ps3ssd` disk as the SSD root, so Phase A's game copies are gone.

### Tools

In `tools/ssdimg/`:

- `stamp.py` (PC): streams the image to `/dev_hdd0/linux/ssdtest.img` with FTP `STOR` from a
  generator (nothing on the PC's disk), prints MB/s, checks the remote size with `SIZE` at the end;
  `dry-run` and `check` for tests. Refuses to overwrite an existing file without `--force`.
- `ps3-imgmap` (the PS3, root): `map`, `verify`, `recheck`, `create`, `remove`, and `export-boot`
  for Phase B (its `--help` has the details). Everything up to `create` only reads `/dev/ps3db`;
  the tool never opens `ps3db` or `ps3da` for writing.
- `test_ssdimg.py` (`make test`, suite `ssdimg`) and `vm-e2e.sh` (`make test-slow`, suite
  `ssdimg-vm`).
- `bench/ssd-arm.sh`: the bench's SSD arms ([below](#the-ab-in-the-bench)).

**Stamp format** (version 1, stamp.py's docstring): one stamp at the start of every 4 KiB block,
the rest of the block zero. 8 bytes magic `PS3IMGv1`, 16 bytes instance UUID, the block's offset in
the image as 8 bytes big-endian, a CRC-32 of those 32 bytes as 4 bytes big-endian. The first MiB is
the image's header area: stamped too, never mapped into a Linux disk, so its stamps stay and
`create` reads them back through the extents at every activation (proof that the list still points
at our file). A real header there is designed but not built ([below](#the-image-header)).

**FIBMAP's unit** (from build/src/linux/linux-6.12.111). `ufs_fill_super` ends with
`sb_set_blocksize(sb, uspi->s_fsize)`, so the super block's block size (FIGETBSZ, statvfs
`f_bsize`) is the fragment size. FIBMAP calls `ufs_bmap` =
`generic_block_bmap(mapping, block, ufs_getfrag_block)`, whose argument is a logical fragment
number (`fragment >> s_fpbshift` is the block, `fragment & s_fpbmask` the fragment in it) and whose
result is the block's address from the inode or indirect block plus that fragment, plus `s_sbbase`
(0 for UFS2): a physical fragment number. Input and output are in units of fs_fsize, and a sector of
`/dev/ps3db` is fragment x fs_fsize / 512 (UFS starts at sector 0 of the region). `map` checks this
from three sides before trusting it: FIGETBSZ = statvfs = the super block's fs_fsize (read from
64 KiB into ps3db), each block's first and last fragment are contiguous, and the file is whole
blocks without holes. FIBMAP's int return limits it to 2^31 fragments; dev_hdd0 has 1.87 G sectors,
fine for any fragment size. On powerpc the ioctl number is 0x20000001 (c87331f: x86's encoding gave
ENOTTY on the PS3).

**Device-mapper and a mounted disk** (seen in the KVM guest): device-mapper claims its disks
exclusively, and so does a mounted file system, so `dmsetup create` over a mounted `/dev/ps3db`
fails with EBUSY. `create` therefore wants dev_hdd0 unmounted, and while `/dev/mapper/ps3ssd`
exists `ps3-gameos-mount` can't mount dev_hdd0. Phase B's initramfs does the same: mount
read-only, recheck, unmount, create.

### Safety points

- Nothing writes before `verify` passed. `create` refuses without a recorded pass of that exact
  list (sha256), without a `recheck` in the same boot, with dev_hdd0 mounted, or when the header
  area's stamps don't read back through the list. A failed verify or a changed recheck voids the
  map.
- `create` runs `blockdev --setro /dev/ps3da` first (the whole disk, VFLASH included, read-only
  until the next boot) and checks it took.
- `create` and everything after it only with the user's explicit go; it asks unless `--yes`.
- Before that go: the XMB Backup Utility (saves at least) to a FAT32 USB drive
  ([research/HDD-REGION.md, "Backup and restore"](research/HDD-REGION.md#3-backup-and-restore-of-gameoss-data)).
  The mapping writes only inside the file's blocks; the backup is for a bug in that.
- `recheck` catches a deleted or rewritten file before any write (a different inode, size or block
  list), and the header check catches a list that went stale anyway. Never answer a GameOS
  "restore the file system" prompt without a recheck after it.
- The state (`/var/lib/ps3-imgmap/`: the map, the verify and recheck records) is on the USB root.
- On a kernel with the guard, `create` also registers and seals the devices' ranges; see
  [the kernel guard](#the-kernel-guard). A guard kernel needs the new `ps3-imgmap` (after 1abb2a1).

### Tested on the PC

2026-10-18. Unit tests of the format, the stream, extent merging, the unit conversion, and every
refusal (hole, tail fragment, read-write mount, no `ufstype=ufs2`, another file system, another
device, the unit cross-check, an unstamped file, an edited map, create without verify, without a
recheck in this boot, with ps3db mounted, a stale map whose header area no longer reads back), on a
fake FIBMAP layer with a file standing in for the disk (real O_DIRECT reads).

Then the real kernel: `vm-e2e.sh` makes big-endian UFS2 images with NetBSD's makefs (Debian's
source rebuilt with the super block at 64 KiB: as packaged it puts UFS2's at 8 KiB,
`#if 0 /* XXX makefs is used for small filesystems. */` in ffs/mkfs.c, where Linux doesn't look),
boots the PC's own kernel (7.2.10, x86_64) in a KVM guest and runs map, verify, recheck, create,
mkfs.ext4 and writes on both mapped devices. It then compares every sector outside the file's
mapped blocks with the original image (identical: UFS metadata, the other files, the header area),
mounts the UFS again (other files unchanged) and rechecks (unchanged). Both geometries passed
(block 16384 / fragment 2048 and 32768 / fragment 4096).

### What ran on the PS3

2026-10-19, kernel 6.12.111+ps3.472, USB root:

- The upload: 17179869184 bytes in 784 s, 20.9 MB/s, to webMANftpd 1.47.50b MOD; `SIZE` reported
  the full 16 GiB. UUID `e6cfa9ac-e983-43ec-8ae1-c22633fe3b38`. A first try stopped at 1.09 GiB
  when webMAN's self-update hung; the second ran with `--force`.
- GameOS's UFS2 geometry: block 16384, fragment 4096. The 16 GiB file is 513 extents (one per
  32 MiB, where UFS2 puts an indirect block).
- `map` took 79 s in Python. `verify` passed all 4194304 blocks, `recheck` passed.
- With the user's go: `create` made `ps3ssd` (15 GiB) and `ps3ssd-swap` (1 GiB), mkfs.ext4, and
  five games' data copied (1.6 GiB, `diff` clean). Luanti's `DATA_DIRS` cover only 16 MB: its
  load's disk wait is its mapped binaries and the world.
- `ps3-vswap` on its dm backing (`PS3_VSWAP_DM=1`) came up as expected, and the `SWAP=ssd` table
  switch under it ran in batch ssd1 (its first real run).

### The GameOS session (the user)

0. Before it (us, on Linux, the user not at the console): `sudo apt install dmsetup` (it is in the
   image now); ps3-system with `ps3-vswap`'s dm backing deployed and
   `echo PS3_VSWAP_DM=1 | sudo tee /etc/default/ps3-vswap`. At the next boot the VRAM swap's backing
   becomes the device-mapper device `ps3vswap-back` over the USB swap partition's first GiB, which
   is what lets the swap arm switch in the same boot; without it everything is as before. Check that
   `modinfo -k <release> dm-mod` and `modinfo -k <release> ufs` find both modules for the kernel
   that will boot (`BLK_DEV_DM=m`, `UFS_FS=m`), then `make bench-push`.
1. GameOS: the Backup Utility. In webMAN MOD's setup page (`http://10.0.0.77/setup.ps3`) the FTP
   server must not be disabled. 16 GiB plus a margin free on the internal disk (the XMB's System
   Information, or webMAN's file manager on `/dev_hdd0`).
2. On the PC:

       python3 tools/ssdimg/stamp.py upload --size 16G
       # defaults: --path /dev_hdd0/linux/ssdtest.img --host 10.0.0.77 --port 21 (any login)

   It prints the UUID, progress and MB/s, and ends with `remote size 17179869184: complete`
   (784 s on our console). A broken upload leaves a short file: run it again with `--force`.
3. As always before Linux: webMAN's fan in SYSCON mode (or fixed at 60% or more), the OtherOS flag,
   Petitboot, the usual USB-root entry.

### On Linux (us, with the user's go where it says so)

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

Every later boot of the USB root (the map is reused; GameOS may have run in between):
`sudo ps3-gameos-mount`, `recheck $M`, `sudo ps3-gameos-mount -u`,
`create $M ps3ssd --swap-mib 1024 --yes`, mount. At the end of a session: `ssd-arm.sh reset`
(run.sh does it when it ends), `sudo umount /mnt/ps3ssd`,
`sudo python3 /var/tmp/ps3-imgmap remove ps3ssd`.

On a kernel with the guard, `create` registers and seals the two devices' ranges of ps3db, once per
boot. A `remove` and a second `create` in the same boot is refused (sealed earlier in this boot);
the way back is a reboot. Before `create` every write to ps3db fails, after it every write outside
the image's blocks.

Since the Phase B install, `/dev/mapper/ps3ssd` holds the SSD root's file system (label `ps3root`).
Mounting it from the USB root is how the install and its fix-ups work; Phase A's game copies and the
`GAMEDATA=ssd` arm's data are no longer on it.

### The A/B in the bench

Two arms, switched between runs by `bench/ssd-arm.sh` (run.sh calls it before each run's
quiet) and recorded by client.sh in bench.txt (`gamedata=`, `gamedata_dirs=`, `swap=`, `swaps=`;
report.py tags `base@gdssd`, `base@gdssd+swssd`):

- `GAMEDATA=ssd`: the recipe's `DATA_DIRS` (BananaBread `/usr/share/games/cube2`, OpenArena
  `/usr/share/games/openarena`, Luanti `/usr/share/luanti`, SuperTuxKart
  `/usr/share/games/supertuxkart`, ClassiCube `~/.local/share/classicube/{texpacks,audio}`) are
  bind-mounted from `/mnt/ps3ssd/games/<dir>` for the run and unmounted for runs without it. The
  quiet drops the page cache after the bind as before every run. The binary, the libraries, Mesa's
  shader cache (`~/.cache`) and Luanti's world (`/var/tmp`) stay on the USB disk.
- `SWAP=ssd`: the VRAM swap's spill goes to the SSD. On the USB root the swap partition is not a
  swap of its own: it is the VRAM swap's backing (`ps3-vswap`: `ps3vswap0` at priority 300, zram at
  100), written when VRAM is wanted back and read on swap-in, and the backing is set once per boot.
  So the arm swaps the table under the device-mapper backing `ps3vswap-back`: VRAM swap off (pages
  back in), `dmsetup suspend/load/resume` onto `/dev/mapper/ps3ssd-swap`, `mkswap`, on again at
  its priority. zram and the VRAM swap keep their places; a third, lower-priority SSD swap would
  only be used once both are full, so it would measure nothing. A switch stops the desktop and
  empties the swaps first (`ps3-bench.sh quiet`).

client.sh refuses a run whose arm isn't in place, and a plain run with binds or the SSD swap left
over. The batch as planned (5 games x 3 arms x 3 rounds, about 3-4 hours, `tools/bench-cam.sh` as
for every run):

    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench supertuxkart lean 3
    STALLS=1 PERF_LOAD=1 ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd" /var/tmp/bench/run.sh bench classicube base 3
    ps3-bench.sh desktop
    python3 /var/tmp/bench/report.py --load --stalls /var/tmp/ps3-bench/<run dir>...

What to compare ([PROFILING.md](PROFILING.md), "Start-up profiling"; [STALLS.md](STALLS.md)):
`load`, `load_disk_s`, loadprof's split of the load; `stalls_warm`, `warm_disk_s`; in the window
`stall_disk_s`, `stall_swap_s`, 1% lows and stalls first (smoothness over fps). `ROTATE=1`
(4954f90) rotates the arms' order each round against order bias.

### Result, batch ssd1

2026-10-18 22:10-23:25Z, kernel 6.12.111+ps3.472, USB root, same boot, `STALLS=1 ROTATE=1`, results
commit 9e6bc57 (row "batch ssd1" in bench/results). Only BananaBread (9 runs) and part of
OpenArena ran before the batch was stopped for Phase B; Luanti, SuperTuxKart and ClassiCube never
got their SSD arms. Medians with [min..max]:

| BananaBread base, n=3 | USB | GAMEDATA=ssd | ssd + SWAP=ssd |
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

- Moving the game data alone did not make the loads CPU-bound. The recorder's disk class counted
  only the USB disk (sda) then, and that wait did not move: BananaBread (Debian's cube2) 5.5 -> 4.9-5.4 s,
  OpenArena 6.3 -> 6.4-6.5 s. The game data was only part of the load's reads from sda
  (BananaBread 93 -> 76 MB, OpenArena 144 -> 93 MB, one run each). The rest is the binary,
  libraries, Mesa, the shader cache and Xorg, which the arm leaves on USB, and those reads cost the
  same 5-6 s. Hence Phase B: the whole system on the SSD.
- BananaBread: load -6% (SSD) and -3% (SSD + swap) in the medians, but the spread is the first
  round. With the arms rotated, round 1 had USB first and cold (19.3 s), and round 3 reads
  15.4 / 15.4 / 15.3 s for USB / SSD / SSD + swap. The SSD arm's stalls (1.0 s against 1.6 s) and
  1% low (26.4 against 24.1) look better but sit inside the USB runs' own spread (0.7-2.4 s,
  23.1-25.9); SSD + swap equals USB. No effect on steady fps (38.5-39.3). Verdict: no proven gain,
  perhaps a small one for the game-data arm; n=3.
- OpenArena: the game's load is about 3 s shorter (-28%) in all four SSD runs, against 9.8 and
  11.6 s on USB, and the map reloads' stall seconds drop 52 -> 41-45 (-17%). Start to first frame
  (`load_end`) improves by about 3.6 s (28.1 s on USB, 23.5-25.7 s on SSD). Gameplay was clean in
  every arm (stalls 0-3, 1% low 23-25), so there was nothing for the SSD to fix there. n=2/1/3:
  indicative only.
- SSD + swap can't be told apart from SSD alone: swap stall seconds were 0.0-0.6 in all arms, the
  spill is rarely read back in these two games. Swap-outs were the same in all arms.
- ALSA underruns in BananaBread (81-187 per run) are the known pattern, not arm-related (SSD
  113-165, USB 153-187, SSD + swap 81-168).
- Excluded: `ps3-bench-20261018T230542Z-openarena-gl2min` (SSD arm, a key press at 195 s:
  interfered, its window 94 s), and a last OpenArena run at about 23:26Z (killed midway, its arm
  reset under it; no run directory pulled). The 16 other runs have a picture (bench-cam photos at
  60 and 150 s).

## Phase B: the whole system on the SSD

The user's decision (2026-10-19): skip a game-data-only stage and put the whole root on the SSD
image. Petitboot loads the kernel and a small initramfs from the USB disk as before; the initramfs
rechecks the image's block list from dev_hdd0 (read-only), turns the file's blocks into
`/dev/mapper/ps3root` and `/dev/mapper/ps3swap`, and starts the system from there. The USB entries
stay as they are: picking one in Petitboot boots the USB root exactly as before, which is the
fallback for everything here.

**State.** Running. The SSD root first booted on 2026-10-19 at about 03:35 from the entry
`ssd-6.12.111+ps3.472`: root `/dev/mapper/ps3root`, the VRAM swap's backing `/dev/mapper/ps3swap`,
the boot's recheck 6.3 s, the boot 24.7 s. The benchmark batches ssdroot1 and ssdroot2 ran on it
([TESTS.md](TESTS.md)): loads 39-61% shorter, the disk's share of a load from about 40% to 11-26%,
BananaBread's gameplay stalls from 1.6 s to 0.05 s
([result](#result-the-ssd-root-batches-ssdroot1-and-ssdroot2)). The comparison against the USB root in
alternating boots is still to run ([below](#the-benchmark-usb-root-against-ssd-root)). Kernel `.472` has no guard, so on this boot the
initramfs logs `kernel has no write guard` and ps3db's writes are not limited to the image's
blocks; it does set `ps3da` and `ps3dc` read-only for the boot.

### What was built

All PC-side, commits fe191f7, df40afd, c9a7304 and 195498a; the guard's registration came in
1abb2a1.

- **`tools/ssdimg/ps3-imgmap-boot.c`**: the boot half of `ps3-imgmap` in C, static (glibc, 0.9 MB
  for ppc64), no Python, no dmsetup, no udev. Its commands:
  - `check MAP`: the boot map's own sha256 line, the extent list's sha256 by the same digest
    ps3-imgmap keeps, extents in order, no overlap, past the UFS2 super block, the layout.
  - `tables MAP root|swap`: the dm-linear lines, exactly as `ps3-imgmap create` builds them.
  - `recheck MAP MOUNTPOINT`: the mount is ufs, `ro`, `ufstype=ufs2`, from `/dev/ps3db`; the super
    block's block and fragment size, FIGETBSZ and statvfs agree with the map; the device's size,
    the file's size and inode, its first block still the stamp of offset 0; then FIBMAP of the
    first and last fragment of every block, merged, compared extent by extent with the map.
  - `create MAP`: needs this boot's recheck of this map and `/dev/ps3db` unmounted; `BLKROSET` on
    `/dev/ps3da` and its sysfs `ro` read back; the header area's 256 stamps read back through the
    extents with O_DIRECT; then DM_DEV_CREATE, DM_TABLE_LOAD, resume, and DM_TABLE_STATUS (the
    kernel's live table compared target by target with what was loaded); the size checked;
    `/dev/mapper/<name>` made; anything half made is removed again. Then the guard's registration
    ([below](#what-the-boot-tools-do)).

  Any difference refuses, exit 2. The device names are fixed in the code (no test seams); FIBMAP,
  BLKROSET and the dm ioctl numbers come from the target's kernel headers (ppc64's FIBMAP is
  0x20000001, checked). Why the dm ioctls and not dmsetup: dmsetup is dynamically linked
  (libdevmapper, libudev, libselinux, libpcre2 and the loader would go into the initramfs) and
  wants udev cookies or `--noudevsync`. The ioctls are about 150 lines, the table goes from the
  verified extents into the kernel without a text round trip, and reading back the live table
  checks the result in the same program.
- **`ps3-imgmap export-boot MAP --out FILE`**: the boot map, a text file of "key value" lines
  (image path on dev_hdd0, device, its size, inode, file size, geometry, byte order, UUID, the
  extent list's sha256, `swap_sectors` from the created record so the boot makes the same two
  devices Phase A made), the extents, and a closing sha256 line over all of it. Refuses a map
  without a verify pass. 12 KB for the PS3's 513 extents.
- **The initramfs**: `make initrd-ssd IMGMAP=<copy of the PS3's map.json>`
  (`tools/ssdimg/build-initrd.sh` in the builder; `INITRD_RELEASE` picks the kernel, default the
  newest release kernel in build/debs/linux). It holds `/init` (`tools/ssdimg/initrd/init`),
  Debian's busybox-static 1.38.0-3 (pinned by sha256 in tools/ssdimg/upstream.mk; links only for the applets the
  init and a rescue shell use, the destructive ones like mke2fs only as `busybox mke2fs`), the
  static tool, `ufs.ko` and `dm-mod.ko` from the release's linux-image deb (vermagic checked, debug
  info stripped), the boot maps in `/ps3ssd/maps`, and `/etc/ps3ssd-initrd`. A reproducible newc
  cpio (`tools/ssdimg/mkcpio.py`: owner root, fixed mtimes, `/dev/console` included), xz with CRC32
  and a 1 MiB dictionary as the kernel wants. About 1.27 MB (the one built from the PS3's map for
  `.472` is 1,273,592 bytes; 3.9 MB unpacked: busybox 2.57 MB, the tool 0.93 MB, the modules
  0.35 MB); two builds give the same sha256. Next to it in build/initrd: `.manifest` (sha256 of
  every file and of the deb's module files), `maps/`, `ps3-imgmap-boot.ppc64`.
- **`/init`**: proc, sys, devtmpfs, /run; checks its kernel release; finds the boot map whose image
  is `ps3ssd.image=`; loads ufs and dm-mod; waits for `/dev/ps3da` and `/dev/ps3db`
  (`ps3ssd.timeout`, 30 s); `blockdev --setro` on ps3da and ps3dc (checked in sysfs); mounts ps3db
  `ro,ufstype=ufs2`, `recheck`, unmounts; `create` with the name from `root=/dev/mapper/<name>` and
  `ps3ssd.swap` (ps3swap); mounts root with `rootfstype=`, `ro`/`rw`, `rootflags=`; moves /dev,
  /proc, /sys, /run into it; `switch_root` to `init=` (/sbin/init). The log stays readable as
  `/run/ps3ssd/init.log`, with the recheck's time, and the tables in `/run/ps3ssd/*.table`. The
  root's fsck is systemd's, as on the USB disk (systemd-fsck-root on the read-only root, then
  systemd-remount-fs). Any failure: a framed message on tty1 and every active console (what failed,
  what may have been written, `reboot -f` and the USB entry to go back), the log's tail, and a
  busybox shell on each. `ps3ssd.break=1` gives a shell after the devices are made, before root is
  mounted.
- **`ps3-boot` 0.498** (in ps3-dev, not stable): `ps3-update-kboot` appends the entries of
  `/etc/kboot.d/*.conf` as written, after the kernels' entries, so a kernel install or removal keeps
  them. Left out, as a comment in kboot.conf and a message: a label already taken, a line that isn't
  an entry or is a global (`default=` and the like), an entry whose image or `initrd=` is missing on
  this disk (it comes back by itself once the files are there). Host tests: suite `ps3-boot`.
- **`tools/ssdimg/ssd-install-image.sh`** (the PS3, root, from the USB root; `--dry-run`
  everywhere; it unmounts the new root again if anything fails midway):
  - `check` (read-only): the USB root is `/`; ps3ssd and ps3ssd-swap exist and their live tables
    are the ones `ps3-imgmap create` wrote from a verified map rechecked this boot; the initramfs
    matches its manifest, its boot map is that map and gives the same two tables; the kernel
    release is in `/boot` with the same ufs and dm-mod files; the guard's state
    ([below](#what-the-boot-tools-do)).
  - `install`: a new ext4 labelled ps3root on `/dev/mapper/ps3ssd`, inode tables and journal
    written at mkfs (no lazy init later). It asks before erasing a file system that is there (e.g.
    Phase A's game copies); `--reuse` keeps an existing ps3root and moves its contents to
    `.old-<date>`. The image tar is extracted with owners, modes and xattrs; then fstab, hostname,
    hosts, SSH host keys, Bluetooth pairings, NetworkManager connections, `/home/tibi`,
    `/var/tmp/bench`, `/var/tmp/ps3-bench`'s state without its run directories,
    `/var/lib/ps3-imgmap`, the `ps3ssd-udev` unit, and the package difference list.
  - `boot`: `/boot/initrd-ssd-<release>.img`, `/etc/kboot.d/ssd.conf`, `ps3-update-kboot`.
- **The SSD root's fstab**: `/dev/mapper/ps3root / ext4 defaults,noatime 0 1`,
  `/dev/mapper/ps3swap none swap sw,pri=200,noauto 0 0` (ps3-vswap takes the first swap line as the
  VRAM swap's backing, so it needs no new setting; `/etc/default/ps3-vswap` is not copied, since
  `PS3_VSWAP_DM` belongs to Phase A's swap arm), tmpfs `/tmp`, and the USB root at `/mnt/usbroot`
  `noauto`. No USB swap.
- **`ps3ssd-udev.service`** (in the SSD root, enabled in sysinit, only when `/run/ps3ssd/init.log`
  exists). Devices made without udev get no `/dev/mapper` link from udev, and with dmsetup's
  55-dm.rules the coldplug "add" marks them not ready for systemd (SYSTEMD_READY=0,
  99-systemd.rules). Nothing in the boot waits on them (systemd adds no device dependency for `/`,
  the swap line is noauto, ps3-vswap uses the node), so this is for consistency: a "change" event
  per dm device after the coldplug, which 55-dm.rules treats as the activation, then
  `udevadm settle`.
- **bench.txt** has `root=` (client.sh, d467daf): the run's root device, USB or
  `/dev/mapper/ps3root`. The stall recorder samples the disks under root and the VRAM swap's
  backing (13734bb, ps3-gpu-tools 0.477~dev), so a load's disk faults on ps3db count as disk.

**Boot time of the recheck.** FIBMAP twice per 16 KiB block, about 2.1 million ioctls for the
16 GiB file. In the KVM guest on a 640 MiB file (81,920 FIBMAPs) the Python recheck took 0.49-0.53 s
and the C tool 0.043 s, about 11x less. On the PS3 the boot's recheck took 6.3 s (Python's map: 79 s).

### Tested on the PC

2026-10-19. `make test` suite `ssdimg`: export-boot and its refusals; the C tool's `tables` equal
`ps3-imgmap create`'s table files on the fake layer, also across several extents and a 3 MiB swap;
every refusal of a damaged map (edited byte, truncated, extent moved, overlap, inside the super
block, swap too big, bad path, field order, bad UUID, trailing lines); recheck and create refuse a
map for another device. Suite `ps3-boot`.

`make test-slow` suite `ssdimg-vm`, per geometry (the PS3's 16384/4096, and 4096/512 where the file
splits every 2 MiB: 15 and 2 targets):

- A guest runs the C tool against the real ufs and dm: its tables are dmsetup's for the same map;
  recheck on tmpfs refused; create without a recheck, with ps3db mounted, with another boot's
  recheck, with the devices there: refused; BLKROSET set by the tool itself.
- A second guest runs `ssd-install-image.sh`: check before and after create, a dry run that writes
  nothing, the question before erasing Phase A's ext4, refusals while mounted and for `--reuse` on
  a foreign file system, a real install, a `--reuse` install, `boot` into a fake USB root with the
  repository's ps3-update-kboot.
- Then the guest boots with the initramfs `boot` installed and the kernel line from the generated
  kboot.conf entry (as Petitboot passes it, plus `rdinit=` for a wrapper that gives vda and vdb the
  PS3's names). The installed test root checks the result: root `/dev/mapper/ps3root` ext4
  read-only, the swap device, ps3da read-only, dev_hdd0 unmounted, the log and tables in
  /run/ps3ssd, /dev moved, the install's fstab, hostname, host key, bench state without run
  directories, the udev unit, then 3 MiB written. Every sector outside the image's mapped part is
  still the original's, and the writes landed inside.
- Three refused boots (no map for `ps3ssd.image`, a header block zeroed, the file moved by a rebuilt
  UFS) each stop in the rescue shell with their reason and leave the disk image byte for byte as it
  was.

The ppc64 build runs `check` and `tables` under qemu-user. The KVM test can't cover systemd on the
SSD root (the test root is busybox) or ps3ssd-udev; the PS3 boot did.

### The steps on the PS3

Done on 2026-10-19 for kernel `.472` (the user not at the console until step 7). Repeat them for a
new kernel or a new map. Phase A's devices must exist: `/dev/mapper/ps3ssd` and `ps3ssd-swap`, made
this boot ([Phase A's "every later boot" steps](#on-linux-us-with-the-users-go-where-it-says-so)).

1. On the PS3, Phase A's arms off and the game copies unmounted:

       /var/tmp/bench/ssd-arm.sh reset
       sudo umount /mnt/ps3ssd

2. On the PC, the map and its records, then the initramfs for them (this replaces any build/initrd
   made from another map):

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
   always. Petitboot's countdown default is the label picked last
   ([Petitboot's behaviour](#petitboots-behaviour)), so after this an unattended boot goes to the
   SSD root; picking a USB entry by hand switches back. With `quiet` the initramfs prints nothing
   on success; a failure is on the screen and, at kernel level 2, on netconsole (`make listen` on
   the PC) if the screen stays black.
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
   version on both roots instead. Then `make bench-push` from the PC if bench changed since
   the copy. The same package versions on both roots are what makes their numbers comparable.

What the 2026-10-19 run showed: the install extracted the tar fresh from the release image and
listed 110 packages to align (2 only in the new root); 81 of them were installed on the SSD root at
the USB root's exact versions (debug symbols and old kernels left out), plus ps3-boot 0.498 and
ps3-gpu-tools 0.477~dev20261019001801. One known gap: tar could not set the `security.capability`
xattr of `/usr/lib/powerpc64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-ptp-helper` ("Operation not
supported"), so that helper runs without its capability on the SSD root. The USB root needs the
same ps3-gpu-tools before the USB/SSD comparison, for the disk sampling.

### Falling back, and kernel updates

Any initramfs failure ends in its rescue shell with the reason: `reboot -f`, then a USB entry. A
broken SSD root: a USB entry. Nothing on the USB root changed except `/boot` (one more file) and
`/etc/kboot.d/ssd.conf`. Removing the entry, on the USB root:

    sudo rm /etc/kboot.d/ssd.conf /boot/initrd-ssd-*.img && sudo ps3-update-kboot

The image file itself is untouched by all of this (Linux writes only inside its blocks); deleting
it in GameOS is the rollback of everything.

Kernel updates must go to both roots: the USB root's `/boot` has the kernel Petitboot loads, the
SSD root has its modules. Install the new kernel on the USB root, build its initramfs
(`make initrd-ssd INITRD_RELEASE=<release> ...`), run `boot` again, and install the same kernel
package on the SSD root.

### The benchmark: USB root against SSD root

Scheduled ([TESTS.md](TESTS.md), "Next"; PLAN 1.14), needs the user for the reboots. Root can't
move inside one boot, so this compares across boots: the one exception to same-boot baselines,
made as small as possible. Boots alternate USB, SSD, USB, SSD (two of each), the same commands in
the same order in every boot, `tools/bench-cam.sh` as always, the measurement protocol (desktop
stopped, swap emptied, nothing deployed between the boots), the same package versions on both
roots (step 9) and the same shader cache and bench state (copied by the install). In each boot:

    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench supertuxkart lean 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench classicube base 3
    STALLS=1 /var/tmp/bench/run.sh bench openarena gl2min 3       # gameplay, without the load profile
    STALLS=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    ps3-bench.sh desktop

The first five are the start-up set of batches 16 and 17 (`load`, `load_disk_s`, loadprof's split,
`stalls_warm`, `warm_disk_s`); the last two are gameplay, judged by stalls and 1% lows first
(`stall_disk_s`, `stall_swap_s`). With the SSD root the VRAM swap spills to the SSD too, so this is
"GAMEDATA=ssd+SWAP=ssd" plus binaries, libraries and the shader cache on the SSD. bench.txt's
`root=` tells the boots apart; report.py over each root's run directories gives the medians, and a
difference counts when both SSD boots agree against both USB boots. The ssdroot1 A baseline (the
five games on the SSD root) was compared against batch ssd1's USB arm and batches 16 and 17, a
cross-boot comparison of its own, below.

### Result, the SSD root (batches ssdroot1 and ssdroot2)

2026-10-19 00:22-04:20Z, one boot, kernel 6.12.111+ps3.472, root `/dev/mapper/ps3root` on the
console's internal SSD image (an upgraded SSD; a stock PS3 has a 2.5" HDD and would land between
these numbers and the USB disk's), Mesa .476, ps3rsx 0.443, ps3-gpu-tools 0.477~dev (its stall
recorder samples `ps3db`, so the SSD's disk wait is counted), `STALLS=1`. Results rows 71-75, commit
named in TESTS.md. **Cross-boot and indicative**: the USB numbers are from other boots. For BananaBread
and OpenArena they are batch ssd1's "-" arm (the same kernel, Mesa and ps3rsx, USB root); for Luanti,
SuperTuxKart and ClassiCube they are batch 16 (kernel .433, Mesa .442~dev, ps3rsx 0.249), so a small
difference for those three may be the software. A same-boot comparison needs alternating boots
(above). Medians, [min..max]:

| Game (n SSD / n USB) | Load, the game's own (s) | Load's disk wait (s) | Gameplay stalls (s) | Longest gameplay frame (ms) | 1% low | Steady fps |
|---|---|---|---|---|---|---|
| BananaBread (4 / 3) | **7.1** [6.9..7.2] / 16.9 [15.4..19.3] | **1.85** [1.7..1.9] / 5.5 [4.7..5.6] | **0.05** [0..0.4] / 1.6 [0.7..2.4] | **103** [80..152] / 459 [388..665] | 25.6 / 24.1 | 39.15 / 38.5 |
| OpenArena gl2min (3 / 2) | **6.9** [6.7..7.1] / 10.7 [9.8..11.6] | **2.5** [2.2..2.6] / 6.3 [6.2..6.4] | 0.2 [0.1..0.5] / 0.3 | 199 [122..202] / 111 | 25.6 / 23.1 | **60.4** [60.3..63.3] / 55.9 |
| Luanti (3 / 3) | **25.0** [24.5..25.1] / 40.8 [40.6..42.6] | **3.4** [3.0..3.5] / 9.8 [9.6..10.4] | 3.5 [2.4..5.8] / 2.5 [2.3..3.2] | 724 [724..907] / 358 [272..365] | 19.1 / 17.5 | 46 / 46 |
| SuperTuxKart lean (3 / 3) | **31.1** [31.0..31.5] / 53.6 [53.2..54.9] | **3.3** [3.1..3.5] / 10.9 [10.4..11.5] | 2.0 [0.2..3.6] / 1.8 [1.2..4.1] | 448 [108..513] / 686 [504..768] | 11.1 / 11.4 | 20 / 20 (HUD) |
| ClassiCube (5 / 2) | **7.65** [7.45..8.24] / 14.2 [13.9..14.4] | 1.3 [0.9..1.3] / 3.4 (batch 17) | 0 / 0.05 | 65 [58..74] / 99 | 30.7 / 30.05 | 48.1 then 46.1 / 45.9 |

(SSD / USB in each cell. OpenArena's gameplay columns leave out the map reloads; its window as a whole
had 36.2 s of stalls against 52.0 s, the reloads 40.7 s against 55.7 s. The SSD rows for BananaBread
include two later `-` arms of the `shadow_fast` A/B, same configuration; SuperTuxKart's include the
two off arms of the force-DXT A/B, and three more runs were excluded as interfered.)

What it shows:

- **The loads halve; the disk wait falls to a third or less.** The game's load is 39-55% shorter and
  the main thread's disk wait in it 3-4 times lower, from 5-11 s to 1.3-3.4 s. Start to the end of the
  loading screens (`load_end`): 19.8 to 7.7 s, 28.1 to 14.2, 43.1 to 26.1, 54.3 to 31.5, 13.9 to 7.5.
- **The expectation that loads become CPU-bound held only in part.** The main thread's CPU seconds in
  the load are the same as on USB, and it is now 47-77% on the CPU; 1-3.4 s of disk wait remain
  (a quarter of BananaBread's load), plus sleeps, GPU waits and reclaim
  ([PROFILING.md](PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)). What is left
  is CPU first: zlib, PNG, JPEG and audio decoders in SuperTuxKart, the map inflate in BananaBread,
  Luanti's mapgen.
- **Stalls dropped where they were the disk's, not elsewhere.** BananaBread's gameplay stalls went
  from 1.6 s to 0.05 s and its longest frame from 459 to 103 ms (the USB stalls were file faults).
  The first minute after a load (stall seconds): BananaBread 16.0 to 6.3, SuperTuxKart 18.5 to 8.8,
  OpenArena 28.6 to 20.0; the disk wait in the warm-up 3.5 to 0.85 s (BananaBread), 5.8 to 1.3
  (OpenArena). OpenArena's gameplay was clean on USB already (0.3 s, 0.2 s now; its longest gameplay
  frame 199 ms against 111, n=3 against 2) and the window
  stalls that remain are its CPU-bound map reloads (31 s of CPU stall). Luanti's and SuperTuxKart's
  window stalls did not improve (Luanti 3.5 s against 2.5 s with direct reclaim 1.6 s against 0.4,
  SuperTuxKart 2.0 against 1.8): their stalls are CPU, reclaim and mesh work, and the USB runs
  were an older Mesa and kernel.
- **Frame rates did not move**, except OpenArena (60.4 against 55.9), which fell back to 56 for the rest
  of the boot (the later runs differ only by the stats variable and perf), and ClassiCube (48.1, then 46.1 from 02:00Z against 45.9
  on USB), so neither is credited to the SSD. Frame cost is the PPE and the GPU, not the disk.
- **Swap and VRAM-swap stalls** were small in every arm (0.0-0.3 s, Luanti 0.2 s with one 1.2 s
  run); the SSD swap spill of batch ssd1 stays untested in the games that swap most.
- Not run: the same set on the USB root in this boot's software (Mesa .476, ps3-gpu-tools 0.477~dev),
  which the alternating boots do.

## Petitboot's behaviour

What our Petitboot does with these disks, read from the installed image on the PC (2026-10-18: its
initramfs unpacked and read, nothing from it run). **[fact]** marks a file, string or source line
behind it, **[inference]** our reading, which wants a check from Petitboot's shell.

**Which image.** `firmware/petitboot/dtbImage.ps3.bin` (the user, 2026-10-18: Evilnat's
instructions expect that version). **[fact]** Kernel "2.6.30.10 (glevand@7track.fastsh.it) ... Mon
Jun 27 15:59:15 UTC 2011"; userland "pb-discover (petitboot) -11.06.27-15.34", BusyBox 1.17.3,
kexec-tools 2.0.2, util-linux-ng 2.13.0.1 `mount`, e2fsprogs 1.41.12, uClibc 0.9.32-rc2, all from
glevand's OpenWrt tree (`/home/glevand/petitboot/openwrt/build_dir/linux-ps3_petitboot`). The
initramfs is an LZMA stream with OpenWrt's settings (properties byte 0x6d) at offset 0x428000,
15.25 MB of newc cpio. The kernel has no embedded config (no `IKCFG_ST`), so kernel options below
come from its strings. Petitboot's own source is upstream petitboot at 17d63c9 (2009-08-26, the last
commit before October 2011 in github.com/open-power/petitboot); the binaries' strings match that
tree, so the file names below refer to it.

**The other build.** `~/Downloads/Petitboot FULL (NOR).zip` has the Jun 4 2011 build
(glevand@bastion; the same file as `firmware/petitboot/dtbImage.ps3.bin.archive-org-0f161587`,
sha256 fdf625a3...). **[fact]** Same size and layout; the kernel differs only in build host, date
and link order. The Jun 27 initramfs adds three symlinks (`/etc/fstab`, `/etc/resolv.conf`, `/etc/TZ`
into `/tmp`), a newer debootstrap (`force_md5`, updated suite scripts) and a rebuilt libcrypto;
pb-discover, pb-cui, kexec and busybox are rebuilds of the same sources (stamps 11.05.29 and
11.06.27). Nothing in this section differs between them.

**Which devices it scans.** **[fact]** `/etc/udev/rules.d/99-petitboot.rules` is
`SUBSYSTEM=="block", RUN+="socket:/tmp/petitboot.udev"`, and pb-discover starts with
`udevadm trigger --subsystem-match=block` (discover/udev.c). No name filter: every block device,
whole disks and partitions, gets a mount attempt: `ps3da`-`ps3dc` (and `ps3dd` if a region
existed), `sd*`, `sr*`, the VFLASH regions (`ps3vflash*`). The kernel's ps3disk is a multi-region
one: it prints "%s:%u: %lu accessible regions found" and "%s is a %s (%llu MiB total, %lu MiB
region)", where mainline 2.6.30 has "... accessible regions found. Only the first one will be used"
and "... MiB for OtherOS" (drivers/ps3/ps3stor_lib.c, drivers/block/ps3disk.c at v2.6.30). It also
has ps3vflash, ps3nflash, ps3rom, ps3stormgr and ps3hvc. **[inference]** Whether regions 1 and 2
answer is lv1's decision (the OtherOS++ ACL skip); same lv1 as our 6.12, which sees `ps3db` and
`ps3dc`, so Petitboot should list them too.

**How it mounts.**
- **[fact]** `mount_device()` in discover/device-handler.c runs
  `/bin/mount <DEVNAME> /var/petitboot/mnt/<name> -o ro`: no `-t`, no other option. On failure it
  removes the directory and forgets the device. `/bin/mount` is util-linux-ng's, typing the device
  with libblkid (which knows ufs, vfat, ext2/3/4, iso9660, udf, ntfs, swap and a few more).
- **[fact]** File systems in the kernel: ext2, ext3, ext4, vfat/msdos, iso9660, udf, ntfs (driver
  2.1.29, R/W), ufs; the loop driver; no device-mapper (no `dm_*` symbols). So Petitboot can't see
  our dm disk, and the kernel and initramfs it boots must sit on a disk it mounts by itself.
- **UFS2 (`dev_hdd0`).** **[fact]** 2.6.30's fs/ufs/super.c: without `ufstype=` it prints "You
  didn't specify the type of your ufs filesystem ... default is ufstype=old" and reads the
  superblock at 8 KiB; UFS2's is at 64 KiB (`SBLOCK_UFS2`), read only with `ufstype=ufs2`, which
  2.6.30 has (both byte orders). **[inference]** pb-discover's mount of `ps3db` fails with
  "ufs_read_super: bad magic number", so dev_hdd0 is never in the menu and a kboot.conf on it is
  never read. Check: `dmesg | grep -i ufs` and `grep petitboot /proc/mounts` in the shell.
- **UFS write.** **[fact]** The image lacks "ufs was compiled with read-only support", which 2.6.30
  prints for a read-write mount when `CONFIG_UFS_FS_WRITE` is off, and 2.6.30 forces only ufstype
  old, hp, nextstep and openstep to read-only; UFS2 isn't forced. **[inference]**
  `CONFIG_UFS_FS_WRITE=y`: in Petitboot's shell, `mount -t ufs -o ufstype=ufs2 /dev/ps3db /mnt`
  without `ro` would mount dev_hdd0 read-write. **Always `-o ro,ufstype=ufs2` there.**
- **vfat (`dev_hdd1`).** **[inference]** blkid says vfat, pb-discover mounts it read-only at
  `/var/petitboot/mnt/ps3dc` and reads a kboot.conf on it. Not verified that 2.6.30's vfat takes
  GameOS's FAT. Check: `ls /var/petitboot/mnt/ps3dc`.
- **ext3/ext4 journals.** **[fact]** A read-only mount still replays a journal that needs recovery:
  "EXT4-fs: INFO: recovery required on readonly filesystem." / "write access will be enabled during
  recovery." (the same for EXT3), plus "orphan cleanup on readonly fs". After an unclean Linux
  shutdown, Petitboot's 2011 ext4 code writes to our USB root before we boot.

**Config files and kboot.conf syntax.**
- **[fact]** Per mounted device, the kboot parser (discover/kboot-parser.c) reads the first of
  `/kboot.conf`, `/kboot.cnf`, `/etc/kboot.conf`, `/etc/kboot.cnf` and the upper-case `/KBOOT.CONF`,
  `/KBOOT.CNF`, `/ETC/KBOOT.CONF`, `/ETC/KBOOT.CNF` that opens. **`/boot/kboot.conf` is not on the
  list**, so a separate /boot partition needs `/kboot.conf` at its root. The yaboot parser reads
  `/yaboot.conf`, `/yaboot.cnf`, `/etc/yaboot.conf`, `/etc/yaboot.cnf`, `/suseboot/yaboot.cnf` (and
  upper case). No grub parser; `native-parser.c` is in the tree but not built (rules.mk:
  `parsers = event kboot yaboot`).
- **[fact]** Syntax (discover/parser-conf.c): one `label=image args` per line, `#` comments, the
  first `=` ends the label, surrounding quotes are stripped. Globals: `initrd=`, `root=`, `video=`
  (the last is stored and never used). `default`, `delay`, `message` and `timeout` are ignored. In
  an entry, `initrd=` and `root=` are taken out; the kernel gets `root=<value>` first, then the
  other words as written.
- **[fact]** Paths (`resolve_path()` in discover/paths.c): no `:` means relative to the root of the
  device that holds the kboot.conf. `dev:path` names another device: `ps3dc:/vmlinux` becomes
  `/var/petitboot/mnt/ps3dc/vmlinux`; `uuid=X:/p` and `label=X:/p` go through
  `/var/petitboot/mnt/disk/by-uuid/X` and `by-label`, links pb-discover makes from udev's
  `ID_FS_UUID` and `ID_FS_LABEL`; `scheme://` URLs are fetched (wget for http; the image has no
  tftp client). It never mounts anything for a path: the other device must be one pb-discover
  mounted.
- **[fact]** The udev rule that sets `ID_FS_*` (lib/udev/rules.d/60-persistent-storage.rules) runs
  `/sbin/blkid -o udev -p`; the image only has e2fsprogs' `/usr/sbin/blkid`, which has neither
  option. **[inference]** `ID_FS_UUID`/`ID_FS_LABEL` stay empty, so `uuid=` and `label=` references
  don't resolve; only kernel names (`ps3dc:`, `sda1:`) work, and `sd*` names follow plug order.

**Default and timeout.** **[fact]** Neither comes from kboot.conf. pb-cui keeps them in the OS
area's database on `/dev/ps3flash` (the `ps3vflashf` link from 70-persistent-ps3flash.rules), owner
"petitboot" (ui/common/ps3.c): key 1 the default item, a hash of device name plus entry label
(`pb_opt_hash()`), key 2 the video mode, key 4 the timeout (read-only for pb-cui, set with
`ps3-bl-option -O`, default forever). The countdown starts only when an entry with the stored hash
shows up. Booting any other entry stores its hash as the new default (`ps3_kexec_cb()`).
**[inference]** The kboot parser never sets the device name, so in practice the hash is the label
alone: the same label on two disks matches on both. So the `default=` line `ps3-update-kboot`
writes is ignored by this Petitboot: what boots after the countdown is the entry last picked by
hand, if its label still exists. Entry labels must differ between roots, which is why the SSD
root's entries are `ssd-<release>`.

**Tools in the shell, and what writes by itself.**
- **[fact]** BusyBox applets: ash/sh, the usual file and text tools, dd, df, du, find, grep, sed,
  awk, vi, ps, top, kill, insmod/lsmod/rmmod, ifconfig, route, udhcpc, ping, telnet, telnetd,
  nslookup, switch_root, pivot_root, reboot, poweroff, halt, dmesg, hexdump, strings. Outside
  BusyBox: util-linux `mount`/`umount` (with `-o loop`), `kexec`, `blkid`, `fdisk`, `parted`,
  `partprobe`, `e2fsck`, `mke2fs`/`mkfs.ext2/3/4`, `tune2fs`, `resize2fs`, `badblocks`, `dosfsck`,
  `mkdosfs`, FreeBSD's UFS tools (`mkfs.ufs`, `fsck.ufs`, `tunefs.ufs`, `growfs.ufs`, `fsdb.ufs`,
  `dumpfs.ufs`, `ffsinfo`, `badsect.ufs`), `debootstrap`, `ps3stor_region`, `ps3hvc_hvcall`,
  `ps3-flash-util`, `ps3-bl-option`, `ps3-boot-game-os`, `ps3-dump-bootloader`, `ps3-video-mode`,
  dropbear (ssh, scp), wget, git, vim, nano, screen, tcpdump. Not present: `dmsetup`, `losetup`,
  LVM, mdadm, a tftp client.
- **[fact]** Writes it makes without being asked: pb-cui's database write to the OS area when an
  entry other than the stored default is booted or a value changed, and at exit when the database
  couldn't be read (it formats the database when it can't read one, "formating db"); and the
  ext3/ext4 journal replay above. Nothing else: every mount is `-o ro`, no fsck runs at boot
  (lib/functions/fsck/e2fsck.sh is OpenWrt glue that nothing in the image calls; there is no fstab
  init script and no hotplug2), `/etc/rc.local` is empty.

**What a kernel booted by it receives.** **[fact]** pb-cui runs
`kexec -l --initrd=<initrd> --append=<args> <image>` (ui/common/ui-system.c), so the initrd is
passed. `<args>` is `root=<root> <rest>`, plus ` video=ps3fb:mode:<N>` when it has no `video=` (N
is the video mode in the database; `ps3_kexec_cb()`). Then it runs `/sbin/shutdown -r now`, which
the image doesn't have, so it falls back to `kexec -e`: the init stop scripts never run and the
read-only mounts are simply dropped. kexec-tools 2.0.2 builds the new device tree from Petitboot's
`/proc/device-tree` (lv1's PS3 tree), with the command line in `/chosen/bootargs` and the initrd in
`linux,initrd-start`/`linux,initrd-end`. Petitboot's own dtb (at 0x946000 in the image) has no
`bootargs`. The SSD root's boot confirmed the initrd path on the PS3.

**Where /boot goes.** Given all that, the kernel, the initramfs and kboot.conf stay on the USB
disk (it is what boots today), and the initramfs builds the dm disk itself. The alternatives:

| option | works with this Petitboot | risks |
|---|---|---|
| (a) USB disk, as today | **[fact]** yes, it's what boots now | the USB disk stays plugged in; journal replay after a crash writes to it (our disk, harmless) |
| (b) dev_hdd1 (vfat) | **[inference]** likely: mounted ro, `/kboot.conf` or `/etc/kboot.conf` found, paths relative to it | kernel updates write GameOS's FAT from Linux; GameOS treats it as a cache and may clear it **[unverified]**; 2.6.30 vfat on GameOS's FAT not checked |
| (c) a file on dev_hdd0 (UFS2) | **[inference]** no automatic boot: the mount without `ufstype=ufs2` fails | only by hand from the shell every boot (`mount -o ro,ufstype=ufs2`, then `kexec`); forgetting `ro` mounts dev_hdd0 read-write; changing Petitboot to do it means rewriting the OS area (flash), out |
| (d1) a small /boot partition on the USB disk (ext2 or vfat, no journal) | **[inference]** yes, needs `/kboot.conf` at its root and paths relative to that partition | none new; removes the journal-replay write |
| (d2) `http://` paths in kboot.conf | **[fact]** wget is there | boot depends on tibi-server and the network; no |
| (d3) ps3dd after a region reformat | **[inference]** yes, like the USB disk | the whole reformat route ([research/HDD-REGION.md](research/HDD-REGION.md)) |

(b) is the way to drop the USB disk for good, with webMAN keeping a backup (next section). (d1) is a
small improvement for later ([backlog/platform.md](backlog/platform.md)).

Not done yet, for the user at Petitboot's shell, all read-only: `grep ps3d /proc/partitions`,
`grep petitboot /proc/mounts`, `ls /var/petitboot/mnt/ps3dc`, `dmesg | grep -i -e ufs -e fat`,
`ps3-bl-option -o` and `-p` (timeout and stored default).

## /boot on dev_hdd1 kept alive by webMAN

Proposed 2026-10-18, a draft exists, not tried. GameOS gets control before every Linux session (the
boot flag is set from GameOS and every boot goes through it), so a GameOS-side script can put the
boot files back on dev_hdd1 even if GameOS clears that cache partition. No plugin needed: webMAN
MOD runs `/dev_hdd0/autoexec.bat` (or `/dev_hdd0/boot_init.txt`) at startup and `/dev_hdd0/onxmb.bat`
when the XMB is up (its README; `include/feat/script.h`: `if exist`/`if not exist`, `md`,
`copy <src>=<dst>`, `del`, `wait`).

The draft, `packages/walnut/system/ps3-boot/webman/autoexec.bat` (the user, 2026-10-18; f964401): Linux installs every
kernel and its initramfs to `/dev_hdd1/linux/boot/` and rewrites `/dev_hdd1/kboot.conf` (vfat, at
its root, where Petitboot looks). At every GameOS start the script mirrors dev_hdd1 to
`/dev_hdd0/linux/boot/` when kboot.conf is there, and copies the mirror back when it isn't. Linux
never writes UFS; GameOS keeps the backup. No rescue kernel: the backup is at most one Linux
session old, and a GameOS start always comes between a Linux session and any game that could clear
dev_hdd1. Petitboot would find `ps3dc:/kboot.conf`, entries naming `ps3dc:/linux/vmlinux-...`; the
USB disk's kboot.conf stays as the fallback, with different labels.

What clears dev_hdd1 (researched 2026-10-18): games get it through the system cache API
(`cellSysCacheMount`), which mounts the game's view of `/dev_hdd1` onto its own directory
`/dev_hdd1/caches/<TITLEID>_<cache name>/` and clears the previous game's directory when a game
with a different cache id mounts (RPCS3's reimplementation, rpcs3/Emu/Cell/Modules/cellSysCache.cpp;
an RPCS3 admin: "on a real PS3 that path is mounted as an empty path whenever you boot a game",
forums.rpcs3.net thread-200213). Nothing in that model touches files outside `caches/`, so a
`/kboot.conf` and `/linux/` at the root should survive **[inference: RPCS3 models observed
behaviour, not Sony's code]**. Not covered by that: the Recovery Menu's Restore/format and a
dev_hdd1 GameOS finds corrupt, which recreate it; the webMAN restore covers both. psdevwiki's
Harddrive page only calls dev_hdd1 the "system cache"; "Files on the PS3" adds "system cache: 2GB
fixed size FAT32" and lists path mounts (`CELL_FS_PATH:`, e.g. a directory mounted over
`/dev_bdvd/PS3_GAME`), the mechanism that lets a game's `/dev_hdd1` be a directory. Neither page
says when it is cleared.

Open before it can be used: whether GameOS ever clears dev_hdd1's root in practice (a file left
there through a few GameOS sessions and games tells), whether Petitboot sees ps3dc (its shell's
`/proc/partitions`), the user's webMAN version running autoexec.bat, whether `copy` takes a
directory, and how long the mirror takes. On our side: the kernel guard keeps ps3dc writable for
this, but the SSD root's `/init` sets ps3dc read-only for each boot, which would have to change.

## The internal install

Walnut's default install (docs/release/ALPHA.md M1): the same image file as Phase B, but nothing is
made per console. The PC uploads over webMAN's FTP (`tools/ssdimg/stamp.py install`):

1. the stamped image to `/dev_hdd0/linux/walnut.img` (whole MiB, default 16 GiB, swap at its end);
2. `rootfs.tar.xz` to `dev_hdd1:/linux/install/`, the kernel and the generic initramfs
   (`make initrd-walnut`) to `dev_hdd1:/linux/boot/`, Petitboot's `walnut` entry in `dev_hdd1:/kboot.conf`
   (other lines kept);
3. the known-good copies of those boot files to `/dev_hdd0/linux/walnut-good/` (written only by the PC);
4. last, the install manifest `dev_hdd1:/linux/install/install.conf` (sizes, the image's UUID, the
   tarball's sha256, a closing sha256 line). Without it the initramfs never installs, so an
   interrupted upload can't install half an image.

The generic initramfs (`build-initrd.sh` with no map: kind generic, guard required, for every kernel
with ufs and dm built in) does the rest on the first boot: `disks` (dev_hdd0 is UFS2, dev_hdd1 is
FAT, before anything mounts), FIBMAP of the image on the read-only UFS2 mount, the full stamp verify
(every 4 KiB block, in C), the map and its state saved to `dev_hdd1:/linux/walnut/` (dev_hdd1 is
writable only for those writes), dm-linear sealed by the guard, mkfs.ext4, the tarball checked and
unpacked, the install's fstab and `PS3_BOOT_TARGET=hdd1`, the manifest renamed `install.done` and the
tarball deleted. An interrupted install resumes at the next boot. Later boots are Phase B's: recheck
against the saved map, the header stamps, the guard. Kernel updates: `ps3-hdd1-boot` (walnut-boot)
writes `walnut` and `walnut-prev` to dev_hdd1. The webMAN autoexec (`packages/walnut/system/ps3-boot/webman/autoexec.bat`)
mirrors dev_hdd1's set to `/dev_hdd0/linux/walnut-mirror` and puts it back if GameOS cleared it.

Tested: `tools/ssdimg/vm-e2e.sh` runs the whole first boot, a second boot and two refusals in a KVM
guest (big-endian UFS2, a FAT dev_hdd1, the guard simulation). On the PS3 (2026-10-21, TESTS.md): this
console boots `walnut` from dev_hdd1 with no USB disk, and kernel upgrades through apt put the new kernel
in `walnut` and the previous one in `walnut-prev` on dev_hdd1 by themselves (the generic initramfs's
log tells `ps3-update-kboot` where the system booted from). The first-boot install itself runs in the VM
only so far.

**On the PS3 (M0, with the user at the console; the SSD/USB root stays the fallback):**

1. Boot the guard kernel (6.12.111+ps3.648 or later) from the USB root; run "On the PS3" steps 1-5
   of "The kernel guard" below, write-backs included.
2. At Petitboot's shell, read only: `grep ps3d /proc/partitions`, `ls /var/petitboot/mnt/ps3dc`,
   `ps3-bl-option -o` and `-p`. Then from Linux `sudo ps3-gameos-mount` and check dev_hdd1 reads.
3. In GameOS: `stamp.py install --size 4G --image /linux/walnut-test.img` (a second image name: the
   live SSD root's file is not touched) with the release rootfs, kernel and `make initrd-walnut`.
4. Boot `walnut` from Petitboot with the USB disk still plugged in: the install's console output,
   then the system; `cat /run/ps3ssd/init.log`, `/sys/block/ps3db/write_guard/{sealed,nr_sectors}`.
5. Boot again (recheck path), then without the USB disk.
6. Several GameOS sessions with games in between: dev_hdd1's files still there? Delete them, boot
   GameOS once (autoexec restores from walnut-mirror), boot `walnut` again.
7. A kernel update on the installed system: `walnut` and `walnut-prev` on dev_hdd1, both boot.

## The kernel guard

`packages/patched/kernel/linux/patches/local/0045-ps3disk-write-guard.patch` (07fa26b; in `packages/patched/kernel/linux/patches/series` since
1abb2a1). A kernel with it refuses every write to `ps3db` that no boot tool registered. Phase A's
and Phase B's devices work only once their tool has registered and sealed their ranges, which both
tools do since 1abb2a1. No kernel with it has been built for the PS3 yet; the next `make linux-deb`
gets it. Its PS3 checks are [below](#on-the-ps3).

**With a guard kernel on the PS3, Phase A needs the new `ps3-imgmap`** (copy it to
`/var/tmp/ps3-imgmap` again). An older copy makes ps3ssd and ps3ssd-swap without sealing, and every
write to them fails with EIO (GameOS's data stays untouched, but ext4 on ps3ssd and the `SWAP=ssd`
arm break) until a reboot and a create with the new one. The SSD root needs an initramfs rebuilt
for the guard kernel's release (it then says `guard=required`).

### Policy

By region index, fixed at build time (no parameter, no command-line switch):

| disk | region | policy | why |
|---|---|---|---|
| `ps3da` | 0 | read-only | the whole drive: VFLASH, the region table, every OS |
| `ps3db` | 1 | writes only inside the ranges registered and sealed in this boot | GameOS's dev_hdd0; our image file's blocks are the only part Linux may write |
| `ps3dc` | 2 | writable | GameOS's dev_hdd1. Linux is meant to keep its boot files there (`kboot.conf`, kernels, initramfs; the webMAN autoexec mirrors them to dev_hdd0), so it stays writable (decision 2026-10-19). The risk: a mistyped mkfs on `ps3dc` wipes GameOS's cache partition. **[inference]** GameOS recreates a dev_hdd1 it finds broken, so what is lost is caches and our boot files, which the autoexec restores from dev_hdd0 |
| `ps3dd` and up | 3+ | writable, as before | an OtherOS++ region |

`ps3da` is marked read-only at probe (`/sys/block/ps3da/ro` is 1, mounts are read-only, `write(2)`
gets EPERM). `blockdev --setrw` (BLKROSET 0) is refused with EPERM, and the request check below
refuses its writes too, since the block layer only warns about a bio sent to a read-only disk and
device-mapper can still send one. `blockdev --setro` (what Phase A's `create` and `ps3-imgmap-boot`
do) still succeeds. Nothing turns it writable short of a rebuilt kernel.

### The check

It runs in ps3disk's `queue_rq` for every request, under the driver's lock, before the data is
copied to the bounce buffer and before anything goes to lv1. Reads and flushes without data always
pass. Every other operation counts as a write (fail closed): writes, the data part of a PREFLUSH/FUA
write (blk-mq sends the flush as its own request and the data as a plain write; the queue has no
FUA), discard and write-zeroes (not advertised by ps3disk, refused here too). A write's
[start, start + sectors), what lv1 is given, and every bio of the request have to lie inside one of
the merged ranges; a merged request that is only partly inside is refused whole. Bios arrive with
absolute sectors of the region: partitions and dm-linear are remapped before ps3disk sees them.
Whitelisted writes also need 512-byte device blocks (ps3disk scales positions by the block size).

A refused request fails with EIO and one kernel line, rate-limited to 10 per 5 s per region:

    ps3disk <device>: ps3db: write guard refused WRITE of 8 sectors at 123456: outside the registered ranges

The reasons: `read-only region`, `no ranges sealed`, `outside the registered ranges`,
`block size is not 512`. `write_guard/refused` counts them. At boot each disk logs its region and
policy (`ps3db (region 1 id N, start S, L sectors): write guard: writes only inside the ranges
sealed in write_guard/`): the first boot shows whether index 1 really is dev_hdd0's region.

### The sysfs interface

`/sys/block/ps3dX/write_guard/`; writing needs root (CAP_SYS_ADMIN):

| file | disks | mode | content |
|---|---|---|---|
| `policy` | all | r | `read-only`, `whitelist` or `writable` |
| `refused` | ps3da, ps3db | r | writes refused since boot |
| `ranges` | ps3db | w (0200) | lines of `start sectors` (rules below). Appended until the seal |
| `sealed` | ps3db | rw (0644) | `0` or `1`. Writing `1` sorts the ranges, merges those that overlap or touch, and turns them on; from then on `ranges` and `sealed` refuse with EPERM until the next boot. Writing `0`: EINVAL |
| `nr_ranges` | ps3db | r | ranges registered so far; after the seal, the merged ranges |
| `nr_sectors` | ps3db | r | sectors the merged ranges cover; 0 before the seal |

The `ranges` rules: decimal, 512-byte sectors of ps3db (the units of a dm-linear table's device
offset), blanks between, one range per line, every line ending with a newline (the last one too).
Each write(2) is taken whole or not at all. EINVAL: a malformed line, hex, a sign, an empty range,
one reaching past the end of ps3db, no final newline. ENOSPC: more than 65536 ranges in all. EPERM:
after the seal. sysfs cuts a write longer than 4096 bytes at 4096 without telling the driver: a cut
inside a line gives EINVAL (that is why the final newline is required), a cut at a line end a short
count. So a writer sends at most 4096 bytes of whole lines per write(2) and stops at the first error
or short count.

Until the seal every write to `ps3db` is refused; sealing with no ranges keeps it read-only for the
rest of the boot. A new probe would forget the seal, so ps3disk has no sysfs bind/unbind
(`suppress_bind_attrs`), and the seal takes a reference on the module (ours is built in,
`CONFIG_PS3_DISK=y`, but a modular build can't be unloaded once sealed).

### What the boot tools do

`ps3-imgmap-boot create` in the initramfs and Phase A's `ps3-imgmap create` take the same steps, in
this order:

1. The recheck as before (dev_hdd0 mounted read-only, then unmounted). Only reads.
2. Before anything is made: `write_guard/policy` must read `whitelist`, `sealed` `0` and
   `nr_ranges` `0` (ranges someone registered without sealing would be sealed along with ours).
   Sealed already: refused ("sealed earlier in this boot ... Reboot"), nothing made. No
   `write_guard/` directory (a kernel without 0045; the user's choice 2026-10-19): the log line
   `kernel has no write guard (no /sys/block/ps3db/write_guard): ps3db's writes are not limited to
   the image's blocks` and on as before, so today's kernels keep booting. Except with
   `--guard required`, which the initramfs passes when `/etc/ps3ssd-initrd` says `guard=required`:
   `build-initrd.sh` sets that when the release's `System.map` in the kernel deb has the guard
   (`ps3disk_guard_group` or `ps3disk_set_read_only`; `GUARD=required|auto` overrides). Such an
   initramfs on a kernel without the guard refuses (the release check already stops a mismatched
   pair; this is the second lock). Phase A has no required mode: it runs on whatever kernel the USB
   root booted.
3. The dm-linear devices (root and swap) made, read back and compared, as before. Still no writes.
   More than 65536 targets in all: refused before this step.
4. The device side of every target of both tables, as the kernel reads it back (`DM_TABLE_STATUS`
   in C, `dmsetup table` in Python; Phase A's create also compares that live table with what it
   loaded), goes into `ranges` as `<offset> <len>` per target. Not the header area (never mapped).
   In chunks of whole lines of at most 4096 bytes, each one `write(2)` that must return its whole
   count; an error or a short count stops it. Then `nr_ranges` must equal the number of lines sent
   (before the seal it counts what was taken).
5. `1` written to `sealed` (one `write(2)` of `1\n`).
6. `sealed` reads `1`, `nr_ranges` equals the tool's own count after sorting by offset and merging
   ranges that overlap or touch (the kernel's rule), `nr_sectors` their sum (the tool first checks
   that the targets don't overlap). The log line: `write guard sealed: N ranges registered in W
   writes, M after merging, S sectors (X MiB) of ps3db writable, nothing else`.
7. Only now root is mounted (ext4's journal replay writes) and later swapon.

Any failure in steps 4-6 removes the devices made in step 3 (and their table records) and refuses:
`write guard: <what>. ps3db's guard can't be reset in this boot (ranges taken stay, a seal stays):
reboot`. The initramfs then stops in its rescue shell (nothing was written: ps3db refused every
write until then); `reboot -f` and the USB entry. Phase A's create says the same. Boots of the USB
root register nothing until Phase A's create, so `ps3db` refuses every write before it.

`ssd-install-image.sh check` (and so `install` and `boot`) reports the guard on the USB root: none
(`kernel without the write guard`), or it must be `whitelist`, sealed, with `nr_ranges` and
`nr_sectors` exactly those of ps3ssd's and ps3ssd-swap's tables merged (`write guard sealed: N
ranges, S sectors of ps3db writable, exactly ps3ssd and ps3ssd-swap's; R writes refused since
boot`). Unsealed or different: refused (reboot, recheck, create).

### Not covered

- Root making raw lv1 storage calls through `/dev/ps3lv1call` (`CONFIG_PS3_LV1CALL=m`, loaded by
  hand for GPU research) goes around the block layer.
- Kernels without the patch (older ones, a kexec'd foreign kernel) have no guard (`write_guard/`
  absent). Today's `.472` is one of them.
- A buffered write to `/dev/ps3db` that the guard refused leaves the changed data in the page
  cache although the disk is unchanged: `blockdev --flushbufs /dev/ps3db` drops it.
- A refused write through a dm device shows as EIO in ext4 (its `errors=` policy then applies).
- A stray write to `/dev/ps3db` that the block layer merges with a dm write fails both (closed, but
  it would hit ps3root). `CONFIG_BLK_DEV_WRITE_MOUNTED=n` would stop anyone opening ps3db for
  writing while device-mapper holds it, but it changes that for every mounted disk and isn't set
  (fsck tools that open a mounted device read-write would get EBUSY).

**No debug switch.** A command-line or runtime switch that turns the check off is not built.
Anything that needs writes elsewhere on ps3db can register those ranges for its boot, and a switch
would make one mistyped kernel argument (Petitboot's editable command line, a kboot.conf entry)
enough to expose dev_hdd0 again. A different region layout needs a changed patch, not a switch.

### Tested on the PC

2026-10-19, the patch: `packages/patched/kernel/linux/tests/ps3disk-guard.sh` (suite `ps3disk-guard`) takes
`ps3disk_guard.h` (the parser, the seal's sort and merge, the check, the policy decision; no kernel
APIs) straight from the patch and runs `ps3disk-guard-test.c` on it, plain with `-Werror` and under
ASan/UBSan: about 50 fixed cases (malformed lines, a write cut inside a line, overflow at 2^64, the
region's end, alignment, the 4096-byte page full of ranges, ranges that touch or overlap, runs across
a merged boundary or into a gap, empty and wrapping runs, every policy sealed and unsealed), 65536
reversed ranges, and 2000 seeded random rounds of several writes each, checked sector by sector
against a bitmap. Five deliberate off-by-one mutations of the header each fail it. The patch applies
on the series as of 0044; `ps3disk.o` compiles without warnings with `W=1` (also as a module), and a
full `vmlinux` links, with both `ps3.config` and `ps3.config` + `ps3-release.config` on
`ps3_defconfig` (the host's powerpc64-linux-gnu-gcc 16.2.1). A second agent (Opus) reviewed the
request path against the 6.12 block layer (merging, the flush machine, requeue, partitions, dm,
sysfs, BLKROSET); its should-fixes are in (among them: sysfs cuts writes at PAGE_SIZE silently,
hence the final-newline rule; the BLKROSET hook without drvdata; the request span checked too).

The boot tools' side:

- `make test` suite `ssdimg`: the C tool's test build (`-DPS3_GUARD_FAKE`:
  `tools/ssdimg/guard-fake.h` replaces the sysfs I/O with a model built on the patch's own
  `ps3disk_guard.h`, which `packages/patched/kernel/linux/tests/guard-header.sh` takes from the patch; it models the
  4096-byte cut, whole-or-nothing writes, the seal's merge, EPERM after it) runs create's guard steps
  on the boot map's tables (`guard-test`): exactly the device side of both tables, then `1` to
  `sealed`; a fragmented 16 MiB layout goes in several writes, each at most 4096 bytes of whole
  lines and filled before the next. Refusals for: no guard with `--guard required`, sealed already,
  policy `writable` and `read-only`, pending ranges, a short count, EINVAL on the first and on the
  second write, the seal failing, `nr_ranges` or `nr_sectors` off, ranges past the disk's end. No
  guard without `required`: the log line, nothing written. Phase A's `ps3-imgmap create` against a
  Python model of the same: the same lines as the C tool, the header area in no range, refusals
  before anything is made (no setro, no dmsetup) and after (both devices removed again, "reboot" in
  the message).
- `make test-slow` suite `ssdimg-vm`: `ps3guard_sim` (`tools/ssdimg/guard-sim`, built against the
  host kernel by vm-e2e.sh) gives the KVM guest disks named ps3da and ps3db over its virtual disks,
  running the patch's own code unchanged (`extract.sh` takes struct ps3disk_guard, the region
  policy, BLKROSET's hook and the whole sysfs interface out of the patch; only the per-write check
  is the same decision on each bio instead of each request). The second Phase B geometry (block
  4096 / fragment 512: 15 + 2 targets, 16 ranges after merging) runs behind it: Phase A's create
  registers and seals, `ssd-install-image.sh check` reports the seal, and mkfs, the tar and the
  fix-ups write through it with nothing refused. The initramfs (built with `guard=required`)
  registers and seals from the kernel's tables, root mounts and writes with nothing refused, the
  test root's writes to ps3db sector 0 and to the header area are refused (EIO, counted), and
  `ranges` after the seal and BLKROSET 0 on ps3da are refused. Without the module the same
  initramfs stops in the rescue shell (the kernel has no guard), the disk unchanged.
- Two more guests. Phase A behind the guard: the files and policies, a write refused before the
  seal, map/verify/recheck, create sealing, ext4 and swap on the devices, writes outside refused and
  one inside straight to ps3db allowed, `ranges` and `sealed` EPERM, `sealed 0` EINVAL, the C tool's
  create refused because of the seal with nothing made, the module pinned, every sector outside the
  image's mapped part unchanged. And sysfs's edges: six malformed writes refused with nothing taken,
  a 4100-byte write taken as 4096 with a short count, a write cut inside a line EINVAL, the C tool
  refusing pending ranges, 512 equal ranges sealed into one of 8 sectors, the sectors just inside
  allowed and one past either end refused; the disk byte for byte the original.
- The first geometry runs without the module: the initramfs and both tools log "kernel has no
  write guard" and boot as before. And the test build against the real device-mapper: what it
  registers is exactly `dmsetup table`'s device side, and a short count, a failed seal or wrong
  sectors remove both devices again.

Not tested: the PS3, and ps3disk's request path in a running kernel.

### On the PS3

Not done yet; scheduled in [TESTS.md](TESTS.md) ("Next"). After a batch, the user not at the
console, nothing published. Every write test below writes back data that is already there, so even
a broken guard changes nothing.

1. PC: the kernel with 0045 (`make linux-deb`, then `make ps3rsx-modules`), the new tools to the PS3
   (`scp tools/ssdimg/ps3-imgmap tibi@10.0.0.77:/var/tmp/ps3-imgmap`; `ssd-install-image.sh` with
   [Phase B's step 3](#the-steps-on-the-ps3) copy). First boot of the new kernel without installing
   it: `make netboot-deb FLAVOUR=release ROOT=usb` (Petitboot, the user), or install it
   (`make deploy KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"`: only with the user's go).
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

3. Phase A as every boot (`ps3-gameos-mount`, `recheck`, `-u`), then
   `sudo python3 /var/tmp/ps3-imgmap create $M ps3ssd --swap-mib 1024 --yes`: it prints
   `write guard sealed: 513 ranges ...` or so (513 extents; the root's and the swap's targets merge
   where they touch). Then `grep . /sys/block/ps3db/write_guard/*` (sealed 1, nr_sectors 33552384 =
   16 GiB minus the 1 MiB header), the same `dd` write-back of header block 5 (EIO, refused + 1,
   "outside the registered ranges"), `echo '0 8' | sudo tee /sys/block/ps3db/write_guard/ranges`
   (EPERM), mount ps3ssd and run the bench's SSD arms as usual: `refused` must not move.
4. The initramfs for the guard kernel from the PS3's real map ([Phase B's step 2](#the-steps-on-the-ps3),
   with `INITRD_RELEASE=<the new release>`): it must print `guard=required`.
   `ssd-install-image.sh check` then reports the seal; `boot` adds the entry `ssd-<new release>`.
5. The first SSD boot on it (the user): `cat /run/ps3ssd/init.log` has `write guard sealed: ...`;
   `grep . /sys/block/ps3db/write_guard/*` sealed 1 and `refused` 0 after the desktop is up and a
   bench run; the header block 5 write-back once more (EIO). If anything refuses at boot, the rescue
   shell says why: `reboot -f`, the USB entry (an older kernel is still installed).

Step 3 was written for Phase A's ext4 on ps3ssd. Since the Phase B install, ps3ssd holds the SSD
root's file system: mount it read-only from the USB root for that step, or skip the mount and the
SSD arms there and let step 5's bench run on the SSD root stand for it.

## The image header

Designed 2026-10-19, not built, and stopped: writing it collides with the kernel guard, and the
benchmark doesn't need it. The full design, its options and the work it takes are in
[backlog/platform.md](backlog/platform.md) and the [archived original](archive/GAMEOS-HDD.md). What
matters for what runs today:

- The header would sit in the image's first MiB (the 256 blocks of 4 KiB never mapped into a dm
  device): block 0 stays the stamp of offset 0 for good (the file's identity for `recheck`), block 1
  a header (`PS3IMGH1`, version, UUID, file size, the extent list's sha256, `swap_sectors`, a
  generation counter, time, the bundle's length and sha256, a self-sha256), blocks 2-255 a bundle
  (the boot map text and kernel arguments). With it, the image would describe itself and the state
  needn't live on the USB root.
- The installed initramfs and Phase A's tool require all 256 stamps to read back, so they refuse
  an image with a header. Every initramfs and `ps3-imgmap` copy must learn the relaxed rule
  (block 1 a stamp or a valid header of this UUID and list) before any header is written, or the
  next boot stops (safely: the rescue shell, the USB fallback).
- On a guard kernel the header area is never registered, so nothing can write it: that is the
  guard's point. The design's answer is a boot spent on it (`ps3-imgmap header-write` registers
  only the header's blocks, seals, writes, verifies; the image's devices wait for the next boot),
  which would be the one command that opens `/dev/ps3db` for writing.

## The GameOS installer

Designed by the user on 2026-10-18, nothing built, and put among the later items on 2026-10-19
(PLAN 5.3). A PKG app for GameOS (PSL1GHT) so users never need a PC shell: it would check the
prerequisites (OtherOS++ with Petitboot, dev_hdd1, webMAN MOD, free space, the fan reminder), copy
the root file system from a USB stick as zstd chunks (FAT32's 4 GiB limit) straight into a USB disk
or a stamped image file on the internal disk (at most about 90% of dev_hdd0's free space), put the
boot files in `/dev_hdd1/linux/<instance>/` with their kboot.conf entries, and add the autoexec
mirror lines. Several instances could sit side by side, each with its own image and labels. The
stamps of the image format above are what it would write instead of zeros. PSL1GHT's own PPU GCC
would be an exception to "one GCC everywhere" that the user decides. The full design is in
[backlog/platform.md](backlog/platform.md).

## Later, for a release

Not needed for the benchmark, and not built (details in [backlog/platform.md](backlog/platform.md)):
the image header and the installer above; `/boot` on dev_hdd1 with the webMAN mirror, so the USB
disk can go; kernel updates in one step for both roots (a kernel hook that rebuilds the initramfs
and the entry); a small journal-less /boot partition on the USB disk (option d1).

## Checklist

Where things stand (2026-10-19):

| item | state |
|---|---|
| `dev_hdd0` read-only mount | works (`.419`, again on `.472` for Phase A) |
| `dev_hdd1` read-only mount | fixed in `.457`, not checked on the PS3 yet |
| Petitboot's view of ps3db/ps3dc, stored default | read from the image; the shell checks above not done |
| Phase A: upload, map, verify, create, the SSD arms | done; batch ssd1 (9e6bc57) |
| Phase B: initramfs, install, boot entry, first SSD boot | done, the SSD root runs (`.472`) |
| USB root against SSD root, alternating boots | scheduled, needs the user |
| Kernel guard local/0045 | built, in the series, PC-tested; no PS3 kernel yet; ["On the PS3"](#on-the-ps3) steps 1-5 to do |
| `/boot` on dev_hdd1 with webMAN | draft script, not tried |
| Image header, installer | designed, not built |

Every Linux session ends with the boot flag back on GameOS, and starts with webMAN's fan in SYSCON
mode or at 60% or more.

## Sources

- The Linux side: `packages/patched/kernel/linux/patches/ps3/0010` (multi-region ps3disk), `packages/patched/kernel/linux/config/ps3.config`
  (`CONFIG_UFS_FS=m`, no `UFS_FS_WRITE`, the NLS options, `PS3_LV1CALL=m`),
  build/src/linux/linux-6.12.111 (fs/ufs: `ufs_fill_super`, `ufs_bmap`), the patch
  `packages/patched/kernel/linux/patches/local/0045-ps3disk-write-guard.patch` and its tests.
- Petitboot: `firmware/petitboot/dtbImage.ps3.bin` strings and its unpacked initramfs, upstream
  petitboot at 17d63c9 (github.com/open-power/petitboot) and Linux v2.6.30 (fs/ufs/super.c,
  drivers/ps3/ps3stor_lib.c, drivers/block/ps3disk.c).
- webMAN MOD's README and `include/feat/script.h`; RPCS3's `rpcs3/Emu/Cell/Modules/cellSysCache.cpp`
  and forums.rpcs3.net thread-200213; psdevwiki's
  [Harddrive](https://www.psdevwiki.com/ps3/Harddrive) and
  [HDD Encryption](https://www.psdevwiki.com/ps3/HDD_Encryption) (XTS, VFLASH at sector 8).
- The region research's sources are in [research/HDD-REGION.md](research/HDD-REGION.md#sources).

## History

- 2026-10-18: `dev_hdd0` read-only on `.419`; `dev_hdd1`'s NLS failure on `.433`, fixed for
  `.457`. The research into a Linux region concluded that stock Evilnat gives only 22 GiB and only
  after a full reformat; the disk-image route was proposed the same day as the alternative.
- 2026-10-18: Phase A built (b0cd553, 95d0ef1, ff91438, 35f2600). Its plan expected, from batch 16's
  start-up numbers, that loads would get 15-51% shorter with the game data on the SSD, assuming the
  load's disk wait was game data. Batch ssd1 showed it is mostly the binary, libraries, Mesa and
  shader cache, which led straight to Phase B.
- 2026-10-19: Phase A on the PS3 (`.472`), batch ssd1 stopped early for Phase B (9e6bc57). Phase B
  built (fe191f7 and following), installed and booted at about 03:35. The kernel guard written
  (07fa26b), kept out of the series until both tools registered and sealed their ranges, then put
  in (1abb2a1). The image header designed and stopped.

Older versions and the dated history: [archive/GAMEOS-HDD.md](archive/GAMEOS-HDD.md).
