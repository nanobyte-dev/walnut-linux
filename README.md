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

- **Kernel**: Linux 6.12 LTS with the OtherOS++ patch set for the PS3 (see [Credits](#credits)), upstream fixes, and our own changes (a swap tier in video memory, the internal-disk write guard, fan and thermal handling).
- **3D Graphics**: Hardware acceleration on the RSX GPU through the `ps3rsx` DRM driver and Mesa Gallium nv30 (OpenGL 2.1).
- **SPEs**: libspeq, resident workers on the Cell's SPEs: Mesa's DXT1/DXT5 texture compression (every GL game), fluidsynth's voice DSP (MIDI music), and an optional skinning path for BananaBread. Programs load it at run time, so they also run without it, on the PPE ([docs/SPU.md](docs/SPU.md)).
- **Desktop**: Fast, lightweight X11 desktop (Openbox, tint2, PCManFM, jgmenu) configured to stay within 256 MB RAM.
- **Game Mode**: Automatic detection of fullscreen 3D applications; unloads desktop components and frees memory during gameplay, restoring them automatically on exit.
- **Sound**: ALSA, and OpenAL Soft on the PPE with AltiVec; MIDI music through fluidsynth, whose voice DSP runs on the SPEs ([docs/SPU-AUDIO.md](docs/SPU-AUDIO.md)).

### Tested Games at a Glance

All 13 games of the benchmark suite run at 1920x1080 with full hardware acceleration. The numbers are
the suite's steady state (desktop stopped, vsync off, a 60 s warm-up and a 120 s window; the
summary batch `suite-summary-2026-10` in [gamedb/benchmarks](gamedb/benchmarks/README.md), every value
with its source), the medal comes from the game's played sessions on the
[Walnut Games site](https://walnut.nanobyte.dev/gamedb/):

<!-- gamedb:suite-table -->
| Game | Medal (played) | Steady fps | Stalls / 1% low / longest | Free RAM / VRAM (MB) | Picture, sound |
|---|---|---|---|---|---|
| [X-Moto](https://walnut.nanobyte.dev/gamedb/games/xmoto/) | Platinum · 96 | 160.7 | 0 s / 106.7 / 43 ms | 51 / 37 | right, right |
| [OpenArena, opengl1](https://walnut.nanobyte.dev/gamedb/games/openarena/) | Gold · 82 | 70.6 | 0 s / 34.6 / 56 ms | 26 / 39 | right, right |
| [Yamagi Quake II](https://walnut.nanobyte.dev/gamedb/games/yquake2/) | Gold · 88 | 68.7 | 0.1 s / 31.1 / 91 ms | 46 / 48 | right, right (our fix) |
| [OpenArena, opengl2](https://walnut.nanobyte.dev/gamedb/games/openarena/) | Gold · 82 | 60.4 | 0.2 s / 25.6 / 199 ms | 15 / 119 | right, right |
| [QuakeSpasm](https://walnut.nanobyte.dev/gamedb/games/quakespasm/) | Platinum · 93 | 91 | 2.5 s / 48.6 / 393 ms | 44 / 49 | right, right |
| [Armagetron](https://walnut.nanobyte.dev/gamedb/games/armagetronad/) | Platinum · 95 | 83 | 0.1 s / 24.1 / 127 ms | 58 / 28 | right, right |
| ClassiCube | not rated yet | 47 | 0 s / 30.7 / 65 ms | 53 / 60 | right, right |
| [Extreme Tux Racer](https://walnut.nanobyte.dev/gamedb/games/etr/) | Gold · 80 | 59 | 2.9 s / 27.3 / 483 ms | 29 / 58 | right, right |
| [Luanti](https://walnut.nanobyte.dev/gamedb/games/luanti/) | Scrap · 56 | 46 | 3.5 s / 19.1 / 724 ms | 19 / 63 | right, no underruns |
| [dsda-doom](https://walnut.nanobyte.dev/gamedb/games/dsda-doom/) | Gold · 90 | 55.9 | 0 s / 40.3 / 34 ms | 43 / 65 | right, music chops |
| [BananaBread](https://walnut.nanobyte.dev/gamedb/games/bananabread/) | Scrap · 51 | 39.1 | 0.05 s / 25.6 / 103 ms | 25 / 117 | right, 60-181 underruns a run |
| [Neverball](https://walnut.nanobyte.dev/gamedb/games/neverball/) | Gold · 87 | 66.5 | 2 s / 41.3 / 3.9 s | 58 / 34 | right, 118 underruns |
| [OpenTTD, 8bpp](https://walnut.nanobyte.dev/gamedb/games/openttd/) | Gold · 83 | 38.3 | 0.4 s / 13.4 / 210 ms | 41 / 18 | right, choppy (3500-4700 underruns) |
| [SuperTuxKart lean](https://walnut.nanobyte.dev/gamedb/games/supertuxkart/) | Silver · 69 | 21 | 1.8 s / 11.8 / 513 ms | 26 / 97 | track draws, sound running |
| [OpenTTD, 32bpp](https://walnut.nanobyte.dev/gamedb/games/openttd/) | Gold · 83 | 11 | 52 s / 5.7 / 311 ms | 17 / 18 | right, choppy (3500-4700 underruns) |
<!-- /gamedb:suite-table -->

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
- The tree: `packages/` holds every source package (`walnut/` our own, `patched/` Debian's or upstream's with our patches, `repackaged/` upstream releases as they are), `apt-repo/` and `servers/` the apt repository and walnut.nanobyte.dev, `website/` the site and the Walnut Games database, `system/` the image and the installer, `bench/` the benchmark harness, `build-containers/`, `github-ci/` and `tools/` the build machinery.
- [docs/LAYOUT.md](docs/LAYOUT.md): Source tree layout and `make` commands.
- [docs/README.md](docs/README.md): Index of design documentation, GPU architecture notes, and test logs.
- [docs/PACKAGING.md](docs/PACKAGING.md): Debian packaging, the apt repositories, and driver builds.
- [CLAUDE.md](CLAUDE.md): Project guidelines and constraints.

---

## Credits

Walnut stands on other people's work:

- **PS3 kernel patches** (`packages/patched/kernel/linux/patches/ps3/`): glevand's OtherOS++ patches, with graf_chokolo's work, refreshed for current kernels by Sombat Tragoongoson (psx-place) and carried for Gentoo by Damian Dudycz ([damiandudycz/ps3](https://github.com/damiandudycz/ps3)), whose repository we took them from.
- **Mainline PS3 support**: Geoff Levand, and his [ps3-linux queue](https://git.kernel.org/pub/scm/linux/kernel/git/geoff/ps3-linux.git/) on kernel.org.
- **Kernel fixes** (`packages/patched/kernel/linux/patches/local/0020`-`0032`): upstream and linuxppc-dev fixes by their authors, named in each patch.
- **Booting**: OtherOS++ and Petitboot.
- **Graphics**: Mesa's nv30 driver from the nouveau project.
- **Base system**: Debian and its ppc64 port (debian-ports).

Ours: the `ps3rsx` GPU driver, our kernel changes in `packages/patched/kernel/linux/patches/local/`, the packaging, installer and tools. Where each kernel patch came from is in [docs/OTHEROS-PATCHES.md](docs/OTHEROS-PATCHES.md).

## License

See [LICENSE](LICENSE) for full licensing details across kernel, userland, and media components.
