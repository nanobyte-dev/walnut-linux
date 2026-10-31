+++
id = "openarena"
title = "OpenArena"
summary = "A fast Quake III Arena style deathmatch shooter with bots, free data included."
tags = ["fps", "arena", "sci-fi", "single-player", "online-multiplayer", "bots", "3d", "first-person", "remake"]
license = "GPL-2.0-or-later"
license_source = "debian/copyright of Debian's openarena 0.8.8+dfsg-13"
homepage = "http://openarena.ws/"
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "The gameplay options menu can draw wrong the first time it opens; leaving it (Escape) and opening it again fixes it.",
  "The opengl2 renderer holds about 15 MB of free memory at its tightest.",
]
+++

OpenArena runs on Walnut's build of ioquake3, with both of its renderers. The fixed-function
`opengl1` renderer is the default and plays at about 70 frames a second with no stalls; the GLSL
`opengl2` renderer in its lean settings reaches 56 to 60. The patches that make this possible are
listed on the page (memory cuts for the GLSL renderer, CPU savings in the engine).
