+++
component = "game-data-packager"
patches = ["patches/0007-Add-transport-tycoon-deluxe-music-fm-the-Windows-CD-.patch"]
aliases = ["gdp 0007"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager: transport-tycoon-deluxe-music-fm

A Transport Tycoon Deluxe for Windows CD has its music only as `fm/9_tt*.fm`, the arrangements for FM
synthesizer cards, and OpenTTD's `orig_win.obm` lists the General MIDI files by MD5, so such a copy had no
original music. The new package installs the FM files with `orig_win_fm.obm`, an OpenTTD base music set
(`original_windows_fm`) that the new ttd plugin writes from the files' checksums. Its `generated_files()`
says what it writes, so `gdp-manifest.py` can record the set file like a game file
([PACKAGING.md](../../../docs/PACKAGING.md)).
