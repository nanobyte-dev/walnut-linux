# Setting up the game data

Walnut Games makes the data for you. Open Doom in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import doom /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (doom-wad) that Remove data takes away again. Doom (DSDA-Doom) appears in Play once the data is there.

What it needs: DOOM.WAD from Doom 1.9 or The Ultimate Doom, from a CD, an installed copy (Steam: the `common/Ultimate Doom` folder), or the GOG installer.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo doom`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
