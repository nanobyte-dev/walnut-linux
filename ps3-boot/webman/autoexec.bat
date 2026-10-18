# webMAN MOD startup script (/dev_hdd0/autoexec.bat), draft 2026-10-18, not tried.
# Puts Linux's boot files back on dev_hdd1 (GameOS's cache partition, where Petitboot finds
# /kboot.conf) if they are missing. The master copy in /dev_hdd0/linux/boot/ is a pinned rescue
# kernel that ps3-boot never purges; Linux itself keeps dev_hdd1 current on every kernel install.
# Merge into an existing autoexec.bat instead of replacing it. docs/GAMEOS-HDD.md.
if not exist /dev_hdd1/kboot.conf
  md /dev_hdd1/linux
  copy /dev_hdd0/linux/boot/vmlinux-rescue=/dev_hdd1/linux/vmlinux-rescue
  copy /dev_hdd0/linux/boot/initrd-rescue=/dev_hdd1/linux/initrd-rescue
  copy /dev_hdd0/linux/boot/kboot.conf=/dev_hdd1/kboot.conf
end if
