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
known_issues = [
  "With the OpenGL driver, water pulses to white or very bright (its palette animation); the software driver doesn't.",
  "With the 32bpp-anim blitter the same game runs at 11 frames a second; NewGRFs that ask for 32bpp bring it back.",
]
+++

OpenTTD 15.1 runs with its OpenGL video driver, which Walnut's patches make start on the RSX (stock
refuses on the missing single-channel texture formats). Walnut sets the 8bpp-optimized blitter as the
default, which takes the benchmark savegame from 11 to about 38 to 45 frames a second.
