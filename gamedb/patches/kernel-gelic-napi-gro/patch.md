+++
component = "kernel"
patches = ["patches/local/0021-gelic-napi-alloc-skb-gro-receive.patch"]
aliases = ["local/0021", "kernel 0021"]
category = "performance"
switch = "none"
default = "always on"
authors = ["Florian Fuchs"]
forwarded = "no"
tests = []
batches = []
+++

# gelic: napi_alloc_skb() and napi_gro_receive() on receive

Florian Fuchs's upstream patch (mainline d8e08149a5ed): the receive path uses the NAPI-aware
`napi_alloc_skb()` and `napi_gro_receive()` instead of `netdev_alloc_skb()` and `netif_receive_skb()`.
Upstream reports more receive throughput, fewer retransmissions and better resilience against skb
allocation failures. It applies after [kernel-gelic-skb-alloc-fail](../kernel-gelic-skb-alloc-fail/patch.md)
(local/0020) and supersedes Geoff Levand's unfinished NAPI receive rework, which the survey of 2026-10-18
did not port ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md)). Not measured on our PS3 beyond a
200 MB transfer on kernel .433.
