+++
id = "mrboom"
title = "Mr.Boom"
summary = "A Bomberman clone for up to eight players: drop bombs, blow up the walls and your friends, last one standing wins."
tags = ["arcade", "action", "local-multiplayer", "bots", "2d", "top-down", "remake", "bomberman"]
license = "MIT"
license_source = "debian/copyright of Debian's mrboom 5.5+ds-1"
homepage = "https://mrboom.mumblecore.org/"
controller = "full"
known_issues = [
  "Debian's build crashed within half a minute on the PS3 (a segfault in the game's own code: its translated 8086 code assumed little-endian registers). Fixed in our mrboom 5.5+ds-1+ps3.872 (the big-endian register layout), which still awaits a check on the PS3.",
]
+++

Mr.Boom 5.5 is a Bomberman clone built on SDL2 (through sdl2-compat) that runs the game's original 320x200 picture stretched to 1920x1080. Debian's build reached the welcome screen at a median of 52 to 55 frames a second in two sessions, with the DualShock opened and sending events, but both ended in a segfault within about 15 seconds. The cause was the game's translated 8086 code assuming little-endian registers, and our package ps3.872 fixes it. That fix passes the recorded demos under qemu and has not yet been played on the PS3, so nothing about a match is measured.
