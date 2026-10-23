+++
id = "openarena"
title = "OpenArena"
summary = "A fast Quake III Arena style deathmatch shooter with bots, free data included."
categories = ["shooter"]
tags = ["fps", "multiplayer", "bots", "3d"]
license = "GPL-2.0-or-later"
homepage = "http://openarena.ws/"
packages = ["openarena"]
patched_by = ["ioquake3"]
recipes = ["openarena:gl2min", "openarena:base"]
needs_data = false
controller = "keyboard"
medal_note = "Only the GL 2.0 launcher has been tested so far; GL 1.2, the default, is the faster one."
known_issues = [
  "The opengl2 renderer holds about 15 MB of free memory at its tightest.",
]
+++

OpenArena runs on Walnut's build of ioquake3, with both of its renderers. The fixed-function
`opengl1` renderer is the default and plays at about 70 frames a second with no stalls; the GLSL
`opengl2` renderer in its lean settings reaches 56 to 60. The patches that make this possible are
listed on the page (memory cuts for the GLSL renderer, CPU savings in the engine).
