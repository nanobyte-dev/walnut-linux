# Teeworlds: ioctl(FIONBIO) with an unsigned long on big-endian 64-bit

Status: draft, not sent. Patch: `teeworlds-0001-ioctl-fionbio-int.patch` (git format-patch, author
from git config; our source is `teeworlds/patches/0001`). Checked: upstream master `src/base/system.c`
(read on GitHub, 2026-10-18) has the same `unsigned long mode` code as 0.7.5, so the patch applies
there as is (it was cut against the 0.7.5 tarball; run `git am` on a fresh clone to be sure).

## GitHub issue (teeworlds/teeworlds)

Title: Client hangs after connecting on big-endian 64-bit (ppc64, s390x): sockets stay blocking

Body:

    net_set_non_blocking() and net_set_blocking() in src/base/system.c pass a pointer to an
    unsigned long to ioctl(sock, FIONBIO, ...). On Linux the kernel reads an int through that
    pointer. On little-endian the int is the low half of the long and holds the 1. On big-endian
    64-bit machines it is the high half, which is 0, so the sockets are never made non-blocking.

    What you see: the client connects to a local teeworlds-server, gets the token and the accept
    ("got accept", "connection online"), then sits in recvfrom() inside CNetClient::Recv, called
    from the loading loop (PumpNetwork). The server waits for the client's NETMSG_INFO, which the
    client only sends on its next main loop pass, so nothing moves. The screen stays on the last
    loading frame (52%), the CPU is idle. A blocking server socket also means the server only
    ticks when a packet arrives.

    Found on a PlayStation 3 (Debian ppc64, teeworlds 0.7.5-3). Reproduced the same way under
    qemu-ppc64 user mode, with a backtrace from qemu's gdb stub. With the fix the client sends its
    info, loads dm1 and enters the game ("player has entered the game" in the server log); on the
    PS3 it runs at 183 fps in game.

    Master still has the same lines (net_set_non_blocking, net_set_blocking). The fix is to use an
    int on everything but Windows, where ioctlsocket() really takes a u_long *. PR follows.

## Pull request

Title: Use an int for ioctl(FIONBIO) on non-Windows systems

Body: the commit message of the patch, plus "Fixes #<issue number>". Tested on ppc64 (PS3, real
hardware, and qemu user mode). Not tested on s390x (same argument, no hardware), not tested on
Windows (the Windows branches are unchanged). On little-endian nothing changes: the int is the
same value the long's low half had.

## Submission steps

1. Fork github.com/teeworlds/teeworlds, clone, `git am docs/upstream/teeworlds-0001-ioctl-fionbio-int.patch`
   (if it fails on master, `git apply --3way` and fix by hand: two functions, same lines).
2. Open the issue with the text above, then the PR from your branch; put the issue number in the PR.
3. Check CONTRIBUTING / the repository's current state first: the project is in maintenance mode, so the
   PR may sit for a while. The Debian bug below does not depend on upstream.
4. Optional: build with `-fsanitize` on x86 once to see nothing else changes (nothing should).

## Debian bug (reportbug-style)

Package: teeworlds. Send to submit@bugs.debian.org (or `reportbug teeworlds`), after the upstream
issue exists so that the upstream link can go in the text. Debian's tracker sets the forwarded
header later with `forwarded NNNNNN https://github.com/teeworlds/teeworlds/pull/XXXX`.

    To: submit@bugs.debian.org
    Subject: teeworlds: client never enters the game on big-endian 64-bit (ppc64, s390x): FIONBIO gets an unsigned long

    Package: teeworlds
    Version: 0.7.5-3
    Severity: important
    Tags: patch upstream
    X-Debbugs-Cc: debian-powerpc@lists.debian.org, debian-s390@lists.debian.org

    Dear Maintainer,

    On big-endian 64-bit architectures the client connects to a server but never gets into the
    game, and a local teeworlds-server only ticks when a packet arrives.

    Cause: net_set_non_blocking() and net_set_blocking() in src/base/system.c call
    ioctl(sock, FIONBIO, (unsigned long *)&mode). Linux reads an int from that pointer. On a
    big-endian 64-bit machine that int is the high half of the unsigned long, which is 0, so the
    sockets stay blocking. The client waits in recvfrom() inside CNetClient::Recv after the
    accept, the server waits for the client's NETMSG_INFO, which the client only sends on its
    next main loop pass.

    Seen on a PlayStation 3 (ppc64) running 0.7.5-3, and under qemu-ppc64 user mode in a ppc64
    chroot. With the attached patch the client sends its info, loads the map and joins; on the PS3
    it renders at 183 fps in game. Little-endian behaviour is unchanged.

    The patch is also sent upstream: <URL of the PR>.

    -- System Information:
    Architecture: ppc64
    Kernel: Linux 6.12.x (PlayStation 3, Cell)

    (attach: teeworlds-0001-ioctl-fionbio-int.patch)

The only other big-endian report in the Debian tracker is #504704 (a 0.4 crash from 2008), so this is
not a duplicate. Re-check `reportbug`'s duplicate list before sending. Debian carries patches in
`debian/patches/` with a `series` file; a DEP-3 header (`Forwarded: <PR url>`) goes on top.

Severity "important" is a judgement call: the client is unusable on ppc64 and s390x, which are
release architectures only for ppc64el today; ppc64 is a ports architecture (debian-ports), so "normal"
is also defensible.
