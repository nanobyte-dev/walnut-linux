+++
id = "openttd"
title = "OpenTTD"
summary = "A transport simulation: build railways, roads, ships and airports and run a profitable company."
categories = ["simulation", "strategy"]
tags = ["transport", "management", "tycoon", "multiplayer"]
license = "GPL-2.0-only"
homepage = "https://www.openttd.org/"
packages = ["openttd", "openttd-opengfx", "openttd-opensfx", "openttd-openmsx"]
patched_by = ["openttd"]
recipes = ["openttd:base"]
needs_data = false
controller = "keyboard"
medal_note = "Rated on a large late game (1962, 30 towns, 820 vehicles) with the 8bpp blitter, Walnut's default."
known_issues = [
  "The music chops and costs about a fifth of the frame rate (3,500 to 4,700 sound underruns in a run).",
  "With the 32bpp-anim blitter the same game runs at 11 frames a second; NewGRFs that ask for 32bpp bring it back.",
]
+++

OpenTTD 15.1 runs with its OpenGL video driver, which Walnut's patches make start on the RSX (stock
refuses on the missing single-channel texture formats). Walnut sets the 8bpp-optimized blitter as the
default, which takes the benchmark savegame from 11 to about 38 to 45 frames a second. The test is
a 1962 savegame with 820 vehicles at normal game speed, with sound and music on, using the free
OpenGFX, OpenSFX and OpenMSX sets.
