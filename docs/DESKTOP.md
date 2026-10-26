# The desktop and game mode

The PS3 boots straight into an Openbox desktop (tint2 panel, PCManFM for the desktop icons and files) running on the RSX. While a game runs, game mode gets the rest of the desktop out of the way so the game has the memory, and puts everything back when the game ends. The desktop has run on the PS3 since 2026-10-14. Game mode's menu path and its wallpaper and services handling have run there too, but its automatic start, the escape keys and the return time have never been seen at the TV (see "What has run on the PS3"). Game mode has also never been benchmarked: every bench number in these docs was taken with the desktop stopped.

The package layout is in [PACKAGING.md](PACKAGING.md), the measured memory budget in [MEMORY.md](MEMORY.md), the stall recorder in [STALLS.md](STALLS.md). Terms are in the [glossary](README.md#glossary).

## The rules game mode follows

The user's rules in [CLAUDE.md](../CLAUDE.md) ("Memory trims", 2026-10-17) are the design:

- Trim only what does not hurt quality of life. System services keep working, and so do the things a desktop user expects (wallpaper, applets, services in normal use).
- The memory comes from game mode. While a game runs, the rest gets out of the way (unloaded, frozen, paused) and comes back after, instead of being disabled for good. Careful work, nothing risky shipped untested.
- Game mode turns on and off by itself: any fullscreen 3D app is detected and gets it, with an environment variable (`PS3_GAME_MODE=off`) to opt out. Launchers are not the only way in.

## The desktop

### Why Openbox, tint2 and PCManFM

The first plan (2026-10-14) was LXQt. It ran, but memory was the limit: with 165 MB of RAM then visible, a full LXQt session pushed 160 MB to swap and the PS3 spent half an hour re-reading 10 GB from NFS because program pages were evicted and fetched again. On 2026-10-15 we measured LXQt, IceWM, Xfce and Enlightenment and kept Openbox + tint2 + PCManFM; the others were dropped from the image (`image/desktop-setup.sh`). The footprints (PSS, swapped pages included) from that round:

| Part | Memory |
|---|---|
| LXQt's own processes | about 72 MB (plus Xorg 27 MB) |
| IceWM (the lightest of the window managers then measured) | 13 MB |
| PCManFM, desktop plus one window | 30 MB |

The whole session with IceWM and PCManFM came to about 103 MB, leaving 80 MB available, and the desktop was up 9 s after X started (LXQt: 20 to 25 s). Openbox replaced IceWM later with the same PCManFM, and the numbers above stand as the reason for the choice. The memory the desktop costs is not the problem game mode solves by trimming, since the rules forbid that; it solves it by moving the desktop out of the way during a game.

The desktop pays for `gvfs` and `gvfs-backends`: trash, the Computer view, USB drives (udisks2), network shares (helpers run only while browsing), cameras and phones. Without gvfs, PCManFM answers "Operation not supported" for `computer:///`, `trash:///` and `network:///`, because GIO cannot open them. The iPhone (afc) and online-accounts (goa) volume monitors are diverted away.

### Boot, login and the session

There is no display manager. A systemd generator (`ps3-desktop-autologin`) adds an agetty autologin on tty1 for the user named in `PS3_DESKTOP_USER` in `/etc/default/ps3-desktop` (unset: the normal login prompt). The generator also keeps tty1 from being reset, so the boot splash (Plymouth, quit with `--retain-splash`) stays up through the login until X draws; `/etc/profile.d/ps3-desktop.sh` starts the desktop in the login shell and switches tty1 back to text whenever no X runs on it. Logging out of the desktop drops back to the console. The splash is covered in [PACKAGING.md](PACKAGING.md).

`ps3-session` is the X session script. In order:

1. It puts `/etc/xdg/ps3` first among the XDG config directories, so the PS3 defaults (Openbox, tint2, GTK, PCManFM) apply and the user's own `~/.config` files still win.
2. It paints the root window with the splash's last frame and, once PCManFM's desktop window covers it, goes back to plain blue (`#14304d`) so the 8 MB picture does not stay in video memory.
3. It sets `GDK_GL=disable`, so GTK programs do not load the GL driver just to probe it (that cost RAM and GART for nothing; games use GL directly).
4. It writes the menu entries for the installed games through `ps3-game` (`ps3-game-menu`, into the runtime directory, first in `XDG_DATA_DIRS`), so every game in the menu starts in game mode.
5. It starts the helpers in `ps3-desktop.slice` (the cgroup game mode freezes): `pcmanfm --desktop`, tint2 (with jgmenu, the tray and the clock), nm-applet, pnmixer, lxpolkit and dunst. Bluetooth is `blueman` from the menu, with no resident tray (it cost about 60 MB of Python). NetworkManager never manages eth0.
6. It starts `ps3-game-detect` and then `exec`s Openbox, which stays with X in `ps3-display.slice` and is never frozen.

The menu has Shut down and Reboot entries. The user D-Bus runs under systemd (`dbus-user-session`).

### Packages

`ps3-desktop` holds the session, autologin, game mode, the Openbox/tint2/GTK/PCManFM defaults in `/etc/xdg/ps3`, the default applications and the X configuration. It is currently at 0.474 in the repository build (0.474 is installed on the PS3). `ps3-apps` is the everyday applications, all light GTK programs that suit 256 MB of RAM: Mousepad, GPicView, zathura, Xarchiver, Galculator, LXTask, scrot (on the Print key) and mpv. PCManFM opens files with them by default, and removing `ps3-apps` removes them. `ps3-games` pulls in the games that run on the RSX; see [games/README.md](games/README.md).

### The X setup

Xorg runs the `modesetting` driver on `ps3rsx` with `glamor` (GL-accelerated 2D through Mesa nv30). `files/xorg/20-ps3rsx.conf` sets `kmsdev /dev/dri/card0`, `AccelMethod glamor`, `ShadowFB true`, and turns off `AutoAddGPU`. ps3rsx is an ordinary module (`/lib/modules/<ver>/extra`) loaded at boot from `modules-load.d`, and the patched Mesa is found without environment variables: `ld.so.conf.d/00-ps3mesa.conf` comes ahead of Debian's, and our build has its driver paths compiled in. The text consoles (Ctrl+Alt+F1 to F6) work, because ps3rsx owns the RSX only while a DRM master (X) is in front or clients without X are submitting; otherwise ps3fb gets it back and draws the console. After a VT switch X's GL clients pause until it returns. The console also comes back when X exits, and at boot.

Known gaps, both open:

- The cursor is software-drawn. X uses the cursor plane but nothing shows, and the 0x10d init froze the PS3 once (probably a wrong first argument; RPCS3 shows a3=1). It is to be tried supervised.
- Some tearing remains, since xserver 21.1's modesetting has no TearFree.

Both are listed in [backlog/platform.md](backlog/platform.md).

### Input

- **USB keyboard and mouse** work through evdev and libinput, nothing to configure.
- **DualShock 3 over USB** uses `hid-sony` (kernel config: `HID_SONY`, `SONY_FF`, `LEDS_CLASS`, `UHID`, `INPUT_UINPUT`). Outside games the joystick X driver maps the sticks to the pointer and the buttons to clicks (`files/xorg/40-joystick-pointer.conf`, matching joysticks): axis 1 and 2 are relative x and y, button 1 is a left click, button 2 a right click. Game mode takes the pads off the pointer while a game runs.
- **DualShock 3 over Bluetooth**: plug it into USB once. `ds3-pair` (in `ps3-system`, run by udev) writes the PS3's Bluetooth adapter address into the controller and registers it in BlueZ as trusted with its HID record (what BlueZ's sixaxis plugin does, without the agent that plugin needs), then restarts `bluetoothd`. Afterwards the PS button connects it wirelessly. Pairing works. SDL mappings per game, rumble and LEDs are not checked ([backlog/platform.md](backlog/platform.md)).
- **Bluetooth keyboards and mice**: `blueman` or `bluetoothctl`. `UHID` is in the kernel for Bluetooth LE devices.

