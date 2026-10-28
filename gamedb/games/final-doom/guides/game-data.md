# Setting up the game data

Walnut Games makes the data for you. Open Final Doom in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import final-doom /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (plutonia-wad and tnt-wad) that Remove data takes away again. the two Final Doom entries appears in Play once the data is there.

What it needs: PLUTONIA.WAD and TNT.WAD (either, or both), from a CD, an installed copy (Steam: `common/Final Doom`), or the GOG installer. For TNT the import also downloads Team TNT's fixed MAP31 (`tnt31fix.zip`, about 120 KB).

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo final-doom`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
