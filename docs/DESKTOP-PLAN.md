# Plan: a usable desktop on the PS3 (after DRIVER-PLAN.md)

**Status 2026-10-15:** the desktop is Openbox + tint2 + PCManFM, not LXQt. It was measured
against LXQt, IceWM, Xfce and Enlightenment, and the others were dropped from the image
(`image/desktop-setup.sh`). The LXQt sections below are kept as the original plan; the
current state is in docs/PLAN.md.

Decided 2026-10-14: LXQt, root stays on NFS (netboot from Petitboot as now), input from USB
keyboard/mouse, a DualShock 3 (USB and Bluetooth) and Bluetooth keyboards/mice.

Budget: Linux sees 165 MB of RAM; zram swap. LXQt with openbox is the heaviest thing that fits.

## 1. Kernel (one rebuild, one reboot by the user)

`linux/config/ps3.config`: `NEW_LEDS`, `LEDS_CLASS` (needed by `HID_SONY`), `HID_SONY` +
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

## 3. Packages (`image/packages.txt`, also installed live)

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

- New kernel booted (hid-sony, uhid, uinput built); ps3rsx loads at boot from ps3rsx.service,
  tty1 autologin works, Bluetooth service up.
- **LXQt runs on the RSX** (wallpaper, panel, desktop icons; webcam + xwd), started with
  `startx /usr/bin/startlxqt` and no Mesa environment variables.
- RAM is the limit. Idle without X: ~53 MB used after dropping PulseAudio (33 MB, came with
  pavucontrol-qt) and blueman's helper (25 MB, now on demand). X + openbox: ~78 MB available.
  Full LXQt: 160 MB swapped out. zram compresses those pages about 5:1 (lzo-rle), so zram grows to
  384 MB at boot; lxqt-powermanagement and upower dropped (no battery on a PS3).
- The first LXQt attempt (with PulseAudio and blueman running) made SSH stop answering; the
  memory log (`~/mem.log`, every 2 s) is how to tell.
- Icons were missing: LXQt's default icon theme is breeze (`breeze-icon-theme` added).
- Kernel has no PSI (/proc/pressure); MemAvailable and SwapFree are the signals.
- Under LXQt the PS3 re-read 10 GB from NFS in ~30 min (program pages evicted and fetched again):
  that is the real slowdown. One apt-get then sat in D state for 20+ min waiting on a file page
  (filemap_fault) while other NFS reads worked; only a reboot clears it. Package work should be
  done with the desktop stopped.
- Seen on netconsole: `os_area_queue_work_handler: Could not update FLASH ROM` (the kernel
  wanting to save the RTC offset to flash; flash writes are compiled out, nothing was written).
- Next: boot with the 384 MB zram and remove `~/.no-desktop` (boot straight into LXQt); test the
  DualShock 3 (USB, Bluetooth) and a Bluetooth keyboard; measure idle memory after login.

## Open issues (2026-10-14, after the performance round)

- Fixed 2026-10-14: resizing GL windows leaked ~3 window-sized buffers per resize until VRAM ran
  out (Mesa 0011: nv30 surfaces now release their texture); ghosting gone. Graph exception 0x106
  = a swizzled render target 1 pixel wide, which glamor creates (Mesa 0012 keeps them linear,
  the validator rejects them). Verified: 400-step drag of xeglgears in LXQt, no exception,
  constant memory. Remaining: some tearing (xserver 21.1 modesetting has no TearFree).
- Text consoles (Ctrl+Alt+F1-F6) work (ps3rsx/src/console.c): ps3rsx owns the RSX only while a
  DRM master (X) is in front or clients without X are submitting; otherwise ps3fb gets it back
  and draws the console. On a VT switch X's GL clients pause until it returns; X's own work
  while away takes the GPU only briefly. The console also comes back when X exits, and at boot.
- Fixed 2026-10-14: the file manager's "Operation not supported" was gvfs missing (installed
  without recommends): GIO itself can't open computer:/// trash:/// network:/// without it.
- Desktop switched to IceWM + PCManFM (GTK) with desktop icons; LXQt stays as an alternative
  (`echo lxqt > ~/.config/ps3-desktop`). Measured footprints (PSS incl. swapped pages):
  LXQt's own processes ~72 MB (+Xorg 27); IceWM 13 MB, PCManFM 30 MB (desktop + a window).
  The saving pays for gvfs + gvfs-backends: trash, Computer, USB drives (udisks2), network
  (helpers only while browsing), cameras and phones. iPhone (afc) and online-accounts (goa)
  volume monitors are diverted away. Whole session ~103 MB, 80 MB available; desktop up 9 s
  after X starts (LXQt 20-25 s).
- Hardware cursor: X uses the cursor plane but nothing shows; the 0x10d init froze the PS3
  (probably wrong first argument: RPCS3 shows a3=1). Software cursor until tested supervised.

## Game mode (2026-10-17)

Rule from the user: memory comes from game mode, not from trimming the desktop. While a game
runs the rest gets out of the way and comes back intact after, however the game ends; nothing
is disabled for good, and system services (network, SSH, sound, the DualShock 3 over USB and
Bluetooth, apt, time sync, logging) keep working.

