# REVIEW.md for ps3-bench-20261029T200808Z-session-luanti-base: answer every line (rules 2.0); a "?" left is unanswered and import-run.py refuses it.
# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. "pc": the PC reference
# shows it too (upstream's); "ps3": PS3/Walnut only. "where": the screen or the moment, short.
verdict: ok, voxel world, sky, water, trees, inventory and hotbar drawn right at 1920x1080 fullscreen; no PC reference shot for this session
# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).
# Hint, measured: no black bars (11 lit shots)
stretched: no
# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.
text_px: 12: hotbar item count
# A mouse pointer over the game. Measured: visible over the game in 4 of 11 shots. Confirm or override: visible or hidden.
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
broken: none
