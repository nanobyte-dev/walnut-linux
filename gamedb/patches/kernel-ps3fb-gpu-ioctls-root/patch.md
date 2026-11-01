+++
component = "kernel"
patches = ["patches/local/0055-ps3fb-restrict-gpu-ioctls.patch"]
aliases = ["local/0055", "kernel 0055"]
category = "security"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3:rsx-probe ref"]
batches = []
+++

# ps3fb: restrict the GPU ioctls to root and copy their arguments

ps3fb's direct GPU interface (the `PS3GPU_IOCTL_*` calls on `/dev/fb0` and the `/dev/ps3gpu_*`
mappings) comes from OtherOS++'s ps3/0030 ([kernel-ps3-ps3fb-gpu-devices](../kernel-ps3-ps3fb-gpu-devices/patch.md)),
written for GPU research tools. This patch keeps it for those tools, run as root, and closes it to
everyone else:

- `PS3GPU_IOCTL_INFO`, `_ENTER` and `_EXIT`, and opening `/dev/ps3gpu_*`, need `CAP_SYS_ADMIN`;
  anyone else gets `-EPERM`. `/dev/fb0` and its other ioctls are unchanged, so the console, X and
  `ps3-video-mode` work as before.
- `ENTER` and `EXIT` copy their int argument in with `get_user()` and take only 0. A second `ENTER`
  gets `-EBUSY`, an `EXIT` without an `ENTER` gets `-EINVAL`, and an `EXIT` from another process
  `-EPERM`.
- The set-attribute, set-context-attribute and four cursor ioctls (numbers 9 to 14) are removed,
  since nothing uses them; ps3fb answers them like any unknown ioctl (`-ENOTTY`). Their structs
  leave the uapi header and the numbers stay reserved.
- Between `ENTER` and `EXIT`, ps3fb skips its own blits and flips (the console, a mode change,
  `FSEL`, the last close). `ENTER` no longer stops ps3fb's blit thread or unmaps its FIFO control
  area, so it can't hang waiting for the thread; `EXIT` re-runs ps3fb's reset program.
- A session ends by itself when the process that entered is gone (checked after each close of
  `/dev/fb0` or `/dev/ps3gpu_*`, at the next `ENTER` and at ps3rsx's takeover), or when no file has
  `/dev/fb0` open any more. A probe killed after `ENTER` no longer leaves the console without
  updates and ps3rsx unable to take the GPU.

ps3rsx doesn't use any of these: it shares the GPU through the in-kernel API
([kernel-ps3fb-sharing-api](../kernel-ps3fb-sharing-api/patch.md)), which this leaves alone. The raw
probes in ps3-gpu-tools (`rsx-probe`, `rsx-clear`, `rsx-tri`, `rsx-mesainit`, `rsx-hold`, `rsx-peek`)
need root, as their scripts already run them.

Not validated on the PS3 yet: ps3rsx's takeover and the console, the desktop and VT switching,
`rsx-probe ref` as root, `-EPERM` for the same calls from the desktop user, and the console coming
back after a probe is killed inside a session.
