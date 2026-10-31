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

Not yet validated on the PS3.
