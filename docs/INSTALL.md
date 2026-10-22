# Installing Walnut Linux on the PlayStation 3

**Walnut Linux** is a modern Debian-based Linux distribution (ppc64 big-endian) for the Sony PlayStation 3, featuring native 3D graphics acceleration on the RSX GPU via our `ps3rsx` kernel driver and Mesa Gallium nv30.

---

## 1. Risk Statement & Alpha Notice

> [!WARNING]
> Walnut Linux is currently in **public alpha** (`0.1-alpha`).
>
> To date, this software has been tested on **only one console**: a PS3 Slim CECH-20xx (NOR flash model). While other Custom Firmware (CFW)-compatible PS3 models (Fat models and early Slims) are architecturally compatible, they remain untested by our team.
>
> Installing custom firmware, running third-party operating systems, and modifying storage carries inherent risks of data loss or hardware issues if instructions are not followed carefully. Everything you do is at your own risk and responsibility.

---

## 2. Prerequisites

Follow these prerequisites strictly in order before beginning installation:

1. **A CFW-Capable PS3 Console**
   - Compatible: All "Fat" models (CECH-A through CECH-Q) and early "Slim" models (CECH-20xx and supported CECH-25xx with minimum downgrade version ≤ 3.56).
   - Incompatible: Late Slim models (CECH-30xx) and all "SuperSlim" models (CECH-40xx) cannot run CFW and cannot run Walnut Linux.
2. **A NOR / NAND Flash Backup FIRST**
   - **Crucial step**: Dump and verify your console's flash memory *before* installing OtherOS++ or touching any system partitions.
   - Follow established community guides on the **PS3 Developer Wiki (psdevwiki)** or **PSX-Place** (e.g. *PS3 Flash Tool Guide* or *PyPS3checker Guide*). Verify your dump with PyPS3checker (ensure 0 errors / 0 dangers) and store multiple copies safely on external media off the console.
   - *Legal notice*: This project hosts no firmware files, CFW packages (PUPs), encryption keys, or proprietary Sony software, and links to nothing illegal.
3. **Custom Firmware (CFW)**
   - Install a modern Cobra-enabled CFW such as **Evilnat 4.92 CEX Cobra**.
4. **webMAN MOD**
   - Install and enable webMAN MOD in GameOS. webMAN MOD provides the FTP server needed for file transfers, system fan controls, and autoexec scripts.
5. **OtherOS++ Installed via xai_plugin**
   - In GameOS XMB, navigate to:  
     `Custom Firmware Tools` -> `Hard Ware Information / OtherOS` -> `Install OtherOS`.
   - This resizes the internal VFLASH table and installs the **Petitboot** bootloader into VFLASH region 5.
   - **No HDD reformat is required** for Walnut's internal install. Your existing GameOS games and saves remain in place.
6. **Set Fan Control in webMAN MOD**
   - **Essential thermal safety rule**: In GameOS webMAN setup, configure the fan to **SYSCON mode (automatic)** or a fixed manual speed of **at least 60%** BEFORE booting into OtherOS.
   - When Linux runs, GameOS's background thermal monitoring is suspended. Walnut's kernel includes an automatic SYSCON fan reset at boot and shutdown, but starting from a safe fan configuration in webMAN is mandatory.
7. **Back Up GameOS Data with the XMB Backup Utility**
   - Before running the internal install, perform a full backup of your saves, games, and user data to an external FAT32 USB drive using GameOS's built-in tool:  
     `Settings` -> `System Settings` -> `Backup Utility`.
   - Walnut's kernel includes an in-kernel write guard that restricts Linux disk writes strictly to Walnut's own disk image file, leaving GameOS partitions protected. Taking an XMB backup first ensures you are protected against accidents.
8. **USB Keyboard and Mouse**
   - A standard USB keyboard is required for the initial setup wizard and rescue shell. A USB mouse is recommended for the desktop environment.
