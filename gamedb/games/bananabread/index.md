+++
id = "bananabread"
title = "BananaBread"
summary = "A first person shooter on the Cube 2 engine, with bots and an in-game map editor: the BananaBread content in Debian's cube2-data."
tags = ["fps", "online-multiplayer", "bots", "3d", "first-person", "map-editor"]
license = "Zlib (engine), CC-BY-3.0 (data)"
license_source = "debian/copyright of Debian's cube2 0.0.20201229+dfsg-1 (zlib) and of cube2-data 1.3-1 (CC-BY-3.0)"
homepage = "https://github.com/kripken/BananaBread"
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "Slow: a played session held a median of about 20 frames a second at 1920x1080, with 22 s of stutter a minute and a longest frame of 1.7 s.",
  "The sound card logged many underruns in that session (288 in 3 minutes), though the sound itself was marked fine.",
  "Debian's cube2-data leaves out many models and sounds (pickups, gun models, gibs and more); Walnut's licensed sauerbraten-nonfree-data package fills them in, and without it the game reports missing models and samples.",
]
+++

BananaBread is Mozilla's Cube 2 engine demo (Debian's `cube2`, with the BananaBread maps of `cube2-data`),
the engine Sauerbraten runs on. It is the heaviest shader game of the set. Walnut's patches compress textures when they
are loaded, skin the models on the CPU (the RSX has no indirect constant addressing, so GPU skinning ran
at 11 frames a second against 28), and send fewer GL calls. Sending the world normals as 16-bit values
raises the rate to about 45 but is not the default yet.