## Game mode

### What it does

`ps3-game` registers a game with `ps3-game-guard`, the user service `ps3-game-mode.service` in `ps3-game.slice`, and starts it or joins it. Two games share one game mode, and it lasts until the last one is gone. The memory arrangement rests on three user slices:

| Slice | Holds | Setting |
|---|---|---|
| `ps3-display.slice` | X and Openbox | `MemoryLow=64M`, `CPUWeight=200` |
| `ps3-game.slice` | the game | `MemoryLow=128M`, `CPUWeight=400` |
| `ps3-desktop.slice` | the desktop helpers (PCManFM, tint2, applets) | `CPUWeight=50` |

`memory.low` tells the kernel to reclaim from everything else first, so the game and X stay resident. Entering runs in this order:

1. The gamepads come off the X pointer.
2. The PCManFM desktop is switched off (`--desktop-off`), and the root window's wallpaper pixmap is released with it.
3. jgmenu is closed and `ps3-x-ready` checks that X is not held (see below), then `ps3-desktop.slice` is frozen and its memory reclaimed, anonymous pages first.
4. The user manager's other services have their anonymous memory pushed to swap.
5. As root (`ps3-game-mode on`): `udisks2`, `polkit` and blueman's helper are frozen and reclaimed (the `FREEZE_UNITS` default); the other system services keep running and have their anonymous memory pushed to swap (`RECLAIM_SYSTEM=1`; they fault back what they use, a few ms the first time); the page cache is dropped, unmapped pages only, so running programs keep their code (`DROP_CACHES=1`), which leaves the CMA area the GART grows into free; and memory is compacted (`COMPACT=1`), because the GART and the VRAM swap work in 1 MB chunks.
6. The game starts after that, at most 30 s later.

