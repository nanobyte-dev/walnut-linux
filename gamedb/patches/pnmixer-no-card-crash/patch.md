+++
component = "pnmixer"
patches = ["patches/0001-audio-don-t-crash-on-a-volume-change-without-a-sound.patch"]
aliases = ["pnmixer 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# pnmixer doesn't crash on a volume change without a sound card

When pnmixer hooks no sound card it runs on with a NULL card, and `audio_set_volume`, `audio_lower_volume`
and `audio_raise_volume` still read the current volume straight from it. They read it through
`audio_get_volume` now, which gives 0 without a card, and `_audio_set_volume` already discards the change.

## Investigation

- **What crashed:** the tray's volume applet: SIGSEGV, a read at 0 in `alsa_card_get_volume`, on a slider
  move, a scroll on the icon or a volume hotkey. The first nightly had no volume icon in the tray.
- **Why:** the three setters didn't check for a missing card.
- **How it was found:** the 0.1 release testing ([TESTING-0.1.md](../../../docs/release/TESTING-0.1.md),
  items 18 and 23): pnmixer segfaulted 37 s into the session.
- **Minimal repro:** None yet: proposed pnmixer under Xvfb with no ALSA card (a container without
  `/dev/snd`), a scroll event on its tray icon sent with xdotool, and the check that the process lives.

## Validation

Not yet validated on the PS3.
