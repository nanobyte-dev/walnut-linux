+++
component = "kernel"
patches = ["patches/local/0005-ps3fb-gpu-sharing-api.patch"]
aliases = ["local/0005", "kernel 0005"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# ps3fb's GPU sharing API for an RSX driver

Everything ps3rsx needs from ps3fb, so that the driver itself can be a plain out-of-tree module:

- `ps3fb_gpu_get_info()`: the memory handle, DDR address and size, line length, mode, frame 0's offset
  and ps3fb's VRAM reservation;
- `ps3fb_gpu_ctx_get()`: the second lv1 GPU context, allocated on the first call and kept until
  shutdown, so module reloads never allocate or free contexts (allocating up to the limit and freeing
  stalled every FIFO, [RSX.md](../../../docs/RSX.md#contexts));
- `ps3fb_gpu_iomap()`: IO window maps that ps3fb records, so shutdown and remove can undo them and free
  our context before ps3fb frees the GPU memory, even with ps3rsx wedged;
- an exception hook called from ps3fb's interrupt, and a sticky exception count;
- `ps3fb_gpu_takeover()`: ps3fb and ps3rsx take turns owning the GPU. While ps3rsx owns it, ps3fb's
  blits, flips, mode changes, blanking and its direct-GPU interface (ioctls and `/dev/ps3gpu_*` mmap)
  are refused or do nothing.

The design point is the lock order: `own_lock`, then `console_lock`, then `ps3_gpu_mutex`, never the
fb_info lock (which already has an ABBA ordering with `console_lock`). A lockdep report on the first day
(commit ba97b74) made ENTER and EXIT only trylock the owner lock. Takeover appends a `SET_REFERENCE` to
ps3fb's own ring and polls it, because GET is not completion. The full description is in
[DRIVER.md](../../../docs/DRIVER.md#takeover-ps3fb-and-ps3rsx-take-turns).

Later patches build on it: [kernel-ps3fb-shadow-release](../kernel-ps3fb-shadow-release/patch.md)
(local/0043) frees the console's XDR frame during a takeover, and
[kernel-ps3fb-exception-dump](../kernel-ps3fb-exception-dump/patch.md) (local/0051) improves the dump the
exception path prints.
