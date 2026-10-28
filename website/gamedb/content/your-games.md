+++
title = "Bring your own games"
summary = "How Walnut Games turns your own CD, disc image, GOG installer or Steam folder into an installed game on the PS3, and which copies work."
+++

**Some games here are free; for the others you bring the game.** Doom, RollerCoaster Tycoon 2,
Quake III and the rest run on free engines, but the games' own files (levels, pictures, sound,
music) belong to their publishers, so Walnut doesn't offer them. If you own the game, Walnut Games
builds them into an installed package from your copy, on the console. Your files are read from the
disc, image or folder you point at and stay on your PS3. The only things fetched from the network
are official free patch files some games need (Quake II's 3.20 patch, Quake III's 1.32 update,
the fix for Final Doom's TNT) and the free shareware and demo editions.

## What counts as a copy

Any of these, as long as it holds the game's files:

- **A CD in the PS3's drive.** Put it in; the app lists it.
- **A disc image:** an ISO, a BIN/CUE pair, or an MDF/MDS file, wherever you copied it (Downloads or
  your home folder are searched for you).
- **A GOG installer** (`setup_*.exe`), unpacked for you; you don't run it.
- **A Steam folder** (`common/Quake`, say) or any installed copy you can reach, on a USB stick or
  disk.
- **A Mac CD**, for the one game where that matters (Hexen).

## How to do it

1. Copy the disc image or installer onto the PS3 (or plug in the stick, or put the disc in).
2. Open **Walnut Games**, find the game and open its page.
3. Choose **Add game files...** and pick your source from the list, or browse for a file or folder.
4. Wait. A first import takes from one to five minutes, and the app shows each step. When it ends
   the game appears in the menu.

In a terminal the same thing is:

    walnut-data import <game> /path/to/your/copy

where `<game>` is the id in the table below (`openrct2`, `quakespasm`, `yquake2`, `quake-3-arena`,
`doom`, and so on). A game made from several parts can be done a part at a time with `-p <package>`.
`walnut-data remove <game>` takes the data off again.

Under the hood this is Debian's game-data-packager, with a few additions of ours. It checks every
file against the checksums of the releases it knows, so a copy it doesn't recognise is refused with
a message instead of half installed.

## Which copies work

"Tried" means the import was run on a PS3 with that kind of copy and the game started from it.
Copies listed without "tried" are ones the tool recognises by checksum (GOG and Steam
editions, mostly); we haven't run them on the console.

| Game (id) | Copies that work | Notes |
|---|---|---|
| [RollerCoaster Tycoon 2](https://walnut.nanobyte.dev/gamedb/games/openrct2/) (`openrct2`) | The CD, an ISO or MDF image of it, a CD folder; GOG's standalone installer (2.0.0.6, the 2012 build); GOG's Triple Thrill Pack | Wacky Worlds and Time Twister come from the Triple Thrill Pack, either GOG installer, or its CD, and add their own scenario tabs. The plain 2002 CD has only the base game. Tried on the console: the CD ISO, and the 2012 GOG installer for the expansions (its base game needed more free space than the PS3 had that day; the Triple Thrill Pack installer was unpacked on a PC). A Steam-rip folder of the Triple Thrill Pack was not accepted for the expansions. |
| [Doom](https://walnut.nanobyte.dev/gamedb/games/doom/) (`doom`) | CD, Collector's Edition, GOG, Steam | Doom 1.9 or The Ultimate Doom. Tried: the Collector's Edition. |
| [Doom II](https://walnut.nanobyte.dev/gamedb/games/doom2/) (`doom2`) | CD, Collector's Edition, GOG, Steam | Version 1.9 and the original 1.666 both work. |
| [Final Doom](https://walnut.nanobyte.dev/gamedb/games/final-doom/) (`final-doom`) | CD, Collector's Edition, GOG, Steam | Plutonia and TNT. TNT's fix is downloaded. |
| [Heretic](https://walnut.nanobyte.dev/gamedb/games/heretic/) (`heretic`) | CD, GOG, Steam | Version 1.3 (Shadow of the Serpent Riders) and the early 1.0 release. Tried: 1.0. |
| [Hexen](https://walnut.nanobyte.dev/gamedb/games/hexen/) (`hexen`) | Version 1.1: CD, Steam, GOG's installer; the Macintosh CD | The Mac CD's WAD plays, but its music goes through the fluidsynth player, not DSDA-Doom's OPL one. Deathkings of the Dark Citadel is optional and has its own menu entry; nobody has tried it yet. |
| [Quake](https://walnut.nanobyte.dev/gamedb/games/quakespasm/) (`quakespasm`) | CD, a BIN/CUE of it (this keeps the CD music), Steam, GOG's The Offering; the free shareware | The shareware (first episode) is downloaded for you. |
| Quake, 2021 re-release | GOG's Quake Enhanced installer, build 60828 | An edition of its own, with its own menu entry, Quake (2021 re-release). It does not stand in for the original game. |
| [Quake II](https://walnut.nanobyte.dev/gamedb/games/yquake2/) (`yquake2`) | CD, a BIN/CUE of it (this keeps the CD music), Steam, GOG's Quad Damage; the free demo | The 3.20 patch is downloaded unless your copy has it. The demo is downloaded for you. |
| [Quake III Arena](https://walnut.nanobyte.dev/gamedb/games/quake-3-arena/) (`quake-3-arena`) | CD or its ISO or MDF image; GOG's Quake III Gold (with Team Arena); Steam | A CD is older than 1.32, so the 1.32 update (30 MB) is downloaded. Tried: the CD ISO. GOG's installer and Team Arena are built the same way but have not been imported on the console yet. |
| [OpenTTD](https://walnut.nanobyte.dev/gamedb/games/openttd/) (`openttd`) | Transport Tycoon Deluxe for Windows: the CD or an installed copy | Optional: OpenTTD plays without it using free graphics. With your copy you get the original graphics and sounds. Music comes from either the General MIDI (`gm`) or the FM (`fm`) files, whichever your copy has. |

## What doesn't work yet

- **Hexen 1.0** (the floppies and the first CD) is refused: DSDA-Doom doesn't support it, and the official update to 1.1 isn't applied by Walnut yet. Hexen 1.1 is fine.
- **The Quake 2021 re-release's add-ons** (Scourge of Armagon, Dissolution of Eternity, Dimension of the Past, Dimension of the Machine, the CTF pak) are in the same installer but aren't imported yet. The main game is.
- **Other builds of the re-release** (Steam's, older GOG updates) haven't been checked.
- **Quake II's expansions** (The Reckoning, Ground Zero, Zaero) and Quake III's demo are known to the tool but haven't been tried on the console.
- **Ripping music from an audio CD** in the PS3's drive is untested. BIN/CUE images work.

## Music

Several of these games played music from the CD's audio tracks. A BIN/CUE image keeps them: Walnut
encodes the tracks to Ogg Vorbis (Quake's tracks 2 to 11, Quake II's the same), which takes a few
minutes on the PS3's processor, and the games play those files. From an ISO, which has no audio
tracks, or a GOG installer, you get what the copy holds: GOG's Quake Enhanced has its own music;
Transport Tycoon's music comes as MIDI files. Quake and Quake II import their music as a second
step (`-p quake-music`, `-p quake2-music`), so import the game and its music from the same
`.cue`.

## Editions

Each game's page says which edition you have: **shareware** or **demo** (the free first part),
**full**, **expansion**, **music**, or **remaster** (Quake's 2021 re-release). A rating from played
sessions only combines sessions of the same edition, so a shareware Quake and a full one don't
average into one number.

## Disk space

Installed sizes are modest: Quake about 50 MB, Quake III about 480 MB, RollerCoaster Tycoon 2
about 550 MB, the Doom family a few tens of MB each. The import needs more room while it works.
Plan for around three times the installed size for a disc or folder, and up to four times for an
installer, which is unpacked first. RollerCoaster Tycoon 2 from an installer can want 2 GB free.
If space is short, the import says so before it starts and changes nothing. Delete the image or
installer afterwards, since the game doesn't need it any more.
