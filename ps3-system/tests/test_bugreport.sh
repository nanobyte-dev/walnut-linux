#!/bin/sh
# test_bugreport.sh: test syntax and redaction rules of walnut-bugreport.
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUGREPORT_BIN="${SCRIPT_DIR}/../files/sbin/walnut-bugreport"

# 1. Syntax check
sh -n "${BUGREPORT_BIN}"
echo "PASS: sh -n syntax check"

# 2. Mock root test
TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/walnut-test-root.XXXXXX")"
REPORT_OUT="${TMPROOT}/output-report.txt"
trap 'rm -rf "${TMPROOT}"' EXIT INT TERM

mkdir -p "${TMPROOT}/etc/default"
mkdir -p "${TMPROOT}/proc/device-tree"
mkdir -p "${TMPROOT}/run/ps3ssd"
mkdir -p "${TMPROOT}/sys/block/ps3da/write_guard"
mkdir -p "${TMPROOT}/sys/block/ps3db/write_guard"

# Populate mock data
cat << 'EOF' > "${TMPROOT}/etc/os-release"
PRETTY_NAME="Debian GNU/Linux trixie/sid"
NAME="Debian GNU/Linux"
ID=debian
EOF

cat << 'EOF' > "${TMPROOT}/fake_uname"
Linux test-console-hostname 6.12.111+ps3.648 #1 SMP ppc64 GNU/Linux
EOF

cat << 'EOF' > "${TMPROOT}/fake_dpkg"
linux-image-ps3	6.12.111+ps3.648	install ok installed
mesa-nv30-ps3	24.2.0+ps3.640	install ok installed
ps3-system	0.500	install ok installed
walnut-firstboot	0.1	install ok installed
EOF

cat << 'EOF' > "${TMPROOT}/proc/cpuinfo"
processor	: 0
cpu		: Cell Broadband Engine, altivec supported
clock		: 3192.000000MHz
revision	: 5.1 (pvr 0070 0501)
EOF

printf 'Sony PS3 CECH-2004A\0' > "${TMPROOT}/proc/device-tree/model"

cat << 'EOF' > "${TMPROOT}/fake_free"
               total        used        free      shared  buff/cache   available
Mem:             217          85          72           4          60         128
Swap:           1024           0        1024
EOF

cat << 'EOF' > "${TMPROOT}/fake_swap"
NAME                  TYPE      SIZE USED PRIO
/dev/mapper/ps3swap   partition   1G   0B  200
/dev/zram0            partition 128M   0B  100
EOF

