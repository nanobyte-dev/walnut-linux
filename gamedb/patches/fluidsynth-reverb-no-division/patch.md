+++
component = "fluidsynth"
patches = ["patches/0003-reverb-Dattorro-s-output-taps-without-a-division-per.patch"]
aliases = ["fluidsynth 0003"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-fluidsynth"]
+++
# The Dattorro reverb's output taps without a division per tap

The default reverb engine reads 14 output taps every sample, and each tap's position was wrapped with an
integer division (`index %= size`). The PPE divides in tens of cycles without pipelining, so the wrap cost
more than the reverb's arithmetic. The tap is always shorter than its line, so one conditional subtraction
gives the same index; the division stays for any other case. The output is bit-identical.

Measured against Debian's build on OpenTTD's music in gamedb:batch/2026-10-19-ssdroot5-fluidsynth.
