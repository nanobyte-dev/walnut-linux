#!/bin/bash
# ps3-sm-temp.sh: the PS3's temperatures (docs/THERMAL.md).
#   ps3-sm-temp.sh             from the kernel's sensors (hwmon ps3_sm), if it has them
#   sudo ps3-sm-temp.sh --raw [ZONE...]
#                              without them: one temperature request per zone (default 0 1: Cell
#                              BE, RSX) through /dev/ps3sysmngr, each reply printed in full (its
#                              service id, tag, bytes and value). This checks the message format on
#                              a kernel without the sensors (and with the char device: 6.12.111+
#                              ps3.419 and older; dropped since). It sends nothing but the System
#                              Manager's temperature request (service 13). While the device is open
#                              the kernel doesn't see the System Manager's events (power button,
#                              thermal alert); the script holds it for at most a second or two, and
#                              prints anything else that arrives meanwhile.
set -u
if [ "${1:-}" != --raw ]; then
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
	echo "no ps3_sm sensors in this kernel (CONFIG_PS3_SYS_MANAGER_HWMON); try: sudo $0 --raw" >&2
	exit 1
fi
shift
exec python3 -I - "$@" <<'EOF'
import errno, os, struct, sys, time

DEV = "/dev/ps3sysmngr"
SID_TEMPERATURE = 13
zones = [int(z, 0) for z in sys.argv[1:]] or [0, 1]
if any(not 0 <= z <= 0xff for z in zones):
    sys.exit("zones are 0..255")


def read_exact(fd, n, deadline):
    # the misc device returns EAGAIN until n bytes are queued, without consuming any
    while True:
        try:
            b = os.read(fd, n)
            if len(b) == n:
                return b
        except OSError as e:
            if e.errno != errno.EAGAIN:
                raise
        if time.monotonic() > deadline:
            return None
        time.sleep(0.01)


fd = os.open(DEV, os.O_RDWR)
try:
    for zone in zones:
        # ps3vuart-tools' get_temperature, byte for byte: header version 1, size 16, reserved,
        # payload size 4 (its struct is padded to 20 bytes), service id, reserved, tag 0;
        # payload version 1, tzone, 2 reserved
        req = struct.pack(">BBHIHHI", 1, 16, 0, 4, SID_TEMPERATURE, 0, 0) + bytes([1, zone, 0, 0])
        os.write(fd, req)
        # long enough that a slow reply comes to us, not to the kernel after the close: the
        # running kernel's handler would leave its payload queued and misread what follows
        deadline = time.monotonic() + 3.0
        while True:
            h = read_exact(fd, 16, deadline)
            if h is None:
                print("zone %#x: no reply in 3 s" % zone)
                break
            ver, size, _, plen, sid, _, rtag = struct.unpack(">BBHIHHI", h)
            if ver != 1 or size != 16 or plen > 64:
                print("zone %#x: odd header %s, stopping" % (zone, h.hex()))
                sys.exit(1)
            p = read_exact(fd, plen, deadline + 1.0) if plen else b""
            if p is None:
                print("zone %#x: header %s without its payload, stopping" % (zone, h.hex()))
                sys.exit(1)
            line = "service %d, tag %#x, payload %d bytes: %s" % (sid, rtag, plen, p.hex())
            if sid in (13, 14) and plen >= 6 and p[0] == 1 and p[1] == zone:
                raw = struct.unpack(">h", p[4:6])[0]
                print("zone %#x: %.2f C, status %d  [%s]" % (zone, raw / 256, p[2], line))
                break
            print("zone %#x: other message [%s]%s" % (
                zone, line, " REQUEST_ERROR: the System Manager refused it" if sid == 6 else ""))
            if sid == 6:
                break
    # whatever else arrived (an event) is printed rather than left half-read for the kernel
    while (h := read_exact(fd, 16, time.monotonic() + 0.3)) is not None:
        plen = struct.unpack(">BBHIHHI", h)[3]
        p = read_exact(fd, plen, time.monotonic() + 1.0) if 0 < plen <= 64 else b""
        print("after: header %s payload %s" % (h.hex(), p.hex() if p else "-"))
finally:
    os.close(fd)   # the kernel takes the port back (its async read is re-armed)
EOF