Nothing is stopped for good: network, SSH, sound, Bluetooth (the DualShock 3), logging, time sync and apt keep running. A root watcher (`ps3-game-mode-watch.service`) undoes the system side once the guard's pid is gone, whatever happened to the guard.

**The kicker.** On kernels without the lost-decrementer fix, idle PPE threads sleep through their timer and the game stalls for seconds ([STALLS.md](STALLS.md)). `ps3-kick`, started by game mode as the unit `ps3-game-kick` (the stopgap, written before the fix was found), sends a cross-CPU wakeup to PPE thread 1 every 10 ms (`KICK_MS=10`). `PS3_GAME_KICK=on|off|force` (the game's environment, or `/etc/default/ps3-game-mode`; the game's value wins) controls it. It skips itself when the kernel has the fix (`/sys/module/kernel/parameters/ps3_idle_prep` reads `Y`), which is the case on the release kernel .472; `force` runs it anyway. It is to be retired once the fixed kernel is the default (PLAN 1.2). It has not been seen working through game mode on the PS3.

**Settings.** `/etc/default/ps3-game-mode` (not shipped; the defaults are the ones above) takes `FREEZE_UNITS`, `RECLAIM_SYSTEM`, `DROP_CACHES`, `COMPACT`, `KICK`, `KICK_MS` and `GAME_SYSCTLS` (`vm.*` values with one number, set only while a game runs and restored after; none by default). `PS3_GAME_MODE=pads` (only the gamepads part) and `PS3_GAME_PREFETCH=on|off|keep` are baselines for A/B runs on the same boot: `off` is game mode without the file record and fetch, `keep` leaves the desktop its file pages and sends only its anonymous memory to swap.

### Leaving

Leaving starts when the last registered game is gone: the guard's `ExecStopPost`, the root watcher on the guard's pid, and any later `ps3-game` that finds state with no guard, all lead there. The order matters, because the first version was slow. After the first [OpenArena](../gamedb/games/openarena/) run on the PS3 (2026-10-17) the desktop took about 30 s to answer again: 1 s from the game's end to `leave`, 5 s more before `sudo ps3-game-mode off`, then about 20 s of the desktop's programs reloading from the USB disk as random reads, since game mode had pushed their file pages out. Now:

