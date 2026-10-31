+++
component = "kernel"
patches = ["patches/local/0003-ps3fb-export-gpu-handles-for-experiments.patch"]
aliases = ["local/0003", "kernel 0003"]
category = "diagnostics"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# ps3fb: export its GPU handles for experiment modules

One exported function, `ps3fb_gpu_handles()`, hands a test module ps3fb's lv1 memory and context
handles, its DDR address and its XDR buffer. The probe modules in
`packages/walnut/drivers/ps3rsx/probes/` (`rsx_ctx_probe.c`, `rsx_ctx_probe2.c`) use it; that is how the
second GPU context next to ps3fb's was tried before ps3rsx was written
([RSX.md, "Contexts"](../../../docs/RSX.md#contexts)). The driver itself goes through the sharing API of
[kernel-ps3fb-sharing-api](../kernel-ps3fb-sharing-api/patch.md) instead
([DRIVER.md](../../../docs/DRIVER.md#ps3fb-patches-the-sharing-api)).
