#!/usr/bin/python3 -I
"""Host test of ps3-gpu-recovered's check(): Tk, the clock, sysfs, the stamp and the X process
table are faked. gpu-recovered-test.py [PATH/TO/ps3-gpu-recovered] (default: this package's
files/libexec). Skipped (77) without tkinter."""
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
import types

sys.dont_write_bytecode = True     # no __pycache__ next to the tested file

pal = types.ModuleType("walnut_palette")
for k in ("BG", "TEXT", "ACCENT", "ON_ACCENT", "ACCENT_LIGHT", "BORDER", "BUTTON_HOVER", "TEXT_FOCUS"):
    setattr(pal, k, "#000000")
sys.modules["walnut_palette"] = pal
try:
    import tkinter  # noqa: F401
except ImportError:
    print("skip: no tkinter")
    sys.exit(77)
target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                            "..", "files", "libexec", "ps3-gpu-recovered")
loader = importlib.machinery.SourceFileLoader("dlg", target)
spec = importlib.util.spec_from_loader("dlg", loader)
dlg = importlib.util.module_from_spec(spec)
loader.exec_module(dlg)

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


class FakeX:
    """tries: Tk() fails this many times; dies: the X goes away before the window maps"""
    def __init__(self, tries=0, dies=False):
        self.tries, self.dies, self.shown_text, self.calls = tries, dies, None, 0


def setup(rec, fail, stamp, x, xstart_seq=None):
    d = tempfile.mkdtemp()
    os.makedirs(d + "/sys")
    for n, v in (("recoveries", rec), ("recovery_failed", fail)):
        with open(f"{d}/sys/{n}", "w") as f:
            f.write(f"{v}\n")
    dlg.SYSFS = d + "/sys"
    dlg.STAMP = d + "/stamp"
    dlg.PENDING = d + "/pending"
    if stamp is not None:
        with open(dlg.STAMP, "w") as f:
            f.write(f"{dlg.boot_id()} {stamp[0]} {stamp[1]}\n")
    clock = [1000.0]
    dlg.time.time = lambda: clock[0]
    dlg.time.monotonic = lambda: clock[0]
    dlg.time.sleep = lambda s: clock.__setitem__(0, clock[0] + s)
    dlg.user_x_env = lambda: {}
    seq = list(xstart_seq or [])
    dlg.newest_xorg_start = lambda: seq.pop(0) if len(seq) > 1 else (seq[0] if seq else 0)

    class Root:
        def __init__(self):
            x.calls += 1
            if x.calls <= x.tries:
                raise dlg.tk.TclError("no display")

        def wait_visibility(self, w):
            if x.dies:
                raise dlg.tk.TclError("X connection broken")

    def fake_dialog(text, root=None, shown=None):
        root = root or Root()
        x.shown_text = text
        if shown:
            root.wait_visibility(root)
            shown()
    dlg.tk.Tk = Root
    dlg.dialog = fake_dialog
    return clock


def stamp():
    try:
        with open(dlg.STAMP) as f:
            return f.read().split()[1:]
    except OSError:
        return None


# 1. recovered, X answers at the third try: the dialog shows, then the stamp
x = FakeX(tries=2)
setup(1, 0, None, x)
dlg.check(live=True)
check(x.shown_text == dlg.RECOVERED and stamp() == ["1", "0"], f"recovered: {x.shown_text!r} {stamp()}")
# 2. failed on the live event: the old X (started at 900) is skipped until a new one (1005)
x = FakeX()
clock = setup(1, 1, ["1", "0"], x, xstart_seq=[900, 900, 900, 1005])
dlg.check(live=True)
check(x.shown_text == dlg.FAILED and stamp() == ["1", "1"], f"failed: {x.shown_text!r} {stamp()}")
check(clock[0] >= 1003, f"failed: shown at {clock[0]}, before the new X")
# 3. X dies before the window maps: no stamp (the service restarts and shows it again)
x = FakeX(dies=True)
setup(1, 1, ["1", "0"], x, xstart_seq=[1005])
try:
    dlg.check(live=True)
    check(False, "dying X: no exception")
except dlg.tk.TclError:
    pass
check(stamp() == ["1", "0"], f"dying X: stamp moved to {stamp()}")
# 4. nothing new: no dialog, the stamp kept current
x = FakeX()
setup(1, 1, ["1", "1"], x)
dlg.check()
check(x.shown_text is None and stamp() == ["1", "1"], "nothing new")
# 5. at the service's start (not live) a failed one shows on any X that answers
x = FakeX(tries=1)
setup(0, 1, None, x, xstart_seq=[900])
dlg.check()
check(x.shown_text == dlg.FAILED and stamp() == ["0", "1"], f"start: {x.shown_text!r} {stamp()}")
# 6. no X at all for 5 minutes: exit 1, no stamp
x = FakeX(tries=10 ** 6)
setup(1, 0, None, x)
try:
    dlg.check(live=True)
    check(False, "no X: no exit")
except SystemExit as e:
    check(e.code == 1 and stamp() is None, f"no X: code {e.code}, stamp {stamp()}")
# 7. X dies under the live failed dialog; the restarted service (not live) still waits for an
# X newer than the live event, not the old one still dying
x = FakeX(dies=True)
setup(1, 1, ["1", "0"], x, xstart_seq=[1005])
try:
    dlg.check(live=True)
except dlg.tk.TclError:
    pass
x2 = FakeX()
x.dies = False
clock = setup(1, 1, ["1", "0"], x2, xstart_seq=[900, 900, 1010])
with open(dlg.PENDING, "w") as f:
    f.write(f"{dlg.boot_id()} 1 998.0\n")
dlg.check()
check(x2.shown_text == dlg.FAILED and stamp() == ["1", "1"] and clock[0] >= 1002,
      f"pending: {x2.shown_text!r} {stamp()} at {clock[0]}")
check(not os.path.exists(dlg.PENDING), "pending: left after showing")
print(f"gpu-recovered-test: {fails} failure(s)" if fails else "gpu-recovered-test: all passed")
sys.exit(1 if fails else 0)