1. The desktop's files come back in one pass (`ps3-game-prefetch fetch`). At entering, `record` noted the files the desktop and display slices and the user manager have mapped, plus the wallpaper and the programs leave runs, only the pages that were in memory then. The fetch sorts them by disk position (FIEMAP), asks for them all with `POSIX_FADV_WILLNEED`, then reads.
2. Beside it, the desktop thaws at once. The cgroup's path is saved at entering, so no `systemctl` has to load first (that was part of the 5 s).
3. Then the gamepads and the PCManFM desktop come back, jgmenu is started again hidden with the environment it had (so the panel button opens it as quickly as before), and the system side last.

Each step goes to the user journal: `leave: desktop thawed`, `leave: PCManFM desktop started`, `leave: system side done`, `ps3-game-prefetch: fetched N files, X MB (Y MB were still in memory) ... all in after T s`, `left game mode`. Entering logs `recorded N files, X MB in memory`, which is what keeping them would cost. The return time after this change has not been measured on the PS3.

### How it starts

**Automatic.** `ps3-game-detect` is a user service that `ps3-session` starts. It watches X by events (one `xev -root` through an awk filter that passes only changes to the active window and to windows covering the screen), so nothing runs while idle or while a game plays: on the host, 0 CPU in 20 s either way and about 14 ms per focus change. A window counts as a game when:

- it is fullscreen (`_NET_WM_STATE_FULLSCREEN` or covering the screen), or it is a mapped override-redirect window covering the screen (SDL 1.2 does this);
- its process (`_NET_WM_PID`, else the X client's pid through XRes) has the GPU open (`/dev/dri`, or Mesa's driver mapped);
- it is not a browser or a video player (`PS3_GAME_EXCLUDE` adds names, `-name` removes one);
- it was not started through `ps3-game`.

Before joining game mode, the game and its children move out of `ps3-desktop.slice` into `ps3-game.slice/ps3-game-auto-<pid>-<start>.scope` (StartTransientUnit with the PIDs). A game whose output goes to a terminal or pipe of the desktop gets only the gamepads part, since freezing the desktop would block it. The detector registers it as `auto-<pid>` with its start time (`ps3-game-guard join`), next to `ps3-game`'s games, so both kinds share one reference-counted game mode. It leaves when the process exits or after 5 s without a fullscreen window (a `vid_restart`'s new window comes within that; F11 out of fullscreen or switching windows gives the desktop back); fullscreen again enters again.

**From the menu.** The menu's launchers stay, since game mode is on before a game loads, and the software-rendered modes ([dsda-doom](../gamedb/games/dsda-doom/), [OpenTTD](../gamedb/games/openttd/)) have no GPU for the detector to see.

**The switch.** `PS3_GAME_MODE` takes `on` (the default), `pads` or `off`, read from each game's own environment. System-wide it goes in `/etc/environment` (pam_env at the tty1 login; Debian's `/usr/lib/environment.d/99-environment.conf` links it for the user manager, whose PAM stack has no pam_env). A user's value goes in `~/.profile` or `~/.config/environment.d/*.conf` (`ps3-session` takes the latter over unless `~/.profile` set its own). One game's goes in its menu entry (`Exec=env PS3_GAME_MODE=off ...`) or on its command line. A game with `PS3_GAME_MODE=on` gets game mode when the default is off.

### Getting out of a game

A hung game in game mode must not trap you (the user, 2026-10-18). At the TV:

