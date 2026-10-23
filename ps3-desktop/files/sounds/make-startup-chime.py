#!/usr/bin/env python3
"""make-startup-chime.py OUT.wav: writes Walnut's startup chime, a soft rising bell (G4 C5 E5 G5),
1.8 s, 48 kHz mono 16-bit (dmix's rate: no resampling), peak at -12 dBFS. Made here from sine
partials, so the sound is ours (GPL-2.0-or-later like the rest of ps3-desktop) and the same bytes
come out of every build (ps3-desktop's debian/rules runs it)."""
import math, struct, sys, wave

RATE = 48000
LENGTH = 1.8
NOTES = (392.00, 523.25, 659.25, 783.99)	# G4 C5 E5 G5
STEP = 0.11				# seconds between the notes' onsets
PARTIALS = ((1.0, 1.0), (2.0, 0.30), (3.0, 0.10))	# (multiple, level): a soft bell, not a pure tone
PEAK = 10 ** (-12 / 20)

n = int(RATE * LENGTH)
out = [0.0] * n
for i, f in enumerate(NOTES):
	start = int(i * STEP * RATE)
	tau = 0.55 - 0.07 * i		# the higher notes ring a little shorter
	for k in range(start, n):
		t = (k - start) / RATE
		env = min(1.0, t / 0.004) * math.exp(-t / tau)	# 4 ms attack, no click
		out[k] += env * sum(a * math.sin(2 * math.pi * f * m * t) for m, a in PARTIALS)
# the last 150 ms fade to silence, so the end doesn't click either
fade = int(0.15 * RATE)
for k in range(n - fade, n):
	out[k] *= (n - k) / fade
scale = PEAK / max(abs(v) for v in out)
with wave.open(sys.argv[1], "wb") as w:
	w.setnchannels(1)
	w.setsampwidth(2)
	w.setframerate(RATE)
	w.writeframes(b"".join(struct.pack("<h", round(v * scale * 32767)) for v in out))
