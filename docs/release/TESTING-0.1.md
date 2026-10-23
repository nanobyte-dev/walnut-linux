# Testing before 0.1.0

The first stable release waits for a round of testing on the PS3 with nightlies (the user,
2026-10-23). Feedback goes here as it comes in: what was seen, and what was done about it. When the
list is clear, the stable run makes 0.1.0 (docs/WALNUT-SERVER.md "Release images").

## Nightly nightly-20261022-2331-36f424d (internal install on the dev console, 2026-10-23)

Installed with the archive's `walnut-install internal --host 10.0.0.77`: 16 GiB image in 836 s
(19.6 MB/s), then the system (875 MiB), the boot files, kboot.conf, walnut-good and install.conf.
It replaced the console's `walnut`/`walnut-prev` entries (ssdtest.img's); their backup is
`build/hdd1-backup/20261023-032653/`.

| # | Seen | Done |
|---|---|---|
| 1 | The installer ends the image upload with "next, on Linux: ps3-imgmap map ...", the old test drive's next step | Only `stamp.py upload` on its own prints it now |
| 2 | The first boot's install is plain log lines: say what is going on, more visually, even in text mode | A Walnut banner (walnut blue, coloured spaces and ASCII only) with "Installing: step N of 5", a bar and the step in plain words, the step's messages below it; green when done, red with the reason on a failure |
| 3 | The block verify's progress scrolls a new line every 5 s | ps3-imgmap-boot rewrites one progress line in place (`\r`); any other message ends it first |
| 4 | A pending install.conf for one image blocks booting any other (`installs X, the kernel command line boots Y`) | Open |
| 5 | Two systems on one console both write the `walnut`/`walnut-prev` entries on kernel updates | Open |
| 6 | testing is older than stable (.708/.712 against .721): stable was built fresh, testing only rebuilds on pushes | Open (a full testing rebuild would level them) |
