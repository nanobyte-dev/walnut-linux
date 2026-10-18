#!/bin/bash
# webcam-audio.sh [SECONDS] [out.wav]: records the room through the webcam's microphone (the BRIO
# next to the TV) and prints what it heard, for checks without the user at the TV: the PS3's fan
# (loudness, low-frequency energy) and game audio (white noise vs music: spectral flatness).
#   SECONDS  default 10;  out.wav default build/bench-cam/audio-<stamp>.wav
# Output: rms_dbfs (loudness), peak_dbfs, band shares (<300 Hz fan rumble, 300-2k, 2k-8k, >8k hiss),
# flatness (0..1; white noise ~0.5+, music and speech ~0.1-0.3, a quiet room with fan ~0.2-0.4),
# the strongest frequency. Numbers compare runs in the same room; they aren't calibrated SPL.
# Env: WEBCAM_MIC (default: the BRIO's PipeWire source by name).
set -euo pipefail
cd "$(dirname "$0")/.."
SECS=${1:-10}
OUT=${2:-build/bench-cam/audio-$(date +%Y%m%dT%H%M%S).wav}
SRC=${WEBCAM_MIC:-$(pactl list short sources | awk '/BRIO/ && /input/ {print $2; exit}')}
[ -n "$SRC" ] || { echo "no webcam microphone found" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
timeout $((SECS + 2)) pw-record --target "$SRC" --rate 48000 --channels 1 --format s16 "$OUT" &
pid=$!
sleep "$SECS"; kill -INT $pid 2>/dev/null || true; wait $pid 2>/dev/null || true
python3 -I - "$OUT" <<'PY'
import sys, wave, numpy as np
w = wave.open(sys.argv[1]); fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64) / 32768.0
if w.getnchannels() > 1: x = x.reshape(-1, w.getnchannels()).mean(axis=1)
x = x[int(0.2 * fs):]                       # skip the device's start-up click
rms = np.sqrt(np.mean(x ** 2)) + 1e-12
print(f"file={sys.argv[1]} seconds={len(x)/fs:.1f}")
print(f"rms_dbfs={20*np.log10(rms):.1f} peak_dbfs={20*np.log10(np.max(np.abs(x))+1e-12):.1f}")
n = 8192; frames = [x[i:i+n] * np.hanning(n) for i in range(0, len(x) - n, n // 2)]
P = np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0) + 1e-20
f = np.fft.rfftfreq(n, 1 / fs); tot = P[f > 20].sum()
bands = [(20, 300), (300, 2000), (2000, 8000), (8000, 20000)]
print("bands " + " ".join(f"{a}-{b}Hz={P[(f>=a)&(f<b)].sum()/tot*100:.0f}%" for a, b in bands))
sel = (f > 100) & (f < 12000)
flat = np.exp(np.mean(np.log(P[sel]))) / np.mean(P[sel])
print(f"flatness={flat:.2f} peak_hz={f[sel][np.argmax(P[sel])]:.0f}")
PY
