# REVIEW.md for ps3-bench-20261026T015608Z-session-yquake2-base: answer every line (rules 2.0); a "?" left is unanswered and import-run.py refuses it.
# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. "pc": the PC reference
# shows it too (upstream's); "ps3": PS3/Walnut only. "where": the screen or the moment, short.
verdict: minor: Quake II's demo level as it looks; the particles square; most shots show the key-binding menu (the player rebinding the shipped E/S/D/F movement to WASD)
# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).
# Hint, measured: no black bars (2 lit shots)
stretched: no
# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.
text_px: 8: the menus' and HUD's text (the key-binding list): the interface at scale 1 at 1080p
# A mouse pointer over the game. Measured: visible over the game in 0 of 8 shots. Confirm or override: visible or hidden.
pointer: hidden
# Each defect category: no, or pc: where, or ps3: where (one line per place is fine).
# garbage textures or geometry (-60)
garbage: no
# a missing element (HUD, models, shadows, an effect) (-25)
missing: ps3: round particles (blood, sparks, bullet trails) draw as squares: the RSX's Mesa driver lacks smooth points (the player; a Mesa fix in progress)
# wrong colours or blending (-25)
colours: no
# flicker, z-fighting or one-frame glitches (-15)
flicker: no
# lighting or shading differs (-10)
lighting: no
# softer or less detailed, from Walnut's own defaults (-5)
softer: no
# Anything broken on screen or in the notes (a menu, a mode, saves, the pad): none, or one line each
# broken: <item> <pc|ps3>: <what>; items: pad_play, pad_setup, pad_menus, pad_hotplug, pad_players, no_keyboard, keyboard_mouse, menus_modes, saves_load, online
broken: none
