#!/usr/bin/python3
"""make-desktop-splash.py <dir>: the desktop's start picture, desktop-splash.svg.

The boot splash's end (theme/walnut.script with the bar full: the logo, and under it the bar
with the walnut over its last cell), padded so that hsetroot -center puts it where Plymouth
drew it (ps3-session). The SVG refers to the theme's images by name: debian/rules renders it
next to them.
"""
import sys

CW, CH = 22, 34             # cell (glyph) size: eq.png, dot.png, the brackets
CELLS = 32                  # cells between the brackets (walnut.script's width)
W = (CELLS + 2) * CW
NUT = 30                    # nut.png
LOGO = 360                  # walnut-logo.png
GAP = 30                    # between the logo and the bar (walnut.script)
Y = LOGO + GAP              # the bar's top


def img(href, x, y, w, h):
    return f'<image x="{x}" y="{y}" width="{w}" height="{h}" xlink:href="{href}"/>\n'


body = img('walnut-logo.png', (W - LOGO) // 2, 0, LOGO, LOGO)
body += img('lbracket.png', 0, Y, CW, CH) + img('rbracket.png', (CELLS + 1) * CW, Y, CW, CH)
for i in range(CELLS):
    body += img('eq.png' if i < CELLS - 1 else 'dot.png', (i + 1) * CW, Y, CW, CH)
cx = CELLS * CW + CW // 2
body += img('nut.png', cx - NUT // 2, Y + (CH - NUT) // 2, NUT, NUT)
# the logo is 60 px above centre in walnut.script: as much padding under the bar's end
open(f'{sys.argv[1]}/desktop-splash.svg', 'w').write(
    '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
    f'width="{W}" height="{LOGO + 120}">\n{body}</svg>\n')
