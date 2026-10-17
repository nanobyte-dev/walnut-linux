#!/usr/bin/env python3
# make-midi.py OUT.mid [SECONDS] [SEED]: a deterministic General MIDI test piece for
# tests/fluidsynth-compare.sh. Dense on purpose: 15 melodic channels and drums, programs changing
# every few bars across the GM set, chords and fast runs over notes 21..108 (high notes on low
# samples: large pitch steps), long held notes (sample loops), sustain pedal, pitch bend sweeps,
# modulation wheel (vibrato), volume/expression/pan moves (gain ramps), reverb and chorus sends,
# brightness (CC74) and velocity spread (the filter's cutoff modulators).
import random
import struct
import sys


def vlq(n):
    out = [n & 0x7f]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7f))
        n >>= 7
    return bytes(reversed(out))


def main():
    out = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    rnd = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    tpq = 480                      # ticks per quarter note, 120 bpm: 960 ticks a second
    end = int(seconds * 960)
    ev = []                        # (tick, order, bytes)

    def add(t, *b):
        ev.append((t, len(ev), bytes(b)))

    for ch in range(16):
        add(0, 0xB0 | ch, 7, 100)
        add(0, 0xB0 | ch, 10, rnd.randrange(128))
        add(0, 0xB0 | ch, 91, rnd.randrange(128))
        add(0, 0xB0 | ch, 93, rnd.randrange(128))
    bar = 4 * tpq
    for start in range(0, end, bar * 4):
        for ch in range(16):
            if ch != 9:
                add(start, 0xC0 | ch, rnd.randrange(128))
    for ch in range(16):
        t = rnd.randrange(bar)
        while t < end:
            kind = rnd.random()
            if ch == 9:                      # drums: short hits
                note, dur, n = rnd.randrange(35, 82), tpq // 8, 1
            elif kind < 0.15:                # long held note (loops, release)
                note, dur, n = rnd.randrange(28, 100), rnd.randrange(2, 8) * tpq, 1
            elif kind < 0.45:                # chord
                note, dur, n = rnd.randrange(36, 90), rnd.randrange(1, 4) * tpq // 2, 3
            else:                            # run, sometimes very high or very low
                note, dur, n = rnd.randrange(21, 109), tpq // rnd.choice((2, 4, 8)), 1
            vel = rnd.randrange(10, 128)
            for k in range(n):
                nk = min(127, note + (0, 4, 7)[k])
                add(t, 0x90 | ch, nk, vel)
                add(t + dur, 0x80 | ch, nk, 0)
            t += dur if rnd.random() < 0.7 else dur + rnd.randrange(tpq)
        # controllers over time
        t = 0
        while t < end:
            c = rnd.random()
            if c < 0.25:                     # pitch bend sweep
                for k in range(16):
                    v = int(8192 + 8191 * (k / 15.0) * rnd.choice((-1, 1)))
                    v = max(0, min(16383, v))
                    add(t + k * 20, 0xE0 | ch, v & 0x7f, v >> 7)
                add(t + 16 * 20 + tpq, 0xE0 | ch, 0, 64)
            elif c < 0.4:
                add(t, 0xB0 | ch, 1, rnd.randrange(128))       # modulation (vibrato)
            elif c < 0.55:
                for k in range(8):                              # pan move: gain ramps
                    add(t + k * 30, 0xB0 | ch, 10, rnd.randrange(128))
            elif c < 0.7:
                add(t, 0xB0 | ch, 11, rnd.randrange(40, 128))   # expression
            elif c < 0.8:
                add(t, 0xB0 | ch, 64, 127)                      # sustain pedal
                add(t + rnd.randrange(tpq, 4 * tpq), 0xB0 | ch, 64, 0)
            elif c < 0.9:
                for k in range(8):                              # brightness sweep
                    add(t + k * 40, 0xB0 | ch, 74, rnd.randrange(128))
            else:
                add(t, 0xB0 | ch, 7, rnd.randrange(60, 128))    # volume
            t += rnd.randrange(tpq // 2, 3 * tpq)
    for ch in range(16):
        add(end + 2 * bar, 0xB0 | ch, 123, 0)                 # all notes off at the end
    ev.sort(key=lambda e: (e[0], e[1]))
    trk = bytearray()
    trk += vlq(0) + bytes((0xFF, 0x51, 3)) + (500000).to_bytes(3, "big")
    last = 0
    for t, _, b in ev:
        trk += vlq(t - last) + b
        last = t
    trk += vlq(0) + bytes((0xFF, 0x2F, 0))
    with open(out, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq))
        f.write(b"MTrk" + struct.pack(">I", len(trk)) + trk)


main()
