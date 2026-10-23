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
| 7 | The first boot installed: the image checked, formatted, the system unpacked, the setup wizard up (webcam, build/bench-cam/firstboot-035621.jpg) | Works |
| 8 | The wizard's prompts came out as kernel log lines (`[ 620.15] walnut-firstboot[1344]: Enter the username...`), late: "Press Enter" seemed to do nothing, the next Enter was taken as an empty user name. Its stderr was journal+console, and the text fallback prompts on stderr | The service's stderr is tty1 (ps3-system) |
| 9 | Those prompts showed `\n` literally and ended in `::` | The text fallback prints the messages with `%b` and one colon |
| 10 | The wizard fell back to plain text: whiptail was only a Recommends of ps3-system, and the image installs no Recommends | ps3-system Depends on whiptail: the wizard's dialog boxes |
| 11 | Each prompt appeared only after it was answered, so "Passwords did not match": the answers went to the question before the one on screen. The prompts have no newline, and journald holds a line until it ends | Same fix as 8: the prompts go to the tty directly |
| 12 | After the wizard: autologin worked and X ran (glamor on the RSX), but the TV kept showing the text console; Ctrl+Alt+F1 didn't bring X | Cause: the video mode. Petitboot passes `video=ps3fb:mode:5` (1080p with underscan: a 1688x964 picture inside the 1920x1080 frame); the dev setup always forced `mode:133` (1080p full screen) and the public image dropped `video=`. Booted with `mode:133` (ps3-update-kboot, PS3_EXTRA_ARGS): the desktop shows. Fix in ps3rsx: open |
| 13 | SSH turned on in the wizard is useless: the server takes keys only and a fresh user has none | Open: the wizard asks for a key (or a URL), or passwords on the LAN |
| 14 | No curl or wget in the image | Open |
| 15 | Renaming the machine with hostnamectl leaves /etc/hosts behind: sudo warns "unable to resolve host" | Open: libnss-myhostname in the image (resolves the own name whatever /etc/hosts says) |
| 16 | No wallpaper, a black desktop: PCManFM 1.4 reads its desktop settings only from ~/.config (strace), never from /etc/xdg/ps3, and writes its black default there on the first start; the dev console only had the wallpaper from its old home | ps3-session puts ours into ~/.config when there is none (ps3-desktop); checked live: the wallpaper shows |
| 17 | The release kernel has no kexec (/sys/kernel/kexec_loaded is missing): kexec-tools in the image can't do anything | Note (a mode test went through Petitboot instead) |
| 18 | Sound broken (the user) | ALSA plays (speaker-test, the PS3 card, PCM 100%): what fails is being found out |

