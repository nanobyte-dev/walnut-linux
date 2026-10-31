+++
component = "kernel"
patches = ["patches/local/0027-spufs-gang-alive-double-decrement.patch"]
aliases = ["local/0027", "kernel 0027"]
category = "stability"
switch = "none"
default = "always on"
authors = ["Zhenhao Wan"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: gang->alive decremented twice on context creation

`spufs_create_context()` takes a gang reference (`gang->alive++`) to hold until the context directory
is closed. Its error epilogue tested `if (ret && gang)`, but `spufs_context_open()` returns a file
descriptor on success, which is non-zero whenever the caller already has an open fd, so the reference
was dropped on success as well, and `unuse_gang()` dropped it again at close. The fix tests
`ret < 0`, as `spufs_create_gang()` does. The bug came with c134deabf478 ("spufs: fix gang directory
lifetimes"), which 6.12.111 has. Posted by Zhenhao Wan on 2026-08-14, not merged.

## Investigation

- **What crashed:** nothing seen on our PS3; we use no gangs. The failure is the gang's count reaching
  zero while contexts still reference it, which removes the gang directory too early.
- **Why:** the success path's non-zero file descriptor was taken as an error.
- **How it was found:** code reading, by the patch's author; checked against our code in the survey of
  2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
- **Minimal repro:** None yet: proposed: create a gang, then two contexts in it from a process with
  other fds open, close one and list the gang directory; without the fix the gang is removed early.

## Validation

Not yet validated on the PS3.
