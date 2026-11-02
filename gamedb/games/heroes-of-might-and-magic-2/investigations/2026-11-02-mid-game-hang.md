+++
id = "mid-game-hang"
games = ["heroes-of-might-and-magic-2"]
driver_patches = []
game_patches = []
sessions = []
batches = []
+++

# Heroes II: the mid-game "hang" was a message box waiting for OKAY (2026-11-02)

## Symptom

On the PS3 (fheroes2 1.1.17+dfsg-1+ps3.1082, SDL2 through our sdl2-compat on SDL3 ps3.1090, Mesa
ps3.1219), the game stopped responding in the middle of a turn. The picture stayed the same for 3 s and
longer: the adventure map, the hero selected, a grey strip on the left of the map view. The process used
11-27% CPU and the GPU logged no error. A gdb backtrace was taken, then the game was killed with SIGABRT
for a core dump. The player said it had played fine until then.

## Evidence

The core (137 MB, kept privately with the backtrace, never in the tree) was read on the PC with Fedora's
gdb, which handles ppc64. The text pages are not in the core, so the libraries had to be the exact builds:
the shared libraries of the `fheroes2-deps` build chroot for the Debian ones, and ours from walnut-testing
(libsdl3-0 ps3.1090, libsdl2-2.0-0 ps3.1184, mesa-libgallium ps3.1219, libspeq0 0.923; sdl2-compat ps3.874 fits the stack as well, the frames used do not tell them apart). Each was checked by
disassembling the call before every return address in the stack: libsdl3 ps3.874 and Debian's own build
fail that check, ps3.1090 passes. The debug symbols came from `build/debs/fheroes2` and, for SDL3,
sdl2-compat and Mesa, from the CI build artifacts: the `testing-debug` suite holds only a kernel `-dbgsym`
package at the moment, not the `-dbgsym` packages of these builds.

The main thread:

```
SDL_SYS_DelayNS <- SDL_Delay (SDL3) <- SDL_Delay (sdl2-compat)
EventProcessing::EventEngine::sleep(1)                         engine/localevent.cpp:426
LocalEvent::HandleEvents(sleepAfterEventProcessing=true)        engine/localevent.cpp:1289
fheroes2::showMessage(buttons=Dialog::OK)                       fheroes2/gui/ui_dialog.cpp:222
fheroes2::showStandardTextMessage                               fheroes2/gui/ui_dialog.cpp:326
Interface::AdventureMap::EventHeroMovement                      fheroes2/gui/interface_events.cpp:191
Interface::ButtonsPanel::queueEventProcessing                   fheroes2/gui/interface_buttons.cpp:179
Interface::AdventureMap::HumanTurn                              fheroes2/game/game_startgame.cpp:1308
Interface::AdventureMap::StartGame / Game::StartGame / Game::mainGameLoop / main
```

So the game was in the modal message "Hero Movement Points: The hero's army cannot move anymore today. The
movement points will be refilled tomorrow after you end your turn." with one OKAY button. fheroes2 shows it
when the Hero Movement button (the horse, at 729,320 to 765,356 in game pixels) is clicked for a hero who
has a path but not enough movement points for its next step. The selected hero had 14 movement points
left and a path. The dialog loop sleeps 1 ms per pass, which is the SDL_Delay in the backtrace, and
re-renders the whole frame every 220 ms for the palette cycling, which is the CPU use.

The other threads are all idle: the SDL audio thread in `ALSA_WaitDevice` (its normal 5 ms wait), the
game's two worker threads (sound, music restart) waiting on empty queues, Mesa's three shader-cache
threads waiting for jobs, libspeq's two SPU workers in their futex wait, one OpenMP worker. No thread
holds or waits on a lock another one holds.

What the game had drawn: the 853x480 8-bit frame in `fheroes2::Display` and the 32-bit SDL surface it was
last converted into were dumped and decoded with the game's palette. The two are identical (0 pixels
differ) and show the map with the "Hero Movement Points" box in the middle (304,159 to 548,321) and its
OKAY button. The grey strip on the left is the part of the view past the map's west edge, which the
game fills with stone tiles; the hero stands at tile (5,10).

