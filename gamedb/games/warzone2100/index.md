+++
id = "warzone2100"
title = "Warzone 2100"
summary = "A 3D real-time strategy game: build a base, research technology, command tanks."
categories = ["strategy"]
tags = ["rts", "3d", "campaign", "skirmish"]
license = "GPL-2.0-or-later"
homepage = "https://wz2100.net/"
packages = ["warzone2100", "warzone2100-data", "warzone2100-music"]
patched_by = ["warzone2100"]
recipes = ["warzone2100:base"]
needs_data = false
controller = "keyboard"
designed_for = "keyboard-mouse"
medal_note = "Version 4.3.5, the last release with an OpenGL 2.1 renderer; Debian's newer versions need OpenGL 3."
known_issues = [
  "Mouse and keyboard only, and the 1999 style interface is small at 1080p (Options, Video, Display Scale helps).",
  "Textures are limited to 512 pixels: the game's default of 2048 runs the GPU out of memory.",
]
+++

Warzone 2100 4.3.5 runs on the PS3 with the right picture and sound, thanks to a set of big-endian and
AltiVec fixes (patches below). As a console game it is a poor fit: it needs a mouse and keyboard.
