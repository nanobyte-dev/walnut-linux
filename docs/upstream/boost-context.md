# Follow-ups for upstream (drafts, not sent)

Two messages: one to the Debian bug (#1146645, boost1.92), one to Boost.Context upstream
(github.com/boostorg/context, a new issue plus a pull request). The patches are in this repository:
`packages/patched/libs/boost-context/packaging/0001-ppc64-fcontext.patch` (Debian) and
`packages/patched/libs/boost-context/patches/0001-context-ppc64-save-fprs-vrs.patch` (Boost.Context).

---

## 1. To 1146645@bugs.debian.org

    To: 1146645@bugs.debian.org
    Cc: debian-powerpc@lists.debian.org
    Subject: Re: libboost-context1.90-dev: BOOST_USE_UCONTEXT is undefined on alpha, hppa, m68k, ppc64, sh4, x32

    Control: reassign -1 src:boost1.92 1.92.0-3
    Control: affects -1 src:wesnoth-1.18 src:cppgir

    Hi,

    I looked at this from the ppc64 side, where wesnoth-1.18 has failed to link since it moved to
    asio::spawn (buildd log of 1:1.18.8-1, 2026-08-25: undefined make_fcontext, jump_fcontext and
    ontop_fcontext from wesnothd's server_base.cpp). Some findings against boost1.92 1.92.0-3,
    which is what boost-defaults points at now:

    1. With the CMake build, Boost::context's exported targets do carry BOOST_USE_UCONTEXT= in
       INTERFACE_COMPILE_DEFINITIONS on ppc64, so CMake users that link the imported target get it.
       Anything that links -lboost_context without CMake (pkg-config, Meson, autotools, a plain
       Makefile, FindBoost's old variables) still compiles the fcontext code path of the headers and
       fails to link, as in the original report.

    2. libboost_coroutine.so.1.92.0 on ppc64 is unusable: Boost.Coroutine (v1) has only an fcontext
       backend, so the library is built with make_fcontext and jump_fcontext undefined, and it does
       not even have a DT_NEEDED on libboost_context (nm -D --undefined-only; readelf -d). Linking a
       program against it fails, loading it lazily fails at the first coroutine.

    3. ppc64 does not need ucontext at all. The list in debian/rules

           ifneq (,$(filter $(DEB_HOST_ARCH), alpha hppa m68k powerpc ppc64 sh4 sparc64 x32))
           CMAKE_ARCH_FLAGS += -DBOOST_CONTEXT_IMPLEMENTATION=ucontext

       is right for powerpc and sparc64 (#1145506, #1145444: CMake falls back to the i386/x86_64
       sources for processors it doesn't know), but libs/context/CMakeLists.txt knows ppc64:
       CMAKE_SYSTEM_PROCESSOR "ppc64" is in its _all_archs list, the ABI defaults to sysv and the
       format to elf, which selects {make,jump,ontop}_ppc64_sysv_elf_gas.S. Those handle ELFv1
       (.opd descriptors, TOC save) and are the same files ppc64el builds. With ppc64 removed from
       the list, the build log says

           -- Boost.Context: architecture ppc64, binary format elf, ABI sysv, assembler gas,
              suffix .S, implementation fcontext

       libboost_context.so.1.92.0 exports the three fcontext functions, libboost_coroutine gets its
       NEEDED on libboost_context, and wesnoth-1.18 1:1.18.8-1 builds and links on ppc64.

    Proposed change (patch attached, one word plus a comment):

        -ifneq (,$(filter $(DEB_HOST_ARCH), alpha hppa m68k powerpc ppc64 sh4 sparc64 x32))
        +ifneq (,$(filter $(DEB_HOST_ARCH), alpha hppa m68k powerpc sh4 sparc64 x32))

    Nothing else in the archive is affected: on ppc64 only libboost-fiber1.92.0 and the boost -dev
    packages depend on libboost-context1.92.0 (checked against the debian-ports Packages of
    2026-10-13), and libboost-fiber is rebuilt from the same source. The library's exported symbols do
    change on ppc64 (the ucontext build exported fiber_activation_record and friends, the fcontext one
    exports make/jump/ontop_fcontext), so a program someone built locally with BOOST_USE_UCONTEXT
    against 1.92.0-3 would need a rebuild; a shlibs bump for ppc64 would make that explicit, e.g.
    dh_makeshlibs -plibboost-context1.92.0 -V'libboost-context1.92.0 (>= 1.92.0-4~)' there.

    For the other architectures in the list (alpha, hppa, m68k, powerpc, sh4, sparc64, x32) the
    original request still stands: either the headers learn which implementation the library was
    built with (an #if on the architecture macros in boost/context/detail/config.hpp, since
    libboost1.92-dev is Multi-Arch: same and the header must be the same everywhere), or
    libboost-coroutine should not be built there, because it cannot work with ucontext.

    One more thing for ppc64 and ppc64el alike, which I'm reporting upstream: the ppc64 fcontext
    assembler doesn't preserve the non-volatile floating point registers f14-f31, FPSCR or the AltiVec
    registers v20-v30 across a switch (only the GPRs, CR and v31). A test that keeps doubles live
    across fiber::resume() in two contexts gets the other context's values back on ppc64 (tested
    under qemu-ppc64 with 1.92.0). Upstream issue/PR: <link once filed>. That is independent of
    this bug; ppc64el has shipped the same code for years.

    Tested: boost1.92 1.92.0-3 rebuilt on ppc64 (qemu-user, the sid snapshot of 2026-10-13) with the
    change, Boost.Context's test_fiber, test_callcc and test_fcontext, Boost.Coroutine's two tests,
    ten of Boost.Fiber's single-threaded tests, and a small program covering fiber, callcc,
    asio::spawn with yield_context, Boost.Coroutine and Boost.Fiber (results below); wesnoth-1.18
    built against it.

    Results (qemu-ppc64, 1.92.0-3 with the change and the Boost.Context patch from part 2):
      ctxtest (-O2, and -O2 -mcpu=cell -maltivec): fiber ping-pong, GPRs, FPRs, AltiVec registers
      across resume, exceptions and unwinding, callcc, asio::spawn + yield_context, Coroutine,
      Fiber: all pass (without the part 2 patch the FPR and AltiVec checks fail)
      libs/context/test: test_fiber, test_callcc, test_fcontext: pass
      libs/coroutine/test: test_asymmetric_coroutine, test_symmetric_coroutine: pass
      libs/fiber/test (single-threaded): fiber_dispatch, fiber_post, mutex, condition_variable,
      future, buffered/unbuffered_channel, barrier, fss, async (dispatch): pass
      wesnoth-1.18 1:1.18.8-1: builds, its conftests pass, wesnothd answers a client

---

## 2. Boost.Context upstream (issue + pull request)

Title: ppc64 (sysv elf): jump_fcontext/ontop_fcontext don't preserve F14-F31, FPSCR and V20-V30

    The ppc64 ELF assembler (jump_ppc64_sysv_elf_gas.S, ontop_ppc64_sysv_elf_gas.S; ELFv1 and
    ELFv2) saves r14-r31, CR, LR and, since the change for ClickHouse (#267), v31. The 64-bit
    PowerPC ELF ABIs (v1 and v2) also make f14-f31 non-volatile, v20-v31 with the AltiVec/VMX ABI,
    and FPSCR is a limited-access register that a call must not change. The FPR saves were in the
    file until 2015 (67d70bd "adapt pppc64/sysv/elf for jump_fcontext()" removed them); the ppc32
    version still saves f14-f31 and FPSCR.

    Since jump_fcontext is an ordinary call to the compiler, it keeps doubles in f14-f31 across
    fiber::resume(). When the other context also uses those registers, the values come back wrong.
    Reproducer (Boost 1.92.0, GCC 16, Debian ppc64 big-endian under qemu-ppc64; the same code
    runs on ppc64le):

        __attribute__((noinline)) double work(boost::context::fiber& other, double seed, int rounds) {
            double a = seed, b = seed * 2, /* ... twelve values ... */ r = seed * 31;
            for (int i = 0; i < rounds; i++) {
                other = std::move(other).resume();   // a..r live across the call
                a += 1.0; b += a; /* ... */ r += q;
            }
            return a + b + /* ... */ r;
        }
        // run work() in main and in a fiber, ping-ponging; compare with the same loop without
        // switches

    Result without the patch: main 0.25 (want 6643713632673.5), fiber 6.35e+18 (want
    620891503438763). The same with __vector float values at -maltivec fails too. The general
    register version of the test passes.

    The attached patch grows the context data from 200 to 552 bytes: f14-f31 at 200..343, FPSCR
    at 344, v20-v31 at 360..551 (the frame size stays 8 mod 16, so with r1 16-byte aligned at entry
    the vector slots are aligned, as the v31 slot at 184 was). The v31 save moves into the new
    block. make_fcontext reserves 552 + 48 bytes and copies the caller's FPSCR into the new
    context (jump restores it, so a fresh context must not get garbage there), like the ppc32 code
    does. Only r31 (already saved) and f0 are used as scratch, so the argument registers are
    untouched. The vector saves use stvx unconditionally, as the existing v31 save already did.

    With the patch the reproducer passes, and so do libs/context/test (test_fiber, test_callcc,
    test_fcontext), libs/coroutine/test and Boost.Fiber's single-threaded tests on ppc64 big-endian
    (ELFv1). Not tested on ppc64le (ELFv2) hardware; the change doesn't touch the ELFv2-specific
    paths (entry, TOC, return registers). The mach-o and xcoff variants have the same omission and
    are left alone.
