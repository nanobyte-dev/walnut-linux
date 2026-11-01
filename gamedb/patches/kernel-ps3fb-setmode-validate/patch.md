+++
component = "kernel"
patches = ["patches/local/0053-ps3fb-validate-setmode.patch"]
aliases = ["local/0053", "kernel 0053"]
category = "security"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3:ps3-video-mode"]
batches = []
+++

# ps3fb: validate PS3FB_IOCTL_SETMODE's argument

`PS3FB_IOCTL_SETMODE` checked only the mode number in its argument. With the patch it also accepts
the option bits that apply to a video mode (full screen, dither, colour, white, DVI, RGB), and refuses
any other value with `-EINVAL`. `ps3-video-mode` and anything else that sets a valid mode works as
before. The autodetected mode for mode 0 is kept in a signed int, so a failed detection is seen as
one.

Not validated on the PS3 yet: on a text VT, `ps3-video-mode` sets 1080p and its full-screen variant as
before, and a value with other bits set is refused.
