+++
id = "bzflag"
title = "BZFlag"
draft = true
status = "tried"
status_note = "Smoke run on the PS3 on 2026-10-28: Debian's 2.4.30 joins a local bzfs with three robot tanks, fullscreen 1920x1080, 59 fps steady (1% low 30), the robots fight, the tank spawns and drives (technical.md). First played session 2026-10-29 (8 min, quit): sound yes, controls full, no glitches; mostly menus and public servers that refused the player, one short local match."
+++

Offline play: the client has no robots option in its menus. Public servers often require a registered
account (a callsign registered at forums.bzflag.org, used with its password), and several refuse an
unregistered player to spawn. Computer tanks (robots) are the offline way to play: start "BZFlag (robots)"
from the menu (it runs `bzflag -solo 3`), choose Start Server, then Connect to localhost; the three robots
join whichever server the client joins.
