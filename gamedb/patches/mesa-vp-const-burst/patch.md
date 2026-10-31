+++
component = "mesa"
patches = ["patches/0025-nv30-upload-consecutive-changed-vertex-program-const.patch"]
aliases = ["Mesa 0025"]
category = "performance"
switch = "NV30_VP_CONST_SINGLE"
default = "off (1 sends one constant per burst)"
gating = "none: every nv30"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262"]
+++

# nv30: upload consecutive changed vertex program constants in one burst

`nv30_vertprog_validate` sent every changed constant on its own: `VP_UPLOAD_CONST_ID`, the slot and four
words, six words per constant. Fixed-function GL changes several consecutive constants per object (the
modelview-projection matrix is four slots, the modelview matrix four more), so a draw after a matrix change
sent 24 to 48 words, a third of them headers. The GPU moves to the next slot after every four words of a
burst (libgcm uploads up to eight constants per burst, and ps3rsx's shadow follows the same rule), so a
run of changed constants in consecutive slots now goes up as one burst of at most four constants. Which
constants are uploaded, and with what values, is unchanged; a matrix costs six words less.

Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262).
