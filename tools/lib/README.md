# tools/lib

Shell helpers for the PC-side scripts (`. tools/lib/NAME.sh`, from the repository root). PC only: they are never
shipped to the PS3, whose helpers stay self-contained. (`bench/lib` is something else: recipe includes.)

| File | What |
|---|---|
| `common.sh` | `ROOT`, `die`, `warn`, `need CMD`, `usage` (the caller's header comment) |
| `ps3.sh` | the one way to reach the PS3: `ps3_ssh`, `ps3_scp`, `ps3_rsync`, each behind `tools/ps3 check` (the lease); the command form is `tools/ps3 ssh ARGS...` |
| `chroot.sh` | `chroot_open`, `chroot_run`, `chroot_close`: the mount and unmount safety of the ppc64 build chroots, in one place |
| `t.sh` | the assertion helpers of the shell tests: `ok`, `no`, `expect`, `skip`, `done_tests` |
