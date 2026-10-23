#!/bin/sh
# walnut-net-kick.sh: three runtime kicks for a wired port that has link but receives nothing, cheapest
# first, watching rx_packets for 20 s after each. Stops at the first one that brings frames in.
# Run on the PS3 as root:  sudo sh /path/to/walnut-net-kick.sh
# Runtime only: nothing is written to disk, the flash or the system controller.
D=$(cd "$(dirname "$0")" && pwd)/walnut-kick-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D" || exit 1
L=$D/log.txt
DRV=/sys/bus/ps3_system_bus/drivers/ps3_gelic_driver
run() {
	printf '\n===== %s\n$ %s\n' "$1" "$2" >> "$L"
	timeout 60 sh -c "$2" >> "$L" 2>&1
	printf -- '-- exit %s\n' "$?" >> "$L"
	sync
}
rxp() { cat /sys/class/net/eth0/statistics/rx_packets 2>/dev/null || echo 0; }
watch() {	# watch <label>: 20 s of counters; returns 0 if anything came in
	b=$(rxp)
	run "watch: $1" 'for i in 1 2 3 4; do sleep 5; printf "%s carrier=%s rx=%s tx=%s\n" $(date +%T) $(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/statistics/rx_packets /sys/class/net/eth0/statistics/tx_packets); done'
	a=$(rxp)
	echo ">> $1: rx $b -> $a"
	printf '>> %s: rx %s -> %s\n' "$1" "$b" "$a" >> "$L"
	[ "$a" -gt "$b" ]
}
done_ok() { run "it works: state" 'ip -s link show eth0; nmcli dev; ip addr show eth0'; dmesg > "$D/dmesg.txt"; sync; echo ">> RX came alive after: $1. Done: $D"; exit 0; }
echo ">> writing to $D"
run "start" 'ip -s link show eth0; ethtool eth0 | grep -E "Speed|Link|Wake"; dmesg | grep -iE "vlan|gelic" | head -5; lsusb 2>/dev/null; ls /sys/bus/usb/devices/'
watch "baseline" && done_ok "nothing (it came back by itself)"

echo ">> kick 1: all-multicast on"
run "allmulti" 'ip link set eth0 allmulticast on; ip link show eth0'
watch "allmulticast" && done_ok "all-multicast"

echo ">> kick 2: tell the hypervisor Wake-on-LAN is off"
run "wol off" 'ethtool -s eth0 wol d; ethtool eth0 | grep Wake; dmesg | tail -3'
watch "wol off" && done_ok "Wake-on-LAN off"

echo ">> kick 3: reset the Bluetooth/Wi-Fi module (USB 054c:036f), then rebind gelic"
run "bt/wlan usb reset" 'for d in /sys/bus/usb/devices/*; do [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = 054c:036f ] || continue; echo "reset $d"; echo 0 > $d/authorized; sleep 3; echo 1 > $d/authorized; done; sleep 5; dmesg | tail -15'
run "gelic rebind" "echo sb_06 > $DRV/unbind; sleep 3; echo sb_06 > $DRV/bind; sleep 8; ls /sys/class/net; dmesg | grep -iE 'vlan|gelic|wlan' | tail -6"
watch "after module reset + rebind" && done_ok "Bluetooth/Wi-Fi module reset + gelic rebind"

run "end" 'ip -s link; dmesg | tail -30'
dmesg > "$D/dmesg.txt"; sync
echo ">> no kick helped. Done: $D (sync, then unmount the stick before pulling it)"
