#!/usr/bin/env python3
# f32-compare.py REF TEST [--exact | --snr DB]: two raw float32 files (host order) compared:
# identical or not, the largest difference, the reference's RMS and the error's, as an SNR.
# Exit 1 when --exact and they differ, or when the SNR is below DB.
import array
import math
import sys


def load(path):
    a = array.array("f")
    with open(path, "rb") as f:
        a.frombytes(f.read())
    return a


def main():
    ref, test = load(sys.argv[1]), load(sys.argv[2])
    exact = "--exact" in sys.argv
    snr_min = float(sys.argv[sys.argv.index("--snr") + 1]) if "--snr" in sys.argv else None
    n = min(len(ref), len(test))
    same = ref == test
    se = sr = 0.0
    dmax = peak = 0.0
    first = -1
    for i in range(n):
        r, t = ref[i], test[i]
        d = t - r
        if d and first < 0:
            first = i
        se += d * d
        sr += r * r
        if abs(d) > dmax:
            dmax = abs(d)
        if abs(r) > peak:
            peak = abs(r)
    rms_r = math.sqrt(sr / n) if n else 0
    rms_e = math.sqrt(se / n) if n else 0
    snr = float("inf") if not se else 10 * math.log10(sr / se) if sr else -float("inf")
    print("  %d frames%s; peak %.4f, rms %.5f; max difference %.3g, rms %.3g, SNR %s" % (
        n // 2, ", identical" if same else
        " (lengths %d/%d)" % (len(ref), len(test)) if len(ref) != len(test) else
        ", first difference at frame %d" % (first // 2),
        peak, rms_r, dmax, rms_e, "inf" if snr == float("inf") else "%.1f dB" % snr))
    if exact and not same:
        return 1
    if snr_min is not None and (len(ref) != len(test) or snr < snr_min):
        return 1
    return 0


sys.exit(main())
