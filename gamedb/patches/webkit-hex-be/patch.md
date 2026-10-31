+++
component = "webkit2gtk"
patches = ["patches/0004-Uint8Array-fromHex-toHex-the-vector-paths-on-big-end.patch"]
aliases = ["webkit2gtk 0004", "WebKit fix 5"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Uint8Array fromHex/toHex: the vector paths on big-endian

Both reinterpret byte vectors as 16-bit lanes and assume that a lane's first byte in memory is its low
byte. On big-endian `fromHex` swapped the two nibbles of every output byte, `toHex` printed every byte low
nibble first, and `fromHex` of a 16-bit string never took the vector path. The lanes are built the other way
round on big-endian; little-endian is unchanged. `jsc` checks the hex methods on the C_LOOP interpreter under
qemu-user ([BROWSERS.md](../../../docs/research/BROWSERS.md)).