- **Ctrl+Alt+Esc** closes the game in front and ends game mode. It is an Openbox keybinding that runs `ps3-game-escape`; Openbox lives with X in `ps3-display.slice` and is never frozen.
- **Hold the PS button for 3 s** on the DualShock 3 (or a pad's guide/home button) does the same. A short press stays the game's. `ps3-pad-escape` reads the pads' event devices (no grab) only while game mode is on, so it works even when a game holds the keyboard. It is Python and costs a few MB while game mode is on.
- **Alt+Tab** to another window, or **Win+D** (show the desktop), during a menu game: 3 s later the desktop thaws (panel, terminals and file manager answer again; the PCManFM icons stay off until the game ends) and game mode stays on. Back in the game, the desktop is frozen again. A game the detector found, not one from the menu, leaves game mode after 5 s instead.

**These keys kill the game. Unsaved progress is lost.** The escape closes every game registered with game mode by its whole scope in `ps3-game.slice` (`systemctl --user kill`): SIGTERM, SIGKILL 3 s later for what is left. The registrations are dropped, so the guard leaves at once and everything comes back as when a game quits. A game that SIGKILL cannot end (stuck in the kernel) does not keep the desktop frozen: the guard's service is stopped then. Outside game mode Ctrl+Alt+Esc only closes a program whose window is fullscreen (a game with `PS3_GAME_MODE=off`), nothing else. A notice (xmessage) says "Closing the game..."; the journal has `ps3-game-escape` lines. The game's environment can set `PS3_GAME_PAD_ESCAPE=off` and `PS3_GAME_PAD_HOLD_S`.

### The menu freeze (2026-10-18, fixed in 0.456)

Game mode's freeze caught jgmenu, which stays running between uses, while it held its keyboard and pointer grab: X stopped answering other clients until jgmenu was killed, so the game just started from that menu found no X (found by [Luanti](../gamedb/games/luanti/investigations/2026-10-18-menu-freeze.md), started from the Games menu).

The fix: before freezing `ps3-desktop.slice` the guard closes jgmenu (SIGTERM; restarted hidden at the end of leave). Then `ps3-x-ready` (Python, ctypes) checks X. It grabs the keyboard and pointer for a moment. If one is already grabbed while an override-redirect window (a menu or popup) of a `ps3-desktop.slice` program is mapped, that program holds it. X not answering within 6 s is a server grab. Either defers the freeze: 5 s at entering, then every 3 s for a minute while the game runs, with the rest of game mode going on. It checks again right after freezing and thaws if X is held then (a program caught in the middle of a grab). A grab with no desktop popup is the game's own and is left alone. The journal says `jgmenu closed`, `the desktop not frozen yet: ...`, `the desktop frozen now`.

We considered freezing only once the game's window is mapped and left it out: `ps3-game` enters before the game starts on purpose, so the game loads into the memory game mode freed, and its prefetch comes after the page cache drop.

### Game prefetch, the desktop's side

Two things share the name. The desktop's files read back on leaving are described above and are on by default (`PS3_GAME_PREFETCH=on`, lists in the guard's state). The game's own files at its start belong to [STALLS.md, "Game prefetch"](STALLS.md#game-prefetch), which has the learning, the budget and the A/B.

On the desktop side: once game mode is on, `ps3-game-prefetch game-watch` reads what the game used last time, in disk order, at low I/O priority and within a memory budget, while the game loads. It serves both entry paths (`ps3-game` and the detector), looks at the game for its first 3 minutes, then exits so nothing of it stays in memory during play, and `leave` learns the list anew once the desktop is back. Lists live in `~/.cache/ps3-game-prefetch`. The game's environment sets `PS3_GAME_PREFETCH_GAME=on|fetch|learn|off`.

It is now off by default. The same-boot A/B on the PS3's USB disk (2026-10-18, kernel .433, 3 rounds, batch 16, results commit 296f99d) found no gain: [BananaBread](../gamedb/games/bananabread/)'s load was 18.1 to 18.6 s with prefetch against 15.2 to 18.8 s without, with the load's disk wait unchanged (4.5 to 5.5 s); [OpenArena](../gamedb/games/openarena/) gl2min loaded about 0.5 s faster with stalls within noise; [Luanti](../gamedb/games/luanti/) loaded 1 to 4 s slower, with two runs holding 3 s hitches in play; [SuperTuxKart](../gamedb/games/supertuxkart/) was unchanged. The sorted reads compete with the game's own on that disk. The code stays for a faster disk. ps3-desktop's commit b85d0b9 made the switch.

## What has run on the PS3

