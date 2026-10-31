"""licensed: tools/licensed.py check (the licensed declarations agree: every licensed directory declared,
no licensed name outside one, Sections, all-or-none) when anything it reads changed."""
import subprocess
import sys

NAME = "licensed"
TARGETS = ("packages/**", "gamedb/*/*/licensed/**", "gamedb/*/*/build.toml", "gamedb/*/*/packages/**",
           "tools/licensed-files.txt", "tools/licensed.py")
SHARED_INPUTS = ()
WHOLE = True


def run(ctx, files):
    r = subprocess.run([sys.executable, "-I", "tools/licensed.py", "check"], cwd=ctx.root, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        for ln in [x for x in (r.stderr + r.stdout).splitlines() if x.strip()][:100] or ["exit %d" % r.returncode]:
            ctx.error(None, "licensed.py check: " + ln)
