# Mesa: 8-bit vertex attributes to float through a table (translate_generic)

Status: draft, not sent. Patch: `mesa-0001-translate-8bit-lut.patch`, cut against Mesa 26.2.4
(`src/gallium/auxiliary/translate/translate_generic.c`). Not rebased on mesa main (could not fetch
it here); the file changes rarely, but run `git am --3way` on a fresh clone and fix hunks by hand.

## Generality (checked)

Our local patch is `mesa/patches/0033` and is named after nv30 (`NV30_TRANSLATE_LUT`), but the code is
generic: it lives in `gallium/auxiliary/translate/translate_generic.c` and applies to every driver
that reaches translate_generic: u_vbuf (r300, nv30/nv50 families, svga, and any driver that has
vertex formats it cannot fetch) and the draw module. The upstream form therefore drops the nv30
environment variable and the PS3 wording and is framed as a general CPU-translate optimisation.
The output is bit-identical to the old path by construction (the table is filled with the format's
own `unpack_rgba`), so there is no switch. llvmpipe fetches SNORM8 itself and does not go through
u_vbuf, so it will not show a change. Our numbers are from nv30 only.

nouveau's nv30 driver is lightly maintained, but this patch does not touch it. Review will come from
whoever owns gallium auxiliary code and u_vbuf (see `scripts/get_reviewers.pl` in the Mesa tree).

What the upstream form changes against the tested patch: only the removal of the env switch
(`debug_get_option_translate_lut`) and of the comments about the PS3. The rest of the code is what ran
on the PS3. The edited file was not compiled in this draft (no Mesa build here); build it before sending.

## Merge request (gitlab.freedesktop.org/mesa/mesa)

Title: translate: convert 8-bit vertex attributes to float through a table

Description:

    translate_generic converts each attribute of each vertex with the format's unpack function and
    an emit function. For plain 8-bit formats going to 32-bit floats this is slow on CPUs where the
    integer to float conversion is expensive (on the Cell PPE it goes through memory, one
    load-hit-store stall per component). Drivers that have to translate such attributes on the CPU
    pay for it on every draw: nv30 has no signed 8-bit vertex type, so u_vbuf converts SNORM8 normals
    to floats on every draw.

    This builds a 256-entry table per channel when the translate key is created, filled with the
    bits that the format's own unpack function returns, and uses it in generic_run_one() for plain
    8-bit array formats with identity swizzle (UNORM, SNORM, USCALED, SSCALED, 1 to 4 channels)
    going to R32..._FLOAT with the same channel count. Everything else takes the old path.

    Correctness: the output is bit-identical to the old path. Checked on an x86 host by building
    translate_generic with the generated format tables and running all 16 formats through run,
    run_elts, run_elts16 and run_elts8 (index clamping, two buffers, attributes mixed with copied
    ones), comparing against the old code. The table path was taken for all 16 formats.

    Performance (PlayStation 3, Cell PPE, nv30 driver, Mesa 26.2.4, same boot, interleaved runs,
    desktop stopped, webcam checked for a correct picture): Sauerbraten demo steady 25.9 -> 39.6 fps
    (+53%), 1% low 18.3 -> 26.9; a profile before the change had 45% of the process CPU time in this
    loop. QuakeSpasm (6% in the loop) did not change in fps (91.0 vs 90.9), CPU time about -3%.
    No change expected on out-of-order x86 beyond a small gain.

    Also fixes a leak on the failure path of translate_generic_create() (FREE(tg) without the
    tables built for earlier elements): generic_release() is used there now.

Testing notes to add: if there is a place for a unit test (src/gallium/auxiliary/ has few), offer the
host test that compares the two paths. The harness we used is not kept in this repository; recreate it
before offering (about 60 lines: build the key, run both paths, memcmp).

## Submission steps

1. Create a gitlab.freedesktop.org account, fork mesa/mesa, clone a current main.
2. `git am --3way docs/upstream/mesa-0001-translate-8bit-lut.patch`; build gallium with a driver that
   uses u_vbuf (e.g. `-Dgallium-drivers=llvmpipe,softpipe`) to compile-check; add the unit test if
   you write one.
3. Push the branch to your fork and open the MR with the text above. Mesa wants a Signed-off-by only
   in some areas; adding one is harmless (the kernel patches already have it).
4. Expect questions about why not do it in u_vbuf, or via a SIMD path; the answer is that the table
   works for every translate_generic user and is bit-identical.
