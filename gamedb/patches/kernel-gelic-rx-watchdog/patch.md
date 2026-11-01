+++
component = "kernel"
patches = ["patches/local/0049-gelic-rx-watchdog.patch"]
aliases = ["local/0049", "kernel 0049"]
category = "stability"
switch = "ps3_gelic.rx_watchdog"
default = "on (ps3_gelic.rx_watchdog=0 off; rx_watchdog_test=N simulates N faults)"
forwarded = "no"
tests = ["ps3:rx_watchdog_test"]
batches = []
+++

# gelic: re-probe the device when the wired port receives nothing

Some seconds after eth0 is opened (20 s at first), a delayed work looks at it: link up, at least 3
packets sent and none received means the receive side is dead, and the device is re-probed with
`device_reprobe()` from a work item of its own, since the re-probe frees the card. Each re-probe without
a frame received doubles the wait (20, 40, 80, 160, 320 s), up to 5; any received frame resets the count.
Any network delivers broadcasts within seconds, so a working port never gets there.

The design point is that it fixes the symptom without knowing the trigger: the full remove and probe
(the hypervisor device closed and opened, the chains and DMA region made again, autonegotiation set) is
what brought a dead port back by hand.

## Investigation

- **What crashed:** a CECH-20xx came up with the wired port's receive dead: link up at 1000/full,
  every packet sent, not one frame received since boot (rx_packets 0, no RX interrupt), so DHCP and even
  a static address got nothing back. No error from the driver or the hypervisor.
- **Why:** cause unknown. The state lives below the driver: ip link down/up, forced 10/100 Mbit,
  all-multicast and Wake-on-LAN off didn't bring it back. A rebind did, though not the first time; the
  same kernel had received fine on earlier boots. One suspect is receive DMA left in a bad state by what
  ran before (Petitboot's own DHCP, then kexec).
- **How it was found:** the second nightly's internal install on the dev console, 2026-10-23
  ([TESTING-0.1.md](../../../docs/release/TESTING-0.1.md) item 22).
- **Minimal repro:** no real fault on demand. `ps3_gelic.rx_watchdog_test=1`, then eth0 down and up,
  simulates one: on kernel +ps3.747 the re-probe came 20 s later and frames came in again 43 s after it.
  The next real fault is still open.

## Validation

Validated on 2026-11-01, both runs. The on run (PS3 session K, the merged test kernel 6.12.111+ps3.1210~dev20261031171117 with ps3rsx 0.1209): `rx_watchdog_test=1`, then eth0 down and up:
20 s later the watchdog re-probed the device, NetworkManager had the address back about 10 s after
that, and the next check, 43 s after the re-probe, saw frames coming in. An ssh session open through
it survived the gap.

The off run (`rx_watchdog=0`, the gelic-off check) came in the U5d session 2 boot the same day, kernel
.1220.dev20261101045143, which includes the merged series: with the watchdog off there was no simulated
fault and no re-probe, and packets kept coming (869 to 1194). The watchdog was put back on afterwards.

```
ps3_gelic_driver sb_06 eth0: receive watchdog: a fault simulated (rx_watchdog_test)
ps3_gelic_driver sb_06 eth0: receive watchdog: link up, 903 packets sent, none received (port status 0x43): re-probing the device, try 1 of 5
ps3_gelic_driver sb_06 eth0: receive watchdog: frames come in again after 1 re-probe(s)
```
