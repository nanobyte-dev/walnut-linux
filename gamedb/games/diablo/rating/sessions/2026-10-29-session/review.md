# REVIEW.md for ps3-bench-20261029T213401Z-session-diablo-base: answer every line (rules 2.0); a "?" left is unanswered and import-run.py refuses it.
# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. "pc": the PC reference
# shows it too (upstream's); "ps3": PS3/Walnut only. "where": the screen or the moment, short.
verdict: ok, 21 shots of a 21.5 min session: Tristram by dusk then cathedral levels, inventory panel open, drawn as DevilutionX does (isometric tiles, wall transparency, belt, health and mana globes, flames); no PC reference, judged against what the game looks like
# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).
# Hint, measured: no black bars (21 lit shots)
stretched: no
# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.
text_px: 26: the panel buttons CHAR/QUESTS/MAP/MENU labels (inventory item text not in shots)
# A mouse pointer over the game. Measured: visible over the game in 16 of 21 shots. Confirm or override: visible or hidden.
pointer: visible
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
# minor cosmetic: a small visual quirk that doesn't affect play or readability (e.g. a semitransparent cursor, a one-pixel seam, a slightly wrong shade in one UI element) (-5)
minor: ps3: the mouse cursor draws semitransparent (the player's form note, 'very minor'); re-filed from colours 2026-10-30; X cursors are not in the xwd captures, so the shots can't show it
# Anything broken on screen or in the notes (a menu, a mode, saves, the pad): none, or one line each
# broken: <item> <pc|ps3>: <what>; items: pad_play, pad_setup, pad_menus, pad_hotplug, pad_players, no_keyboard, keyboard_mouse, menus_modes, saves_load, online
# The player corrected this 2026-10-30: the hero name prompt has a usable default the pad accepts, so nothing needs the keyboard (the 32 key presses in the input log were typing a name).
check: no_keyboard yes
broken: none
