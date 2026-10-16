# Porting GCC's SPU backend to current GCC: viability report

2026-10-15. Question: can the Cell SPE compiler (GCC's `spu` backend, removed in GCC 10) be brought
forward to the latest GCC, or do we stay on the last release that has it (GCC 9.5)?

## Answer

**Yes, it's viable, in two steps that cost very different amounts.**

1. **The backend on GCC 16.2, still using the old reload pass: done here in about 2 hours.**
   - The GCC 9.5 backend was copied into GCC 16.2. About 20 mechanical edits (186 changed lines,
     `spu-toolchain/gcc/0002-*.patch`) make it build.
   - On the real SPEs it passes **1621** of GCC's execute torture tests, against **1618** for
     GCC 9.5, with no test that GCC 9.5 passes failing.
2. **Converting to LRA, which GCC will eventually require: the real cost, an estimated 4–8+
   weeks with uncertain risk.**
   - With LRA switched on, 108 of 3695 torture files crash the compiler, against 19 with old
     reload.
   - 89 of those crashes come from one design conflict: the SPU keeps scalars in the *first* word
     of a 128-bit register, while GCC's generic big-endian subregs put them in the *last*.
   - GCC 16 and current trunk (17) still ship old reload. GCC 15 announced its removal "for
     GCC 16", and that has slipped. So this step is only forced when reload is actually deleted.

**Recommendation:**
- **Now:** use the pinned GCC 9.5 toolchain (`make spu-toolchain`), which is proven on hardware.
- **When newer C or newer optimisations matter:** carry the GCC 16 port as a patch series, like
  the Mesa fork, and pin GCC 16.x. Its runtime results on the SPEs are already as good as
  GCC 9.5's.
- **Defer the LRA conversion** until a GCC release removes reload, and budget it as its own
  project then.

## Status (2026-10-15, later): GCC 16.2 is our SPU compiler

`make spu-toolchain` now builds spu-elf **GCC 16.2.0** from the release tarball plus our three
patches:
- `0001` transplant;
- `0002` API port;
- `0003` an `expmed` fix for the `__complex__` crashes.

It uses old reload and links against newlib 4.6 (`spu-toolchain/newlib-4.6/`) and libgcc, both built by
GCC 16.2, and binutils 2.47 with one patch. Rerun on the SPEs with this toolchain (2026-10-15):
1629 of 1677 pass, and every test has the same result as with binutils 2.33.1 and newlib
1.20.0 (both binary sets run on the same day; results in `build/spu-port/exec-*.txt`).

**On the SPEs, with everything built by GCC 16.2:**
- **1629** of 1677 execute tests pass, against GCC 9.5's 1618 of 1671;
- **no regressions**;
- hello world runs.

**binutils 2.47 is not an exception any more.** It needs one patch
(`spu-toolchain/binutils/0001-gas-spu-nop-is-an-instruction.patch`), not yet upstream (binutils master of
2026-10-15 still has the bug):
- **Symptom:** "Relocation doesn't fit" on an `hbrr` (first seen in libgloss's `open.c`), for
  GCC 9.5's output as well as ours.
- **Cause:** binutils commit `b1766e7ce886` (2.36, Sep 2020) added a generic `.nop` directive.
  The SPU assembler looks pseudo-ops up without the dot (`NO_PSEUDO_DOT`), so `nop` became the
  directive instead of the SPU instruction. `058430b4a1ae` (also 2.36) then gave `.nop` an
  optional size, so GCC's `nop 127` (`nop $127`, padding for dual issue) assembles to 32 nops
  that pad 127 bytes. Branches end up beyond the 9-bit word offset of the hints GCC placed, so
  some hints fail to assemble. The rest are inflated with no warning: 1245 of 1590
  torture-test objects differ from 2.33.1.
- **Fix:** `tc-spu.c` masks the generic pseudo-op, as it already does for `set` and `eqv`:
  `nop` is the instruction and `.nop` stays the directive.
- **Checked against 2.33.1, both with GCC 16.2 + newlib 4.6, built side by side:**
  - assembler: every libgcc, newlib, libgloss and crt object, plus 1572 `-O2` objects
    (`gcc.c-torture/execute` and `ps3-spu/src/hello.c`), has the same code, relocations and
    non-debug sections. The only difference is the 4 `__FILE__` strings of the build path
    in libc.
  - linker: the 1562 linked test programs come out the same when the 2.47 objects are linked
    with ld 2.33.1. ld 2.47 itself merges constants in a different order (`lqr`/`ila`
    addresses), aligns `.note.spu_name` to 4 instead of 16, and lays out merged strings
    differently (in `string-opt-17` the empty string gets its own 16 bytes): 16 more bytes
    of `.rodata` in 4 programs.
  - the gas testsuite (`spu.exp`, `gas.exp`, `elf.exp`) passes as unpatched 2.47 does, the
    generic `.nop` test included.
  - on the SPEs: the same result per test as 2.33.1 (1629 of 1677 pass), so ld 2.47's new
    layout changes nothing that runs.

**newlib 4.6.0.20260123 is not an exception any more.** It needs seven small patches
(`spu-toolchain/newlib-4.6/`, each with its reason in the file) and builds with plain `-O2`:
- `sys/sched.h` lacks `struct sched_param`;
- `math.h` declares `isinf`/`isnan`, which SPU's `ieeefp.h` defines as macros;
- SPU's `fenv.h` has `void` returns and a const `__fe_dfl_env`, and the generic `libm/fenv`
  placeholders that 4.x compiles for every target clash with them;
- the non-recursive makefiles lost the `-I` of the SPU source directories;
- SPU's own stdio never defined the global `__sf[3]` and `__stdio_exit_handler` that 4.x expects;
- `mmap_ea` returns an integer as a pointer, `signal.c` and `__dprintf.c` call `getpid`/`write`
  with no declaration (both errors since GCC 14).

1.20.0 still builds (version and sha256 in `config.mk`; it needs `-std=gnu17 -fpermissive`).
Local store use (text+data+bss) against 1.20.0: an empty `main` takes 3.6 KB instead of 2.7 KB
(text +0.9 KB, bss +0.5 KB: the 4.x `_reent` and exit handling are bigger), a `puts` program
3.8 KB instead of 3.0 KB, and a `snprintf`/`printf`/`sqrt` program 4.9 KB instead of 5.0 KB.
The libc/libgloss PPE-assisted ABI (stop code 0x2100 and its argument blocks, the libgloss
syscalls) is unchanged: the SPU machine code of `fopen`/`fclose`/`fread`/`fputs` differs only
in the `_reent` field offsets. `getentropy` (`arc4random`) and `regcomp` (`rpmatch`) are not
provided, as 4.x pulls them in only from those two functions.

## What exists today

- **Removal:** gcc commit `2f2aeda98f3a` (Ulrich Weigand, 2019-09-03) deleted 34k lines: the
  backend `gcc/config/spu` (20.7k lines), libgcc support (1.5k), the testsuite and the docs.
  GCC 9 already marked it obsolete (`--enable-obsolete` is needed).
- **Backend profile (GCC 9.5):**
  - `spu.c` has 7.5k lines and `spu.md` 5.3k, with about 210 `define_insn`, 144 `define_expand`
    and 29 `define_insn_and_split`.
  - It uses old reload (`TARGET_LRA_P` false, `LEGITIMIZE_RELOAD_ADDRESS`, 7 `reload_in_progress`
    checks).
  - It has no cc0.
  - Notable features: the `__ea` address space (about 60 references), branch-hint (`hbr`)
    insertion in machine reorg, and a pipeline description.
- **binutils:** SPU is still upstream at HEAD (bfd, gas, opcodes). We use 2.47 with one gas
  patch (`spu-toolchain/binutils/`, see above).
- **newlib:** SPU is still upstream, but has bit-rotted. 3.3.0 doesn't build for SPU: its SPU
  `sys/sched.h` lacks `struct sched_param`, which the newer pthread types need. With seven
  patches 4.6 builds (see above); the PS3 homebrew toolchain (ps3dev/ps3toolchain)
  still uses 1.20.0.
- **GDB:** Cell support was removed in GDB 9, so 8.3.1 would be the pin (not built here).
- **Other efforts:**
  - No forward port of the SPU backend exists that we could find. PS3DK's tracking issue (#26,
    2026-10) lists it as deferred.
  - LLVM's CellSPU backend was removed in 2012 (LLVM 3.3) and never revived.
- **PPE side:** libspe2 is gone from Debian. `ps3-spu/src/spurun.c` (106 lines) runs SPU ELFs
  through spufs directly; libspeq (ps3-spu/lib) keeps them resident as job workers. newlib's SPU
  code calls the PPE through stop codes: 0x2100 for C99 stdio (`printf` and co.) and 0x2101 for
  POSIX calls (`write`, `open`, ...), both served by the PPE program, which neither spurun nor
  libspeq does yet, so test programs avoid stdio; 0x2104 runs Linux system calls (a whitelist:
  `write`, `futex`, `nanosleep`, ...) and is served by the kernel inside `spu_run` itself
  (`spufs/run.c`; newlib's `__linux_syscall`). The word after the stop is
  `opcode << 24 | LS address` of the argument block; `spu_run` returns with npc pointing at it.

## The experiment

Everything below is reproducible:
- `tools/spu-port-experiment.sh` does the transplant;
- `spu-toolchain/gcc/*.patch` hold the result;
- `spu-toolchain/scripts/` have the test harnesses.

### Step 1: transplant without edits

The GCC 9.5 backend files were copied into GCC 16.2.0, with only file renames and build-system
glue:
- `.c` to `.cc` (GCC 13);
- the target stanza in `config.gcc` and `config.host`, without `dbxelf.h` (stabs were removed in
  GCC 13);
- taking `spu*` off the list of explicitly unsupported targets;
- an empty `spu.opt.urls` (GCC 14).

### Step 2: API catch-up (GCC 10 → 16)

These are all the changes needed for `make all-gcc` to succeed. Each was found by the compiler,
in rounds of incremental rebuilds:

| GCC | Change | SPU edit |
|---|---|---|
| 10 | `params.h` removed; parameters are options | `SET_OPTION_IF_UNSET` in option override; drop `TARGET_OPTION_DEFAULT_PARAMS` |
| 10 | arg hooks take `function_arg_info` | `function_arg`, `function_arg_advance`, `pass_by_reference`, `setup_incoming_varargs` (plus the C23 no-named-args case) |
| 10 | `pass_by_reference` signature | `pass_va_arg_by_reference` in va_arg |
| 11 | `Report` option flag dropped | `spu.opt` |
| 12 | vector cost hooks became the `vector_costs` class | drop SPU's `init/add_stmt/finish/destroy_cost` (the default class uses SPU's `builtin_vectorization_cost`; loses an inner-loop weighting) |
| 13 | `emit_conditional_move` takes `rtx_comparison` | 2 calls (`__ea` pointer conversion) |
| 14 | address hooks take `code_helper` | `legitimate_address_p`, `addr_space_legitimate_address_p` |
| 14 | scoped attribute tables | `TARGET_GNU_ATTRIBUTES` |
| 14/15 | `resolve_overloaded_builtin` gains `bool complain` | `spu-c.cc` |
| 15 | `FLOAT/DOUBLE/LONG_DOUBLE_TYPE_SIZE` poisoned | removed (the default mode hook gives SPU's SF/DF/DF) |
| — | `opt_mode` and `insn_gen_fn` typing | `.require ()` (2×), `GEN_FCN (icode) ()` |

That's about one breaking change per GCC release, all mechanical. That's the ongoing upkeep.

### Step 3: compile test (`gcc.c-torture`, compile and execute directories, 3695 files, `-O2 -fpermissive -std=gnu17`)

| Compiler | Compiles | Compiler crash (ICE) | Rejected |
|---|---|---|---|
| GCC 9.5 SPU | 3637 | 9 | 48 |
| GCC 16.2 + port, old reload | 3636 | 19 | 40 |
| GCC 16.2 + port, LRA | 3550 | 108 | 36 |

- **Reload, 19 ICEs:** one bug. `__complex__` moves go through SPU's `extv` pattern, and newer
  GCC rejects the subreg it builds (`gen_rtx_SUBREG`). This is a contained fix. The 9 ICEs GCC
  9.5 had on these files are gone (fixed upstream).
- **LRA, plus 89 ICEs in `spu_expand_mov`:** LRA's `match_reload` emits moves like
  `(set (subreg:SI (reg:V4SI) 12) (reg:SI $sp))`. Offset 12 is GCC's big-endian lowpart, but the
  SPU ABI keeps scalars in the preferred slot (offset 0), so the backend's `valid_subreg` rejects
  it and nothing can lower it. Old reload never produced such subregs. Treating LRA like reload
  (`lra_in_progress` beside `reload_in_progress`) changes nothing: this is the register model,
  not a flag.

### Step 4: run test on the SPEs

`gcc.c-torture/execute` programs (they check their own results, then `abort()` or `exit(0)`)
were built at `-O2`:
- with GCC 9.5;
- with the GCC 16 port (old reload; GCC 9.5's binutils, newlib and libgcc link it).

Each ran on a real SPE through `spurun`:

| Compiler | Pass | Fail | No exit (stdio etc.) | Timeout |
|---|---|---|---|---|
| GCC 9.5 | 1618 | 22 | 30 | 1 |
| GCC 16.2 + port | 1621 | 15 | 32 | 1 |

Per test:
- 6 tests go from fail to pass (middle-end bugs fixed upstream after GCC 9);
- 1 goes from fail to no-exit (`memcpy-1`);
- **none regress.**

## What the LRA conversion would take

- **The preferred-slot model.** Either make every scalar↔vector mode change go through explicit
  SPU patterns (forbid them in `TARGET_CAN_CHANGE_MODE_CLASS`, plus reload patterns or
  secondary memory), or teach the move expanders to rewrite lowpart subregs while
  `lra_in_progress`. Either way it's the SPU's most delicate area: sub-word loads and stores
  are quadword read-modify-write sequences (`spu_split_load`/`spu_split_store`) that need
  scratch registers.
- **`LEGITIMIZE_RELOAD_ADDRESS`** has no LRA equivalent. Address legitimisation moves to the
  `legitimate_address` hooks and constraints.
- **Wrong code is the real risk, not ICEs.** The m68k, sh and pa LRA conversions spent years
  behind `-mlra` opt-in flags because of wrong-code bugs. The execute suite on real SPEs is the
  safety net, and it's cheap: about 1700 programs in a few minutes.
- **Estimate:** 4–8 weeks for someone fluent in GCC backends, with real uncertainty, plus
  rebasing the patch series about once per GCC release.

## Not covered here

- libgcc and newlib built with the new compiler: the run test used GCC 9.5's libraries.
- C++/libstdc++, `-O0/-Os/-O3`, the `__ea`/software-cache options, and the SPU intrinsics
  headers under GCC 16.
- GDB.
