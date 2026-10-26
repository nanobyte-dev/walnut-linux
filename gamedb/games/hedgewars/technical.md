# Hedgewars: technical notes

## Status

Parked. Our ppc64 engine ([packages/hedgewars](packages/hedgewars/): `hedgewars-engine` with FPU exceptions
masked at start, patch 0001) runs without the Qt frontend: [benchmark/lib/hwdrive.py](benchmark/lib/hwdrive.py)
starts it with `--internal --port N` and plays the frontend's IPC for a training mission. It runs on the PS3
with sound (562-650 underruns), but the mission ends by itself after 54-55 s with no input
([the investigation](investigations/2026-10-19-mission-ends-by-itself.md)): the ppc64 engine's physics differ.
The smoke is useful for what it shows (the engine starts, loads, draws, plays sound for 55 s), not for a 120 s
window.

- hwengine takes `--showfps` with no value: a stray argument is read as a demo file name.
- The first smoke (gamedb:batch/2026-10-19-ssdroot4-smoke) failed: the driver passed the mission as a demo file
  (`hwdrive: the engine never connected`, `Error opening file 1`, rc 1); fixed in hwdrive.py.

## Investigations

- [2026-10-19 the mission ends by itself](investigations/2026-10-19-mission-ends-by-itself.md).
