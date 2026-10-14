# Plan: a usable LXQt desktop on the PS3 (after DRIVER-PLAN.md)

Decided 2026-10-14: LXQt, root stays on NFS (netboot from Petitboot as now), input from USB
keyboard/mouse, a DualShock 3 (USB and Bluetooth) and Bluetooth keyboards/mice.

Budget: Linux sees 165 MB of RAM; zram swap. LXQt with openbox is the heaviest thing that fits.

## 1. Kernel (one rebuild, one reboot by the user)

`kernel/config/ps3.config`: `NEW_LEDS`, `LEDS_CLASS` (needed by `HID_SONY`), `HID_SONY` +
`SONY_FF` (DualShock 3: USB init, rumble, Bluetooth), `UHID` (Bluetooth LE keyboards/mice),
`INPUT_UINPUT`. Bluetooth (`BT`, `BT_HCIBTUSB`, `BT_HIDP`), evdev, joydev and `SND_PS3` are
already there.

## 2. Boot straight into the desktop

- ps3rsx installed as a normal module (`/lib/modules/<ver>/extra`, depmod) and loaded at boot
  (`modules-load.d`); it pulls in drm_kms_helper.
- Patched Mesa found without environment variables: `ld.so.conf.d/00-ps3mesa.conf` is ahead of
  Debian's Mesa, and our build has its driver paths compiled in. Check that X and GL clients work
  with no `env.sh`.
- No display manager (saves RAM): getty autologin of `tibi` on tty1, `startx` from
  `~/.bash_profile`, session `startlxqt`. Logging out drops back to the console.

## 3. Packages (`rootfs/packages.txt`, also installed live)

lxqt-core, openbox, lxqt-powermanagement, lxqt-sudo, featherpad, lximage-qt, pavucontrol-qt,
alsa-utils, bluez, blueman (pairing GUI), xserver-xorg-input-joystick (DualShock 3 as a
pointer), netsurf-gtk (light browser), a font set. No compositing.

## 4. Input

- USB keyboard/mouse: works through evdev/libinput already.
- DualShock 3 over USB: hid-sony; the joystick X driver maps sticks to pointer and buttons to
  clicks (xorg.conf.d snippet for the Sony vendor ID).
- DualShock 3 over Bluetooth: bluez's sixaxis plugin pairs it when plugged in by USB once; then
  the PS button connects it wirelessly.
- Bluetooth keyboards/mice: blueman or bluetoothctl.

## 5. Verify

Boot to the desktop with no SSH involvement (webcam); panel, menu, terminal, file manager open;
glxgears inside LXQt on NV47; DualShock 3 moves the pointer over USB and over Bluetooth; a
Bluetooth keyboard types; `free -m` after login with the desktop idle.

## Later

- Performance of the CPU texture-copy path (xclock-style antialiased drawing drops glxgears from
  58 to ~12 fps).
- Sound through PulseAudio/PipeWire if ALSA alone isn't enough.

## Status (2026-10-14)

- Kernel built with the input options (hid-sony, uhid, uinput); not booted yet. Its modules
  must reach the NFS root before the next netboot: `make nfs-modules` (sudo).
- Packages installed live; `rootfs/desktop-setup.sh` applied live (ps3rsx.service, tty1
  autologin, Xwrapper, gamepad pointer).
- X without any Mesa environment variables comes up (cursor shown), but starting LXQt then
  stopped SSH answering (ping fine) for 10+ minutes: probably memory thrashing in 165 MB.
  `~/.no-desktop` keeps the next boot at a shell. Next: start X alone, then openbox, then each
  LXQt component, watching `free -m` and /proc/pressure/memory; trim (pcmanfm-qt desktop,
  notifications, power management) or grow zram until it fits.
- Seen on netconsole: `os_area_queue_work_handler: Could not update FLASH ROM` (the kernel
  wanting to save the RTC offset to flash; flash writes are compiled out, nothing was written).
