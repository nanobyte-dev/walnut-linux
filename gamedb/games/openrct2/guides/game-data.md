# Setting up the game data

Walnut Games makes the data for you. Open RollerCoaster Tycoon 2 in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import openrct2 /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (roller-coaster-tycoon2-data) that Remove data takes away again. OpenRCT2 appears in Play once the data is there.

What it needs: RollerCoaster Tycoon 2: its CD, an installed copy (a Steam or GOG folder) or the GOG installer. The CD works in the drive or as an ISO.

The Wacky Worlds and Time Twister expansions are optional. They come with the Triple Thrill Pack (its CD, a Steam or GOG folder, or either GOG installer, `setup_rollercoaster_tycoon2_2.0.0.6.exe` or `setup_rollercoaster_tycoon_2_triple_thrill_pack_2.01.043_(17987).exe`), and the same import installs them as roller-coaster-tycoon2-wacky-worlds-time-twister-data. Their scenarios then get their own tabs in OpenRCT2's scenario list. The original 2002 CD has only the base game.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo openrct2`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
