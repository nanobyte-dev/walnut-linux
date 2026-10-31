+++
component = "webkit2gtk"
patches = ["patches/0003-SIMD-findInterleaved-low-bytes-in-val-0-on-big-endia.patch"]
aliases = ["webkit2gtk 0003", "WebKit fix 4"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# SIMD::findInterleaved: the low bytes in val[0] on big-endian too

findInterleaved de-interleaves UTF-16 text with `vld2q_u8` and gives the matcher the characters' low bytes
in `val[0]`. A byte de-interleave puts the bytes at even addresses there, which are the high bytes on
big-endian, so the HTML fast parser (16-bit input from `innerHTML` and fragments) never found '<', '&', CR
or the closing quote in ASCII text and read tags as text. The halves are swapped on big-endian. A standalone
test under qemu-user showed the stock code missing '<' in 31 of 33 positions
([BROWSERS.md](../../../docs/research/BROWSERS.md)).
