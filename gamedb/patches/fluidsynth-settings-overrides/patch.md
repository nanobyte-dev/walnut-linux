+++
component = "fluidsynth"
patches = ["patches/0002-synth-setting-overrides-from-etc-fluidsynth-settings.patch"]
aliases = ["fluidsynth 0002"]
category = "feature"
switch = "/etc/fluidsynth/settings, FLUIDSYNTH_SETTINGS"
default = "off (nothing set)"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-fluidsynth"]
+++
# FluidSynth settings that win over the program's

Programs that embed libfluidsynth set its settings themselves, and most expose none of them: OpenTTD's
music driver leaves reverb and chorus on. `new_fluid_synth()` now applies overrides before it reads any
setting, first from `/etc/fluidsynth/settings` (lines "name value" or "name=value"), then from
`FLUIDSYNTH_SETTINGS` ("name=value" pairs). Each value takes its setting's type; unknown names and values
out of range are warned about and skipped, and what was applied is logged once. Without the file and the
variable nothing changes.

It exists so a setting can be switched system-wide or for one benchmark run without touching the game:
gamedb:batch/2026-10-19-ssdroot5-fluidsynth used it to measure what turning the synthesizer's effects off
would save on OpenTTD.
