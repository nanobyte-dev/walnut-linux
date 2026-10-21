# Walnut Installer (`walnut-install`)

The PC-side installer for Walnut Linux on the PlayStation 3 (milestone M5 of the Walnut public alpha).

`walnut-install` runs on Linux (and inside WSL2 on Windows) using only Python 3 standard library modules.

## Installation Targets

1. **Internal Install (`internal`)**
   Walnut's default installation method. Streams the stamped disk image directly to `/dev_hdd0/linux/walnut.img` and uploads boot files, the system rootfs tarball, Petitboot's `kboot.conf` entry, and the install manifest to `dev_hdd1` over webMAN MOD's FTP server. The console initramfs verifies the stamped blocks and formats ext4 on the first boot.
2. **USB Install (`usb`)**
   Prepares an external USB drive for Petitboot:
   - Verifies the disk is a whole drive on USB transport and not mounted or the host PC's root disk.
   - Prompts for confirmation by requiring the user to type the disk's model name.
   - Re-elevates with `sudo` if needed.
   - Partitions with `sfdisk` (DOS label, random 32-bit disk ID): partition 1 as swap (labelled `ps3swap`), partition 2 as ext4 (labelled `ps3root`, formatted with `-O ^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file` so Petitboot's legacy e2fsprogs can mount it).
   - Unpacks the rootfs tarball preserving numeric ownership and extended attributes (`tar --xattrs --xattrs-include='*' --numeric-owner`).
   - Configures `/etc/fstab` (root and swap by PARTUUID), `/etc/default/ps3-boot` (`PS3_ROOT_ARGS`), and `/etc/kboot.conf` (`walnut-usb`).

## Requirements

- **Python 3** (standard library only; no pip dependencies).
- **For Internal Install:**
  - The PS3 running GameOS with Custom Firmware (CFW) and webMAN MOD with FTP server enabled.
  - A wired network connection between PC and PS3 is recommended.
  - Windows users run `walnut-install` inside WSL2.
- **For USB Install:**
  - Linux with root/sudo access.
  - Standard system tools: `sfdisk`, `mkfs.ext4`, `mkswap`, and `tar`.

## Usage

### Internal Install
```bash
# Preview operations without network transfer:
./installer/walnut-install internal --release-dir /path/to/release --host 192.168.1.50 --dry-run

# Run the install:
./installer/walnut-install internal --release-dir /path/to/release --host 192.168.1.50 [--size 16G] [--swap-mib 1024]
```
Host can also be set via the `PS3_IP` environment variable.

### USB Install
```bash
# Preview operations without writing to disk:
./installer/walnut-install usb --release-dir /path/to/release --disk /dev/sdX --dry-run

# Run the install:
./installer/walnut-install usb --release-dir /path/to/release --disk /dev/sdX [--swap-gib 4]
```

### Dry Run
`--dry-run` is supported on all subcommands. It verifies `SHA256SUMS`, inspects release files, validates the target disk or FTP parameters, and prints all planned commands and configuration files without writing to any block device or PS3.
