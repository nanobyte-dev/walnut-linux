+++
component = "boost-context"
patches = ["packaging/0001-ppc64-fcontext.patch"]
aliases = ["boost-context packaging/0001", "Debian #1146645"]
category = "port"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Boost.Context's fcontext assembler on ppc64

Debian builds the ucontext variant of libboost_context on ppc64, which lacks `make_fcontext`, `jump_fcontext`
and `ontop_fcontext`, the symbols the headers use unless the consumer defines `BOOST_USE_UCONTEXT`. Only
CMake users of `Boost::context` get that define, so Wesnoth (asio spawn) and anything linking with pkg-config
or a plain `-lboost_context` failed to link, and libboost_coroutine shipped with those symbols undefined
(Debian [#1146645](https://bugs.debian.org/1146645)). Upstream's CMake detects ppc64 big-endian ELFv1
correctly and builds the `ppc64_sysv_elf_gas.S` code ppc64el has always used. The assembler itself needed
a fix ([boost-context-save-fprs](../boost-context-save-fprs/patch.md)).
