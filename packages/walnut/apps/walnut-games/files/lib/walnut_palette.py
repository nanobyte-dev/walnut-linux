"""walnut_palette.py (walnut-games package): every colour Walnut Games draws with, by name (the website's
palette). walnut_games.py imports these; nothing else in the app writes a colour value.
Text is white or light gray; yellow is for links only (the user, 2026-10-31)."""

# surfaces
BG = "#14304d"              # the window
PANEL = "#0e2238"           # the side pane, Details, menus
BORDER = "#1d3e61"          # borders, plain buttons, the focused check box's row
ENTRY = "#091726"           # entry fields, list boxes, troughs
CARD_HOVER = "#12304f"      # a game card under the pointer
BUTTON_HOVER = "#26507c"    # a plain button or scrollbar under the pointer or focused

# text
TEXT = "#ececec"            # body text
TEXT_FOCUS = "#ffffff"      # the text of a focused or hovered check box
MUTED = "#b4bec9"           # secondary text (light gray)
DISABLED = "#6c7d90"        # a disabled button's text
ON_ACCENT = "#14304d"       # dark text on the accent's buttons and selections
ON_DARK = "#f4f4f4"         # light text on a dark medal pill

# the accent: links, the main button, focus rings, selections
ACCENT, ACCENT_DEEP, ACCENT_LIGHT = "#d49a5c", "#b5763a", "#e3ae73"
LINK, LINK_HOVER = ACCENT, ACCENT_LIGHT

MEDAL_COLORS = {"Platinum": "#e5e4e2", "Gold": "#d4af37", "Silver": "#c0c0c0", "Bronze": "#cd7f32",
                "Tin": "#9a9ea3", "Rust": "#8a4b32", "Scrap": "#5f5a55", "Untested": "#6f7a86"}
INSTALLED_BADGE = ACCENT    # the installed game's corner triangle on its card (a shape; its check mark is ON_ACCENT)
