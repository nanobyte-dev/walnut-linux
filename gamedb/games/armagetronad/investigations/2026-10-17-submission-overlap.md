+++
id = "submission-overlap"
games = ["armagetronad"]
driver_patches = ["mesa/0027"]
game_patches = []
sessions = []
batches = ["2026-10-17-mesa-246", "2026-10-17-mesa-262", "2026-10-17-mesa-262-repeats", "2026-10-17-mesa-246-control", "2026-10-17-mesa-282-armagetron", "2026-10-17-mesa-299"]
+++

# Armagetron: Mesa ps3.262's -13%, the lost CPU/GPU overlap (2026-10-17)

**Symptom.** Mesa ps3.262 (0019-0025, fewer words per draw) sped up every game of the suite by 11-24%,
except Armagetron: its playback went from 60 fps HUD median on ps3.246 (59 and 61 in the same boot,
gamedb:batch/2026-10-17-mesa-246-control) to 52.5 (-13%, gamedb:batch/2026-10-17-mesa-262), and 50 on
ps3.282.

**Evidence.** Not 0020's texture invalidation: `texinv` gave 52 (gamedb:batch/2026-10-17-mesa-262-repeats).
On ps3.282 (gamedb:batch/2026-10-17-mesa-282-armagetron) the switches told: with relocations back on, or a
fixed kick every 50 or 100 draws (`kick50`, `kick100`), it was 60.5 again. In the first profile (Mesa
ps3.223) "other kernel" was 21% of its CPU.

**Cause.** 0019 dropped the submissions the relocation limit used to force mid-frame. Armagetron's frames
are small enough that the whole frame then reached the GPU only at the swap: the CPU and the GPU no longer
overlapped.

**Fix.** Mesa 0027's adaptive submission (ps3.299) kicks the push buffer as the frame grows: 61.5
(gamedb:batch/2026-10-17-mesa-299). A fixed `kick100` was rejected, because it cost X-Moto 9%.
