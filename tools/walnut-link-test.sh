#!/bin/sh
# walnut-link-test.sh: installs ethtool from the stick (no network needed), then tries the wired port at
# forced 100 Mbit/full and back at autonegotiation, checking whether anything is received in each.
# Run on the PS3 as root:  sudo sh /path/to/walnut-link-test.sh
# Runtime only (the link mode resets at the next boot), apart from ethtool being installed.
H=$(cd "$(dirname "$0")" && pwd)
D=$H/walnut-link-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D" || exit 1
L=$D/log.txt
run() {
	printf '\n===== %s\n$ %s\n' "$1" "$2" >> "$L"
	timeout 60 sh -c "$2" >> "$L" 2>&1
	printf -- '-- exit %s\n' "$?" >> "$L"
	sync
}
rx() { run "rx: $1" 'ip -s link show eth0 | sed -n "3,6p"; cat /sys/class/net/eth0/carrier_changes /sys/class/net/eth0/carrier; ethtool eth0 | grep -E "Speed|Duplex|Auto-negotiation|Link detected"'; }
echo ">> writing to $D"
run "install ethtool" "command -v ethtool || dpkg -i '$H'/ethtool_7.1-1_ppc64.deb"
run "ethtool" 'ethtool eth0; ethtool -i eth0; ethtool -S eth0; ethtool -a eth0'
rx "start"
run "watch 30 s at autoneg" 'for i in 1 2 3 4 5 6; do sleep 5; printf "%s carrier=%s changes=%s rx=%s\n" $(date +%T) $(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/carrier_changes /sys/class/net/eth0/statistics/rx_packets); done'
echo ">> forced 100/full (30 s)"
run "force 100/full" 'ethtool -s eth0 speed 100 duplex full autoneg off; sleep 10; for i in 1 2 3 4; do sleep 5; printf "%s carrier=%s changes=%s rx=%s\n" $(date +%T) $(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/carrier_changes /sys/class/net/eth0/statistics/rx_packets); done'
rx "at 100/full"
run "nm at 100" 'nmcli dev; ip addr show eth0'
echo ">> forced 10/full (30 s)"
run "force 10/full" 'ethtool -s eth0 speed 10 duplex full autoneg off; sleep 10; for i in 1 2 3 4; do sleep 5; printf "%s carrier=%s changes=%s rx=%s\n" $(date +%T) $(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/carrier_changes /sys/class/net/eth0/statistics/rx_packets); done'
rx "at 10/full"
echo ">> back to autoneg (30 s)"
run "autoneg" 'ethtool -s eth0 autoneg on; sleep 10; for i in 1 2 3 4; do sleep 5; printf "%s carrier=%s changes=%s rx=%s\n" $(date +%T) $(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/carrier_changes /sys/class/net/eth0/statistics/rx_packets); done'
rx "end"
run "dmesg" 'dmesg | grep -iE "gelic|eth0" | tail -30'
sync
echo ">> done: $D (sync, then unmount the stick before pulling it)"
grep -E "rx=" "$L" | tail -3
