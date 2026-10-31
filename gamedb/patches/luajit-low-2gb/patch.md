+++
component = "luajit"
patches = ["patches/0001-ppc64-big-endian-allocate-in-the-low-2-GB-where-chec.patch"]
aliases = ["luajit 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# LuaJIT on ppc64 big-endian allocates in the low 2 GB

On ppc64 ELFv1 `lj_def.h` checks the global state with `checkptr31` (only ELFv2 gets `checkptr32`), but
`lj_alloc.c` probed random hints anywhere in the low 4 GB. The probe is now limited to 2 GB. The library
serves every LuaJIT program on the image, not only Luanti.

## Investigation

- **What crashed:** `luaL_newstate()` returned NULL about 40% of the time, and Luanti didn't start; its
  unit tests aborted in TestScriptApi.
- **Why:** whenever the state landed between 2 and 4 GB, LuaJIT refused its own allocation.
- **How it was found:** Luanti's first ppc64 build
  ([gamedb:luanti/luajit-state-allocation](../../games/luanti/investigations/2026-10-17-luajit-state-allocation.md)).
- **Minimal repro:** a minimal program that calls `luaL_newstate()` in the ppc64 chroot (qemu-user honours
  the hints like the kernel): 16 of 40 starts failed before, 60 of 60 passed after. Not in the repository
  yet; proposed as a host suite under qemu-ppc64.

## Validation

Not yet validated on the PS3.
