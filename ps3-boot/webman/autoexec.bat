# webMAN MOD startup script (/dev_hdd0/autoexec.bat) for the internal install, draft, not tried yet
# (docs/release/ALPHA.md M0 tests it). Merge into an existing autoexec.bat instead of replacing it.
# Linux's boot files and the image's saved block map live on dev_hdd1 (FAT: Petitboot reads
# /kboot.conf there, Linux writes it on kernel updates, the first boot saves /linux/walnut/boot.map).
# Linux can't write dev_hdd0 (UFS2, read-only from Linux), so GameOS keeps the copies:
# - at every GameOS start, dev_hdd1's set is mirrored to /dev_hdd0/linux/walnut-mirror;
# - if GameOS cleared dev_hdd1, the mirror is put back.
# /dev_hdd0/linux/walnut-good is the PC installer's known-good set (stamp.py install): this script
# never writes it, so a bad kernel mirrored here can't replace it. Restoring from it is a manual
# step (docs/GAMEOS-HDD.md "The internal install"). Whether webMAN's copy takes a directory is
# not verified yet (GAMEOS-HDD.md).
if exist /dev_hdd1/kboot.conf
  md /dev_hdd0/linux
  md /dev_hdd0/linux/walnut-mirror
  md /dev_hdd0/linux/walnut-mirror/boot
  md /dev_hdd0/linux/walnut-mirror/walnut
  copy /dev_hdd1/linux/boot=/dev_hdd0/linux/walnut-mirror/boot
  copy /dev_hdd1/linux/walnut=/dev_hdd0/linux/walnut-mirror/walnut
  copy /dev_hdd1/kboot.conf=/dev_hdd0/linux/walnut-mirror/kboot.conf
else
  md /dev_hdd1/linux
  md /dev_hdd1/linux/boot
  md /dev_hdd1/linux/walnut
  copy /dev_hdd0/linux/walnut-mirror/boot=/dev_hdd1/linux/boot
  copy /dev_hdd0/linux/walnut-mirror/walnut=/dev_hdd1/linux/walnut
  copy /dev_hdd0/linux/walnut-mirror/kboot.conf=/dev_hdd1/kboot.conf
end if
