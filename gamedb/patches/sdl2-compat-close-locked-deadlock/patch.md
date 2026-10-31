+++
component = "sdl2-compat"
patches = ["patches/0001-audio-don-t-deadlock-when-the-app-closes-the-device-.patch"]
aliases = ["sdl2-compat 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["sdl2-compat-close-locked"]
batches = []
+++
# sdl2-compat: no deadlock when the app closes the audio device while holding its lock

`SDL_LockAudioDevice()` locks the device's SDL3 audio stream, while SDL3's audio thread takes the physical
device's lock and then the stream's. `SDL_CloseAudioDevice()` wants the device lock. sdl2-compat now remembers
which thread holds the device lock; when that thread closes the device, the callback is taken away from the
stream first (so the app's callback never runs again, as with SDL2, which paused the device under the lock),
then its locks are released and the device closed. A close from any other thread is unchanged.

## Investigation

- **What crashed:** Armagetron Advanced 0.2.9 hung forever when the player quit.
- **Why:** a lock-order inversion. The app thread holds the stream lock and waits for the device lock; the
  audio thread holds the device lock and waits for the stream lock. SDL 1.2 apps through sdl12-compat do
  exactly this: Armagetron's `se_SoundExit()` holds `SDL_LockAudio()` across `Mix_FadeOutMusic()`,
  `SDL_Delay(100)` and `Mix_CloseAudio()`. SDL 1.2 allowed it, because a paused 1.2 device thread never took
  the lock.
- **How it was found:** Armagetron's quit; the played session of 2026-10-25 then quit cleanly with the fix
  ([Armagetron Advanced](../../games/armagetronad/technical.md)).
- **Minimal repro:** `packages/patched/graphics/sdl2-compat/tests/close-locked-test.sh` (host only, run it with
  `tools/run-tests.sh --only sdl2-compat-close-locked`). It builds sdl2-compat 2.32.74 stock and with this patch,
  then runs a small program (dlopen, no SDL headers) with the dummy audio driver: open the device, let the audio
  thread run, `SDL_LockAudioDevice()`, then `SDL_CloseAudioDevice()`; once through SDL2's API and once through
  SDL 1.2's (the host's sdl12-compat on our libSDL2). The stock build hangs every time (killed by the timeout) in
  both; the patched build closes at once. About 35 s cold (two builds), 11 s warm (the two hangs' timeouts).

## Validation

Not yet validated on the PS3.
