+++
component = "webkit2gtk"
patches = ["patches/0008-LLInt-load-the-callee-as-a-whole-slot-under-JSVALUE6.patch"]
aliases = ["webkit2gtk 0008", "WebKit fix 2"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# LLInt loads the callee as a whole slot under JSVALUE64

`getVMFromCallFrame`, used on every exception unwind, read the callee at `Callee + PayloadOffset` when
WebAssembly is off. That is harmless where PayloadOffset is 0, but on 64-bit big-endian (ppc64 and s390x,
C_LOOP builds without WebAssembly) it is 4, so the load took half the callee pointer and half the next
slot. `op_super_construct`'s callee cache had the same offset. Under JSVALUE64 both now load the whole slot.

## Investigation

- **What crashed:** every caught JavaScript exception killed the process: `try { null.x } catch {}` in
  `jsc`, Wikipedia in the browser.
- **Why:** the VM lookup from a half-pointer (`convertJSCalleeToVM`) faulted.
- **How it was found:** running our ppc64 build under qemu-user; the faulting load came from the qemu core
  ([BROWSERS.md](../../../docs/research/BROWSERS.md)).
- **Minimal repro:** `try { null.x } catch {}` in our ppc64 `jsc` under qemu-user; not a
  `tools/run-tests.sh` suite yet.

## Validation

Not yet validated on the PS3.
