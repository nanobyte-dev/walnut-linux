# Setting up the game data

Walnut Games makes the data for you. Open Hexen in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import hexen /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (hexen-wad) that Remove data takes away again. Hexen (DSDA-Doom) appears in Play once the data is there.

What it needs: HEXEN.WAD from Hexen: Beyond Heretic 1.1: a CD, an installed copy (Steam: `common/Hexen`) or GOG's installer (`setup_hexen_beyond_heretic_1.1_(42801).exe`, which has no Deathkings). Version 1.0 (the floppies and the first CD) does not run and is refused; Raven's update to 1.1 isn't applied for you yet. The Macintosh CD's Hexen.WAD is taken as a 1.1 copy (tagged `mac`): it is the same game with Macintosh fonts and menu art, but it has no GENMIDI instrument bank, so DSDA-Doom's OPL music player is unavailable with it; our default player (fluidsynth) plays the same music lumps.

Deathkings of the Dark Citadel, the expansion, is optional: HEXDD.WAD from its CD or an installed copy (Steam: `common/Hexen Deathkings of the Dark Citadel`). It gets its own entry in Play.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo hexen`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
