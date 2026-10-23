#!/bin/sh
# walnut-net-all.sh: everything for a wired port that has link but receives nothing, in one run.
# Part 1 reads (Linux's view and the hypervisor's, through lv1net.py). Part 2 tries the kicks in
# order, cheapest first, watching rx_packets for 20 s after each, and stops at the first that brings
# frames in. At the end it puts back what it changed (NetworkManager, the Bluetooth modules).
# Run on the PS3 as root:  sudo sh /path/to/walnut-net-all.sh      (about 6 minutes at most)
# Runtime only: nothing is written to disk (but this folder), the flash or the system controller.
H=$(cd "$(dirname "$0")" && pwd)
D=$H/walnut-all-$(date +%Y%m%d-%H%M%S)
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
hv() { run "hypervisor: $1" "python3 -I '$H/lv1net.py'"; }
watch() {	# watch <label> [seconds/5]: counters; 0 if anything came in
	b=$(rxp)
	run "watch: $1" "for i in \$(seq ${2:-4}); do sleep 5; printf '%s carrier=%s rx=%s tx=%s\n' \$(date +%T) \$(cat /sys/class/net/eth0/carrier /sys/class/net/eth0/statistics/rx_packets /sys/class/net/eth0/statistics/tx_packets); done"
	a=$(rxp)
	echo ">> $1: rx $b -> $a"
	printf '>> %s: rx %s -> %s\n' "$1" "$b" "$a" >> "$L"
	[ "$a" -gt "$b" ]
}
rebind() { run "gelic rebind" "echo sb_06 > $DRV/unbind; sleep 3; echo sb_06 > $DRV/bind; sleep 8; ls /sys/class/net; dmesg | grep -iE 'vlan|gelic|wlan' | tail -4"; }
restore() {
	run "restore" 'modprobe btusb; modprobe bnep; systemctl start NetworkManager; sleep 10; nmcli dev'
}
finish() {	# finish <what helped>
	hv "at the end"
	run "end state" 'ip -s link; ip addr; ip route; ethtool eth0; cat /proc/interrupts | grep -iE "eth|gelic"'
	dmesg > "$D/dmesg.txt" 2>&1
	timeout 30 journalctl -b --no-pager -o short-monotonic -u NetworkManager --since "-15 min" > "$D/journal-nm.txt" 2>&1
	sync
	echo ">> $1"
	echo ">> done: $D (sync, then unmount the stick before pulling it)"
	exit 0
}
works() { printf '\n>>>>> RX CAME ALIVE AFTER: %s\n' "$1" >> "$L"; restore; finish "RX came alive after: $1"; }
echo ">> writing to $D"

echo ">> part 1: reading"
run "versions" 'uname -a; cat /proc/cmdline; uptime; dpkg-query -W linux-image-ps3 ps3-system ethtool 2>&1'
run "linux view" 'ip -s link show eth0; ethtool eth0; cat /sys/class/net/eth0/carrier_changes; grep -iE "eth|gelic|wlan" /proc/interrupts'
hv "first look"
run "probe and wireless messages" 'dmesg | grep -iE "gelic|vlan|wlan|eurus|bluetooth|hci0|btusb|054c|036f" | grep -v "speed unknown" | head -40'
run "usb devices" 'for d in /sys/bus/usb/devices/*-*; do [ -e $d/idVendor ] && echo "$d $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product 2>/dev/null) authorized=$(cat $d/authorized)"; done'
run "rfkill" 'for r in /sys/class/rfkill/*; do [ -e $r/name ] && echo "$r $(cat $r/name) $(cat $r/type) soft=$(cat $r/soft) hard=$(cat $r/hard)"; done; nmcli radio all'
run "system manager / wake" 'dmesg | grep -iE "sys_manager|ps3_sys|wake|wol" | tail -10'
run "memory" 'free -m; cat /proc/buddyinfo'
watch "baseline" && works "nothing (it came back by itself)"

echo ">> part 2: kicks"
echo ">> kick 1: all-multicast"
run "allmulti" 'ip link set eth0 allmulticast on'
watch "all-multicast" && works "all-multicast on"

echo ">> kick 2: Wake-on-LAN off at the hypervisor"
run "wol off" 'ethtool -s eth0 wol d; ethtool eth0 | grep -i wake'
watch "wol off" && works "Wake-on-LAN off (ethtool -s eth0 wol d)"

echo ">> kick 3: NetworkManager out of the way, a static address, 30 s quiet"
run "nm stop + static" 'systemctl stop NetworkManager; sleep 2; ip addr flush dev eth0; ip link set eth0 up; ip addr add 10.0.0.77/24 dev eth0; ip route add default via 10.0.0.1 2>/dev/null; ip addr show eth0'
run "ping the router" 'ping -c 5 -W 2 10.0.0.1; ip neigh show dev eth0'
watch "static, no NetworkManager" 6 && works "NetworkManager stopped, static 10.0.0.77"

echo ">> kick 4: link down for 15 s, then up"
run "down 15 s" 'ip link set eth0 down; sleep 15; ip link set eth0 up; sleep 5; ip addr add 10.0.0.77/24 dev eth0 2>/dev/null; ping -c 3 -W 2 10.0.0.1'
watch "after a long down" && works "eth0 down 15 s, then up"

echo ">> kick 5: gelic rebind (the hypervisor asked again)"
rebind
hv "after rebind"
run "static again" 'ip link set eth0 up; ip addr add 10.0.0.77/24 dev eth0 2>/dev/null; ping -c 3 -W 2 10.0.0.1'
watch "after rebind" && works "gelic rebind"

echo ">> kick 6: Bluetooth off, the Bluetooth/Wi-Fi module reset, then gelic rebind"
run "bluetooth modules out" 'systemctl stop bluetooth 2>/dev/null; modprobe -r bnep btusb btbcm btintel btrtl 2>&1; lsmod | grep -iE "^bt|blue" || echo "bluetooth modules unloaded"'
run "module usb reset" 'for d in /sys/bus/usb/devices/*-*; do [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = 054c:036f ] || continue; echo "reset $d"; echo 0 > $d/authorized; sleep 3; echo 1 > $d/authorized; done; sleep 3; dmesg | tail -8'
rebind
hv "after module reset + rebind"
run "static again" 'ip link set eth0 up; ip addr add 10.0.0.77/24 dev eth0 2>/dev/null; ping -c 3 -W 2 10.0.0.1'
watch "after module reset + rebind" && works "Bluetooth off + module reset + gelic rebind"

echo ">> kick 7: the module left unauthorized (off) while gelic probes"
run "module off" 'for d in /sys/bus/usb/devices/*-*; do [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = 054c:036f ] && echo 0 > $d/authorized && echo "off: $d"; done'
rebind
hv "with the module off"
run "static again" 'ip link set eth0 up; ip addr add 10.0.0.77/24 dev eth0 2>/dev/null; ping -c 3 -W 2 10.0.0.1'
watch "module off + rebind" && { run "module on" 'for d in /sys/bus/usb/devices/*-*; do [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = 054c:036f ] && echo 1 > $d/authorized; done'; works "Bluetooth/Wi-Fi module off + gelic rebind"; }
run "module on" 'for d in /sys/bus/usb/devices/*-*; do [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = 054c:036f ] && echo 1 > $d/authorized; done'

printf '\n>>>>> NO KICK HELPED\n' >> "$L"
restore
finish "no kick helped"
