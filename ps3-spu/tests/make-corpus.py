#!/usr/bin/python3 -I
"""make-corpus.py OUTDIR DEB... [--png DIR...]: the DXT test corpus as PAM (RGB_ALPHA) for speq-dxt
and mesa-dxt.

- OUTDIR/oa/: 50 opaque OpenArena textures (jpg, and tga without real alpha), picked evenly
  from the sorted list of those with sides that are multiples of 4, 32 to 512;
- OUTDIR/oa-alpha/: 12 alpha-tested ones (alpha both <= 127 and > 127), for RGBA DXT1;
- OUTDIR/oa-a5/: 24 OpenArena textures with any alpha (smooth, alpha-tested, glows), for DXT5;
- OUTDIR/png-a5/: 40 PNG textures with alpha from the --png directories (SuperTuxKart's,
  Neverball's, ...: their source trees in build/work), sides multiples of 4, 16 to 1024, for DXT5;
- OUTDIR/synth/: gradients (along the RGB diagonal, across it, 2D, radial) and noise, 256x256;
  synth-a5/: the same with alpha ramps, alpha noise and cut-outs, for DXT5.
The textures come from the pk3 files in the given openarena data debs (build/apt-cache). Images
are written with alpha 255 where opaque, as OpenArena uploads them (RGBA sources). Needs PIL
and numpy. The corpus is generated data: it goes under build/tmp, never into git.
"""
import io
import os
import sys
import tarfile
import zipfile

import numpy as np
from PIL import Image

N_OPAQUE, N_ALPHA, N_A5, N_PNG = 50, 12, 24, 40


