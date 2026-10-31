+++
component = "kernel"
patches = ["patches/local/0044-ps3-os-area-no-flash-writes.patch"]
aliases = ["local/0044", "kernel 0044"]
category = "stability"
switch = "ps3_os_area.write"
default = "0 (no OS area writes)"
forwarded = "no (project policy: Linux does not write the PS3's flash; docs/FLASH-SYSCON.md)"
tests = []
batches = []
+++

# os-area: never write the OS area unless ps3_os_area.write=1

Setting the PS3's RTC from Linux only changes the kernel's offset to lv1's read-only clock, but
`ps3_os_area_set_rtc_diff()` then queued a work item that wrote the OS area database back to flash
through ps3flash. With `CONFIG_RTC_SYSTOHC` and an NTP client that happens minutes after the first sync
and again every 11 minutes. With the patch, `os_area_flash_write()` returns -EPERM before it takes the
flash ops unless `ps3_os_area.write=1` is on the command line; the work item still updates
`linux,rtc_diff` in the device tree (memory only), and reading is unchanged. RTC changes last until the
next reboot, and NTP corrects the clock after it.

## Investigation

- **What crashed:** nothing broke. dmesg had `os_area_queue_work_handler: Could not update FLASH ROM`
  after NTP synced, which showed the kernel trying to write the PS3's flash.
- **Why:** on a NOR console the flash device is VFLASH on the internal disk, and the OS area's first
  256 KiB block holds the OS area header, the parameters (the GameOS/OtherOS boot flag) and the database,
  followed by the boot loader. ps3flash writes in 256 KiB blocks, so a 1 KiB database update rewrites
  the header, the boot flag and the start of Petitboot as well. It failed only because `PS3_FLASH` is
  compiled out; any build with it would have rewritten that block every 11 minutes, against the
  project's rule that Linux never writes the PS3's flash.
- **How it was found:** the warning line, traced to the RTC sync on 2026-10-18 (commit 5603d77, PLAN
  2.5, [CONSOLE.md](../../../docs/CONSOLE.md#the-kernels-os-area-write)).
- **Minimal repro:** the passive check: boot a kernel with the patch, let NTP sync and watch dmesg for
  any FLASH ROM update attempt. It passed on kernel .472 (no write attempt in a 13 h boot, 2026-10-20).

## Validation

Not yet validated on the PS3.
