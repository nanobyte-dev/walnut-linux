# The SPU compiler: GCC 16.2 with the SPU backend ported forward

GCC removed its Cell SPE backend (`spu`) in GCC 10. We carry it forward as a patch series, and GCC 16.2 is now our SPU compiler: `make spu-toolchain` builds spu-elf GCC 16.2.0 with binutils 2.47 and newlib 4.6, all patched by us. On the real SPEs, with everything built by GCC 16.2, 1629 of 1677 GCC execute torture tests pass (GCC 9.5 passes 1618 of 1671), with no regression. The one big job left is converting the backend from the old reload pass to LRA, which is only forced when GCC deletes reload. See [SPU.md](SPU.md) for what runs on the SPEs.

Written 2026-10-15 as a viability report, since turned into the record of what was done.

## The answer

Yes, it was viable, in two steps that cost very different amounts.

1. **The backend on GCC 16.2, still using the old reload pass: done, in about 2 hours.** The GCC 9.5 backend was copied into GCC 16.2. About 20 mechanical edits (186 changed lines) make it build.
2. **Converting to LRA: the real cost, an estimated 4 to 8+ weeks with uncertain risk.** With LRA switched on, 108 of 3695 torture files crash the compiler, against 19 with old reload. 89 of those crashes come from one design conflict: the SPU keeps scalars in the first word of a 128-bit register, while GCC's generic big-endian subregs put them in the last. GCC 16 and current trunk (17) still ship old reload. GCC 15 announced its removal "for GCC 16", and that has slipped. So this step is forced only when reload is actually deleted. We defer it and would budget it as its own project then.

## Status

`make spu-toolchain` builds spu-elf **GCC 16.2.0** from the release tarball plus three patches in `packages/patched/toolchain/spu/gcc/`:

- `0001-spu-transplant-gcc9.5-backend-into-gcc16.2.patch`: the transplant;
- `0002-spu-port-to-gcc16.2-api.patch`: the API port;
- `0003-expmed-no-invalid-subregs-for-extv-insv.patch`: an `expmed` fix for the `__complex__` crashes.

It uses old reload and links against newlib 4.6 (`packages/patched/toolchain/spu/newlib-4.6/`) and libgcc, both built by GCC 16.2, and binutils 2.47 with one patch. The version pins and sha256 of the sources are in `config.mk`.

Results on the SPEs, 2026-10-15, with everything built by GCC 16.2:

- **1629 of 1677** execute tests pass, against GCC 9.5's 1618 of 1671;
- no regressions, and every test has the same result as with binutils 2.33.1 and newlib 1.20.0 (both binary sets run on the same day; results in `build/spu-port/exec-*.txt`);
- hello world runs.

