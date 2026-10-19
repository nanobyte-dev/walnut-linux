#!/usr/bin/env python3
"""irqoff.py RUN_DIR...: the soft-masked time of a PERF=1 run, split by where it ended
(docs/HOTPATHS.md "Soft-masked time"). perf-post.sh writes it to perf-irqoff.txt.

The PS3 has no PMU driver, so perf samples with cpu-clock, an hrtimer on the decrementer. On ppc64 a
decrementer that fires while interrupts are soft-disabled (spin_lock_irqsave, the hash fault
handler, lv1 hcalls made under a lock) is only taken when they come back on, in
arch_local_irq_restore -> __replay_soft_interrupts, and that is where perf books the sample. The
replay itself is cheap: the samples are the time of the masked section before it, and the call
chain above arch_local_irq_restore says which section that was. This splits those samples (leaf
__replay_soft_interrupts or arch_local_irq_restore in perf.folded) by that site:

  hash fault (user)    back to user mode after an exception taken in user code: almost all hash
                       page faults (HPTE insertions through lv1, interrupts soft-masked for the
                       whole handler), plus device interrupts' handlers (hard-masked); the frame
                       under it is the user code that faulted
  hpte inval munmap    ps3_hpte_invalidate's unlock under munmap (one lv1 call per page)
  hpte inval reclaim   the same from kswapd or direct reclaim (pages unmapped to be freed)
  hpte inval other     the same anywhere else (fork, mprotect, a COW fault)
  hash preload (fault) the HPTE a Linux page fault inserts right away (update_mmu_cache)
  alsa pcm lock        snd_pcm's stream lock (snd_pcm_delay, the hw pointer update)
  context switch       the scheduler's switch, up to finish_task_switch
  syscall entry/exit   between a system call's entry or exit and interrupts on
  wakeup unlock        a wakeup's runqueue/waitqueue lock (futex, pipes, sockets, poll)
  other                anything else, by its last kernel frames

With the irq/ snapshots client.sh takes at the window's ends (PERF=1), also the interrupts and the
htab hcalls (kernel local/0046's /sys/module/ps3_htab/parameters/stats) a second over the window.
"""
import os
import re
import sys
from collections import Counter

REPLAY = re.compile(r"\.(__replay_soft_interrupts|arch_local_irq_restore)(_\[k\])?$")


def site(frames):
    """the masked section a replay sample stands for, from its folded frames"""
    k = [i for i, f in enumerate(frames) if f.endswith("_[k]")]
    if not k:
        return "other", ""
    kern = [(f[:-4] if f.endswith("_[k]") else f).lstrip(".") for f in frames[k[0]:]]
    s = ";".join(kern)
    user = frames[k[0] - 1] if k[0] > 0 else ""
    if "ps3_hpte_invalidate" in s:
        if "unmap" in s and ("munmap" in s or "vms_" in s or "unmap_vmas" in s):
            return "hpte inval munmap", ""
        if frames[0] == "kswapd0" or "shrink_" in s or "try_to_free_pages" in s:
            return "hpte inval reclaim", ""
        return "hpte inval other", ""
    if kern[0].startswith("interrupt_return_srr_user") and "system_call" not in s:
        return "hash fault (user)", user
    if "update_mmu_cache" in s:
        return "hash preload (fault)", ""
    if "snd_pcm" in s:
        return "alsa pcm lock", ""
    if "finish_task_switch" in s:
        return "context switch", ""
    if any(x in s for x in ("try_to_wake_up", "__wake_up_common", "wake_up_q")):
        return "wakeup unlock", ""
    if kern[0].startswith(("system_call", "syscall")) and len(kern) <= 6 and \
            any(x in s for x in ("system_call_exception", "syscall_exit", "interrupt_exit_user")):
        return "syscall entry/exit", ""
    return "other", ";".join(kern[-5:-2])


def folded(d):
    tot = game = 0
    by = Counter()
    users = Counter()
    other = Counter()
    for line in open(os.path.join(d, "perf.folded"), errors="replace"):
        s, _, n = line.rstrip("\n").rpartition(" ")
        if not s or not n.isdigit():
            continue
        n = int(n)
        frames = s.split(";")
        g = frames[0] not in ("Xorg", "kswapd0")
        tot += n
        game += n if g else 0
        if not REPLAY.search(frames[-1]):
            continue
        what, extra = site(frames)
        by[(what, g)] += n
        if what == "hash fault (user)" and g:
            users[extra] += n
        if what == "other" and g:
            other[extra] += n
    return tot, game, by, users, other


