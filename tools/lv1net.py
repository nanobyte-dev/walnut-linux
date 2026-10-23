#!/usr/bin/env python3
"""lv1net.py: asks the PS3 hypervisor about the wired network device, read-only, through
/dev/ps3lv1call (the ps3lv1call module): the port status, the MAC address, the internal VLAN IDs
(with the hypervisor's result codes, which gelic only logs at debug level) and the firmware version.
Only lv1_net_control's GET commands (1 MAC, 2 port status, 4 VLAN ID, 6 channel) and
lv1_get_version_info are called. Run as root: python3 -I lv1net.py"""
import os, struct, subprocess, sys

DEV = "/dev/ps3lv1call"
NET_CONTROL, VERSION_INFO = 194, 127
GET_MAC, GET_PORT, GET_VLAN, GET_CHANNEL = 1, 2, 4, 6
VLAN = {"tx ethernet": 0x02, "tx wireless": 0x03, "rx ethernet": 0x12, "rx wireless": 0x13}
RESULT = {0: "SUCCESS", -2: "RESOURCE_SHORTAGE", -3: "NO_PRIVILEGE", -4: "DENIED_BY_POLICY",
	-5: "ACCESS_VIOLATION", -6: "NO_ENTRY", -7: "DUPLICATE_ENTRY", -8: "TYPE_MISMATCH", -9: "BUSY",
	-10: "EMPTY", -11: "WRONG_STATE", -13: "NO_MATCH", -14: "ALREADY_CONNECTED",
	-15: "UNSUPPORTED_PARAMETER_VALUE", -16: "CONDITION_NOT_SATISFIED", -17: "ILLEGAL_PARAMETER_VALUE",
	-18: "BAD_OPTION", -19: "IMPLEMENTATION_LIMITATION", -20: "NOT_IMPLEMENTED", -21: "INVALID_CLASS_ID",
	-22: "CONSTRAINT_NOT_SATISFIED", -23: "ALIGNMENT_ERROR", -24: "HARDWARE_ERROR",
	-25: "INVALID_DATA_FORMAT", -26: "INVALID_OPERATION", -32768: "INTERNAL_ERROR"}
ALLOWED = {(NET_CONTROL, c) for c in (GET_MAC, GET_PORT, GET_VLAN, GET_CHANNEL)} | {(VERSION_INFO, None)}


def call(num, *args, nout=2):
	assert (num, args[2] if num == NET_CONTROL else None) in ALLOWED, "not a read-only call"
	fd = os.open(DEV, os.O_RDWR)
	try:
		os.write(fd, struct.pack(">%dQ" % (1 + len(args)), num, *[a & (2**64 - 1) for a in args]))
		buf = os.read(fd, 8 * (1 + nout))
	finally:
		os.close(fd)
	err = struct.unpack(">q", buf[:8])[0]
	outs = struct.unpack(">%dQ" % ((len(buf) - 8) // 8), buf[8:]) if err == 0 else ()
	return err, outs


def name(err):
	return "%d %s" % (err, RESULT.get(err, "?"))


def port_status(v):
	speed = {0x10: "10", 0x20: "100", 0x40: "1000"}.get(v & 0x70, "unknown")
	return "0x%x: link %s, %s duplex, autoneg %s, speed %s" % (v, "up" if v & 1 else "DOWN",
		"full" if v & 2 else "half", "on" if v & 4 else "off", speed)


def main():
	if not os.path.exists(DEV):
		subprocess.run(["modprobe", "ps3lv1call"], check=False)
	if not os.path.exists(DEV):
		sys.exit("no %s (modprobe ps3lv1call failed)" % DEV)
	mac = open("/sys/class/net/eth0/address").read().strip().replace(":", "")
	err, outs = call(VERSION_INFO, nout=2)
	print("firmware (lv1_get_version_info): %s%s" % (name(err), ", 0x%x 0x%x" % outs if err == 0 else ""))
	found = None
	for bus in range(0, 8):
		for dev in range(0, 8):
			err, outs = call(NET_CONTROL, bus, dev, GET_MAC, 0, 0, 0)
			if err == 0 and "%012x" % (outs[0] & 0xffffffffffff) == mac:
				found = (bus, dev)
				break
		if found:
			break
	if not found:
		sys.exit("no net device with eth0's MAC %s on bus 0-7, dev 0-7" % mac)
	bus, dev = found
	print("gelic: bus %d dev %d (MAC %s matches eth0)" % (bus, dev, mac))
	err, outs = call(NET_CONTROL, bus, dev, GET_PORT, 0x02, 0, 0)
	print("port status: %s" % (port_status(outs[0]) if err == 0 else name(err)))
	for label, ix in VLAN.items():
		err, outs = call(NET_CONTROL, bus, dev, GET_VLAN, ix, 0, 0)
		print("vlan %-12s: %s%s" % (label, name(err), ", id %d (v2 0x%x)" % (outs[0], outs[1]) if err == 0 else ""))
	err, outs = call(NET_CONTROL, bus, dev, GET_CHANNEL, 0, 0, 0)
	print("wireless channel info: %s%s" % (name(err), ", 0x%x 0x%x" % outs if err == 0 else ""))


main()
