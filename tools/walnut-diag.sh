#!/bin/sh
# walnut-diag.sh: collects the wired network (gelic) and sound state into a folder next to this
# script. Run on the PS3 as root:  sudo sh /path/to/walnut-diag.sh
# Part 1 only reads. Part 2 bounces eth0 and NetworkManager (runtime only, nothing is saved).
# Every command has a timeout, so a hung driver call doesn't stop the rest.
D=$(cd "$(dirname "$0")" && pwd)/walnut-diag-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D" || exit 1
L=$D/log.txt
run() {	# run <title> <command...>: output to log.txt, with the exit code
	printf '\n===== %s\n$ %s\n' "$1" "$2" >> "$L"
	timeout 25 sh -c "$2" >> "$L" 2>&1
	printf -- '-- exit %s\n' "$?" >> "$L"
	sync
}
counters() { run "counters: $1" 'ip -s link show eth0; grep -iE "gelic|eth" /proc/interrupts; cat /sys/class/net/eth0/carrier /sys/class/net/eth0/operstate'; }
echo ">> writing to $D"

echo ">> part 1: reading"
run uname 'uname -a; cat /proc/cmdline; uptime; cat /etc/os-release | head -3'
run packages 'dpkg-query -W linux-image-ps3 ps3-system ps3-desktop ps3-boot ps3rsx-modules-ps3 network-manager systemd pnmixer 2>&1'
counters "first look"
run "ip addr/route" 'ip addr; ip route; ip -6 route; cat /etc/resolv.conf'
run ethtool 'ethtool eth0; ethtool -i eth0; ethtool -S eth0'
run sysfs 'cd /sys/class/net/eth0 && for f in address carrier carrier_changes carrier_up_count carrier_down_count dormant duplex flags mtu operstate speed tx_queue_len; do printf "%s=" $f; cat $f 2>&1; done; ls -l device device/driver 2>&1'
run "all interfaces" 'ls -l /sys/class/net; ip -d link'
run rfkill 'rfkill list 2>&1; iw dev 2>&1'
run modules 'lsmod'
run memory 'free -m; cat /proc/meminfo; cat /proc/buddyinfo'
run nmcli 'nmcli general; nmcli dev; nmcli -f all dev show eth0; nmcli con show; nmcli -f all con show "Wired connection 1"'
run "NM config" 'NetworkManager --print-config; ls -la /etc/NetworkManager/system-connections /run/NetworkManager/system-connections 2>&1; ls /usr/lib/NetworkManager/conf.d /etc/NetworkManager/conf.d'
run networkd 'systemctl status systemd-networkd systemd-networkd.socket NetworkManager --no-pager -l; networkctl status -a --no-pager 2>&1; ls -la /etc/systemd/network /run/systemd/network /usr/lib/systemd/network 2>&1'
run "failed units" 'systemctl --failed --no-pager; systemctl list-jobs --no-pager'
dmesg > "$D/dmesg.txt" 2>&1
timeout 60 journalctl -b --no-pager -o short-monotonic > "$D/journal.txt" 2>&1
timeout 30 journalctl -b --no-pager -o short-monotonic -u NetworkManager -u systemd-networkd -u wpa_supplicant > "$D/journal-net.txt" 2>&1
run "dmesg: net" 'dmesg | grep -iE "gelic|eth0|net|ps3_sys_manager|lv1|dma|irq" | tail -80'

echo ">> sound"
run sound 'cat /proc/asound/cards; ls -l /dev/snd; aplay -l; amixer -c 0 scontents 2>&1 | head -40'
run "dmesg: sound" 'dmesg | grep -iE "snd|sound|ps3av|audio|alsa"'
run "pnmixer log" 'journalctl -b --no-pager -o short-monotonic | grep -iE "pnmixer|segfault|alsa" | tail -40; ps -eo pid,user,etimes,args | grep -E "[p]nmixer|[p]ulse|[p]ipewire"'
run "user groups" 'for u in $(awk -F: "\$3>=1000 && \$3<60000 {print \$1}" /etc/passwd); do id $u; done; getfacl /dev/snd/* 2>&1 | head -30'

echo ">> part 2: link tests (runtime only)"
run "wait 10 s, counters again" 'sleep 10'
counters "after 10 s"
run "eth0 down/up" 'ip link set eth0 down; sleep 2; ip link set eth0 up; sleep 8'
counters "after down/up"
run "ethtool after down/up" 'ethtool eth0'
run "autoneg on" 'ethtool -s eth0 autoneg on; sleep 8; ethtool eth0'
counters "after autoneg"
run "forced 100/full, then autoneg again" 'ethtool -s eth0 speed 100 duplex full autoneg off; sleep 8; ethtool eth0 | grep -E "Speed|Duplex|Link"; ethtool -s eth0 autoneg on; sleep 8; ethtool eth0 | grep -E "Speed|Duplex|Link"'
counters "after forced mode"
run "NetworkManager restart" 'systemctl restart NetworkManager; sleep 15; nmcli dev'
counters "after NM restart"
run "connect" 'nmcli -w 20 con up "Wired connection 1"'
counters "after connect"
run "dhcp by hand (5 tries)" 'command -v dhclient && dhclient -v -1 eth0 || (command -v udhcpc && udhcpc -i eth0 -n -q -t 5) || echo "no dhcp client"'
counters "last"
run "dmesg since part 2" 'dmesg | tail -60'
dmesg > "$D/dmesg-end.txt" 2>&1
sync
echo ">> done: $D (now: sync, then unmount the stick before pulling it)"