9. **Wired Ethernet Connection**
   - A wired Ethernet connection to your local network is strongly recommended for the internal install. Transferring the ~16 GiB disk image over FTP takes approximately 12–15 minutes on a wired connection.

---

## 3. Getting to Petitboot

Petitboot is the bootloader menu that OtherOS++ loads.

1. **Setting the OtherOS Boot Flag from GameOS (XMB)**
   - In the XMB, go to `Custom Firmware Tools` -> `Hard Ware Information / OtherOS` -> `Boot OtherOS` (or trigger it via webMAN).
   - Alternatively, select `Set OtherOS Boot Flag` and restart the system.
2. **What Petitboot Looks Like**
   - Your TV or monitor will display Petitboot's menu on a dark blue background.
   - The menu lists discovered boot entries (such as `walnut` or `walnut-usb`).
   - You can navigate the menu using the arrow keys on your USB keyboard.
3. **Petitboot Default Behavior**
   - Petitboot's default countdown timeout is set to wait indefinitely at the menu. It will not boot an entry until you press Enter or have explicitly configured an automatic boot entry.
4. **Returning to GameOS**
   - The OtherOS boot flag is temporary. **Every reboot or shutdown from Linux automatically returns to GameOS (XMB)**. GameOS always remains your default primary operating system.

---

## 4. Installation

### Getting the release

