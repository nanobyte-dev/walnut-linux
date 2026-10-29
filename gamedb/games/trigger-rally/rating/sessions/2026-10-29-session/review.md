# REVIEW.md for ps3-bench-20261029T221408Z-session-trigger-rally-base: answer every line (rules 2.0); a "?" left is unanswered and import-run.py refuses it.
# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. "pc": the PC reference
# shows it too (upstream's); "ps3": PS3/Walnut only. "where": the screen or the moment, short.
verdict: wrong: 5 shots of three Trigger Cup stages (warmup, operation, old): the terrain, the HUD, the minimap and the co-driver signs are textured, but the car is black, the buildings and rocks are flat untextured grey and the sky is a grey fog gradient (the player's note: no textures, grayscale skybox, black car); dmesg has a stream of ps3rsx 'out of VRAM' failures for 1024x1024 and smaller textures. The smoke run on the desert stage (2026-10-28) was fully textured. No PC reference
# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).
# Hint, measured: no black bars (5 lit shots)
stretched: no
# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.
text_px: 18: the MPH label under the speed (HUD, bottom right)
# A mouse pointer over the game. Measured: visible over the game in 0 of 5 shots. Confirm or override: visible or hidden.
pointer: hidden
# Each defect category: no, or pc: where, or ps3: where (one line per place is fine).
# garbage textures or geometry (-60)
garbage: no
# a missing element (HUD, models, shadows, an effect) (-25)
missing: ps3: textures on the car (drawn black), the buildings and rocks (flat grey) and the sky (a grey gradient), every stage of the session
# wrong colours or blending (-25)
colours: no
# flicker, z-fighting or one-frame glitches (-15)
flicker: no
# lighting or shading differs (-10)
lighting: no
# softer or less detailed, from Walnut's own defaults (-5)
softer: no
# minor cosmetic: a small visual quirk that doesn't affect play or readability (e.g. a semitransparent cursor, a one-pixel seam, a slightly wrong shade in one UI element) (-5)
minor: no
# Anything broken on screen or in the notes (a menu, a mode, saves, the pad): none, or one line each
# broken: <item> <pc|ps3>: <what>; items: pad_play, pad_setup, pad_menus, pad_hotplug, pad_players, no_keyboard, keyboard_mouse, menus_modes, saves_load, online
broken: pad_play pc: the DualShock drives nothing (the game's default configuration has both joystick blocks enable="no")
broken: pad_menus pc: the menus answer only the mouse (upstream 0.6.6.1)
