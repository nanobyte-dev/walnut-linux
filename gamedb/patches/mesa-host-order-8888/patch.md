+++
component = "mesa"
patches = ["patches/0009-dri-gbm-loader-host-order-8888.patch"]
aliases = ["Mesa 0009"]
category = "port"
switch = "none"
default = "always on"
gating = "none: the formats are host-order words, identical to before on little-endian"
forwarded = "no"
tests = []
batches = []
+++

# dri, gbm, loader: the 8888 RGB formats as host-order words

The RSX renders and scans out A8R8G8B8 words: bytes A, R, G, B in memory. A big-endian X server asks
gbm and DRI for ARGB8888 and XRGB8888 meaning exactly that. Mesa's tables in `dri_helpers.c`,
`gbm_dri.c` and `loader_dri_helper.c` mapped the DRM 8888 fourccs to byte-order Gallium formats
(`PIPE_FORMAT_B8G8R8A8_UNORM` and friends), which on big-endian is the wrong byte order. They now map to
the host-order aliases (`PIPE_FORMAT_BGRA8888_UNORM`, `PIPE_FORMAT_ARGB8888_UNORM`, ...), which are the
same formats on little-endian and the right ones on big-endian. This is what made the X desktop's colours
right on the RSX ([RSX.md](../../../docs/RSX.md#mesa-kms-and-x-on-the-driver)); ps3rsx's KMS side
matches it with BGRX8888 and `quirk_addfb_prefer_host_byte_order`.
