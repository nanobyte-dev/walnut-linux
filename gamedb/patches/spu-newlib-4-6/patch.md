+++
component = "spu-newlib"
patches = ["0001-spu-sys-sched.h-struct-sched_param.patch", "0002-math.h-declare-isinf-isnan-macro-safe.patch", "0003-spu-fenv-c99-signatures.patch", "0004-spu-include-paths.patch", "0005-spu-stdio-reent.patch", "0006-spu-mmap_ea-failed-pointer.patch", "0007-add-unistd.h-for-option-4-syscall-macros.patch"]
aliases = ["newlib 0001-0007"]
category = "port"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# newlib 4.6 for the SPU

newlib's SPU support has been unmaintained since about 2012; these seven fixes make 4.6 build and work for
`spu-elf`:

- 0001: SPU's `sys/sched.h` lacks `struct sched_param`, which the generic `sys/_pthreadtypes.h` uses.
- 0002: `math.h`'s declarations of `isinf`/`isnan` expanded SPU's macros of the same names.
- 0003: SPU's `fenv.h` with the C99 signatures the generic placeholders use, and the `_fe_dfl_env`
  convention of powerpc, mips and arm.
- 0004: the libc and libm SPU directories on the include path again (the non-recursive makefiles of 4.x
  dropped it; `Makefile.in` edited by hand too).
- 0005: SPU's own stdio: the global `__sf[3]` that `_REENT_INIT_PTR_ZEROED` points at, and
  `__stdio_exit_handler` set, so open files are still closed at exit.
- 0006, 0007: two errors since GCC 14 (an int-to-pointer return, implicit declarations of `getpid` and
  `write`).

One logical change, the port to 4.6, in seven reviewable steps. The old newlib 1.20.0 still builds
without them (with `-std=gnu17 -fpermissive`; `packages/patched/toolchain/spu/upstream.mk`).
