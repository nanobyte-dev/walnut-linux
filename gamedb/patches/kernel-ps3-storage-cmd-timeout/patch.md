+++
component = "kernel-ps3"
patches = ["patches/ps3/0020-ps3stor-send-cmd-timeout.patch"]
aliases = ["ps3/0020"]
category = "stability"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# A storage device command times out instead of waiting forever

`ps3stor_send_command()` waited for the hypervisor's completion with `wait_for_completion()`, with no
limit. The patch waits at most 2 s, logs `send_device_command 0x... timed out` and returns an error.
It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. The storage library is built in, so this runs for every ps3disk, ps3rom and storage command.

## Investigation

- **What crashed:** nothing on our console. The patch is about a hang: a storage command whose
  completion interrupt never arrives blocked its caller for good.
- **Why:** the old code had no timeout around the completion. Why a completion would go missing is
  not recorded in the patch set.
- **How it was found:** it came with glevand's set; [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE and keeps it.
- **Minimal repro:** None yet: proposed a debug build of ps3stor_lib that drops one completion in
  its interrupt handler, run on the PS3; the command should fail after 2 s with the timeout line.

## Validation

Not yet validated on the PS3.
