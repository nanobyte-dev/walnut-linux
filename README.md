# Walnut Linux

> **Status: 0.1-alpha in preparation, not released yet.** The install guide is written; the downloads and
> the package repository at walnut.nanobyte.dev come with the release.

**Walnut Linux** is a modern Debian-based Linux distribution (ppc64 big-endian) for the Sony PlayStation 3, featuring native 3D graphics acceleration on the RSX GPU via our `ps3rsx` kernel driver and Mesa nv30.

Walnut provides a lightweight desktop environment tailored to the console's 256 MB memory limit, automatic game mode, Synergistic Processing Element (SPE) coprocessor acceleration for audio and graphics tasks, and a full suite of playable 3D games and applications.

---

## Hardware Requirements

- **Console**: a PlayStation 3 that can run Custom Firmware (CFW, e.g. Evilnat 4.92) and OtherOS++. This alpha has been tested on one console only, a Slim CECH-20xx; other CFW-capable models (Fat, other Slims) should work but are untested, and reports are welcome. SuperSlims and consoles that can't run CFW can't run it.
- **Bootloader**: OtherOS++ enabled with Petitboot installed.
- **Storage**:
  - **USB install**: A USB hard disk or flash drive (8 GB or larger recommended).
  - **Internal install**: dev_hdd0 / dev_hdd1 via Petitboot and webMAN.
- **Input**: USB keyboard and mouse (required for setup and desktop). DualShock 3 controllers are supported via USB and Bluetooth.
- **Display**: HDMI or AV Multi-Out (1080p, 720p, or SD).
- **Network**: Wired Ethernet (recommended) or Wi-Fi.

---

## What Works

- **Kernel**: Linux 6.12 LTS with PS3-specific platform support, gelic Ethernet, spufs, and custom memory management.
- **3D Graphics**: Hardware acceleration on the RSX GPU through the `ps3rsx` DRM driver and Mesa Gallium nv30 (OpenGL 2.1).
- **SPU Coprocessor**: libspeq resident SPE workers handling audio synthesis, DXT1/DXT5 texture compression, and matrix operations.
- **Desktop**: Fast, lightweight X11 desktop (Openbox, tint2, PCManFM, jgmenu) configured to stay within 256 MB RAM.
- **Game Mode**: Automatic detection of fullscreen 3D applications; unloads desktop components and frees memory during gameplay, restoring them automatically on exit.
- **Sound**: ALSA and OpenAL Soft with AltiVec and SPE acceleration, low-latency playback.

### Tested Games at a Glance

All 13 games of the benchmark suite run at 1920x1080 with full hardware acceleration:

| Game | Runs | Steady fps | Stalls / 1% low / longest | Free RAM / VRAM (MB) | Picture, sound |
|---|---|---|---|---|---|
| [QuakeSpasm](docs/games/quake.md) | yes | 91.0 (row 58) | 2.5 s / 48.6 / 393 ms | 44 / 49 | right, right |
| [Yamagi Quake II](docs/games/quake.md) | yes | 68.7 (row 57, small PRNG default) | 0.1 s / 31.1 / 91 ms | 46 / 48 | right, right |
| [X-Moto](docs/games/light-games.md) | yes | 160.7; 162.1 with stream buffer (row 68) | 0.0 s / 106.7 / 43 ms | 51 / 37 | right, right |
| [Armagetron](docs/games/light-games.md) | yes | 83, live AI rounds (row 60) | 0.1 s / 24.1 / 127 ms | 58 / 28 | right, right |
| [Neverball](docs/games/light-games.md) | yes | 66.5 (row 54) | 51 frames > 100 ms / 41.3 / 3.9 s | 58 / 34 | right, 118 underruns |
| [OpenArena (opengl1)](docs/games/openarena.md) | yes | 70.6 (row 59) | 0.0 s / 34.6 / 56 ms | 26 / 39 | right, right |
| [Extreme Tux Racer](docs/games/light-games.md) | yes | 59 (row 69) | 2.9 s / 27.3 / 483 ms | 29 / 58 | right, right |
| [ClassiCube](docs/games/classicube.md) | yes | 46.1-48.1 on SSD (rows 71, 75); 45.9 on USB | 0.0 s / 30.7 / 65 ms | 53 / 60 | right, right |
| [dsda-doom (GL)](docs/games/doom.md) | yes | 55.9 (row 61) | 0.0 s / 40.3 / 34 ms | 43 / 65 | right, music choppy |
| [Luanti](docs/games/luanti.md) | yes | 46 on SSD (row 71); 45.5 on USB (row 67) | 3.5 s / 19.1 / 724 ms | 19 / 63 | right, right |
| [Sauerbraten](docs/games/sauerbraten.md) | yes | 39.1 on SSD; 45.1 with shortnormals (row 69) | 0.05 s / 25.6 / 103 ms | 25 / 117 | right, mic floor clean |
| [SuperTuxKart (`lean`)](docs/games/supertuxkart.md) | yes | 20-22 by Gallium HUD (row 61) | 0.2-3.6 s / 10.2-12.8 / 108-513 ms | 26 / 97 | track draws, sound running |
| [OpenArena (opengl2 `gl2min`)](docs/games/openarena.md) | yes | 60.4 on SSD (row 71); 55.9 on USB (row 70) | 0.2 s / 25.6 / 199 ms | 15 / 119 | right, right |
| [OpenTTD](docs/games/openttd.md) | yes | 34-43 with 8bpp blitter (now default) | 0.4 s / 13.4 / 210 ms | 17 / 18 | right, choppy music |

See [docs/games/README.md](docs/games/README.md) for detailed benchmarks, logs, and game-specific notes.

---

## Installation

> [!NOTE]
> Public release packages and pre-built image links will be available at `walnut.nanobyte.dev` (release candidate placeholder).

See [docs/INSTALL.md](docs/INSTALL.md) for detailed installation instructions:
1. **Prerequisites**: Ensure CFW and OtherOS++ are active and a NOR/NAND backup is stored safely off-console.
2. **Target Drive**: Prepare an external USB drive or configure internal HDD storage.
3. **Booting**: Boot into Petitboot and select the Walnut Linux entry.
4. **First-Run Wizard**: On first boot, the `walnut-firstboot` setup wizard will guide you through creating your user account, setting your hostname, timezone, and enabling or disabling SSH.

---

## Risks and Important Safety Rules

- **Cooling & Thermal Safety**: When running OtherOS/Linux, GameOS and webMAN fan control loops do not run. Before launching Petitboot, ensure your fan control in webMAN is set to **SYSCON mode** or a **fixed speed of at least 60%**. Never operate Linux with fans disabled or unattended. If a thermal alert occurs, shut down immediately.
- **NAND/NOR Flash Backup**: Always back up your console's flash memory before installing or repartitioning OtherOS. Keep this backup in a safe location off the console.
- **GameOS Coexistence**: Walnut Linux is designed to coexist with GameOS. GameOS (XMB) remains your primary operating system on boot.

---

## Developer Documentation & Layout

For repository structure, build directions, and developer workflows:
- [docs/LAYOUT.md](docs/LAYOUT.md): Source tree layout and `make` commands.
- [docs/README.md](docs/README.md): Index of design documentation, GPU architecture notes, and test logs.
- [docs/PACKAGING.md](docs/PACKAGING.md): Debian packaging, aptly repositories, and driver builds.
- [CLAUDE.md](CLAUDE.md): Project guidelines and constraints.

---

## License

See [LICENSE](LICENSE) for full licensing details across kernel, userland, and media components.
