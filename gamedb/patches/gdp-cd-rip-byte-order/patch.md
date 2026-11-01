+++
component = "game-data-packager"
patches = ["patches/0020-Swap-back-a-CD-rip-whose-samples-come-out-byte-swapp.patch"]
aliases = ["gdp 0020"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no (the cdparanoia bug underneath is the real fix: docs/upstream/README.md)"
tests = ["game-data-packager", "ps3:walnut-data import yquake2 -p quake2-music /dev/sr0"]
batches = []
+++
# game-data-packager swaps back a CD rip whose samples come out byte-swapped

The first rip from a real disc in the PS3's drive (a burned Quake II CD, 2026-11-01) made
quake2-music, and the user heard white noise in Yamagi Quake II. The rip was the problem, not the
encoder or the game: gdp runs `cdparanoia -d DEVICE N rip.wav` and gives the WAV to oggenc, and on the
PS3 every sample in that WAV is byte-swapped.

## Investigation

- **The rip is byte-swapped:** a 5 s rip of track 2 at 0:30 (cdparanoia 3.10.2+debian-14+b1, ppc64,
  drive "SONY PS-SYSTEM 304R" over SG_IO) has a zero-crossing rate of 0.50 on the left channel read
  as little-endian, as the WAV says it is. White noise is 0.5. Read big-endian it is 0.13, which is
  music. The decoded quake2-music track had 0.44 (noise through a lossy encoder).
- **The swapped bytes are the CD's own:** swapped back, the rip is byte-identical to the CD image the
  disc was burned from (`Quake2.bin`), 708 bytes in (the drive's read offset). So the disc and the
  drive deliver the right audio; only the order inside each sample is wrong.
- **cdparanoia's switches don't change it:** `-E` and `-L` (the drive's byte order forced big or
  little), with and without `-Z`, give md5-identical output to the default. `-p` (host order) gives
  the same bytes as `-r` (little-endian), and only `-R` (big-endian) comes out little-endian. That
  points at cdparanoia's byte-order handling on a big-endian host, not at the drive. It is unproven
  without a second big-endian machine with a CD drive.

## The fix

After each rip, `game_data_packager/cd_byte_order.py` reads 5 s from the middle of the WAV's data
chunk and compares the zero-crossing rate of the first channel as stored with the byte-swapped one.
It swaps the whole data chunk in place, logging one warning line, only when the stored rate is above
0.35 and the swapped one under half of it. Music sits around 0.01-0.15 and swapped music around 0.5,
so the margins are wide. A genuinely noisy track (cymbals, applause, a high tone) is high both ways
and is left alone, and so are silence, other sample formats and anything that isn't a 16-bit PCM
WAV. A rip that is already right is never touched, on any host, so the patch is safe on amd64 too.
Pure Python (array, struct), no new dependency.

Tests (`game-data-packager`, in the deb builder against the built package): synthetic 44.1 kHz
stereo WAVs. A sine and music-like chords come back byte-for-byte when swapped and are left alone when
not. White noise and a 9 kHz tone in noise are left alone both ways, and so are silence, 8-bit PCM
and a non-WAV. A header with a 0 data size is still fixed. A source check confirms that build.py
calls it on every rip before the encoder.

## Validation

PS3, 2026-11-01, kernel 6.12.111+ps3.1220~dev, game-data-packager 89+ps3.1212~dev, the
burned Quake II CD in the drive. Without the patch, `walnut-data import yquake2 -p quake2-music
/dev/sr0` made noise (the user heard it in Yamagi Quake II, and the installed track measured 0.44).
With it the import took 514 s and swapped all ten rips back: 0.50 stored against 0.03-0.14 swapped
(track 11: 0.40 against 0.04). The installed oggs (21 MB) measure 0.017-0.135, music, and Yamagi
Quake II's full entry plays with `music/07.ogg` open, and the user, listening at the TV, said "that
music is great".
