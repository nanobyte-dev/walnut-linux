+++
id = "bomberclone"
title = "BomberClone"
summary = "A Bomberman clone: blow up walls and the other players with bombs, alone against bots or with friends."
tags = ["arcade", "action", "single-player", "local-multiplayer", "online-multiplayer", "bots", "2d", "top-down", "remake", "bomberman"]
license = "GPL-2.0-only"
license_source = "debian/copyright of Debian's bomberclone 0.11.9-9 (GPL-2: version 2 of the GPL, no later-version clause)"
homepage = "https://www.bomberclone.de/"
controller = "partial"
known_issues = [
  "The gamepad works in matches but not in the menus: they need the keyboard. Fixed in our next bomberclone (the pad works the menus, checked with a virtual pad).",
  "The HUD text is small at 1080p.",
]
+++

BomberClone is Debian's 0.11.9-9 (SDL 1.2 on sdl12-compat) with three patches of ours: a clean exit, full screen by default, and gamepads in the menus. One played session of about 5 minutes against bots held a median of 50 frames a second (1% low 28) at 1920x1080, with the picture at 4:3 and black bars at the sides. It never crashed, and the sound effects and music were heard. The DualShock played matches fine, but in that session the menus ignored it because they read the keyboard only. Our patch makes the pads work them, checked on the PS3 with a virtual pad but not yet in a played session. The HUD text is 12 px high at 1080p.