The earlier, first run of the port (old reload, but linked with GCC 9.5's binutils, newlib and libgcc) gave 1621 passes against 1618, with none failing that GCC 9.5 passes. That is the table under "The experiment", step 4.

### binutils 2.47

binutils 2.47 needs one patch, `packages/patched/toolchain/spu/binutils/0001-gas-spu-nop-is-an-instruction.patch`. It is not upstream yet (binutils master of 2026-10-15 still has the bug).

- **Symptom:** "Relocation doesn't fit" on an `hbrr` (first seen in libgloss's `open.c`), for GCC 9.5's output as well as ours.
- **Cause:** binutils commit `b1766e7ce886` (2.36, September 2020) added a generic `.nop` directive. The SPU assembler looks pseudo-ops up without the dot (`NO_PSEUDO_DOT`), so `nop` became the directive instead of the SPU instruction. `058430b4a1ae` (also 2.36) then gave `.nop` an optional size, so GCC's `nop 127` (`nop $127`, padding for dual issue) assembles to 32 nops that pad 127 bytes. Branches end up beyond the 9-bit word offset of the hints GCC placed, so some hints fail to assemble. The rest are inflated with no warning: 1245 of 1590 torture-test objects differ from 2.33.1.
- **Fix:** `tc-spu.c` masks the generic pseudo-op, as it already does for `set` and `eqv`: `nop` is the instruction and `.nop` stays the directive.

Checked against 2.33.1, both with GCC 16.2 and newlib 4.6, built side by side:

- Assembler: every libgcc, newlib, libgloss and crt object, plus 1572 `-O2` objects (`gcc.c-torture/execute` and `packages/walnut/tools/ps3-spu/src/hello.c`), has the same code, relocations and non-debug sections. The only difference is the 4 `__FILE__` strings of the build path in libc.
- Linker: the 1562 linked test programs come out the same when the 2.47 objects are linked with ld 2.33.1. ld 2.47 itself merges constants in a different order (`lqr`/`ila` addresses), aligns `.note.spu_name` to 4 instead of 16, and lays out merged strings differently (in `string-opt-17` the empty string gets its own 16 bytes): 16 more bytes of `.rodata` in 4 programs.
- The gas testsuite (`spu.exp`, `gas.exp`, `elf.exp`) passes as unpatched 2.47 does, the generic `.nop` test included.
- On the SPEs: the same result per test as 2.33.1 (1629 of 1677 pass), so ld 2.47's new layout changes nothing that runs.

### newlib 4.6.0.20260123

newlib 3.3.0 doesn't build for SPU. 4.6 needs seven small patches (`packages/patched/toolchain/spu/newlib-4.6/`, each with its reason in the file) and builds with plain `-O2`:

- `sys/sched.h` lacks `struct sched_param`;
- `math.h` declares `isinf`/`isnan`, which SPU's `ieeefp.h` defines as macros;
- SPU's `fenv.h` has `void` returns and a const `__fe_dfl_env`, and the generic `libm/fenv` placeholders that 4.x compiles for every target clash with them;
- the non-recursive makefiles lost the `-I` of the SPU source directories;
- SPU's own stdio never defined the global `__sf[3]` and `__stdio_exit_handler` that 4.x expects;
- `mmap_ea` returns an integer as a pointer, and `signal.c` and `__dprintf.c` call `getpid`/`write` with no declaration (both errors since GCC 14).

1.20.0 still builds (version and sha256 in `config.mk`; it needs `-std=gnu17 -fpermissive`). Local store use (text+data+bss) against 1.20.0: an empty `main` takes 3.6 KB instead of 2.7 KB (text +0.9 KB, bss +0.5 KB, since the 4.x `_reent` and exit handling are bigger), a `puts` program 3.8 KB instead of 3.0 KB, and a `snprintf`/`printf`/`sqrt` program 4.9 KB instead of 5.0 KB.

The libc/libgloss PPE-assisted ABI (stop code 0x2100 and its argument blocks, the libgloss syscalls) is unchanged. The SPU machine code of `fopen`/`fclose`/`fread`/`fputs` differs only in the `_reent` field offsets. `getentropy` (`arc4random`) and `regcomp` (`rpmatch`) are not provided, as 4.x pulls them in only from those two functions.

## What exists

- **Removal:** gcc commit `2f2aeda98f3a` (Ulrich Weigand, 2019-09-03) deleted 34k lines: the backend `gcc/config/spu` (20.7k lines), libgcc support (1.5k), the testsuite and the docs. GCC 9 already marked it obsolete (`--enable-obsolete` is needed).
- **Backend profile (GCC 9.5):** `spu.c` has 7.5k lines and `spu.md` 5.3k, with about 210 `define_insn`, 144 `define_expand` and 29 `define_insn_and_split`. It uses old reload (`TARGET_LRA_P` false, `LEGITIMIZE_RELOAD_ADDRESS`, 7 `reload_in_progress` checks) and has no cc0. Notable features: the `__ea` address space (about 60 references), branch-hint (`hbr`) insertion in machine reorg, and a pipeline description.
- **binutils:** SPU is still upstream at HEAD (bfd, gas, opcodes). We use 2.47 with the gas patch above.
- **newlib:** SPU is still upstream but has bit-rotted. With the seven patches 4.6 builds; the PS3 homebrew toolchain (ps3dev/ps3toolchain) still uses 1.20.0.
- **GDB:** Cell support was removed in GDB 9, so 8.3.1 would be the pin (not built here).
- **Other efforts:** we found no forward port of the SPU backend. PS3DK's tracking issue (#26, 2026-10) lists it as deferred. LLVM's CellSPU backend was removed in 2012 (LLVM 3.3) and never revived.
- **PPE side:** libspe2 is gone from Debian. `packages/walnut/tools/ps3-spu/src/spurun.c` (106 lines) runs SPU ELFs through spufs directly, and libspeq (`packages/walnut/tools/ps3-spu/lib`) keeps them resident as job workers. newlib's SPU code calls the PPE through stop codes. 0x2100 is C99 stdio (`printf` and co.) and 0x2101 is POSIX calls (`write`, `open`, ...); both are served by the PPE program, which neither spurun nor libspeq does yet, so test programs avoid stdio. 0x2104 runs Linux system calls (a whitelist: `write`, `futex`, `nanosleep`, ...) and is served by the kernel inside `spu_run` itself (`spufs/run.c`; newlib's `__linux_syscall`). The word after the stop is `opcode << 24 | LS address` of the argument block; `spu_run` returns with npc pointing at it.

## The experiment

Reproducible: `tools/spu-port-experiment.sh` does the transplant, `packages/patched/toolchain/spu/gcc/*.patch` hold the result, and `packages/patched/toolchain/spu/scripts/` has the test harnesses (`rebuild.sh`, `torture.sh`, `exec-build.sh`, `exec-build16.sh`, `run-exec.sh`).

### Step 1: transplant without edits

The GCC 9.5 backend files were copied into GCC 16.2.0 with only file renames and build-system glue:

- `.c` to `.cc` (GCC 13);
- the target stanza in `config.gcc` and `config.host`, without `dbxelf.h` (stabs were removed in GCC 13);
- `spu*` taken off the list of explicitly unsupported targets;
- an empty `spu.opt.urls` (GCC 14).

### Step 2: API catch-up (GCC 10 to 16)

All the changes needed for `make all-gcc` to succeed, each found by the compiler in rounds of incremental rebuilds:

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
| n/a | `opt_mode` and `insn_gen_fn` typing | `.require ()` (2 times), `GEN_FCN (icode) ()` |

That is about one breaking change per GCC release, all mechanical, and it is the ongoing upkeep.

### Step 3: compile test

`gcc.c-torture`, compile and execute directories, 3695 files, `-O2 -fpermissive -std=gnu17`:

| Compiler | Compiles | Compiler crash (ICE) | Rejected |
|---|---|---|---|
| GCC 9.5 SPU | 3637 | 9 | 48 |
| GCC 16.2 + port, old reload | 3636 | 19 | 40 |
| GCC 16.2 + port, LRA | 3550 | 108 | 36 |

- **Reload, 19 ICEs:** one bug. `__complex__` moves go through SPU's `extv` pattern, and newer GCC rejects the subreg it builds (`gen_rtx_SUBREG`). It is a contained fix (patch 0003). The 9 ICEs GCC 9.5 had on these files are gone (fixed upstream).
- **LRA, plus 89 ICEs in `spu_expand_mov`:** LRA's `match_reload` emits moves like `(set (subreg:SI (reg:V4SI) 12) (reg:SI $sp))`. Offset 12 is GCC's big-endian lowpart, but the SPU ABI keeps scalars in the preferred slot (offset 0), so the backend's `valid_subreg` rejects it and nothing can lower it. Old reload never produced such subregs. Treating LRA like reload (`lra_in_progress` beside `reload_in_progress`) changes nothing: this is the register model, not a flag.

### Step 4: run test on the SPEs

The `gcc.c-torture/execute` programs (they check their own results, then `abort()` or `exit(0)`) were built at `-O2` with GCC 9.5 and with the GCC 16 port (old reload; GCC 9.5's binutils, newlib and libgcc linked it). Each ran on a real SPE through `spurun`:

| Compiler | Pass | Fail | No exit (stdio etc.) | Timeout |
|---|---|---|---|---|
| GCC 9.5 | 1618 | 22 | 30 | 1 |
| GCC 16.2 + port | 1621 | 15 | 32 | 1 |

Per test: 6 go from fail to pass (middle-end bugs fixed upstream after GCC 9), 1 goes from fail to no-exit (`memcpy-1`), and none regress.

## What the LRA conversion would take

- **The preferred-slot model.** Either make every scalar-to-vector mode change go through explicit SPU patterns (forbid them in `TARGET_CAN_CHANGE_MODE_CLASS`, plus reload patterns or secondary memory), or teach the move expanders to rewrite lowpart subregs while `lra_in_progress`. Either way it is the SPU's most delicate area: sub-word loads and stores are quadword read-modify-write sequences (`spu_split_load`/`spu_split_store`) that need scratch registers.
- **`LEGITIMIZE_RELOAD_ADDRESS`** has no LRA equivalent. Address legitimisation moves to the `legitimate_address` hooks and constraints.
- **Wrong code is the real risk, not ICEs.** The m68k, sh and pa LRA conversions spent years behind `-mlra` opt-in flags because of wrong-code bugs. The execute suite on real SPEs is the safety net, and it is cheap: about 1700 programs in a few minutes.
- **Estimate:** 4 to 8 weeks for someone fluent in GCC backends, with real uncertainty, plus rebasing the patch series about once per GCC release.

## Not covered

- C++/libstdc++, `-O0`/`-Os`/`-O3`, the `__ea` and software-cache options, and the SPU intrinsics headers under GCC 16.
- GDB.

(The first run used GCC 9.5's libraries; the libgcc and newlib built by GCC 16.2 are covered by the 1629-of-1677 run above.)

Older versions and the dated history: [archive/SPU-GCC.md](archive/SPU-GCC.md).
