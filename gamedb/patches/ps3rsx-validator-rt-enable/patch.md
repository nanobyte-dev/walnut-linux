+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.1210"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow", "ps3:rsxdrm-smoke quirks rt12"]
batches = []
+++
# The validator refuses RT_ENABLE values Mesa never sends

RT_ENABLE (0x220 on the 3D object) is refused unless it is one of the values Mesa's nv30 driver writes: colour
targets 0 to n-1, plus MRT only with more than one target (0, 1, 3, 7, 0xf, 0x13, 0x17, 0x1f;
`rt_enable_ok()` in `src/validate.c`). Every word of 0x220 goes through the slow path, so the check covers
incrementing and non-incrementing headers alike.

## Investigation

- **What crashed:** `rsxdrm-smoke quirks` wedged the RSX with graph exception 0x106 on channel 1 (PS3 session
  M, 2026-10-31, ps3rsx 0.924), and again on purpose on 2026-11-01 (ps3rsx 0.1209). The test printed
  "vertex format type 0: NOT rejected", which pointed at the wrong push.
- **Why:** the second push binds the 3D object and writes RT_ENABLE 0x12: colour target 1 plus MRT, without
  target 0, and target 1's offset and pitch never set. The RSX traps on it (NSTATUS bit 25, BAD_ARGUMENT).
  Whether a Mesa-legal value with an unset target traps too is not tested; Mesa always sets the offsets
  first.
- **How it was found:** the three quirks pushes replayed through both validators on the PC (0.924's and the
  current one) accept push 2 and refuse push 3; session M's dmesg has no "rejected push" line, so push 3 never
  reached the validator. On 2026-11-01 the exception dump in the new layout (kernel local/0051) showed PGRAPH
  had taken the bind and both RT_ENABLE writes, and PGRAPH's own trap registers, read directly, held
  TRAPPED_ADDR 0x220 and TRAPPED_DATA 0x12 ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)).
- **Minimal repro:** `validate-test.c` tries every value 0 to 0x3f and replays the quirks pushes
  (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant `rt-enable`) the case
  fails. On the PS3, `rsxdrm-smoke quirks rt12` sends the push on its own channel: it must be refused. Run it
  only on a ps3rsx with this rule: on an older one it wedges the GPU.

## Validation

2026-11-01, kernel 6.12.111+ps3.1210~dev (the merged series), ps3rsx 0.1210~dev, ps3-gpu-tools 0.1215~dev.

- Fix off (ps3rsx 0.1209, the old `rsxdrm-smoke quirks`): graph exception 0x106 on channel 1, GPU wedged until
  reboot.
- Fix on, after the reboot: the push is refused and the GPU keeps running; egl-tri and gl-mipread pass after
  it.

```
rt12: RT_ENABLE 0x12: refused, errno 22
ps3rsx: rejected push: RT_ENABLE other than Mesa's (targets 0 to n-1, MRT only with more than one) (00040220)
```
