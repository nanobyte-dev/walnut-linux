+++
component = "kernel"
patches = ["patches/local/0054-ps3fb-fullscreen-default.patch"]
aliases = ["local/0054", "kernel 0054"]
category = "feature"
switch = "ps3fb.fullscreen"
default = "1 (the full frame; ps3fb.fullscreen=0, which PS3_UNDERSCAN=1 in /etc/default/ps3-boot adds to the boot entries, keeps the underscan inset)"
forwarded = "no (on by default for Walnut; an upstream version would default to off)"
tests = ["ps3-boot", "ps3:cat /sys/class/graphics/fb0/virtual_size", "ps3:dmesg | grep cnv_ns"]
batches = []
+++

# ps3fb: start in the boot mode's full-frame variant

Each broadcast mode (480i to 1080p50) has two frame layouts in ps3fb with the same video signal: an
underscan inset for TVs that crop the edges (1688x964 in 1080p, 1124x644 in 720p, 576x384 in 480p) and
the full frame, mode N+128 (`PS3AV_MODE_FULL`). Petitboot passes the mode from its settings without the
bit (`video=ps3fb:mode:5` at 1080p), and with no `video=` ps3fb takes ps3av's autodetected mode, also
without it. So the console, fbcon's logo and the boot splash sat inside black bars, while the desktop
(ps3rsx's KMS) always used the whole frame.

With the patch ps3fb ORs the bit into whatever broadcast mode it starts in, so the console keeps the
resolution Petitboot or the TV chose and only loses the inset. Forcing `mode:133` on the command line
was the old way, and on a 720p or SD TV it was a black screen (docs/release/TESTING-0.1.md 26): the
patch never picks a mode. VESA modes, auto mode with nothing detected, a mode named by resolution
(`video=ps3fb:1688x964`, or a global `video=<WxH>`) and `PS3FB_IOCTL_SETMODE` are left as given. The parameter is read-only at
run time and applied once, at probe.

The video timing is the same in both layouts. One thing did read the bit, though: ps3av's audio setup
indexed its mode table with the unmasked mode. The full frame on every boot would have made that read
run past the table and send 480p's HDMI audio N value, so local/0052 (the mask) has to come before
this patch.

The frame in VRAM is the native size in both layouts, and the XDR frame grows only up to it, so the
default 9 MiB of ps3fb memory holds one frame either way (docs/KERNEL-MEMORY.md, the 1080p row).

The opt-out is `PS3_UNDERSCAN=1` in `/etc/default/ps3-boot`: ps3-update-kboot and ps3-hdd1-boot add
`ps3fb.fullscreen=0` after each kernel's own arguments and at the end of the `/etc/kboot.d` entries
(unless an entry sets `ps3fb.fullscreen=` itself). An installer's or image build's first menu has no
setting yet, and the next `ps3-update-kboot` run rewrites it. ps3rsx gives X the native frame either way
(`native_frame()`), so the setting insets only the console and the splash until ps3rsx presents the
inset too (docs/backlog/platform.md "Display variety"). Kernels without the patch ignore the argument
(an unknown `module.param` on the command line is dropped).

Not validated on the PS3 yet: the plan is 1080p, 720p and 480p on HDMI, a resolution change in the XMB
between boots, `PS3_UNDERSCAN=1`, an empty `dmesg | grep cnv_ns`, and HDMI audio checked by ear.
