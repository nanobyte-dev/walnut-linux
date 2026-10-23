#!/bin/sh
# walnut-gelic-rebind.sh: unbinds and rebinds the wired network device (gelic, sb_06), which re-runs
# its probe (the hypervisor's internal VLAN query included), then checks whether frames arrive.
# Run on the PS3 as root:  sudo sh /path/to/walnut-gelic-rebind.sh
# Runtime only. If the console hangs, hold the power button: nothing on disk is changed.
D=$(cd "$(dirname "$0")" && pwd)/walnut-rebind-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D" || exit 1
L=$D/log.txt
DRV=/sys/bus/ps3_system_bus/drivers/ps3_gelic_driver
run() {
	printf '\n===== %s\n$ %s\n' "$1" "$2" >> "$L"
	timeout 40 sh -c "$2" >> "$L" 2>&1
	printf -- '-- exit %s\n' "$?" >> "$L"
	sync
}
state() { run "state: $1" 'ip -s link; grep -iE "eth|wlan|gelic" /proc/interrupts; ls /sys/class/net; dmesg | grep -iE "gelic|vlan|wlan|eurus" | tail -15'; }
echo ">> writing to $D"
run "before" "ls -l $DRV; uptime"
state "before"
echo ">> unbind"
run unbind "echo sb_06 > $DRV/unbind; sleep 3; ls /sys/class/net"
echo ">> bind"
run bind "echo sb_06 > $DRV/bind; sleep 5; ls /sys/class/net"
state "after bind"
echo ">> waiting 40 s for NetworkManager"
run "wait" 'sleep 35; nmcli dev'
state "after 40 s"
run "addresses" 'ip addr; ip route; ping -c 3 -W 2 $(ip route | sed -n "s/^default via \([^ ]*\).*/\1/p" | head -1)'
dmesg > "$D/dmesg.txt" 2>&1
timeout 30 journalctl -b --no-pager -o short-monotonic -u NetworkManager --since "-5 min" > "$D/journal-nm.txt" 2>&1
sync
echo ">> done: $D (sync, then unmount the stick before pulling it)"
grep -E "RX:|^ +[0-9]+ +[0-9]+ " "$L" | tail -4