Seen there so far: the menu path (`ps3-game`, which caught the jgmenu freeze), the wallpaper and services getting out of the way, the slow return after [OpenArena](../gamedb/games/openarena/) (about 30 s, which led to the desktop prefetch), and the desktop itself since 2026-10-14. ps3-desktop 0.474 is installed (it carries 0.458's jgmenu fix, `ps3-x-ready` and the escape keys), and none of the escape keys has been tried at the TV.

Not seen on the PS3: the automatic start (`ps3-game-detect`, including F11 and a game started from a terminal), the desktop prefetch's return time, the kicker unit (`ps3-game-kick`; it skips itself on a kernel with `ps3_idle_prep` on), the game prefetch from game mode (the bench's learn runs used the harness's `game-run`), and `gamemode-check.sh`'s numbers. [TESTS.md](TESTS.md) lists "Game mode at the TV" as waiting for the user (PLAN 1.8 to 1.13, 4.2, 4.4); nothing has run since. The manual list for the user's return: [Luanti](../gamedb/games/luanti/) from the menu (sound and game mode), a terminal game with F11, the desktop's return time, Ctrl+Alt+Esc and the PS button hold, Alt+Tab and Win+D.

PLAN 2.2 is the benchmark round through game mode (games started from the desktop, as players do, on the same boot as a desktop-stopped baseline). Until it runs, the bench numbers say how builds compare, and not whether a player sees the same fps.

### The host tests

All in `packages/walnut/system/ps3-desktop/tests/` and `bench/`, none needing the PS3:

| Test | Covers |
|---|---|
| `menu-check.sh` | `ps3-game-menu`'s entries |
| `autogm-check.sh` | `ps3-game-detect` on Xvfb and Openbox with a stand-in GL game: 47 checks for the detector, plus checks 11 and 12 (a stand-in jgmenu holding both grabs that starts a game through `ps3-game`; a tray popup holding them when a game is detected) and 13 to 15 (a game ignoring SIGTERM closed by the escape, `BTN_MODE` events into the listener, Alt+Tab and Win+D through XTEST with Openbox). Needs Xvfb and openbox, or docker/podman; run by `make test-slow` |
| `gprefetch-check.py` | the prefetch program's learning and fetching (suite `gprefetch`) |
| `kick-check.sh` | the kicker's switches |
| `gamemode-check.sh` (`bench/`) | on the desktop: memory idle, in game mode and after, and checks that everything frozen or unloaded comes back (guard gone, nothing frozen, same helpers alive, PCManFM desktop and wallpaper, tray icons, pads, the user and system buses, X, ALSA, the network). Options `-t`, `-a`, `-m`, `--crash`, `--two`, `--sound`; results in `/var/tmp/ps3-bench/<stamp>-gamemode/`. It has never run on the PS3 |

## Early performance fixes (2026-10-14)

The first performance round, when Linux had 165 MB of RAM and the root was on NFS. The kernel now gives 217 to 222 MB (the release kernel; [KERNEL-MEMORY.md](KERNEL-MEMORY.md)), the root is on the USB disk, and swap is the VRAM swap in front of the USB swap partition plus zram; the table is kept for the reasons and for what is still true.

| Problem | Effect | Fix |
|---|---|---|
| `ps3_defconfig` enables lockdep and lock/list debugging (`PROVE_LOCKING`, `DEBUG_LOCKDEP`, `DEBUG_LIST`, `DEBUG_WW_MUTEX_SLOWPATH`, `DEBUG_RWSEMS`, `DEBUG_IRQFLAGS`) | every lock checked against a dependency graph; the kernel image took 30 MB in RAM (lockdep tables about 10 MB of BSS) | turned off: 15 MB |
| NFS root default `rsize`/`wsize` 4 KB | 8 MB/s sequential, about 24 ms per page-fault read, about 95% system time during installs | 64 KB on the command line |
| PulseAudio (33 MB), blueman's helper (25 MB) running at idle | about 58 of 165 MB | removed / on demand |
| No perf events | could not profile | `CONFIG_PERF_EVENTS` |

Measured after those fixes:

- Boot: systemd 13.8 s (1.2 s kernel, 12.6 s userspace); SSH answered 21 s after the kexec (was about 40 s). RAM 177 MB total, 117 MB available at the shell (was 165 and about 80).
- Installing the icon theme took 86 s; with 4 KB NFS it had not finished unpacking after 20 minutes.
- LXQt's start (X up to the desktop window) took 25 s (was 2 to 3 minutes). Idle LXQt: 75 MB available, 90 MB in zram, no swap traffic; lxqt-panel 24 MB, pcmanfm-qt 32 MB, Xorg 21 MB RSS.
- A CPU-bound shell loop took 11.2 s, 11x slower than on the Ryzen-class dev PC: branchy, interpreted code is where the in-order PPE hurts most. The PPE per thread is closer to a Pentium 4 than a Core 2.
- The GART reservation went to 16 MB on 2026-10-15 (from 32). Measured with Openbox and [ClassiCube](../gamedb/games/classicube/): 27 MB used while Mesa put vertex buffers in GART and every process and context had 4 x 512 KB push buffers; 9 MB once vertex buffers moved to VRAM and PS3 push buffers shrank (2 x 64 KB per screen, 2 x 256 KB per context). (The GART now grows on demand from CMA.) ClassiCube's occasional stutters then coincided with kswapd/zram activity and NFS major faults.
- Under LXQt on NFS, the PS3 re-read 10 GB in about 30 minutes, and one `apt-get` sat in D state for 20+ minutes on a file page (`filemap_fault`) while other NFS reads worked; only a reboot cleared it. Package work is done with the desktop stopped.
- `top` and `vmstat` show about 98% hardirq ("hi") even with X stopped and ps3rsx unloaded: an artifact of tick-based CPU accounting on the PS3, not load.
- Don't run `perf record -a -g`: it hung the PS3 (its `sleep 60` workload blocked in D state about 4 minutes after boot, then userspace and the network stopped, with netconsole printing hung-task warnings). The suspect is system-wide call-graph sampling through the Cell PMU behind lv1. Profiling approaches that work are in [PROFILING.md](PROFILING.md).
- zram compresses these pages about 5:1 (lzo-rle). The kernel has no PSI (`/proc/pressure`), so `MemAvailable` and `SwapFree` are the signals; a 2 s memory log (`~/mem.log`) is how to tell why SSH stopped answering (it did on the first LXQt run with PulseAudio and blueman running).
- The `os_area_queue_work_handler: Could not update FLASH ROM` line on netconsole is the kernel wanting to save the RTC offset to flash. Flash writes are compiled out; nothing was written.

Graphics fixes found while bringing the desktop up (2026-10-14):

- Resizing GL windows leaked about 3 window-sized buffers per resize until VRAM ran out; Mesa 0011 makes nv30 surfaces release their texture. Graph exception 0x106 came from a swizzled render target 1 pixel wide, which glamor creates; Mesa 0012 keeps them linear, and the validator rejects them. Checked with a 400-step drag of `xeglgears` in LXQt: no exception, constant memory.
- The glibc `memcpy` into VRAM mappings hits a `dcbz` trap and runs at 19 MB/s. Antialiased drawing (xclock-style) took glxgears from 58 to about 12 fps for that reason. A store loop for memcpys into VRAM mappings, and counting the alignment faults, are listed in [backlog/platform.md](backlog/platform.md).

## Not done

Open items from the desktop plans, moved to [backlog/platform.md](backlog/platform.md): the hardware cursor, TearFree, sound through PulseAudio or PipeWire if ALSA alone is not enough, desktop application quality (the browser, mpv video decode, responsiveness, the `dcbz` fix), Wi-Fi from nm-applet and NetworkManager owning eth0 now that the root is not on NFS, and the couch session with only the controller (PLAN 4.7, 4.8, 4.11, 4.14).

## History

- 2026-10-14: LXQt chosen and brought up (`startx /usr/bin/startlxqt`, no Mesa environment variables) with the root on NFS. Memory was the limit, so it moved to IceWM, then on 2026-10-15 to Openbox after the comparison above. The plan, the LXQt package list and its kernel changes are in the archive.
- 2026-10-17: the rule "memory comes from game mode, not from trimming the desktop" and the first game mode (PLAN, CLAUDE.md "Memory trims").
- 2026-10-18: the menu freeze (0.456), the escape keys (0.458), game prefetch switched off by default (0.474).

Older versions and the dated history: [archive/DESKTOP-PLAN.md](archive/DESKTOP-PLAN.md), [archive/PERF.md](archive/PERF.md).
