#!/bin/sh
# walnut-wol-test.sh: is Wake-on-LAN what kills the wired port's receive? With the network working:
#   A. arm WoL at the hypervisor (lv1net.py wol-arm: the hypervisor only), rebind gelic: does RX die?
#   B. disarm WoL (lv1net.py wol-disarm, ethtool wol d), rebind gelic: does RX come back?
#   C. NetworkManager stopped, WoL armed, rebind: does RX die without NetworkManager too?
# Each step pings the router and watches rx_packets. It always ends with WoL off and a rebind, so the
# port is left as it is now (Linux's default: WoL off; the system manager's power-off wake flag is never set).
# Run on the PS3 as root, with the network working:  sudo sh /path/to/walnut-wol-test.sh
H=$(cd "$(dirname "$0")" && pwd)
D=$H/walnut-wol-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D" || exit 1
L=$D/log.txt
DRV=/sys/bus/ps3_system_bus/drivers/ps3_gelic_driver
# The address and the router this script puts back on eth0 (a static address while NetworkManager is stopped):
# those eth0 has now, else WALNUT_STATIC_ADDR (with the /prefix) and WALNUT_GATEWAY, else documentation addresses.
ADDR=${WALNUT_STATIC_ADDR:-$(ip -4 -o addr show dev eth0 2>/dev/null | awk '{print $4; exit}')}; ADDR=${ADDR:-192.0.2.77/24}
GW=${WALNUT_GATEWAY:-$(ip route 2>/dev/null | sed -n 's/^default via \([^ ]*\).*/\1/p' | head -1)}; GW=${GW:-192.0.2.1}
export ADDR GW
run() {
	printf '\n===== %s\n$ %s\n' "$1" "$2" >> "$L"
	timeout 60 sh -c "$2" >> "$L" 2>&1
	printf -- '-- exit %s\n' "$?" >> "$L"
	sync
}
rxp() { cat /sys/class/net/eth0/statistics/rx_packets 2>/dev/null || echo 0; }
rebind() { run "rebind" "echo sb_06 > $DRV/unbind; sleep 3; echo sb_06 > $DRV/bind; sleep 10"; }
check() {	# check <label>: 20 s of pings to the router; "alive" or "DEAD"
	b=$(rxp)
	run "check: $1" "ip addr show eth0 | grep inet; ping -c 10 -i 2 -W 2 $GW | tail -2"
	a=$(rxp)
	[ "$a" -gt "$b" ] && r=alive || r=DEAD
	echo ">> $1: rx $b -> $a: $r"
	printf '>> %s: rx %s -> %s: %s\n' "$1" "$b" "$a" "$r" >> "$L"
}
static() { run "static address" "ip link set eth0 up; ip addr add $ADDR dev eth0 2>/dev/null; true"; }
echo ">> writing to $D (router $GW)"
check "start (should be alive)"

echo ">> A: WoL armed, rebind (NetworkManager running)"
run "wol on (hypervisor only)" "python3 -I '$H/lv1net.py' wol-arm"
check "A1: WoL armed, before rebind"
rebind
sleep 20
check "A2: WoL armed, after rebind"

echo ">> B: WoL off, rebind"
run "wol off" "python3 -I '$H/lv1net.py' wol-disarm; ethtool -s eth0 wol d"
rebind
sleep 20
check "B: WoL off, after rebind"

echo ">> C: NetworkManager stopped, WoL armed, rebind"
run "nm stop" 'systemctl stop NetworkManager'
run "wol on (hypervisor only)" "python3 -I '$H/lv1net.py' wol-arm"
rebind
static
check "C: no NetworkManager, WoL armed, after rebind"

echo ">> end: WoL off, rebind, NetworkManager back"
run "wol off" "python3 -I '$H/lv1net.py' wol-disarm; ethtool -s eth0 wol d"
rebind
run "nm start" 'systemctl start NetworkManager; sleep 20; nmcli dev'
check "end (should be alive)"
dmesg > "$D/dmesg.txt" 2>&1; sync
grep '^>>' "$L"
echo ">> done: $D (sync, then unmount the stick before pulling it)"
