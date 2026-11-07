#!/bin/bash
# Host test of tools/lib/chroot.sh in a private mount namespace (unshare -Urmpnf): open mounts proc, sysfs, /dev and
# the binds under a scratch root, close leaves nothing mounted, removes the tree, and leaves what was bound in
# (a bound directory and the host's /dev are never walked). Skips (77) where unprivileged user namespaces are refused.
set -u
here=$(cd "$(dirname "$0")" && pwd)
if [ "${1:-}" != inner ]; then
	# the build scripts keep no mount logic of their own: it is all tools/lib/chroot.sh
	. "$here/lib/t.sh"
	for f in deb-build.sh xcc-build.sh ladder-mesa-build.sh; do
		no "$f has no mount or umount of its own" grep -En '(^|[;&|] *)(mount|umount) ' "$here/$f"
		ok "$f uses chroot_open and chroot_close" grep -q chroot_open "$here/$f"
	done
	done_tests || exit 1
	unshare -Urmpnf --mount-proc true 2>/dev/null || { echo "skip: no unprivileged user, mount and pid namespaces (unshare -Urmpnf)"; exit 77; }
	exec unshare -Urmpnf --mount-proc bash "$0" inner
fi
. "$here/lib/t.sh"
. "$here/lib/chroot.sh"
W=$(mktemp -d)
R=$W/root
# the test's own cleanup obeys the library's rule: the scratch tree is removed only once nothing is mounted under it
trap 'for d in "$W"/*; do chroot_unmount "$d" 2>/dev/null || true; done; mounted "$W" || rm -rf --one-file-system "$W"' EXIT
mkdir -p "$W/shared" "$W/ro"; echo keep > "$W/shared/file"; echo ro > "$W/ro/file"
mkdir -p "$R/etc"; echo marker > "$R/etc/marker"
mounted() { awk -v r="$1" 'index($2, r "/") == 1 { f = 1 } END { exit !f }' /proc/mounts; }
survives() { test "$(cat "$W/shared/file")" = keep; }

chroot_open "$R" "$W/shared:/mnt/shared" "$W/ro:/mnt/ro:ro"
ok "proc is mounted in the root" test -e "$R/proc/self"
ok "/dev is bound into the root" test -e "$R/dev/null"
ok "a bind shows the host directory" test "$(cat "$R/mnt/shared/file")" = keep
ok "the nested /dev mounts are under the root too" mounted "$R"
no "refuses to close a relative root" chroot_close relative/root
no "refuses the root of the filesystem" chroot_close /
touch "$R/mnt/shared/made-inside"
chroot_close "$R"
no "nothing is mounted under the root after close" mounted "$R"
no "the root is gone" test -e "$R"
ok "the bound directory survives, with its file" survives
ok "what was made through the bind survives" test -e "$W/shared/made-inside"
ok "the read-only bound directory survives" test "$(cat "$W/ro/file")" = ro
ok "the host's /dev is still there" test -e /dev/null
mkdir -p "$R/x"; chroot_close "$R"; no "a plain tree is removed" test -e "$R"
mkdir -p "$R/mnt/old"; mount --bind "$W/shared" "$R/mnt/old"
chroot_close "$R"; no "a leftover bind is unmounted, then the root removed" test -e "$R"
ok "and the leftover's source is intact" survives
ok "close of a root that is not there is harmless" chroot_close "$W/never-was"

# the spellings of ROOT that are not the mount table's: a trailing slash, a symlink in the path
mkdir -p "$W/real"; ln -s real "$W/link"
chroot_open "$W/real/r" "$W/shared:/mnt/shared" 2>/dev/null || { mkdir -p "$W/real/r"; chroot_open "$W/real/r" "$W/shared:/mnt/shared"; }
chroot_close "$W/real/r/"
no "a trailing slash: nothing left mounted" mounted "$W/real/r"
ok "a trailing slash: the bound file survives" survives
no "a trailing slash: the root is gone" test -e "$W/real/r"
mkdir -p "$W/real/r"; chroot_open "$W/real/r" "$W/shared:/mnt/shared"
chroot_close "$W/link/r"
no "a symlink in the path: nothing left mounted" mounted "$W/real/r"
ok "a symlink in the path: the bound file survives" survives
no "a symlink in the path: the root is gone" test -e "$W/real/r"
# whitespace and backslashes are refused outright, before anything is mounted or removed
mkdir -p "$W/sp ace"
no "a space in ROOT: open refuses" chroot_open "$W/sp ace" "$W/shared:/mnt/shared"
no "a space in ROOT: nothing was mounted" mounted "$W/sp ace"
no "a space in ROOT: close refuses" chroot_close "$W/sp ace"
ok "and the directory is still there" test -d "$W/sp ace"
mkdir -p "$W/back\\slash"
no "a backslash in ROOT: close refuses" chroot_close "$W/back\\slash"
# a sibling whose name starts with ROOT's is not under it
mkdir -p "$W/root2"; chroot_open "$W/root2" "$W/shared:/mnt/shared"; mkdir -p "$R"; chroot_open "$R" "$W/ro:/mnt/ro:ro"
chroot_close "$R"
ok "a sibling with the same prefix keeps its mounts" mounted "$W/root2"
chroot_close "$W/root2"; ok "and then closes itself" test ! -e "$W/root2"
# a symlink in the tree that leads out of ROOT: the bind is refused, not made somewhere close never looks
mkdir -p "$R" "$W/elsewhere"; ln -s "$W/elsewhere" "$R/out"
no "a bind that leads out of ROOT is refused" chroot_open "$R" "$W/shared:/out/x"
no "and nothing was mounted outside" mounted "$W/elsewhere"
no "and nothing was created outside either" test -e "$W/elsewhere/x"
chroot_close "$R"
# a mount close cannot see through (a newline in the name): refuse, keep the tree and the source
mkdir -p "$R"; chroot_open "$R" "$W/shared:/mnt/nl
x"
no "a mount with a newline in its name: close refuses" chroot_close "$R"
ok "and the tree is still there" test -d "$R/mnt"
ok "and so is the bound file" survives
umount -R -l "$R/mnt/nl
x" 2>/dev/null; chroot_close "$R"; no "after unmounting by hand the close works" test -e "$R"
# the old logic (ROOT as a string against the mount table, rm after it) deletes the bound file: this test would catch it
old_close() { local r=$1; for m in $(awk -v p="$r/" 'index($2, p) == 1 { print $2 }' /proc/mounts | sort -r); do umount -R -l "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || true; done
	grep -q " $r/" /proc/mounts && return 1; rm -rf --one-file-system "$r" 2>/dev/null; }
mkdir -p "$R"; chroot_open "$R" "$W/shared:/mnt/shared"
old_close "$R/"
no "fix-off: the old logic with a trailing slash loses the bound file" survives
done_tests
