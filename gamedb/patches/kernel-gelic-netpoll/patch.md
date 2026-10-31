+++
component = "kernel"
patches = ["patches/local/0006-gelic-poll-controller-passes-card.patch"]
aliases = ["local/0006", "kernel 0006"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# gelic: netpoll passes the card to the interrupt handler

`gelic_net_poll_controller()`, the netpoll path netconsole uses, called `gelic_card_interrupt()` with the
`net_device` as its cookie. The handler was registered with the card as its cookie (`request_irq`) and
treats the argument as a `struct gelic_card`. The one-line fix passes the card.

## Investigation

- **What crashed:** oopses and hangs while netconsole was on.
- **Why:** the handler read a `net_device` as a `gelic_card`, so it worked on the wrong structure every
  time netpoll ran it.
- **How it was found:** 2026-10-14, while bringing up ps3rsx with netconsole as the place where the
  driver's wedge dumps go ([DRIVER.md](../../../docs/DRIVER.md#submission-submitc-execc)); the fix went in
  with commit f4084fc and is one of the Phase 1 kernel patches.
- **Minimal repro:** None yet: proposed: on the PS3 without the patch, netconsole to a listener on the
  LAN and a burst of kernel messages (for example `echo t > /proc/sysrq-trigger`), which makes netpoll
  call the poll controller.

## Validation

Not yet validated on the PS3.