- `ps3-game` registers the game and starts or joins `ps3-game-guard` (the user service
  `ps3-game-mode.service`, in ps3-game.slice). Entering: gamepads off the X pointer; the PCManFM
  desktop off (`--desktop-off`; the root window's wallpaper pixmap released with it);
  ps3-desktop.slice frozen and reclaimed (anonymous pages first); the user manager's other
  services' anonymous memory to swap; then root (`ps3-game-mode on`): udisks2, polkit and
  blueman's helper frozen and reclaimed, system.slice's anonymous memory to swap, the unmapped
  page cache dropped, memory compacted. The game starts after that (at most 30 s).
- Leaving runs when the last registered game is gone: the guard's ExecStopPost, a root watcher
  on the guard's pid (thaws the system units), and any later `ps3-game` that finds state with no
  guard. Two games share one game mode.
- At login, `ps3-game-menu` writes the installed games' menu entries with Exec through
  `ps3-game` into the runtime directory, first in XDG_DATA_DIRS.
- Leaving (2026-10-17, after the first PS3 report: about 30 s until the desktop answered
  again after OpenArena; 1 s from the game's end to `leave`, 5 s more before `sudo
  ps3-game-mode off`, then about 20 s of the desktop's programs reloading from the USB hard disk
  as random reads, since game mode had pushed their file pages out). Now, in this order: the
  desktop's files come back in one pass (`ps3-game-prefetch fetch`: what `record` noted at
  entering, the files the desktop and display slices and the user manager have mapped plus the
  wallpaper and the programs leave runs, only the pages that were in memory then, sorted by
  their place on the disk (FIEMAP), all asked for with POSIX_FADV_WILLNEED, then read), beside
  it the desktop thaws at once (the cgroup's path saved at entering: no `systemctl` to load
  first, which was part of the 5 s), then the gamepads and the PCManFM desktop, the system side
  last. Each step goes to the user journal (`leave: desktop thawed`, `leave: PCManFM desktop
  started`, `leave: system side done`, `ps3-game-prefetch: fetched N files, X MB (Y MB were
  still in memory) ... all in after T s`, `left game mode`); entering logs `recorded N files,
  X MB in memory` (what keeping them would cost). Not measured on the PS3 yet.
- Automatic (2026-10-17, the user: any fullscreen 3D program, not only the menu's games):
  `ps3-game-detect`, a user service ps3-session starts, watches X by events (one `xev -root`
  through an awk filter that passes only changes to the active window and to windows covering
  the screen; nothing runs while idle or while a game plays; host: 0 CPU in 20 s either way,
  about 14 ms per focus change). A game: the active window fullscreen (_NET_WM_STATE_FULLSCREEN
  or covering the screen) or a mapped override-redirect window covering it (SDL 1.2), its
  process (_NET_WM_PID, else the X client's pid through XRes) with the GPU open (/dev/dri or
  Mesa's driver mapped), not a browser or video player (`PS3_GAME_EXCLUDE` adds names, `-name`
  removes one), not started through `ps3-game`. Before joining game mode the game and its
  children move out of ps3-desktop.slice into `ps3-game.slice/ps3-game-auto-<pid>-<start>.scope`
  (StartTransientUnit with PIDs); a game whose output goes to a terminal or pipe of the desktop
  gets the gamepads part only (the freeze would block it). It leaves when the process exits or
  after 5 s without a fullscreen window (vid_restart's new window comes within that; F11 out of
  fullscreen or switching windows gives the desktop back); fullscreen again enters again.
  Registered as `auto-<pid>` with its start time next to ps3-game's games (`ps3-game-guard
  join`), so both kinds share one reference-counted game mode. The menu's launchers stay: game
  mode is on before a game loads, and the software-rendered modes (dsda-doom, OpenTTD) have no
  GPU to detect. Tested on the host (`ps3-desktop/tests/autogm-check.sh`: Xvfb and Openbox, a
  stand-in GL game, 47 checks), not on the PS3 yet.
- The one switch is `PS3_GAME_MODE` (on, the default; pads; off), read from each game's own
  environment: system-wide in `/etc/environment` (pam_env at the tty1 login; Debian's
  `/usr/lib/environment.d/99-environment.conf` links it for the user manager, whose PAM stack has
  no pam_env), the user's in `~/.profile` or `~/.config/environment.d/*.conf` (ps3-session takes
  that one over unless ~/.profile set its own), one game's in its menu entry (`Exec=env
  PS3_GAME_MODE=off ...`) or command line. A game with `PS3_GAME_MODE=on` gets game mode when
  the default is off.
- Knobs: `PS3_GAME_MODE=pads|off` (baselines); `PS3_GAME_PREFETCH=on|off|keep` (the game's
  environment, for A/B on the same boot: off is game mode without the record and fetch; keep
  leaves the desktop its file pages, only its anonymous memory goes to swap);
  `/etc/default/ps3-game-mode` (FREEZE_UNITS, RECLAIM_SYSTEM, DROP_CACHES, COMPACT,
  GAME_SYSCTLS: vm.* values only while a game runs).
- Measured with `games/bench/gamemode-check.sh` on the desktop (idle, game mode, after; checks
  that everything is back). Not yet run on the PS3.
