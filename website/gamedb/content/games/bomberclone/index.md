+++
id = "bomberclone"
title = "BomberClone"
summary = "A Bomberman clone: blow up walls and the other players with bombs, alone against bots or with friends."
categories = ["arcade", "action"]
tags = ["2d", "bomberman", "multiplayer", "local multiplayer"]
license = "GPL-2.0-or-later"
homepage = "https://www.bomberclone.de/"
packages = ["bomberclone", "bomberclone-data"]
patched_by = ["bomberclone"]
recipes = ["bomberclone:base"]
needs_data = false
controller = "partial"
known_issues = [
  "The gamepad works in matches but not in the menus: they need the keyboard.",
  "The HUD text is small at 1080p.",
]
+++
