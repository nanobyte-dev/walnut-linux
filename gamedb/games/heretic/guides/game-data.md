# Setting up the game data

Walnut Games makes the data for you. Open Heretic in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import heretic /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (heretic-wad) that Remove data takes away again. Heretic (DSDA-Doom) appears in Play once the data is there.

What it needs: HERETIC.WAD from Heretic: Shadow of the Serpent Riders (1.3) or the original 1.0 release (three episodes), from a CD, an installed copy (Steam: `common/Heretic Shadow of the Serpent Riders`), or the GOG installer.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo heretic`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
