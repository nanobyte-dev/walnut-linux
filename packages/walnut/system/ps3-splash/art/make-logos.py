#!/usr/bin/python3
"""make-logos.py: writes the Walnut Linux logo SVGs (after the user's reference design): a tilted
single-colour walnut whose seam and wrinkles are cut out (an SVG mask, so any background shows
through), a soft shadow, and the WALNUT / LINUX wordmark. walnut-logo-light.svg (on white),
walnut-logo-dark.svg (on the boot screen's blue), walnut-mark.svg (the nut alone: kernel logo)."""

def nut(color, mid):
    return f'''<defs><mask id="cut{mid}" maskUnits="userSpaceOnUse" x="-200" y="-200" width="400" height="400">
  <ellipse cx="0" cy="0" rx="82" ry="94" fill="white"/>
  <g stroke="black" fill="none" stroke-linecap="round" stroke-linejoin="round" stroke-width="4">
    <path d="M3 -100 C -8 -56, 12 -18, -2 20 C -10 50, 6 76, -2 100" stroke-width="8"/>
    <path d="M-30 -74 C -40 -66, -50 -62, -56 -50 C -60 -42, -66 -40, -68 -30"/>
    <path d="M-72 -6 C -66 6, -72 16, -66 28 C -62 36, -66 44, -60 52"/>
    <path d="M-50 70 C -42 76, -34 74, -26 80"/>
    <path d="M-40 -34 C -44 -24, -38 -16, -42 -6"/>
    <path d="M-36 22 C -40 32, -34 40, -38 50"/>
    <path d="M30 -78 C 40 -72, 48 -64, 56 -54 C 62 -46, 66 -40, 68 -30"/>
    <path d="M72 -2 C 66 10, 72 20, 66 32 C 62 40, 66 48, 58 58"/>
    <path d="M48 74 C 40 80, 32 78, 22 84"/>
    <path d="M38 -30 C 42 -20, 36 -12, 40 -2"/>
    <path d="M36 26 C 40 36, 34 44, 38 54"/>
  </g></mask></defs>
  <ellipse cx="0" cy="0" rx="82" ry="94" fill="{color}" mask="url(#cut{mid})" transform="rotate(40)"/>'''

def logo(nut_color, text_color, sub_color, shadow, bg=None, w=420, h=420):
    bgr = f'<rect width="{w}" height="{h}" fill="{bg}"/>' if bg else ''
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">{bgr}
<ellipse cx="{w/2}" cy="238" rx="100" ry="9" fill="{shadow}"/>
<g transform="translate({w/2},126)">{nut(nut_color, "logo")}</g>
<text x="{w/2}" y="318" text-anchor="middle" font-family="DejaVu Sans" font-weight="bold" font-size="66" letter-spacing="2" fill="{text_color}">WALNUT</text>
<text x="{w/2}" y="358" text-anchor="middle" font-family="DejaVu Sans" font-weight="bold" font-size="24" letter-spacing="7" fill="{sub_color}">LINUX</text>
</svg>
'''

open('walnut-logo-light.svg', 'w').write(logo('#b5763a', '#b5763a', '#9a6431', '#ececec', bg='#ffffff'))
open('walnut-logo-dark.svg', 'w').write(logo('#d49a5c', '#e3ae73', '#c9a87c', '#0e2238'))
open('walnut-mark.svg', 'w').write(
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-110 -110 220 220" width="220" height="220">{nut("#d49a5c", "mark")}</svg>\n')
