#!/bin/bash
# release-notes.sh VERSION DIR: the GitHub release's text for a stable release in DIR
# (build-release.sh's output, signed), on stdout
set -euo pipefail
ver=$1 dir=$2
url=https://walnut.nanobyte.dev/download/$ver
repo=https://github.com/nanobyte-dev/walnut-linux/blob/main
v() { awk -v p="$1" '$1==p {print $2}' "$dir/MANIFEST"; }
kernel=$(tar -tf "$dir/walnut-$ver.tar" | sed -n 's|^walnut-[^/]*/dev_hdd1/linux/boot/vmlinux-||p')
size() { du -h --apparent-size "$dir/$1" | cut -f1; }
cat <<EOF
Linux for the PlayStation 3: Debian (ppc64) with a 6.12 kernel, the RSX driven by nouveau's nv30
Mesa and our ps3rsx driver, and open-source games set up to run on the console.

**This is an alpha.** It has been tested on one console (a CECH-20xx Slim). Read
[INSTALL.md]($repo/docs/INSTALL.md) before you start: you need a console running CFW with
OtherOS++ (Petitboot) and webMAN MOD, and a backup of your flash.

### Files

The same files are at $url/.

| File | What it is |
|---|---|
| \`walnut-$ver.tar\` ($(size "walnut-$ver.tar")) | everything: the installer, the system, the boot files and Petitboot's entry laid out like the PS3's two disks, and README.TXT for installing by hand (any FTP client) |
| \`MANIFEST\` | every package in the system, with its version |
| \`SHA256SUMS\`, \`SHA256SUMS.asc\` | checksums, signed by the archive key |

Check them, then unpack:

\`\`\`
curl -fsSL https://walnut.nanobyte.dev/walnut-archive.asc | gpg --import
gpg --verify SHA256SUMS.asc SHA256SUMS    # Walnut archive key 8FFE 9D3F C1E2 4ADC 4702  50BB 5701 1432 F8A9 8072
sha256sum -c SHA256SUMS
tar -xf walnut-$ver.tar && cd walnut-$ver && less README.TXT
\`\`\`

### Inside

- kernel $kernel (Debian's 6.12 with the OtherOS++ patch set and ours)
- Mesa $(v mesa-libgallium), ps3rsx $(v ps3rsx-modules-ps3)
- games from the \`ps3-games-free\` set; more with \`apt install\` from walnut.nanobyte.dev's stable suite

Updates come through apt from the signed \`stable\` suite at https://walnut.nanobyte.dev/apt.
Credits for the kernel patches and everything else: [README]($repo/README.md#credits).
EOF
