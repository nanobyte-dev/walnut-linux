+++
component = "kernel"
patches = ["patches/local/0020-gelic-handle-skb-allocation-failures.patch"]
aliases = ["local/0020", "kernel 0020"]
category = "stability"
switch = "none"
default = "always on"
authors = ["Florian Fuchs"]
forwarded = "no"
tests = []
batches = []
+++

# gelic: handle skb allocation failures on receive

Florian Fuchs's upstream fix (mainline 0f08f0b0fb5e, after 6.12 and not in 6.12.y as of 6.12.111). When
refilling the receive ring fails with -ENOMEM, gelic now completes NAPI polling, retries from a timer and
re-enables receive DMA explicitly after the out-of-memory case, so the DMA controller doesn't stay
stopped. The backport changes one thing: 6.12 has no `timer_container_of()`, so `gelic_rx_oom_timer()`
uses `from_timer()`.

It came in with the survey of 2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md)), which
took it because our root and swap ran over the network then and RAM is tight.
[kernel-gelic-napi-gro](../kernel-gelic-napi-gro/patch.md) (local/0021) follows it upstream and needs it.

## Investigation

- **What crashed:** upstream's report: under memory pressure, an skb allocation failure in the receive
  path led to an oops in `gelic_net_poll()` ("Kernel access of bad area, sig: 11") and receive stalls,
  then the system stopped. Not seen on our PS3.
- **Why:** the refill path didn't handle a NULL skb and left the receive DMA stopped after a failure.
- **How it was found:** by its author, with a long scp and the kernel's failslab injection; we took it
  from mainline in the survey of 2026-10-18.
- **Minimal repro:** None yet: proposed: the patch's own steps on the PS3, a long transfer over gelic
  with failslab injection (needs fault injection in the kernel config, which a test build would add).
  Kernel .433 passed a 200 MB transfer with it, but the failure case has not been provoked on purpose.

## Validation

Not yet validated on the PS3.
