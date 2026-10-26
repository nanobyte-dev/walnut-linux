# OpenRCT2: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": no hard no: the GL renderer needs GL 3.3 core (no GL2 path), but the software renderer through SDL works; contrib, `architecture-is-little-endian`; the BE port can reuse the closed AmigaOS PR #27114 (~1,700 endian lines in 33 files, applies to 0.5.6 with 2 conflicts) plus TD6/TD4 import, RLE zoom, ImageImporter, Ogg/FLAC fixes: 7-11 person-days; data: our rct2-data deb; measured GUI no-GL 282-300 MB RSS, headless 154 MB, trimmed estimate 210-230 MB. Effort L. Verdict: worth a build, staged (RAM decides).

## Investigations

- [2026-10-19 big-endian port estimate](investigations/2026-10-19-big-endian-port-estimate.md): the packed structs, streams, bit-fields, pixels, network and tests a big-endian port touches, the AmigaOS PR #27114 it can reuse and its gaps, RAM, speed, effort and risks (written before the port).
