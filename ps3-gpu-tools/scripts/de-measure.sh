#!/bin/sh
# de-measure.sh NAME [SETTLE_S]: start desktop NAME (ps3-session: icewm lxqt xfce enlightenment
# openbox) fresh with NetworkManager and Bluetooth running, let it settle, then report the memory
# (PSS + SwapPss: resident and swapped share) of every process it involves: Xorg, tibi's session
# processes and the system services behind its features. Excludes this SSH session.
# SETTLE_S "now": measure the running session without restarting it.
name=$1; settle=${2:-90}
if [ "$settle" != now ]; then
	mkdir -p ~/.config && echo "$name" > ~/.config/ps3-desktop
	for p in $(pgrep -x xinit) $(pgrep -x Xorg); do sudo kill $p; done
	sleep 8
	sudo systemctl start NetworkManager bluetooth
	setsid startx /usr/local/bin/ps3-session -- :0 vt7 -nolisten tcp > ~/.xsession-$name.log 2>&1 \
		< /dev/null &
	sleep "$settle"
fi
me=$(ps -o sid= -p $$ | tr -d ' ')
for p in $(ps -eo pid=,sid=,user=,comm= | awk -v me="$me" '
	$2 != me && ($3 == "tibi" || $4 ~ /^(Xorg|NetworkManager|udisksd|bluetoothd|polkitd|upowerd|wpa_supplicant)$/) {print $1}'); do
	sudo awk -v n="$(cat /proc/$p/comm 2>/dev/null)" '/^Pss:/ {r=$2} /^SwapPss:/ {s=$2}
		END {if (n != "") printf "%-22s %6d kB\n", n, r + s}' /proc/$p/smaps_rollup 2>/dev/null
done | sort -k2 -n -r | awk -v n="$name" '{t += $2; print} END {printf "== %s total: %d kB\n", n, t}'
grep -E "MemAvailable|SwapTotal|SwapFree" /proc/meminfo | tr -s ' ' | tr '\n' ' '; echo