The rendering path below the game was working: no GL error was pending in Mesa's context (SDL does not
call glGetError in a release build, so one would have stayed there); the nouveau device counted 0
cancelled submissions; the context's fences stood at 5080 emitted, 5079 signalled; the DRI3 drawable had
sent 2057 presents and received 2056 completions (copy mode, two back buffers, no failed allocation),
the last completion 0.26 s before the dump. The SDL window was shown, fullscreen at 1920x1080, with
input and mouse focus; SDL's renderer was not marked hidden; SDL's event queue was empty.

Timeline, from the game's own timers (steady clock) and SDL's recycled event entries, in seconds
before the dump:

- 297: the game started (the event handler was created).
- about 203: the last left button release that SDL kept a trace of, on the map.
- 201.2: the adventure map loop ran for the last time (its 10 ms hero timer was last reset 201.19 s
  before, its 250 ms map animation timer never fired again). The dialog opened then.
- 68.4: one left click while the dialog was up, pressed at (452,364) and released at (471,363) in game
  pixels: on the map, over the village 40 pixels below the box, not on OKAY (about 380 to 470, 296 to
  318). A modal fheroes2 dialog ignores clicks outside its buttons.
- 68.2: the last input event of any kind (a mouse motion to 474,359). SDL received nothing after it: no
  motion, no button, no key. Clicks between 201 and 68 s before the dump leave no trace in the core
  (SDL reuses one queue entry for single events, and the game keeps only the last click's position).

The input came from a mouse (XInput device 7); no game controller was open.

## The player's account and the PC

The player: "I clicked a bit and the game didn't react". On the PC the same dialog behaved the same, but
the animations on the map (the mill) kept moving there, while on the PS3 they stood still.

The PC repeat (2026-11-02, our package built for amd64 in the `fheroes2-amd64` image, under Xvfb at
1920x1080 with the game at 853x480 as on the PS3, the player's data, "4 Dimensions"): the hero sent along
the road south until it stopped short of its path, then the horse button. The same box came up over the
same part of the map (the windmill on the left, the village below). Under it the windmill's sails stood
still; five screenshots 1 s apart differed only in 160 pixels of the lava field near the volcano, which
alternate (palette cycling). A click where the PS3's last click was (1017,819 on the screen) changed
nothing; a click on OKAY closed the box.

So the stopped animations are upstream's behaviour: a modal message stops the adventure map's loop, and
with it the sprite animations (the windmill's sails among them); only the palette-cycled colours (water,
lava) keep moving, because the dialog loop re-renders with a new palette every 220 ms. On the PS3 the
view at the hang had no lava or water in it (1 cycling pixel), so nothing could move there. Whether
cycling runs on the PS3 when such colours are in view is still to be seen (the session below records it).

## Reading

Nothing hung inside the game. It waited 201 s for OKAY in a message box that it had drawn and handed to
SDL, and that the GL stack kept presenting 4.5 times a second; the clicks that reached it were outside
the box's button, which a modal fheroes2 message ignores on any platform. A "picture unchanged for 3 s"
rule calls any open dialog in this game a hang when no cycling colours are in view.

Open: whether every click the player made reached the game. The core holds only the last one (below the
box, 128 screen pixels under OKAY). If the player aimed at OKAY and the click landed elsewhere, or never
arrived, that is a PS3-side input problem (a grab held by another X client, the focus elsewhere, the
pointer drawn away from where X has it). Nothing in the core points to one: the window had input and mouse
focus, and the last click arrived with sane coordinates.

## What would settle it

A played session with diagnostics alongside (scripts prepared, not yet run): SDL's event log in the
game's output (`SDL_EVENT_LOGGING=1`, an environment variable only); the X server's raw buttons and keys
and focus changes from `xinput test-xi2 --root`; every 2 s the X focus, the pointer position and the
window under it, jgmenu's state and the game's main thread; every 1 s a checksum of
four screen regions over the map. If the player reports clicks that did nothing again, the raw events
against SDL's log tell whether X got them and the game did not; the pointer position at each click tells
where they landed; the webcam shows where the cursor was drawn.

## Fix

None in fheroes2 so far: upstream's code is the same in 1.1.17, the newest release, and on master
(`EventHeroMovement` unchanged), and a search of its issue tracker found no report of it. Letting a click
outside the box close it would be a UI change of our own, so it is a proposal for the player, not a
patch.
