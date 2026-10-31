+++
id = "etr"
title = "Extreme Tux Racer"
summary = "A 3D downhill racing game in which Tux the penguin slides down snowy courses collecting herrings."
tags = ["racing", "sports", "family", "single-player", "3d", "penguin", "downhill", "courses"]
license = "GPL-2.0-or-later"
license_source = "debian/copyright of Debian's extremetuxracer 0.8.4-2"
homepage = "https://sourceforge.net/projects/extremetuxracer/"
controller = "full"
known_issues = [
  "A few short pauses: a played session had a 1% low of 24 frames a second and a longest frame of 1.6 s, with about 1 s of stutter a minute.",
  "Sound dropouts were logged in that session (86 in 12 minutes) though the player heard none.",
  "Its memory grows by about 4 MB a minute in a race; a longer run hasn't shown whether it stops.",
]
+++

Extreme Tux Racer 0.8.4 runs with the right picture and sound at about 59 frames a second. Walnut's
six patches send fewer, larger OpenGL draws (terrain from buffers, trees and snow in one draw per
texture), taking a frame from 19,400 GL calls to 850.
