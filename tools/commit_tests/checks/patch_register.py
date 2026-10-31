"""patch-register: every patch in a packages/** series has exactly one register entry in
gamedb/patches/<slug>/patch.md, series and folder agree, a series ends in a newline (tools/gamedb/patches.py,
the register's own checker). Runs when a series, a patch or the register changes. Until patches.py exists
the check says so and passes."""
import subprocess
import sys

NAME = "patch-register"
TARGETS = ("packages/**/series", "packages/**/*.patch", "gamedb/patches/**")
SHARED_INPUTS = ("tools/gamedb/patches.py",)
WHOLE = True
MODULE = "tools/gamedb/patches.py"


def run(ctx, files):
    if not ctx.exists(MODULE):
        ctx.notice(None, "%s isn't there yet (the register work): patches aren't matched to register entries" % MODULE)
        return
    r = subprocess.run([sys.executable, "-I", MODULE], cwd=ctx.root, capture_output=True, text=True, timeout=300)
    out = (r.stdout + r.stderr).splitlines()
    for ln in out:
        if ln.startswith("warning: "):
            ctx.warn(None, "patches.py: " + ln[9:])
        elif ln.startswith("error: "):
            ctx.error(None, "patches.py: " + ln[7:])
    if r.returncode != 0 and not any(ln.startswith("error: ") for ln in out):
        ctx.error(None, "patches.py failed (exit %d): %s" % (r.returncode, "\n".join(out[-20:])))
