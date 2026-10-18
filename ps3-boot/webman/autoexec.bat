# webMAN MOD startup script (/dev_hdd0/autoexec.bat), draft 2026-10-18, not tried.
# Linux's boot files live on dev_hdd1 (FAT: Linux writes it on every kernel install, and Petitboot
# reads /kboot.conf there). Linux can't write dev_hdd0 (UFS2, read-only from Linux), so GameOS keeps
# the backup: at every GameOS start, mirror dev_hdd1 to /dev_hdd0/linux/boot, or put it back if
# GameOS cleared dev_hdd1. Merge into an existing autoexec.bat instead of replacing it.
# docs/GAMEOS-HDD.md "/boot on dev_hdd1 kept alive by webMAN".
if exist /dev_hdd1/kboot.conf
  md /dev_hdd0/linux
  md /dev_hdd0/linux/boot
  copy /dev_hdd1/linux/boot=/dev_hdd0/linux/boot
  copy /dev_hdd1/kboot.conf=/dev_hdd0/linux/boot/kboot.conf
else
  md /dev_hdd1/linux
  md /dev_hdd1/linux/boot
  copy /dev_hdd0/linux/boot=/dev_hdd1/linux/boot
  copy /dev_hdd0/linux/boot/kboot.conf=/dev_hdd1/kboot.conf
end if
