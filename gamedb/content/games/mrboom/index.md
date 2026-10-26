+++
id = "mrboom"
title = "Mr.Boom"
summary = "A Bomberman clone for up to eight players: drop bombs, blow up the walls and your friends, last one standing wins."
categories = ["arcade", "action"]
tags = ["2d", "bomberman", "multiplayer", "local multiplayer"]
license = "MIT"
homepage = "https://mrboom.mumblecore.org/"
packages = ["mrboom"]
recipes = ["mrboom:base"]
needs_data = false
controller = "full"
known_issues = [
  "Crashes within half a minute on the PS3 (a segfault in the game's own code, likely a little-endian assumption of its translated 8086 code): being investigated.",
]
+++
