#!/usr/bin/python3
"""make-frames.py <dir>: the boot splash's progress frames and the desktop's start picture as SVGs.

Plymouth's two-step plugin (the theme's ModuleName) shows progress-NNNN.png by boot progress
under the logo (watermark.png), all prerendered: no text, so Plymouth needs no font renderer.
Frame k is the bar [=====@......] with the walnut rolled k/(N-1) of the way along it, turned by
the distance it rolled (angle = distance / radius). desktop-splash.svg is the logo with the last
frame, padded so that hsetroot -center puts it where Plymouth drew them (ps3-session).
The SVGs refer to the theme's images by name: debian/rules copies those next to them and renders.
"""
import math
import sys

N = 100                     # frames
CW, CH = 22, 34             # cell (glyph) size
CELLS = 32                  # cells between the brackets
W = (CELLS + 2) * CW        # frame width: brackets and cells
NUT = 30                    # the walnut's size
X0 = CW + CW // 2           # the walnut's centre over the first cell ...
X1 = CELLS * CW + CW // 2   # ... and over the last one
LOGO = 360                  # watermark.png, from walnut-logo-dark.svg
GAP = 30                    # between the logo and the bar (also in walnut.plymouth's alignments)

out = sys.argv[1]


def svg(w, h, body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
            f'width="{w}" height="{h}">\n{body}</svg>\n')


def img(href, x, y, w, h, extra=''):
    return f'<image x="{x}" y="{y}" width="{w}" height="{h}" xlink:href="{href}"{extra}/>\n'


def bar(k, y=0):
    cx = X0 + (X1 - X0) * k / (N - 1)
    deg = math.degrees((cx - X0) / (NUT / 2))
    b = img('lbracket.svg', 0, y, CW, CH) + img('rbracket.svg', (CELLS + 1) * CW, y, CW, CH)
    for i in range(CELLS):
        centre = (i + 1) * CW + CW // 2
        b += img('eq.svg' if centre < cx else 'dot.svg', (i + 1) * CW, y, CW, CH)
    b += img('walnut-mark.svg', f'{cx - NUT / 2:.2f}', y + (CH - NUT) // 2, NUT, NUT,
             f' transform="rotate({deg:.2f} {cx:.2f} {y + CH / 2})"')
    return b


for k in range(N):
    open(f'{out}/progress-{k:04d}.svg', 'w').write(svg(W, CH, bar(k)))
open(f'{out}/desktop-splash.svg', 'w').write(svg(W, LOGO + 4 * GAP,
     img('watermark.png', (W - LOGO) // 2, 0, LOGO, LOGO) + bar(N - 1, LOGO + GAP)))
