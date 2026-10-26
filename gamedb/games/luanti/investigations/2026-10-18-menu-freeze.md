+++
id = "menu-freeze"
games = ["luanti"]
driver_patches = []
game_patches = []
sessions = []
batches = []
+++

# Started from the Games menu, Luanti found no X (2026-10-18)

A start from the desktop's Games menu on the PS3 that went wrong. The bug was game
mode's, not Luanti's; its fix (ps3-desktop 0.456, commit ba80992) is described in
[docs/DESKTOP.md](../../../../docs/DESKTOP.md#the-menu-freeze-2026-10-18-fixed-in-0456).

## Symptom

On the PS3, Luanti started from the Games menu found no video device and quit. About 10 s after the
click, X stopped answering other clients; killing jgmenu freed it.

## Cause

Game mode freezes the desktop's programs (`ps3-desktop.slice`) when a game starts. The freeze had
caught jgmenu, which stays running between uses, while it held its keyboard and pointer grab, so X
waited on a frozen client and Luanti's connection to it got no answer.

## Fix

In ps3-desktop: the guard closes jgmenu before freezing (started again hidden on leave), and
`ps3-x-ready` defers the freeze while a desktop popup holds a grab or X doesn't answer, then checks again
once frozen. ps3-desktop 0.474, installed on the PS3, carries it; Luanti started from the menu again is
on the manual list in docs/DESKTOP.md "What has run on the PS3".