def deb_members(path):
    """yields (name, bytes) of the files in a .deb's data tarball"""
    d = open(path, 'rb').read()
    if d[:8] != b'!<arch>\n':
        sys.exit(f'{path}: not a deb')
    pos = 8
    while pos < len(d):
        name = d[pos:pos + 16].decode().strip().rstrip('/')
        size = int(d[pos + 48:pos + 58])
        body = d[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if name.startswith('data.tar'):
            with tarfile.open(fileobj=io.BytesIO(body)) as t:
                for m in t.getmembers():
                    if m.isfile():
                        yield m.name, t.extractfile(m).read()


def write_pam(path, rgba):
    h, w, _ = rgba.shape
    with open(path, 'wb') as f:
        f.write(f'P7\nWIDTH {w}\nHEIGHT {h}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'.encode())
        f.write(np.ascontiguousarray(rgba, dtype=np.uint8).tobytes())


def textures(debs):
    """(name, RGBA array) of every usable texture in the debs' pk3 files"""
    for deb in debs:
        for name, data in deb_members(deb):
            if not name.endswith('.pk3'):
                continue
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for n in sorted(z.namelist()):
                    if not n.startswith('textures/') or not n.lower().endswith(('.jpg', '.tga')):
                        continue
                    try:
                        im = Image.open(io.BytesIO(z.read(n)))
                        im.load()
                    except Exception:
                        continue
                    w, h = im.size
                    if w % 4 or h % 4 or min(w, h) < 32 or max(w, h) > 512:
                        continue
                    yield n, np.asarray(im.convert('RGBA'))


def spread(items, k):
    if len(items) <= k:
        return items
    return [items[i * len(items) // k] for i in range(k)]


def synth(out):
    n = 256
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    rng = np.random.default_rng(1)
    imgs = {
        'grad-diag': np.stack([x, x * 0.75 + 30, x * 0.5 + 60], -1),
        'grad-anti': np.stack([x, 255 - x, (x + y) / 2], -1),
        'grad-2d': np.stack([x, y, 128 + 0 * x], -1),
        'radial': np.stack([np.hypot(x - 128, y - 128) * 1.4] * 3, -1) * [1, 0.8, 0.5],
        'noise': rng.integers(0, 256, (n, n, 3)),
        'noise-low': np.stack([x, y, 255 - x], -1) * 0.8 + rng.integers(0, 24, (n, n, 3)),
    }
    for name, rgb in imgs.items():
        rgb = np.clip(np.rint(rgb), 0, 255).astype(np.uint8)
        a = np.full((n, n, 1), 255, np.uint8)
        write_pam(os.path.join(out, name + '.pam'), np.concatenate([rgb, a], -1))
    return len(imgs)


def synth_a5(out):
    n = 256
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    rng = np.random.default_rng(2)
    rgb = np.stack([x, y, 255 - (x + y) / 2], -1)
    alphas = {
        'a-ramp': x,
        'a-radial': 255 - np.clip(np.hypot(x - 128, y - 128) * 2, 0, 255),
        'a-noise': rng.integers(0, 256, (n, n)),
        'a-cutout': np.where(np.hypot(x - 128, y - 128) < 90, 255, 0) + 0 * x,
        'a-soft-edge': np.clip((100 - np.hypot(x - 128, y - 128)) * 8 + 128, 0, 255),
        'a-stripes': 128 + 127 * np.sin(x / 5.0),
    }
    for name, a in alphas.items():
        rgba = np.concatenate([rgb, a[..., None]], -1)
        write_pam(os.path.join(out, name + '.pam'), np.clip(np.rint(rgba), 0, 255).astype(np.uint8))
    return len(alphas)


def pngs(dirs):
    """(name, RGBA array) of every PNG with alpha below 255 somewhere in the directories"""
    for top in dirs:
        for root, _, files in sorted(os.walk(top)):
            for f in sorted(files):
                if not f.lower().endswith('.png'):
                    continue
                path = os.path.join(root, f)
                try:
                    im = Image.open(path)
                    im.load()
                except Exception:
                    continue
                w, h = im.size
                if w % 4 or h % 4 or min(w, h) < 16 or max(w, h) > 1024:
                    continue
                if im.mode not in ('RGBA', 'LA', 'P') and 'transparency' not in im.info:
                    continue
                rgba = np.asarray(im.convert('RGBA'))
                if rgba[..., 3].min() < 255:
                    yield os.path.relpath(path, os.path.dirname(top)), rgba


def main():
    args = sys.argv[1:]
    png_dirs = []
    while '--png' in args:
        i = args.index('--png')
        png_dirs.append(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    out, debs = args[0], args[1:]
    for d in ('oa', 'oa-alpha', 'oa-a5', 'png-a5', 'synth', 'synth-a5'):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    opaque, alpha, a5 = [], [], []  # names only: a second pass writes the chosen ones
    for name, rgba in textures(debs):
        a = rgba[..., 3]
        if a.min() == 255:
            opaque.append(name)
            continue
        a5.append(name)
        if (a <= 127).any() and (a > 127).any():
            alpha.append(name)
    chosen = {n: ['oa'] for n in spread(opaque, N_OPAQUE)}
    for n in spread(alpha, N_ALPHA):
        chosen.setdefault(n, []).append('oa-alpha')
    for n in spread(a5, N_A5):
        chosen.setdefault(n, []).append('oa-a5')
    for name, rgba in textures(debs):
        for d in chosen.get(name, []):
            base = name[len('textures/'):].rsplit('.', 1)[0].replace('/', '_')
            write_pam(os.path.join(out, d, base + '.pam'), rgba)
    npng = 0
    if png_dirs:
        names = [n for n, _ in pngs(png_dirs)]
        pick = set(spread(names, N_PNG))
        for name, rgba in pngs(png_dirs):
            if name in pick:
                base = name.rsplit('.', 1)[0].replace('/', '_').replace(' ', '_')
                write_pam(os.path.join(out, 'png-a5', base + '.pam'), rgba)
                npng += 1
    print(f'{min(len(opaque), N_OPAQUE)} opaque of {len(opaque)}, {min(len(alpha), N_ALPHA)} '
          f'alpha-tested of {len(alpha)}, {min(len(a5), N_A5)} with alpha of {len(a5)}, '
          f'{npng} PNGs with alpha, {synth(os.path.join(out, "synth"))} + '
          f'{synth_a5(os.path.join(out, "synth-a5"))} synthetic in {out}')


main()
