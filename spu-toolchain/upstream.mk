# Upstream pins of spu-toolchain/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- SPU toolchain (spu-elf, for the Cell's SPEs) ---------------------------------
# The same GCC and binutils versions as the PPE (docs/PLAN.md): GCC 16.2 with our port of the SPU
# backend (removed upstream in GCC 10; spu-toolchain/gcc/*.patch, docs/SPU-GCC.md), binutils 2.47 (SPU still
# upstream), newlib 4.6 with spu-toolchain/newlib-4.6/*.patch. GNU tarballs' signatures checked against
# gnu-keyring.gpg, newlib against sourceware's sha512.sum (2026-10-15).
SPU_GCC_VERSION     := 16.2.0
SPU_GCC_SHA256      := e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e
SPU_GCC_URL         := https://ftp.gnu.org/gnu/gcc/gcc-$(SPU_GCC_VERSION)/gcc-$(SPU_GCC_VERSION).tar.xz
# binutils 2.47 needs spu-toolchain/binutils/*.patch: since 2.36 gas reads GCC's "nop 127" as the generic
# ".nop 127" directive (32 nops), which pushed branches out of hint range ("Relocation doesn't
# fit" on hbrr). 2.33.1 (sha256 ab66fc2d1c3ec0359b8e08843c9f33b63e8707efdff5e4cc5c200eae24722cbf)
# needs no patch: to go back, set it and empty spu-toolchain/binutils/.
SPU_BINUTILS_VERSION := 2.47
SPU_BINUTILS_SHA256 := 154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff
SPU_BINUTILS_URL    := https://ftp.gnu.org/gnu/binutils/binutils-$(SPU_BINUTILS_VERSION).tar.xz
# newlib's SPU support is unmaintained since ~2012; 4.6 builds with spu-toolchain/newlib-4.6/*.patch (sys/sched.h,
# math.h's isinf/isnan, fenv signatures, include paths, SPU stdio on the 4.x reent, ...). 1.20.0
# (needs -std=gnu17 -fpermissive, no patches) still builds: set the version back, with
# sha256 c644b2847244278c57bec2ddda69d8fab5a7c767f3b9af69aa7aa3da823ff692.
SPU_NEWLIB_VERSION  := 4.6.0.20260123
SPU_NEWLIB_SHA256   := 6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3
SPU_NEWLIB_URL      := https://sourceware.org/pub/newlib/newlib-$(SPU_NEWLIB_VERSION).tar.gz
