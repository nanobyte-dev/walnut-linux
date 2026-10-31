+++
component = "luajit"
patches = ["patches/0002-ppc64-string.lower-upper-and-reverse-reset-the-buffe.patch"]
aliases = ["luajit 0002"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-28-luanti-view190"]
+++
# LuaJIT ppc64: string.lower, upper and reverse reset the buffer with 64-bit stores

The interpreter's fast functions for `string.lower`, `string.upper` and `string.reverse` reset the global
temporary buffer (`w = b`) with 32-bit `lwz`/`stw`, but those are 64-bit pointers on ppc64. On big-endian
the load read the pointer's high word (0) and the store wrote 0 into w's high word, so every result started
with whatever the buffer held last: `("Hello World"):lower()` after `print(2)` gave "2hello world". The
pointer-sized `lp`/`stp` macros are used now.

Found through Luanti's settings menu, which built its page ids with `(section .. "_" .. name):lower()` and
failed on every open
([gamedb:luanti/settings-menu-string-lower](../../games/luanti/investigations/2026-10-28-settings-menu-string-lower.md)).
