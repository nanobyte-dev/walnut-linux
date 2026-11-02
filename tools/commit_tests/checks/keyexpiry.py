"""keyexpiry: the signing subkey of a keyring in the tree is not close to its expiry. A warning from 120
days before, an error from 30 days before, until the key is extended (docs/PACKAGING.md, "The signing
subkey"). WALNUT_TODAY=YYYY-MM-DD replaces today's date (the test uses it)."""
import os
import subprocess
import tempfile
import time

NAME = "keyexpiry"
TARGETS = ("apt-repo/keyrings/*.gpg", "packages/walnut/system/ps3-system/files/apt/*.gpg")
SHARED_INPUTS = ()
WARN_DAYS = 120
ERROR_DAYS = 30
DAY = 86400


def today():
    v = os.environ.get("WALNUT_TODAY")
    return time.mktime(time.strptime(v, "%Y-%m-%d")) if v else time.time()


def expiries(data):
    """the expiry times (epoch seconds) of the signing-capable subkeys in a keyring; 0 means none"""
    with tempfile.TemporaryDirectory() as home:
        r = subprocess.run(["gpg", "--homedir", home, "--batch", "--show-keys", "--with-colons"],
                           input=data, capture_output=True, timeout=60)
    if r.returncode:
        raise RuntimeError("gpg --show-keys: " + r.stderr.decode("utf-8", "replace").strip())
    out = []
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        f = line.split(":")
        if f[0] == "sub" and len(f) > 11 and "s" in f[11] and f[6]:
            out.append(int(f[6]))
    return out


def run(ctx, files):
    if not ctx.tool("gpg", "gpg", "install gnupg"):
        return
    now = today()
    for c in files:
        if c.status == "D":
            continue
        b = ctx.read_bytes(c.path)
        if not b:
            continue
        for exp in expiries(b):
            left = (exp - now) / DAY
            when = time.strftime("%Y-%m-%d", time.localtime(exp))
            if left < ERROR_DAYS:
                ctx.error(c.path, "the signing subkey expires %s (%d days): extend it (docs/PACKAGING.md)" % (when, left))
            elif left < WARN_DAYS:
                ctx.warn(c.path, "the signing subkey expires %s (%d days): extend it soon (docs/PACKAGING.md)" % (when, left))