def counters(path):
    """/proc/interrupts as {name: total over CPUs}"""
    out = {}
    try:
        lines = open(path).read().splitlines()
    except OSError:
        return out
    for l in lines[1:]:
        f = l.split()
        if not f or not f[0].endswith(":"):
            continue
        nums = []
        for x in f[1:]:
            if x.isdigit():
                nums.append(int(x))
            else:
                break
        name = f[0][:-1]
        rest = f[1 + len(nums):]
        if name.isdigit() and rest:	# a device interrupt: its number and handler's name
            name = "%s %s" % (name, rest[-1])
        out[name] = sum(nums)
    return out


def htab(path):
    try:
        f = open(path).read().split()
    except OSError:
        return None
    return {f[i]: int(f[i + 1]) for i in range(0, len(f) - 1, 2) if f[i + 1].isdigit()}


def info_time(path):
    try:
        m = re.search(r"real=([0-9.]+)", open(path).read())
        return float(m.group(1)) if m else None
    except OSError:
        return None


def window(d):
    a, b = os.path.join(d, "irq", "start"), os.path.join(d, "irq", "end")
    t0, t1 = info_time(a + ".info"), info_time(b + ".info")
    if t0 is None or t1 is None or t1 <= t0:
        return None
    dt = t1 - t0
    i0, i1 = counters(a + ".interrupts"), counters(b + ".interrupts")
    irq = sorted(((k, (i1[k] - i0.get(k, 0)) / dt) for k in i1), key=lambda x: -x[1])
    h0, h1 = htab(a + ".htab"), htab(b + ".htab")
    hrate = None
    if h0 and h1:
        hrate = {k: (h1[k] - h0.get(k, 0)) / dt for k in h1}
    return dt, irq, hrate


def run(d):
    tot, game, by, users, other = folded(d)
    print("== %s: %d samples, %d of the game's threads" % (os.path.basename(d.rstrip("/")), tot, game))
    sites = sorted({w for w, _ in by}, key=lambda w: -(by[(w, True)] + by[(w, False)]))
    allg = sum(v for (w, g), v in by.items() if g)
    print("soft-masked (replay) samples: %.1f%% of the game's, %.1f%% of all" %
          (100.0 * allg / max(game, 1), 100.0 * sum(by.values()) / max(tot, 1)))
    print("  %-20s %8s %8s" % ("ended at", "game %", "all %"))
    for w in sites:
        print("  %-20s %8.2f %8.2f" % (w, 100.0 * by[(w, True)] / max(game, 1),
                                       100.0 * (by[(w, True)] + by[(w, False)]) / max(tot, 1)))
    if users:
        print("  hash fault (user), by the user frame that faulted (game %):")
        for u, n in users.most_common(6):
            print("    %6.2f  %s" % (100.0 * n / max(game, 1), u[:100]))
    if other:
        print("  other, by its last kernel frames (game %):")
        for u, n in other.most_common(6):
            print("    %6.2f  %s" % (100.0 * n / max(game, 1), u[:120]))
    w = window(d)
    if w:
        dt, irq, hrate = w
        print("interrupts a second over the window (%.0f s):" % dt)
        print("  " + ", ".join("%s %.0f" % (k, v) for k, v in irq if v >= 1)[:600])
        if hrate:
            us = sum(v for k, v in hrate.items() if k.endswith("_us"))
            print("htab hcalls a second: inserts %.0f (%.1f us each), evictions %.0f, read-backs %.0f, "
                  "invalidates %.0f (%.1f us each), updatepp %.0f, mismatches %.0f; in lv1: %.1f%% of a thread" %
                  (hrate.get("inserts", 0), hrate.get("insert_us", 0) / max(hrate.get("inserts", 0), 1e-9),
                   hrate.get("evictions", 0), hrate.get("readbacks", 0), hrate.get("invalidates", 0),
                   hrate.get("invalidate_us", 0) / max(hrate.get("invalidates", 0), 1e-9),
                   hrate.get("updatepps", 0), hrate.get("mismatches", 0), us / 10000.0))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for d in sys.argv[1:]:
        if os.path.exists(os.path.join(d, "perf.folded")):
            run(d)
        else:
            print("== %s: no perf.folded" % d)