Each release is at `https://walnut.nanobyte.dev/download/VERSION/` and on the
[GitHub releases page](https://github.com/nanobyte-dev/walnut-linux/releases), the same files in both
places. Put all of them in one directory (`DIR`), check the signature and the checksums, and unpack the
installer:

```bash
cd DIR
curl -fsSL https://walnut.nanobyte.dev/walnut-archive.asc | gpg --import
gpg --verify SHA256SUMS.asc SHA256SUMS   # "Good signature" from 8FFE 9D3F C1E2 4ADC 4702  50BB 5701 1432 F8A9 8072
sha256sum -c SHA256SUMS
tar -xzf walnut-installer-VERSION.tar.gz
```

| File | What it is |
|---|---|
| `walnut-VERSION-ppc64.tar.xz` | the root filesystem |
| `vmlinux-RELEASE`, `RELEASE.args` | the kernel and its boot arguments |
| `initrd-walnut.img` | the internal install's initramfs (it formats the image and unpacks the system on the first boot) |
| `walnut-installer-VERSION.tar.gz` | the PC installer (Python 3, nothing to install) |
| `MANIFEST` | every package in the image, with its version |
| `SHA256SUMS`, `SHA256SUMS.asc` | the checksums and their signature |

Choose either **Install A** (Internal HDD, default) or **Install B** (External USB Drive).

### Install A: Internal HDD (Default & Recommended)

Walnut runs from a single stamped disk image file (`/dev_hdd0/linux/walnut.img`) residing inside GameOS's standard filesystem. Boot configuration files live on the small cache partition `dev_hdd1`. GameOS is not erased and no disk repartitioning is necessary.

1. **Verify Free Space in GameOS**
   - In the XMB, verify that your internal hard drive (`dev_hdd0`) has at least 18–20 GB of free space (16 GB for Walnut plus working margin).
2. **Ensure webMAN FTP Server is Active**
   - Confirm webMAN MOD is loaded and FTP is enabled on your PS3. Note your console's local network IP address (e.g. displayed in webMAN setup or XMB Network Settings).
3. **Run the PC Installer**
   - On your PC (Linux, or WSL2 on Windows), get the release files into one directory (`DIR`) and check them: see [Getting the release](#getting-the-release) below.
   - Run the PC installer:
     ```bash
     DIR/walnut-installer-VERSION/installer/walnut-install internal --release-dir DIR [--size 16G] [--host PS3-IP]
     ```
     - `--release-dir DIR`: the directory with the release files (the rootfs tarball, kernel, initramfs, `SHA256SUMS`). The installer checks every file against `SHA256SUMS` and makes the stamped image itself.
     - `--size 16G`: Total image size (default is 16 GiB: 15 GiB root filesystem + 1 GiB swap). Smaller sizes can be used on 40–80 GB internal drives.
     - `--host PS3-IP`: The IP address of your PS3 on your local network.
   - The installer streams the stamped disk image directly to `/dev_hdd0/linux/walnut.img`, uploads boot files and the rootfs tarball to `dev_hdd1:/linux/`, and configures `dev_hdd1:/kboot.conf`.
4. **First Boot in Petitboot**
   - In GameOS, boot OtherOS to enter Petitboot.
   - Select the `walnut` entry from the Petitboot menu and press Enter.
5. **Initial System Unpacking**
   - On this first boot, Walnut's generic initramfs automatically executes the installation sequence:
     - Verifies every 4 KiB block stamp of the image to ensure the upload was complete and uncorrupted.
     - Creates the device-mapper linear volume sealed by the kernel write guard.
     - Formats the root filesystem (`ext4`).
     - Verifies SHA-256 and unpacks `rootfs.tar.xz`.
     - Cleans up temporary installation files and marks the install complete.
   - This process takes several minutes. Progress and verification steps are printed to the screen.

---

### Install B: External USB Drive

If you prefer to keep your internal console drive untouched, you can install Walnut onto an external USB hard disk or high-speed flash drive (8 GB or larger).

1. **Connect the USB Drive to Your PC**
   - Identify the drive device node on your Linux PC (e.g. `/dev/sdX`).
2. **Run the PC Installer**
   ```bash
   DIR/walnut-installer-VERSION/installer/walnut-install usb --release-dir DIR --disk /dev/sdX
   ```
   > [!CAUTION]
   > This command will partition and format `/dev/sdX`, completely erasing all data on that USB drive. Verify the drive device name carefully before confirming.
3. **Boot on the PS3**
   - Plug the prepared USB drive into any USB port on your PS3.
   - From GameOS, switch to OtherOS and enter Petitboot.
   - Select the `walnut-usb` entry and press Enter.

---

## 5. First Boot & Setup Wizard

When Walnut Linux boots for the first time, it automatically launches the `walnut-firstboot` setup wizard on the console screen (`tty1`).

Ensure your USB keyboard is connected, then follow the on-screen prompts:

1. **User Account**: Enter your desired username (regular user account) and set a password. The user is automatically granted administrative privileges (`sudo`) and added to the required hardware access groups (`video`, `render`, `audio`, `input`).
2. **Hostname**: Choose a system hostname (default: `walnut`).
3. **Timezone**: Select your local timezone (default: `UTC`).
4. **SSH Server**: Choose whether to enable the OpenSSH server. For security, SSH is **disabled by default**.
5. **System Clock Check**: The wizard checks your system clock against the release build date. Because the PS3 real-time clock under Linux does not persist offsets to flash without GameOS, an incorrect clock can break package manager signature verifications. If the clock is in the past, the wizard prompts you to set the current date and time.

Once the wizard completes, system services initialize and the graphical desktop (Openbox) starts automatically.

---

## 6. System Updates

Updating Walnut Linux packages:

1. **Safe Update Procedure**
   - Because the PS3 has 256 MB of main memory, performing major kernel or Mesa upgrades while a full graphical desktop and multiple browser tabs or apps are open can exhaust memory.
   - To update safely, switch to a text console with `Ctrl+Alt+F2` (or open a terminal with no heavy 3D apps running) and run:
     ```bash
     sudo apt update && sudo apt full-upgrade
     ```
   - *(A dedicated `walnut-update` utility that stops graphical services, performs updates with optimized disk I/O, and restarts the desktop is planned for a future release).*
2. **Kernel Updates & Fallback**
   - Kernel packages automatically detect your boot configuration:
     - On internal installs, boot files update on `dev_hdd1`.
     - On USB installs, boot files update on the USB drive.
   - The bootloader configuration automatically keeps the previous working kernel as **`walnut-prev`** in Petitboot. If an updated kernel fails to boot, reboot and select `walnut-prev` to return to your working system.

---

## 7. Getting Out, Recovery & Uninstallation

### Returning to GameOS
- To exit Linux, simply reboot or shut down:
  ```bash
  sudo reboot
  ```
  or press the PS3 power button briefly.
- Every reboot from Linux automatically starts GameOS (XMB).

### Recovery via Petitboot
- **Rescue Shell**: If a kernel boot or filesystem check fails in early boot, the initramfs drops to a rescue shell and displays a framed message explaining the cause. To reboot from the rescue shell, type:
  ```sh
  reboot -f
  ```
  Then in Petitboot, choose the fallback kernel entry `walnut-prev`.
- **Editing Kernel Parameters**: In the Petitboot menu, highlight any boot entry and press `e` to edit its command line. You can append maintenance parameters (such as `init=/bin/sh` for emergency recovery). Note that the root account is locked by default for security, so system repairs should be performed using standard recovery steps or via sudo credentials.

### Uninstallation
Walnut's internal install is completely self-contained. To remove Walnut entirely from your console:
1. Boot into GameOS (XMB).
2. Connect to the PS3 over FTP (via webMAN MOD or multiMAN) or use a GameOS file manager.
3. Delete the following files and directories:
   - `/dev_hdd0/linux/walnut.img` (the virtual disk image)
   - `/dev_hdd0/linux/walnut-mirror/` (if present)
   - `/dev_hdd1/linux/`
   - `/dev_hdd1/kboot.conf`
4. GameOS remains completely untouched, and all allocated space on `dev_hdd0` and `dev_hdd1` is immediately reclaimed.

---

## 8. Troubleshooting

- **No Picture / Black Screen on TV**
  - Walnut's kernel inherits the video mode set by Petitboot. If your monitor or TV displays "Signal Out of Range" or a black screen, adjust the display resolution in Petitboot before booting Linux (or configure Petitboot's video mode using `ps3-video-mode` in Petitboot's shell). Both HDMI and AV Multi-Out are supported at 1080p, 720p, and standard definition (SD).
- **Fan Noise & Temperatures**
  - Ensure that webMAN MOD was configured to SYSCON automatic mode or at least 60% manual speed before booting OtherOS.
  - If temperatures rise unusually high, check console ventilation. Linux monitors temperatures via the System Manager driver (`temp1`: Cell BE, `temp2`: RSX).
- **Log Locations**
  - Boot and drive mapping log: `/run/ps3ssd/init.log`
  - Kernel log: `dmesg`
  - Graphical X11 desktop log: `~/.local/share/xorg/Xorg.0.log` or `/var/log/Xorg.0.log`
  - Failed system services: `systemctl --failed`
- **Generating a Bug Report**
  - If you encounter a bug or hardware incompatibility, run the diagnostic collection tool:
    ```bash
    walnut-bugreport
    ```
  - This generates a text file in your current directory containing system info, kernel logs, and package versions, with private network addresses, hostnames, and user names automatically redacted. Attach this report when submitting an issue.

---

## 9. Hardware Compatibility & Limits

- **Tested Hardware**: This alpha release has been validated on a **CECH-20xx Slim** console with NOR flash. Reports and feedback on other CFW-capable models (Fat CECH-A/B/C/E/G/H/J/K/L/M/P/Q and Slim CECH-21xx/25xx) are welcomed via the issue tracker.
- **Game Data & Packages**: Walnut includes open-source game engines and freely redistributable game data in `ps3-games-free`. Proprietary and commercial game assets (such as full Quake or Quake II game data) are not bundled and must be provided by the user.
- **Supported Games**: See [docs/games/README.md](docs/games/README.md) for detailed performance metrics, steady frame rates, and configurations for all 13 games tested in the Walnut suite.
