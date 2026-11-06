#!/bin/bash
# ps3-sm-temp.sh: the PS3's temperatures from the kernel's sensors (hwmon ps3_sm), docs/THERMAL.md.
set -u
for h in /sys/class/hwmon/hwmon*; do
	[ "$(cat "$h/name" 2>/dev/null)" = ps3_sm ] || continue
	for f in "$h"/temp*_input; do
		n=${f%_input}
		v=$(cat "$f" 2>&1) && v=$(awk -v m="$v" 'BEGIN { printf "%.2f C", m / 1000 }')
		printf '%-12s %s%s\n' "$(cat "${n}_label" 2>/dev/null)" "$v" \
			"$([ "$(cat "${n}_alarm" 2>/dev/null)" = 1 ] && echo '  ALARM')"
	done
	exit 0
done
echo "no ps3_sm sensors in this kernel (CONFIG_PS3_SYS_MANAGER_HWMON)" >&2
exit 1
