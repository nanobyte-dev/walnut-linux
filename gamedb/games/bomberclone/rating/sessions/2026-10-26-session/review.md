# REVIEW.md for ps3-bench-20261026T032625Z-session-bomberclone-base: answer every line (rules 2.0); a "?" left is unanswered and import-run.py refuses it.
# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. "pc": the PC reference
# shows it too (upstream's); "ps3": PS3/Walnut only. "where": the screen or the moment, short.
verdict: ok: the arenas, bombs, explosions and the HUD as BomberClone looks, 4:3 with bars at the sides
# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).
# Hint, measured: pillarbox on 5 of 5 lit shots, the picture 1440x1080 on 1920x1080
stretched: no
# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.
text_px: 12: the HUD at the top (Player1, Bombs, Range, Speed)
# A mouse pointer over the game. Measured: visible over the game in 0 of 5 shots. Confirm or override: visible or hidden.
pointer: hidden
# Each defect category: no, or pc: where, or ps3: where (one line per place is fine).
# garbage textures or geometry (-60)
garbage: no
# a missing element (HUD, models, shadows, an effect) (-25)
missing: no
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
broken: pad_menus pc: the gamepad doesn't move in the menus (the player; BomberClone's menus read the keyboard only)
