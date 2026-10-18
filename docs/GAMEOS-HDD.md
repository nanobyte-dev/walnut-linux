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

The mount itself needs a kernel with the `ufs` module booted on the PS3 (6.12.111+ps3.403 or
later). The first try on 6.12.111+ps3.433 (2026-10-18): hdd0 not tried, hdd1 failed because
`mount -o iocharset=utf8` found no `nls_utf8` ("missing codepage or helper program"). Fixed
2026-10-18: the kernel config has `CONFIG_NLS_UTF8=y`, `NLS_CODEPAGE_437=y` and
`NLS_ISO8859_1=y` (vfat's defaults) built in from 6.12.111+ps3.457, and `ps3-gameos-mount` retries
a failed vfat mount without `iocharset` (file names with non-ASCII characters then show as `?`).
Still to check on 6.12.111+ps3.457: `sudo ps3-gameos-mount` mounts both, `ls /mnt/gameos/hdd0` and
`/mnt/gameos/hdd1` list files, `dmesg | tail` shows no UFS/FAT errors, `sudo ps3-gameos-mount -u`
unmounts both.
