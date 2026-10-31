"""palette: Walnut's apps name every colour in their palette module (walnut_palette.py), so no hex colour
is written anywhere else in packages/walnut/apps/ (images and the tests aside)."""
import re

NAME = "palette"
TARGETS = ("packages/walnut/apps/**",)
SHARED_INPUTS = ()
EXEMPT = ("**/*_palette.py", "packages/walnut/apps/*/tests/**", "**/*.svg", "**/*.png", "**/*.jpg", "**/*.ico", "**/*.xpm")
# the app's own test (test_walnut_games.py's Palette) uses the same pattern
HEX_RE = re.compile(r"#[0-9a-fA-F]{8}\b(?=[\"'\s;,)])|#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b(?=[\"';,)])")


def run(ctx, files):
    import core
    for c in files:
        if c.status == "D" or core.matches(c.path, EXEMPT):
            continue
        b = ctx.read_bytes(c.path)
        if b is None or b"\0" in b[:8192]:
            continue
        for n, line in enumerate(b.decode("utf-8", "replace").splitlines(), 1):
            for m in HEX_RE.finditer(line):
                ctx.error(c.path, "line %d: the colour %s written in place: name it in the app's *_palette.py" % (n, m[0]))
