# Walnut 0.1-alpha: the release plan

## Context

Everything so far runs on one console and one LAN. The image has the builder's user and SSH key in
it, plus netconsole IPs, a 1080p-only video mode and licensed game data. The apt repo is unsigned
and LAN-only, and its `ps3` suite holds old Mojang and Quake data. The installers are developer
tools, and there are no user docs.

The goal is a first public alpha, **Walnut**, that a stranger can install, with:
- a PC-side installer;
- a first-run wizard;
- signed apt suites and downloads on `walnut.nanobyte.dev`;
- the GitHub repo with its history;
- CI;
- articles.

Decisions (the user, 2026-10-20/21):
- **Name:** "Walnut" in everything public (distro, repo, site, images, installer, docs); "PS3" only where it names the hardware. The version suffix stays `+ps3.N`.
- **Hosting:** `walnut.nanobyte.dev` on the OVH box, with apt suites `stable` (gated) and `testing`, plus a password-protected `licensed` suite for the user only.
- **GitHub:** the full history is kept. Only the AI attribution trailers are filtered out of commit messages.
- **Install targets:**
  1. **Internal (the default):** the image file on dev_hdd0, with boot files on dev_hdd1 and a webMAN autoexec restore. The dev_hdd1 path gets tested first.
  2. **External USB.** No OtherOS++ region reformat.
- **Installer runs on a PC**, Linux or Windows: a USB write, or a webMAN FTP upload. A GameOS app comes later.
- **First-run wizard:** Debian-style, it creates the first sudo user.
- **CI:** GitHub Actions with per-package workflows (testing on push, stable gated) and an image release workflow.
- **Articles:** start with the early hard problems (booting Linux, the kernel, the GPU).

The plan was reviewed by an agent against the code (2026-10-21); its blockers are folded in below and marked (R).

## Milestones

### M0. Prove the internal boot path, in stages (the user at the console)

