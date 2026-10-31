+++
id = "xmoto"
title = "X-Moto"
summary = "A 2D motorbike trials game: ride through physics-based levels without falling off, collecting strawberries."
tags = ["platformer", "racing", "single-player", "2d", "side-on", "physics", "bike", "levels"]
license = "GPL-2.0-or-later"
license_source = "debian/copyright of Debian's xmoto 0.6.3+repack-1"
homepage = "https://xmoto.tuxfamily.org/"
controller = "full"
+++

X-Moto 0.6.3 runs at 1920x1080 with the right picture and sound. Walnut's package batches the game's
OpenGL drawing (the first version made about 18,500 GL calls a frame, now 165), which took the
benchmark replay from 44 to about 160 frames a second.
