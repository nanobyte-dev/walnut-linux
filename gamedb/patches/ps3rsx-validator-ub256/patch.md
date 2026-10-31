+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.545~dev", "ps3rsx 5c3a8ad"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# The validator refuses UB256 vertex formats with other than four components

Since ps3rsx 0.545~dev (commit 5c3a8ad, 2026-10-19) the validator refuses vertex format type 7 (U8_USCALED,
libgcm's UB256) with a size other than 4 or 0 (disabled), on the fast path and in `check_method()`. Only
UB256 x4 is known to be safe; 1 and 2 components were never sent and are treated as forbidden too.

The trigger was Mesa's own [mesa-vtx-snorm8](../mesa-vtx-snorm8/patch.md) (0043, `NV30_VTX_SNORM8=1`),
which fetched R8G8B8_SNORM normals as UB256 with 3 components; 0043 now always fetches four bytes.

## Investigation

- **What crashed:** BananaBread's demo (the Cube 2 engine) with `NV30_VTX_SNORM8=1` on Mesa ps3.545~dev
  raised graph exception 0x106 2.2 s in; the run before it in the same boot, without the switch,
  finished. The dump is `docs/dumps/exception-0x106-ub256-snorm8.txt`.
- **Why:** the dump's GET is a pending BEGIN_END(TRIANGLES) right after the world vertex's format words,
  so the fault came at the start of a draw. The format slot with UB256 x3 isn't among the printed entries;
  it is blamed because RPCS3 asserts UB256 has 4 components and nouveau's table has U8_USCALED only as
  R8G8B8A8_USCALED ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)).
- **How it was found:** batch ssdroot6's 0043 run on 2026-10-19 (not part of a recorded batch), the dump
  read entry by entry.
- **Minimal repro:** `validate-test.c` checks every vertex type and size in every slot (35,333 checks; the
  old validator failed 1,935 of them), `tools/run-tests.sh --only ps3rsx-shadow`. The controlled PS3
  re-test of 0043 is in [TESTS.md](../../../docs/TESTS.md) Next.

## Validation

Not yet validated on the PS3.
