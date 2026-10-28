+++
id = "settings-menu-string-lower"
games = ["luanti"]
driver_patches = ["luajit/0002"]
game_patches = ["luanti/0008", "luanti/0009"]
sessions = ["2026-10-24-session"]
batches = ["2026-10-28-luanti-view190"]
+++

# Luanti: the Settings menu and string.lower on ppc64 (2026-10-28)

**Symptom.** The played session of 2026-10-24 noted "Options menu is broken". Every click on the main
menu's gear, and on Settings in the pause menu, logged to `~/.minetest/debug.txt`:

    ModError: Runtime error from mod '' in callback open_settings():
    /usr/share/luanti/builtin/common/settings/dlg_settings.lua:139:
    attempt to index field 'controls_touchscreen' (a nil value)

**Narrowing it down.** `builtin/settingtypes.txt` was installed and complete. The parser
(`settingtypes.lua`) and the page builder (`load_settingtypes` in `dlg_settings.lua`), run on the PS3
through the system's libluajit from a small ctypes harness with stubs for `core`, gave 643 entries and 74
categories, Touchscreen among them, but the page ids came out as `Controls_Touchscreencontrols_touchscreen`:
the original string followed by its lower-case copy. Then by itself:

    print(("Hello World"):lower())             --> 2hello world (after print(2, ...))
    print(("Controls" .. "Touchscreen"):upper()) --> ControlsTouchscreenCONTROLSTOUCHSCREEN
    print(("ab"):reverse())                     --> the same: the previous contents first

with the JIT off (`jit.status()` is false on ppc64): the interpreter's fast functions.

**Cause.** In `vm_ppc.dasc`, `ffstring_op` (string.lower, upper, reverse) resets LuaJIT's global
temporary buffer, `w = b`, with `lwz`/`stw`. The buffer's `w`, `e` and `b` are `char *`, 64 bits on
ppc64. On big-endian the 32-bit load reads the pointer's high word (0) and the store writes 0 over w's
high word, so `w` keeps pointing past whatever the buffer held, and every result starts with it. On
little-endian ppc64 the same instructions touch the low words, which works while the buffer sits below
4 GB, which is why nobody saw it upstream.

**Fix.** luajit 0002 (packages/patched/libs/luajit/): the pointer-sized `lp`/`stp` macros (`ld`/`std` on
P64). With the dev build installed (2.1.0+openresty20251030-1+ps3.931~dev), the same harness prints the
right strings and `controls_touchscreen` exists; a smoke run of the main menu (`run.sh smoke luanti menu`,
the gear clicked through XTEST) shows the Settings dialog with all its pages, and no ERROR in the log.
The bug affected every Lua program on Walnut's LuaJIT, not only Luanti.

**And the defaults.** The same session started in a 1024x600 window (luanti 0008: fullscreen by default)
and ran on Luanti's default viewing range of 190 nodes. One bench run each in the same boot: at 190 the
flight ran at 15 fps steady with 56 s of frames over 100 ms in 135 s; at 60, the recipe's value, at 46
fps with 2.3 s (gamedb:batch/2026-10-28-luanti-view190). luanti 0009 makes 60 the default.