cat << 'EOF' > "${TMPROOT}/fake_lsblk"
NAME           MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
ps3da          254:0    0  894G  1 disk
ps3db          254:1    0  892G  0 disk
|-ps3root      253:0    0   15G  0 dm   /
`-ps3swap      253:1    0    1G  0 dm   [SWAP]
ps3dc          254:2    0    2G  0 disk
EOF

cat << 'EOF' > "${TMPROOT}/fake_mounts"
/dev/mapper/ps3root / ext4 rw,noatime 0 0
/dev/ps3dc /mnt/gameos/hdd1 vfat ro,noatime,nodev,nosuid,noexec 0 0
EOF

cat << 'EOF' > "${TMPROOT}/etc/kboot.conf"
walnut='/linux/boot/vmlinux-6.12.111+ps3.648 initrd=/linux/boot/initrd-walnut.img root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/walnut.img'
walnut-prev='/linux/boot/vmlinux-6.12.111+ps3.620 initrd=/linux/boot/initrd-walnut-prev.img root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/walnut.img'
EOF

cat << 'EOF' > "${TMPROOT}/etc/default/ps3-boot"
PS3_DEFAULT=walnut
PS3_TIMEOUT=0
EOF

cat << 'EOF' > "${TMPROOT}/run/ps3ssd/init.log"
ps3ssd: generic initramfs boot
ps3ssd: verified image /linux/walnut.img (16384 MiB)
ps3ssd: dm-linear ps3root created (15359 MiB)
EOF

cat << 'EOF' > "${TMPROOT}/fake_dmesg"
[    0.000000] Linux version 6.12.111+ps3.648 (builder@host)
[    1.234567] gelic_net: eth0: HWaddr 00:1a:2b:3c:4d:5e
[    2.345678] Bluetooth: HCI device 00-11-22-33-44-55 registered
[    5.678901] systemd[1]: Set hostname to <test-console-hostname>.
[   10.123456] eth0: IPv4 198.51.100.42/24 gateway 192.0.2.1
[   10.123457] lo: IPv4 127.0.0.1/8
[   12.345678] ps3disk ps3db: write guard sealed: 513 ranges
EOF

cat << 'EOF' > "${TMPROOT}/xorg.log"
(==) Log file: "/home/mockuser/.local/share/xorg/Xorg.0.log", Time: Wed Oct 21 14:00:00 2026
(==) Using config file: "/home/mockuser/.xorg.conf"
(II) [KMS] Kernel modesetting enabled.
EOF

cat << 'EOF' > "${TMPROOT}/fake_systemctl"
0 loaded units listed.
EOF

# Write guard files
echo "read-only" > "${TMPROOT}/sys/block/ps3da/write_guard/policy"
echo "0" > "${TMPROOT}/sys/block/ps3da/write_guard/refused"

echo "whitelist" > "${TMPROOT}/sys/block/ps3db/write_guard/policy"
echo "1" > "${TMPROOT}/sys/block/ps3db/write_guard/sealed"
echo "513" > "${TMPROOT}/sys/block/ps3db/write_guard/nr_ranges"
echo "33552384" > "${TMPROOT}/sys/block/ps3db/write_guard/nr_sectors"
echo "0" > "${TMPROOT}/sys/block/ps3db/write_guard/refused"
touch "${TMPROOT}/sys/block/ps3db/write_guard/ranges" # mode 0200 in real sysfs

# Run walnut-bugreport with mock root
WALNUT_ROOT_PREFIX="${TMPROOT}" \
WALNUT_TEST_HOST="test-console-hostname" \
WALNUT_TEST_USER="mockuser" \
sh "${BUGREPORT_BIN}" "${REPORT_OUT}"

[ -f "${REPORT_OUT}" ] || { echo "FAIL: output report file was not created"; exit 1; }

# 3. Assertions on output
fail=0
assert_contains() {
	if ! grep -Fq "$1" "${REPORT_OUT}"; then
		echo "FAIL: expected string missing: $1" >&2
		fail=1
	fi
}

assert_not_contains() {
	if grep -Fq "$1" "${REPORT_OUT}"; then
		echo "FAIL: forbidden string found: $1" >&2
		fail=1
	fi
}

# Sections present
assert_contains "=== Date ==="
assert_contains "=== Kernel (uname -a) ==="
assert_contains "=== OS Release (/etc/os-release) ==="
assert_contains "=== Packages (dpkg-query) ==="
assert_contains "=== CPU Info (/proc/cpuinfo) ==="
assert_contains "=== Console Model ==="
assert_contains "=== Memory (free -m) ==="
assert_contains "=== Swap ==="
assert_contains "=== Block Devices (lsblk) ==="
assert_contains "=== Mounts ==="
assert_contains "=== /etc/kboot.conf ==="
assert_contains "=== /etc/default/ps3-boot ==="
assert_contains "=== Boot Target (/run/ps3ssd/init.log) ==="
assert_contains "=== Kernel Log (dmesg, last 400 lines) ==="
assert_contains "=== Xorg Log (last 200 lines) ==="
assert_contains "=== Failed Services (systemctl --failed) ==="
assert_contains "=== Write Guard State ==="

# Model read correctly
assert_contains "Model: Sony PS3 CECH-2004A"

# Write guard content
assert_contains "ps3da:"
assert_contains "policy: read-only"
assert_contains "ps3db:"
assert_contains "sealed: 1"
assert_contains "nr_ranges: 513"

# Redaction assertions
# 1. Loopback preserved
assert_contains "127.0.0.1"

# 2. Private IPs redacted
assert_not_contains "198.51.100.42"
assert_not_contains "192.0.2.1"
assert_contains "x.x.x.x"

# 3. MAC addresses redacted
assert_not_contains "00:1a:2b:3c:4d:5e"
assert_contains "xx:xx:xx:xx:xx:xx"
assert_not_contains "00-11-22-33-44-55"
assert_contains "xx-xx-xx-xx-xx-xx"

# 4. Hostname redacted
assert_not_contains "test-console-hostname"
assert_contains "[hostname]"

# 5. User path redacted
assert_not_contains "mockuser"
assert_contains "/home/[user]/.local/share/xorg/Xorg.0.log"
assert_contains "/home/[user]/.xorg.conf"

# 6. Public names preserved
assert_contains "walnut-firstboot"
assert_contains "/linux/walnut.img"

if [ "$fail" -eq 0 ]; then
	echo "PASS: all bugreport and redaction checks passed"
	exit 0
else
	echo "FAIL: one or more assertions failed"
	cat "${REPORT_OUT}"
	exit 1
fi