Each stage runs from the working USB or SSD root, with the fallback plugged in (R: B5).
1. **The guard kernel.** Commit `packages/patched/kernel/linux/patches/series` and 0048 (the user's OK), then build the release kernel with local/0045, 0047 and 0048. Boot it from the USB root and run GAMEOS-HDD.md "On the PS3" steps 1-5, including the write-back tests, not only "sealed".
2. **Read-only checks.** At Petitboot's shell (GAMEOS-HDD.md:855-857): `/proc/partitions`, `ls /var/petitboot/mnt/ps3dc`, `ps3-bl-option -o/-p`. Then the `ps3-gameos-mount` check of dev_hdd1 (GAMEOS-HDD.md:92-98).
3. **dev_hdd1 as a writable boot partition** (R: B6).
   - `tools/ssdimg/initrd/init:164-169` and `ps3-gameos-mount` allow writes to ps3dc only after `blkid` shows GameOS's vfat, and never to ps3da or ps3db.
   - A refusal if ps3db isn't UFS2 or ps3dc isn't vfat (R: gap 9).
4. **Boot from dev_hdd1** with the USB disk still plugged in, then without it.
5. **Survival.** Several GameOS sessions with games played in between: does dev_hdd1 keep the files, and does the autoexec mirror (`packages/walnut/system/ps3-boot/webman/autoexec.bat:7-11`, which copies dev_hdd1 to dev_hdd0) work, including whether webMAN's `copy` takes a directory (GAMEOS-HDD.md:893)?
   - The installer also writes a separate **known-good set** on dev_hdd0 that the mirror never overwrites, so a bad kernel can't wipe out the good backup (R: B6).
6. **Record it** in TESTS.md and GAMEOS-HDD.md. If dev_hdd1 doesn't hold up, the fallback is a small USB stick for `/boot`.

**M0 status (2026-10-21):** steps 1-4 done on the PS3: the guard kernel (.648) seals exactly the image and refuses everything else; Petitboot mounts dev_hdd1 by itself; this console now boots `walnut` from dev_hdd1 with no USB disk (generic initramfs, saved map), and kernel updates go to dev_hdd1 by themselves (TESTS.md 10-21). Left: dev_hdd1 across several GameOS sessions, the autoexec restore, walnut-prev after a second kernel, the package shipping initrd-walnut.img.

### M1. The internal install, redesigned: stamped upload, install on the console (R: B1, B7, gap 1)

The PC never builds ext4. It only uploads, which reuses `tools/ssdimg/stamp.py`'s streaming FTP upload and works natively on Windows without e2fsprogs.
1. **What the PC uploads:**
   - the stamped image file to `/dev_hdd0/linux/walnut.img`;
   - `rootfs.tar.xz` and the generic boot files to dev_hdd1.

   The stamped header goes last, so a partial upload never validates. Sizes are whole UFS blocks (GAMEOS-HDD.md:119), and smaller sizes are offered for 40-80 GB consoles.
2. **Generic initramfs, one per walnut-boot version (R: B7).**
   - The kernel config gets `CONFIG_UFS_FS=y` (still no UFS write) and `CONFIG_BLK_DEV_DM=y`.
   - `/init` drops its release check; it already handles built-ins.
   - The kernel hook only copies `vmlinux` and rewrites kboot.conf on dev_hdd1.
3. **First boot (install mode) in the initramfs:**
   - FIBMAP the extents, reusing `cmd_recheck` in `ps3-imgmap-boot.c:754-790`;
   - **a C port of the full stamp verify** (Python `ps3-imgmap` `cmd_verify`, line 430);
   - save the map and its sha256 to dev_hdd1;
   - create dm-linear and seal it with the guard;
   - `mkfs.ext4`, unpack `rootfs.tar.xz` from dev_hdd1, then delete the tarball.
4. **Later boots:** today's path: the FIBMAP recheck against the saved map, the 256 header stamps (`header_check`, lines 799-835), then the guard. The format stays compatible with this console's current SSD root, so its boot is never broken.
5. **Fixed Petitboot labels** `walnut` and `walnut-prev` (`packages/walnut/system/ps3-boot/ps3-update-kboot:59-69`), different from the USB install's, so updates don't strand the user on an old entry (R: B7).
6. **Test:** `tools/ssdimg/vm-e2e.sh`, `test_ssdimg.py` and `guard-sim` first, then on the PS3 with a second image file (M0's guard kernel), with the user's go.

**Status (2026-10-21):** done in code and in the VM: the generic initramfs with install mode, the C verify, `stamp.py install` (manifest last), `ps3-hdd1-boot` (walnut/walnut-prev), the autoexec mirror apart from the known-good set; `tools/ssdimg/vm-e2e.sh` passes end to end (install, second boot, refusals). The PS3 steps are in GAMEOS-HDD.md "The internal install" (with M0).

### M2. A Walnut-clean tree and public image

1. **Private config.** The LAN values move from `config.mk:110-120` into a gitignored `config.local.mk`. The `10.0.0.77` defaults go out of `stamp.py` and `bench-cam.sh`.
   - *Status: Done (b49bc44). `config.local.mk` gitignored, `config.local.mk.example` created, `tools/config-get.sh` added.*
2. **`PROFILE=public` in `tools/build-image.sh`:**
   - **Licensed data:** no `ps3-games-licensed`, no licensed debs in the flat repo, and the `ps3-licensed` probe at `build-image.sh:47-53` switched off (R: B3).
   - **Packages:** no debug kernel or debug ps3rsx. A public `ps3-games` without the data-less entries: classicube, quakespasm and yamagi-quake2 depend on licensed data (`packages/walnut/system/ps3-desktop/debian/control:40`).
   - **Accounts:** no `tibi`, no key, no NOPASSWD sudo, root locked (`customize.sh:32`), and the `build-image.sh:34` autologin user removed.
   - **Kernel arguments:** no netconsole and **no `video=`** (`packages/patched/kernel/linux/flavours/release.args`), so the video mode follows Petitboot's, which works on 720p/SD TVs (R: B9).
   - **DNS** through NetworkManager or systemd-resolved instead of `/proc/net/pnp`. That's a ps3-system change to `00-ps3.conf` (`dns=none`, eth0 unmanaged) (R: B8).
   - *Status: Done (a3ca92f). `PROFILE=public` supported in `tools/build-image.sh`, `system/image/customize.sh`, and `Makefile`. `ps3-games-free` metapackage added to `packages/walnut/system/ps3-desktop/debian/control`. Root locked, no default user, NetworkManager manages DNS and eth0 on public profile, dev profile kept intact. Kernel args stripped of netconsole, ip=dhcp, and video= in public `/etc/default/ps3-boot` and `/etc/kboot.conf`. (Note for later: `packages/patched/kernel/linux/flavours/release.args` still contains `video=ps3fb:mode:133`; it should be removed upstream from `release.args` when kernel patches are updated so the video mode follows Petitboot natively without image-side stripping).*
3. **First-run wizard `walnut-firstboot`:**
   - a text UI on tty1 before autologin, with a USB keyboard;
   - it asks for the user and password (sudo, video, render, audio, input), hostname and timezone, and whether to turn on SSH (off by default);
   - it checks the clock, since 0044 means `rtc_diff` isn't saved and a wrong clock breaks apt's signature checks (R: gap 9);
   - it sets autologin, then runs `daemon-reload` and restarts getty;
   - an optional preseed file comes from the installer.
   - *Status: Done (bda98d7). `walnut-firstboot` and `walnut-firstboot.service` added to `ps3-system`. Whiptail/dialog text UI on tty1 before getty, sets user/pwd/groups/hostname/timezone/SSH, checks clock against release date threshold, writes `PS3_DESKTOP_USER` to `/etc/default/ps3-desktop`, reloads daemon, restarts getty. Preseed `walnut-setup.conf` supported. Tested via `--dry-run` and `bash -n`.*
4. **User-visible names** become Walnut: README, docs, image and installer names, hostname, splash text. **Package renames are deferred** (see "Cuts").
   - *Status: Done (3fed924). Default hostname is walnut, splash theme is walnut, docs and README use Walnut Linux.*
5. **Licenses.** A top-level `LICENSE`, SPDX headers, and the wallpaper and logo sources. The kernel parts are GPL-2.0-only.
   - *Status: Done (3fed924). Top-level `LICENSE` created with GPL-2.0-only for kernel/RSX, userland license pending user decision (no SPDX yet), CC BY-NC 4.0 for wallpapers, logo procedural SVG sources documented.*
6. **Public README;** the current one becomes `docs/LAYOUT.md`.
   - *Status: Done (3fed924). Public `README.md` created with 13-game suite table, hardware requirements, risks/safety, and install placeholder. Old README moved to `docs/LAYOUT.md`, links fixed in `CLAUDE.md` and `docs/README.md`.*
7. **USB install:** the fixed PARTUUID `50533301-02` becomes a random disk id per install (R: gap 10).
   - *Status: Done (5f77cc5). Random disk id per install in `tools/build-image.sh` and `tools/usb-root-remote.sh`.*

### M3. Licensed separation everywhere (R: B2, B3)

1. **ClassiCube.** Split the source: engine and data. `gamedb/games/classicube/packages/classicube/package.conf`'s `COMPONENTS` hold Mojang's texpack and audio, which would otherwise ship as deb-src orig components and be downloaded on a CI runner.
   - *Status: Done (cced0cc, 35c2a3c). Engine source repacked `+dfsg` without console port assets and Mojang textures; data in `classicube-data`.*
2. **Sources and the leak scan.** Source publishing excludes every licensed source: sauerbraten-nonfree-data, ps3-games-licensed, and the rct2, tyrian and quake data. The leak scan also checks source tarballs.
   - *Status: Done (35c2a3c). `tools/aptly-publish.sh` and `tools/repo-publish.sh` route licensed sources to `ps3-licensed`, free sources leak-checked against licensed debs and `tools/licensed-files.txt` via `tools/leak-check.py`. Test suite `test_leak_check.py` integrated into `tools/run-tests.sh`.*
3. **New aptly local repos** for `stable` and `testing`, never the tainted `ps3-main` or its `img-*` snapshots. (Built instead without a database: `apt-repo/walnut-repo.py`, M6. The LAN repository and its aptly state were retired on 2026-10-26.)
   - *Status: Done (bff7a9d). Documented in `docs/PACKAGING.md`; `walnut-testing`, `walnut-stable`, `walnut-deps`, `walnut-licensed` never include legacy `ps3-main` or `img-*` snapshots.*

### M4. walnut.nanobyte.dev: signed suites, sources, downloads

1. **Keys.** The **stable key stays offline** on the PC, off any runner. A CI subkey signs `testing` only. The installer pins the fingerprint, and expiry and rotation are planned. `tools/aptly.conf` turns signing on (today `gpgDisableSign: true`).
2. **The suites:**
   - **`stable`:** only **promotes the exact tested binaries** from `testing` (an aptly copy or snapshot, no rebuild), with the user's approval (archive/RELEASE-CANDIDATES-2026-10-19.md, its first lines).
   - **`testing`:** the newest 5 of each package.
   - **`licensed`:** behind basic auth, set up in the OVH "main-proxy" docker, HTTPS only.
3. **Debian base and GPL.**
   - Extend the existing `deps` component (`make deps`) so the image's Debian packages come from our repo at the pinned snapshot, rather than live sid or rate-limited snapshot.debian.org.
   - Publish `deb-src` for ours and for the mirrored Debian packages (GPL source offer).
4. **apt config.** `walnut-system` ships signed sources for `stable` (and opt-in `testing`). The `ps3.pref` pins (`o=PS3, n=ps3`) are updated to the new origin. The LAN sources move to a dev-only file.
   - *Status: Done (2026-10-23). ps3-system ships `walnut.sources` (stable on, testing `Enabled: no`), both keyrings and `preferences.d/walnut` (o=Walnut); the LAN sources (`ps3-lan.*`) were for the dev image and `make deploy` only, and went with the LAN repository on 2026-10-26.*

**M4 status (2026-10-21):** walnut.nanobyte.dev is up on the OVH box (/data/nanobyte/Walnut, behind the main proxy and its *.nanobyte.dev certificate), set up by `make walnut-server` (servers/walnut.nanobyte.dev/deploy.sh):
- `walnut-repo`: nginx with the site built in (`walnut-site:<id>`), serving www/apt, www/download and www/licensed (basic auth; the login is in the untracked config.local.mk). Noindex until the release.
- `walnut-upload`: the only way in for uploads. sshd + rrsync as an ordinary user on port 2223, keys only (servers/walnut.nanobyte.dev/deploy-keys.pub: the PC's and GitHub's), write-only into incoming/, no overwrite, no delete, no shell. Its host key is pinned in servers/walnut.nanobyte.dev/known_hosts. IPv4 only (over IPv6 docker-proxy hides the client's address); its log goes to the journal, where fail2ban's walnut-upload jail (servers/walnut.nanobyte.dev/fail2ban/) bans 5 failures in an hour for a day, growing to 4 weeks, in DOCKER-USER like the nginx-scanners jail. GitHub's runner ranges are no allowlist (7,000+ shared Azure CIDRs).
- `walnut-watch` (systemd path unit + 10 min timer, as the directory's owner): checks each upload against its manifest (uploaded last) and publishes it with no half-written moment: the site as a new image and container (back to the old one if it isn't healthy), a release directory in one rename, apt suites as new pool files then a dists generation switched by one symlink rename (old by-hash files carried over). Published files never change. Releases and apt suites must be signed by the archive key, `8FFE9D3FC1E24ADC470250BB57011432F8A98072` (ed25519, primary expires 2029-10-20, kept on the user's PC; the signing subkey `AC79A36489EB7EE1970CB804270E74ED906C508E` expires 2027-10-21 and is renewed with `gpg --quick-set-expire`; public: apt-repo/keyrings/archive-keyring.gpg, installed as keys/archive-keyring.gpg, and walnut.nanobyte.dev/walnut-archive.asc): a release carries SHA256SUMS + SHA256SUMS.asc listing exactly its files; each InRelease is checked with gpgv and every index file against its signed hashes. No keyring on the server: nothing signed is published. Tests: `make walnut-server-test`.
- Publishing: `servers/walnut.nanobyte.dev/walnut-upload.sh KIND ID DIR`; the site with `make walnut-site` or the GitHub workflow "Walnut site" (secret WALNUT_DEPLOY_KEY in the environment walnut-site).

Next: the archive key (the user), then the walnut-testing and walnut-stable aptly repos, signed and uploaded as apt bundles; with keys/archive-keyring.gpg on the server the watcher refuses an InRelease not signed by it.

### M5. The PC installer (`system/installer/walnut-install`, pure Python 3)

- **Common steps.** Download a release, check SHA256SUMS against the pinned key, and write the optional preseed. `--dry-run` shows every step.
- **`internal`** (Linux, and Windows natively): the M1 upload. It checks free space, shows an estimated time and needs a wired connection: 16 GiB took 784 s at 20.9 MB/s (GAMEOS-HDD.md:215). Resume works where webMAN supports REST/APPE, and that gets checked. `uninstall` removes everything.
- **`usb`** (Linux): `tools/usb-root-remote.sh`'s logic, with a USB transport check, a typed disk-model confirmation and a random disk id. The WSL2 USB path is deferred.

### M6. CI on GitHub Actions (R: B4, gap 3)

- **The runner** goes in a dedicated VM or on the OVH box, never this PC. Builds run `docker --privileged`, which makes a job root on the host, and this PC holds `backups/`, `~/.ssh` and the keys.
- **Only `push` to main and `workflow_dispatch`;** no fork `pull_request` triggers.
- **One aptly home** with a `concurrency:` group. The local `make publish` stops writing the public suites.
- **Checkouts use `fetch-depth: 0`,** because `deb-version.sh` counts commits.
  - Path filters include the shared tooling, since a tooling change bumps every package's N.
  - ps3rsx-modules follows linux.
  - ps3-apps and ps3-games build with the `ps3-desktop` source.
- **Workflows:**
  - `build-package.yml`, reusable: build, then publish to `testing`;
  - `promote-stable.yml`: gated, copies tested binaries to `stable`;
  - `release-image.yml`: gated, builds the image from walnut.nanobyte.dev's suite (`WALNUT_SUITE`), `make rootfs PROFILE=public` and `make checksums`, then uploads to `/download/<ver>/` and creates the GitHub release.
- **Licensed packages are never built in CI.**
- **Prerequisite:** the first stable publish (D1, the image session) hasn't run yet, so the first `stable` comes from this flow.

**M6 status (2026-10-21):** in place and proven on ps3-system (5 min) and zlib-ng (native under qemu, 16 min).
- Every package GitHub may build (all but the licensed ones: tools/licensed.py) has `.github/workflows/pkg-<name>.yml`, generated by `github-ci/gen-workflows.py`, calling `.github/workflows/walnut-package.yml`. A push touching what its version counts (tools/deb-version.sh) builds it for testing; "Run workflow" has a target list, testing or stable, and builds fresh for it. ps3rsx also builds after every linux build that goes out to testing.
- build (no secrets): `github-ci/build-package.sh` on ubuntu-24.04, ppc64 under qemu-user-static, our build dependencies (LOCAL_DEPS, ps3rsx's kernel) from the target suite, checked against its signed index; `tools/leak-check.py` on the debs and sources.
- publish (`github-ci/publish.sh`): testing has one publisher, `.github/workflows/walnut-publish.yml` (`github-ci/batch-publish.py`), which takes every build testing lacks in one upload (docs/PACKAGING.md); stable publishes from the package's own run. Testing gets everything (debug kernel and modules included), signed with the CI key `C902FADFC765FE6C12156A13F9C4DD0442CBA6F1` (secret in pass Projects/Walnut/TestingKey; the server takes it for testing only); stable gets the release build, signed with the archive key's signing subkey, after the owner approves the run (environment walnut-stable). Symbols go to testing-debug / stable-debug in the same upload. Suites, pools and retention: docs/PACKAGING.md "Walnut's suites on GitHub Actions".
- `apt-repo/walnut-repo.py` keeps the suites without a database: each publish starts from the published, signed state and names it in the signed Release (X-Walnut-Base); walnut-watch refuses a stale one and `github-ci/publish.sh` makes it again. With one testing publisher that is rare (a stable publish touches other suites). Tests: `apt-repo/test_walnut_repo.py` (against the real walnut-watch), `github-ci/test_batch_publish.py`.
- First runs (2026-10-21) failed on snapshot.debian.org's throttling (503 TooManyRequests) while building the builder images; builds now fetch the snapshot through walnut.nanobyte.dev/debian-snapshot (walnut-aptcache, config.mk SNAPSHOT_URL).
- Open: a site update recreates walnut-repo, which also serves apt: a few seconds of 502s for apt clients (CI retries them, apt-repo/walnut-repo.py); serve www/ from a container the site switch doesn't touch, or switch the site with nginx reload instead.
- Open: build times on 4-core runners (Mesa, the big games under qemu, against the 6-hour limit), cached builder images (GHCR), pruning testing's pool on the server.

### M7. User docs and release artefacts

1. **The release:** `make release VER=0.1-alpha`, reusing `make checksums`. Artefacts: the rootfs `.tar.xz`, stamped-image tooling, kernel, generic initramfs, installer, SHA256SUMS and its signature.
2. **`docs/INSTALL.md`:**
   - **Prerequisites:** CFW, a NOR/NAND backup *first*, OtherOS++ through xai_plugin, webMAN, the fan in SYSCON mode or ≥60%, and the **XMB Backup Utility before an internal install** (GAMEOS-HDD.md:184).
   - **Reaching Petitboot:** every reboot from Linux lands in GameOS (CONSOLE.md:55), and Petitboot's timeout is "forever" by default.
   - **Installing:** the internal (wired) and USB installs, then the wizard.
   - **Getting out:** rescue via `init=` edited in Petitboot (root is locked), recovery, back to GameOS, uninstall.
   - **Limits:** tested on a CECH-20xx only, the fan policy checked on one SYSCON type, no licensed games.
   - **Legal:** "Linux for the PlayStation 3", no Sony branding; we host no firmware, CFW, keys or PUPs, only links.
3. **`walnut-bugreport`:** versions, dmesg, the Xorg log, the model, kboot.conf. The issue template asks for it.

**M7 status (2026-10-23):** the release is `.github/workflows/release-image.yml` ("Walnut release", by hand), two channels like the apt suites: stable gets a version A.B.C (A.B in the top-level `VERSION` file, C counted up), testing a nightly id; docs/WALNUT-SERVER.md "Release images". `github-ci/build-release.sh` builds the public image with our packages from the chosen signed suite (`make rootfs PROFILE=public WALNUT_SUITE=...`; a nightly's apt follows testing), takes the kernel and its arguments out of it and packs one archive, `walnut-<id>.tar`: the installer, README.TXT (the install by hand, any FTP client), a MANIFEST of every package, and what goes on the PS3 laid out like its two disks (`stamp.py prepare`: the stamped 16 GiB image as xz, 33 MB, with the install.conf for it; the boot files and the known-good copy; kboot.conf; the system), and refuses an image with a licensed package, dev-profile files or a rootfs tarball too big for dev_hdd1; `github-ci/publish-release.sh` signs SHA256SUMS (archive subkey for stable, CI key for nightlies) and publishes to walnut.nanobyte.dev/download/; a stable one also becomes a GitHub release. `publish: off` builds only, for a look at the artifact.

**Released: Walnut 0.1.0 (2026-10-23)**, the first stable alpha: https://walnut.nanobyte.dev/download/0.1.0/ and the GitHub pre-release v0.1.0 (`walnut-0.1.0.tar`, 981 MB, signed by the archive key). Kernel 6.12.111+ps3.751, Mesa +ps3.721, ps3rsx 0.735, ps3-system 0.755 (the system names itself Walnut 0.1 (alpha), Debian-based), the startup chime, the gelic receive watchdog, USB Ethernet drivers. Tested on the PS3 through two nightlies; the list in [TESTING-0.1.md](TESTING-0.1.md).

### M8. GitHub

- **History:** `git filter-repo` on a fresh clone, matching only `^Co-Authored-By: Claude`, `^Claude-Session:` and `Generated with [Claude Code]`. Most other "Claude" mentions are the file CLAUDE.md and stay (R: gap 7). The filter is deterministic, so the public repo fast-forwards.
- **Pre-push scan** for `backups/`, `firmware/`, `.bin`/`.img` and licensed paths in history.
- **For the alpha the public repo is a read-only mirror:** issues are welcome, PRs land as patches into the private repo. Making GitHub the primary repo is a later decision.
- **The rest:** the Walnut README, the install link, the issue templates, and the v0.1-alpha release from `release-image.yml`.

**M8 status (2026-10-21):** published: github.com/nanobyte-dev/walnut-linux (693 commits; AI trailers removed, the Pixabay wallpaper blob stripped, the NOR dump hash replaced through the untracked tools/public-replace.local.txt, history scan clean). Updates: `make public-push` (deterministic, fast-forwards).

### M9. Articles and the site

- **The content is a self-contained Hugo section:** `content/walnut/`, a sidebar block in `themes/nanobyte/layouts/partials/sidebar.html`, and menu entries in **all three configs** (`config.toml`, `hugo.toml`, `config-legacy.toml`). That way it fits either a nanobyte.dev section or a separate walnut.nanobyte.dev site (still undecided).
- **First wave, from the docs and early git history; the user reviews each:**
  1. Making Linux boot on a modern kernel (OtherOS++ and Petitboot, 6.12 from Debian's source).
  2. Kernel work (the lost decrementer, the sys-manager hang, the release config, VT/DPMS).
  3. Getting the GPU going (ps3rsx, Mesa nv30, dynamic GART, VRAM swap).
  4. Graph exceptions and the validator.
- **Deploy** through the site CI: test, then approved prod.

### M10. Updates that don't thrash (found 2026-10-21)

A kernel + Mesa + desktop upgrade on the USB root (32 packages, debug symbols included) hung the
PS3 for over an hour with the desktop already stopped: ping answers, sshd can't start a session, no
OOM in the kernel log and no disk activity (the user), so a hang rather than thrashing; dpkg stopped while
unpacking openttd-dbgsym. The instrumented rerun (netconsole, sysrq-w every 30 s, meminfo every second)
completed, so the cause is still open; keep that instrumentation for the next occurrence. Measure first (PSI, vmstat, dpkg's timeline on a small upgrade), then each fix
against the same run:
1. No `-dbgsym` packages on normal systems (the dev deploy installs them; public images never).
2. A small dirty-page cap (`vm.dirty_bytes` ~16-32 MB, ps3-system) so writes to a slow disk trickle
   instead of stalling everything.
3. A lighter apt: fewer sources (our repo and a trimmed Debian subset), no translations or deb-src.
4. man-db's auto-update off.
5. `walnut-update`: download first, then install; stop the desktop with a message on the text
   console ("Updating the system: the desktop comes back when it's done", the user 2026-10-21; the
   dev deploy gets the same message); dpkg `force-unsafe-io`; kernel and Mesa one at a time; the
   desktop back after.

## Cuts for the alpha (to ship sooner; the user may veto)

- **Package renames** (`ps3-*` to `walnut-*`): conffiles move between packages, which needs Replaces/Breaks and `dpkg-maintscript-helper`, and the pins and dependencies change. User-visible strings only for now; the renames come after the alpha.
- **Wi-Fi in the wizard:** never tested (PLAN 4.8); Ethernet only for the alpha.
- **The WSL2 USB path (usbipd):** Windows users get the internal FTP install natively.
- **The licensed suite on OVH:** it can stay on the LAN at first.

## Order

1. **First, in parallel:** M0 with the user, M2 and M3, and M9's articles.
2. **Then M1,** after M0 proves the guard and dev_hdd1.
3. **M4, then M5:** the USB path once M2 is done, the internal path once M1 is done.
4. **M6, M7 and M8** last.

Agents: two at a time, three when I'm idle. Opus for M1 (the initramfs install mode and verify in C). Sonnet for the rest.

## Verification

- **M0:**
  - steps 1-5 of "On the PS3" pass;
  - a boot from dev_hdd1 works with no USB disk;
  - the files survive several GameOS sessions, and the known-good set restores;
  - all recorded in TESTS.md.
- **M1:**
  - the VM harness;
  - on the PS3, a second image file goes through first boot (install mode) and a normal boot. A truncated upload and a moved file are both refused;
  - the current SSD root still boots.
- **Public image:**
  - no `tibi`, keys, netconsole or `video=` args;
  - nothing licensed (`tools/licensed.py names`), and no licensed source in the published `deb-src`;
  - it boots on a 720p TV;
  - DNS works without `ip=dhcp`.
- **Wizard:** a sudo user gets created, autologin follows it, and the clock is correct.
- **apt:**
  - signatures are checked and a wrong key is refused;
  - `licensed` gives 401 without credentials;
  - `stable` holds exactly the bytes tested in `testing`.
- **CI:** a one-package push publishes only that package to `testing`. Promote and release wait for approval, and versions only grow.
- **A stranger's install:** a fresh USB disk, then a fresh image file, using only the public docs and downloads.
- **History:** zero exact trailers left, re-runs give identical IDs, and the scan is clean.
- **Site:** `run-local.sh`, then test.nanobyte.dev, then approved prod.

## Decided (the user, 2026-10-21)

- **License** of our own code (tools, installer, packaging, scripts, ps3-gpu-tools, SPU code): GPL-2.0-or-later;
  the kernel patches and ps3rsx stay GPL-2.0-only.
- **GitHub owner:** the nanobyte-dev organisation.
- **Site:** a section of nanobyte.dev (`/walnut/`), linked from the channel; walnut.nanobyte.dev serves apt and downloads.
- **CI:** GitHub-hosted runners (a throwaway VM per job, so privileged docker reaches nothing private; free for a
  public repo). Limits to handle: 6 h per job on 4 cores (measure Mesa's qemu-native build first; ccache through
  actions/cache, cross builds for the heavy ones, or local builds as the fallback), ~14 GB free disk (~30 GB after
  removing the preinstalled toolchains). Only a testing subkey in the secrets; stable promotion signs offline.
