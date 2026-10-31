+++
component = "webkit2gtk"
patches = ["patches/0002-CompactVariant-keep-small-payloads-in-the-storage-s-.patch"]
aliases = ["webkit2gtk 0002", "WebKit fix 1"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# CompactVariant keeps small payloads in the storage's low-order bytes on big-endian

A payload of at most 4 bytes was constructed at the address of the `uint64_t` storage and read back from
it. On big-endian that address holds the high-order bytes, so any non-zero value (a float length, a
percentage) met the top byte, and `RELEASE_ASSERT(!(data & topByteMask))` aborted the web process. The
payload now sits at offset `sizeof(Storage) - sizeof(T)` on big-endian; pointers and other representations
fill the whole storage and keep offset 0. Little-endian code is unchanged.

## Investigation

- **What crashed:** the web process aborted on nearly every page (`RELEASE_ASSERT` in CompactVariant).
- **Why:** a small payload stored at the start of a wider word lands in its high-order bytes on big-endian.
- **How it was found:** Debian's ppc64 crash report ([#1117148](https://bugs.debian.org/1117148)) and
  WebKit [bug 300442](https://bugs.webkit.org/show_bug.cgi?id=300442), which has no patch upstream
  ([BROWSERS.md](../../../docs/research/BROWSERS.md)).
- **Minimal repro:** a standalone test of the patched header under qemu-user (the stock CompactVariant
  aborts, the patched one passes), described in BROWSERS.md; not a `tools/run-tests.sh` suite yet.

## Validation

Not yet validated on the PS3.
