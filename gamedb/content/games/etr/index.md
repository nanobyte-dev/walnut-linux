+++
id = "etr"
title = "Extreme Tux Racer"
summary = "A 3D downhill racing game in which Tux the penguin slides down snowy courses collecting herrings."
categories = ["racing", "sports"]
tags = ["3d", "penguin", "downhill", "courses"]
license = "GPL-2.0-or-later"
homepage = "https://sourceforge.net/projects/extremetuxracer/"
packages = ["extremetuxracer", "extremetuxracer-data"]
patched_by = ["etr"]
recipes = ["etr:base"]
needs_data = false
controller = "keyboard"
+++

Extreme Tux Racer 0.8.4 runs with the right picture and sound at about 59 frames a second. Walnut's six
patches send fewer, larger OpenGL draws (terrain from buffers, trees and snow in one draw per texture),
taking a frame from 19,400 GL calls to 850. The test replays a race that a person played once, as
keyboard events, and repeats it; the window includes a game-over screen and a reload, which show up as a
few long frames.
