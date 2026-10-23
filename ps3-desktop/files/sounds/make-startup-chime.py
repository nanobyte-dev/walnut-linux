#!/usr/bin/env python3
"""make-startup-chime.py OUT.wav: writes Walnut's startup chime, one Csus4 chord, 4.2 s, 48 kHz
stereo 16-bit (dmix's rate: no resampling), peak at -16 dBFS:
  strings: a soft ensemble swelling in under it (C3 G3 C4 F4; three detuned voices each, spread
    left and right, a gentle delayed vibrato)
  keys: G4 C5 F5, slowly rolled, nearly sine tones with slow attacks (no bell strike)
  celesta: four accents high up (C6 F6 G6 C7), quiet, alternating sides
Made here from sine partials, so the sound is ours (GPL-2.0-or-later like the rest of ps3-desktop)
and the same bytes come out of every build (ps3-desktop's debian/rules runs it)."""
import math, struct, sys, wave

RATE = 48000
LENGTH = 4.2
PEAK = 10 ** (-16 / 20)
TAU = 2 * math.pi
n = int(RATE * LENGTH)
left = [0.0] * n
right = [0.0] * n


def add(onset, f, level, pan, env, partials, vib=None):
	"""Adds one voice: env(t) the envelope, partials ((multiple, level), ...), pan -1 left .. 1 right,
	vib (rate Hz, depth as a fraction of f, delay s) for a vibrato fading in."""
	gl, gr = level * math.sqrt((1 - pan) / 2), level * math.sqrt((1 + pan) / 2)
	for k in range(int(onset * RATE), n):
		t = k / RATE - onset
		e = env(t)
		if e < 1e-5 and t > 0.5:
			break
		ph = TAU * f * t
		if vib:
			rate, depth, delay = vib
			w = min(1.0, max(0.0, (t - delay) / 0.8))
			ph += w * depth * f / rate * math.sin(TAU * rate * t)
		s = e * sum(a * math.sin(m * ph) for m, a in partials)
		left[k] += gl * s
		right[k] += gr * s


def swell(attack, hold, release):
	"""A slow string swell: a raised-cosine rise, a hold, then a long exponential release."""
	def env(t):
		if t < attack:
			return 0.5 - 0.5 * math.cos(math.pi * t / attack)
		if t < attack + hold:
			return 1.0
		return math.exp(-(t - attack - hold) / release)
	return env


def struck(attack, decay):
	return lambda t: min(1.0, t / attack) * math.exp(-t / decay)


# strings: a band-limited, darkened sawtooth (harmonic n at 1/n, rolled off), three voices a few
# cents apart, the outer two to the sides
STRINGS = tuple((m, (1 / m) * math.exp(-(m - 1) / 2.5)) for m in range(1, 9))
for f, level in ((130.81, 0.30), (196.00, 0.22), (261.63, 0.20), (349.23, 0.16)):	# C3 G3 C4 F4
	for cents, pan in ((-5, -0.6), (0, 0.0), (5, 0.6)):
		add(0.0, f * 2 ** (cents / 1200), level / 3, pan, swell(1.1, 0.6, 1.0), STRINGS,
		    vib=(5.0, 0.0025, 0.6))

# keys: the chord itself, rolled slowly
KEYS = ((1.0, 1.0), (2.0, 0.12), (3.0, 0.03))
for onset, f, level, pan in ((0.20, 392.00, 0.42, -0.15), (0.50, 523.25, 0.38, 0.0),
			     (0.80, 698.46, 0.36, 0.15)):	# G4 C5 F5
	add(onset, f, level, pan, struck(0.05, 1.5), KEYS)

# celesta: metal bars, a quick strike, a bright slightly inharmonic overtone, a short ring
CELESTA = ((1.0, 1.0), (2.0, 0.30), (3.93, 0.10), (5.4, 0.03))
for onset, f, level, pan in ((1.05, 1046.50, 0.13, 0.35), (1.40, 1396.91, 0.11, -0.35),
			     (1.75, 1567.98, 0.10, 0.35), (2.15, 2093.00, 0.08, -0.25)):	# C6 F6 G6 C7
	add(onset, f, level, pan, struck(0.003, 0.7), CELESTA)

# the last 800 ms fade to silence
fade = int(0.8 * RATE)
for k in range(n - fade, n):
	g = (n - k) / fade
	left[k] *= g
	right[k] *= g
scale = PEAK / max(max(abs(v) for v in left), max(abs(v) for v in right))
with wave.open(sys.argv[1], "wb") as w:
	w.setnchannels(2)
	w.setsampwidth(2)
	w.setframerate(RATE)
	w.writeframes(b"".join(struct.pack("<hh", round(a * scale * 32767), round(b * scale * 32767))
			       for a, b in zip(left, right)))
