+++
component = "kernel"
patches = ["patches/local/0052-ps3av-audio-mode-index-mask.patch"]
aliases = ["local/0052", "kernel 0052"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no (an upstream bug; to send after a PS3 test)"
tests = ["ps3:dmesg | grep cnv_ns"]
batches = []
+++

# ps3av: mask the option bits off the mode in the audio setup

`ps3av_set_audio_mode()` looked up the current mode's vid with `video_mode_table[ps3av_mode]`, but
`ps3av_mode` keeps the option bits ps3fb passes along with the mode: full screen (0x80), DVI (0x40),
RGB (0x20) and the rest. With any of them set, the index ran past the 14-entry table. That happens in
the full-screen modes (`mode:133` and the like) and with a DVI monitor in auto mode. The value read
there was usually not a known vid, so `ps3av_vid_video2av()` fell back to 480p, and the HDMI audio N
value sent to the AV manager was 480p's whatever the mode was (6272 instead of 8918 at 1080p). Every
other lookup masks the id first; the patch does the same here.

The dev console booted with `video=ps3fb:mode:133` for weeks, so its HDMI audio has probably run with
480p's N value all along. Nobody noticed by ear, but strict HDMI sinks may drop out or go silent with
it. It was an upstream bug that was already reachable; kernel local/0054 (full screen by default)
would have hit it on every boot, so this patch comes first.

Not validated on the PS3 yet: check that `dmesg | grep cnv_ns` comes up empty, and the user checks
HDMI audio by ear at 1080p and 720p, along with local/0054's test.
