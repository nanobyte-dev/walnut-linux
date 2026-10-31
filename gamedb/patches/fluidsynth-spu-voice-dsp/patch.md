+++
component = "fluidsynth"
patches = ["patches/0001-rvoice-voice-DSP-through-libspeq-on-the-PS3-s-SPEs.patch"]
aliases = ["fluidsynth 0001"]
category = "performance"
switch = "SPEQ_SYNTH (libspeq's)"
default = "on when libspeq is installed"
forwarded = "no"
tests = []
batches = ["2026-10-17-fluidsynth-spu-dsp", "2026-10-17-fluidsynth-309", "2026-10-17-speq-367-deadline-pool"]
+++
# FluidSynth's voice rendering on the SPEs

FluidSynth's voice rendering (sample interpolation, the IIR filters, the mix into the output buffers) took
about 40% of the PPE time a game has on the PS3. The patch hands it to the Cell's SPEs through libspeq,
loaded at run time with dlopen: FluidSynth doesn't depend on it, and without it, or when it declines
(`SPEQ_SYNTH=off`, no SPEs), the mixer is the stock one.

The design point is the split of `fluid_rvoice_write()` into a control part (envelopes, LFOs, pitch,
filter coefficients) that stays on the PPE and a DSP chain that libspeq runs a round of blocks at a time.
The control part needs nothing the DSP computes from samples, so every voice's control runs first and one
libspeq call renders the whole round in single precision. Voices libspeq doesn't take (sinc interpolation,
24-bit samples) render on the PPE. All policy (which unit, sizes, timeouts, the fallback) lives in libspeq
([SPU-AUDIO.md](../../../docs/SPU-AUDIO.md)).

The batches measured three versions of the SPE side on dsda-doom's music: the first one
(gamedb:batch/2026-10-17-fluidsynth-spu-dsp), single-precision vector code on two SPUs
(gamedb:batch/2026-10-17-fluidsynth-309) and libspeq 0.367's shared pool with deadlines
(gamedb:batch/2026-10-17-speq-367-deadline-pool).
