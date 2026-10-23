+++
id = "quakespasm"
title = "QuakeSpasm"
summary = "A modern source port of id Software's Quake, with the original campaign and mods."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "single player", "mods"]
license = "GPL-2.0-or-later"
homepage = "https://quakespasm.sourceforge.net/"
packages = ["quakespasm"]
recipes = ["quakespasm:base"]
needs_data = true
data_guide = "https://quakespasm.sourceforge.net/"
controller = "keyboard"
medal_note = "Rated with Quake's shareware data (the first episode)."
+++

QuakeSpasm 0.96.3 is Debian's package, unpatched, and plays the shareware demo1 timedemo at about 91
frames a second. It is started with `-nopackedpixels`, because 0.96.3's packed lightmaps show wrong
colours on a big-endian CPU. It needs Quake's `id1/pak0.pak` (the shareware one is enough for the
first episode); the data is not part of the package. A few frames take up to 400 ms, 2.5 s in
total over a two minute window.
