+++
id = "bzflag"
title = "BZFlag"
summary = "A 3D tank battle game: drive and shoot across online servers or against computer tanks."
tags = ["shooter", "online-multiplayer", "bots", "3d"]
license = "LGPL-2.1-only OR MPL-2.0"
license_source = "debian/copyright of Debian's bzflag 2.4.30-4"
homepage = "https://www.bzflag.org/"
controller = "partial"
designed_for = "keyboard-mouse"
known_issues = [
  "Naming yourself (the callsign) needs the keyboard; the rest plays with the DualShock.",
  "Many public servers refuse unregistered players: play against the robots, or register a callsign.",
]
+++

Offline play: the client has no robots option in its menus. Public servers often require a registered
account (a callsign registered at forums.bzflag.org, used with its password), and several refuse an
unregistered player to spawn. Computer tanks (robots) are the offline way to play: start "BZFlag (robots)"
from the menu (it runs `bzflag -solo 3`), choose Start Server, then Connect to localhost; the three robots
join whichever server the client joins.
