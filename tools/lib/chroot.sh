# shellcheck shell=bash
# tools/lib/chroot.sh: the build chroots' mounts, opened and closed in one place; . tools/lib/chroot.sh as root
# (the deb builder container; PC only, see tools/lib/README.md).
#   chroot_open ROOT [SRC:DEST[:ro] ...]   proc, a read-only sysfs and a recursive bind of /dev under ROOT, then each
#                                          SRC (a host path) bound at ROOT/DEST; ROOT's tree is already unpacked
#   chroot_run ROOT VAR=value... CMD...    CMD in ROOT with exactly that environment (env -i)
#   chroot_unmount ROOT                    unmounts everything under ROOT; fails, saying so, while anything is left
#   chroot_close ROOT                      chroot_unmount, and only then rm -rf --one-file-system ROOT
# Why: in the privileged container /sys and /dev are the host's, and a bound directory on the same filesystem
# (ccache, ladder's clone) would be deleted by rm. Nothing may walk the tree (rm -r, chown -R, find) while anything is
# bound into it, so removal happens only after the mount table shows nothing under ROOT.
# ROOT is made canonical first (realpath -e; a trailing slash or a symlink in the path would never match the mount
# table's spelling), refused if it is / or has whitespace or a backslash, and that one path is used for the mount
# table, the unmounts and the rm. The table is matched as fixed strings on whole path components.
# One run per ROOT: a flock on ROOT.lock is held from the first call on that ROOT to the process's exit.
# Callers: chroot_close in their EXIT trap, and once before chroot_open to clear what a crashed run left. Do not
# chown ROOT's tree; chown the outputs copied out of it.

# _chroot_canon ROOT: sets CHROOT_R to the canonical ROOT, or returns 1 with the reason (ROOT missing: returns 2)
_chroot_canon() {
	case $1 in /?*) ;; *) echo "!! chroot: ROOT must be an absolute path below /, not '$1'" >&2; return 1 ;; esac
	if [ ! -e "$1" ] && [ ! -L "$1" ]; then return 2; fi
	CHROOT_R=$(realpath -e -- "$1") || { echo "!! chroot: cannot resolve '$1'" >&2; return 1; }
	case $CHROOT_R in
	/ | *[[:space:]\\]*) echo "!! chroot: refusing ROOT '$CHROOT_R' (/, or whitespace or a backslash in it)" >&2; return 1 ;;
	esac
}

_chroot_lock() {	# _chroot_lock ROOT: one run at a time per ROOT; kept until this process ends
	local fd
	case "
${_CHROOT_LOCKED:-}
" in *"
$1
"*) return 0 ;; esac
	exec {fd}>"$1.lock" || return 1
	flock -w 60 "$fd" || { echo "!! chroot: $1 is in use by another run ($1.lock)" >&2; return 1; }
	_CHROOT_LOCKED="${_CHROOT_LOCKED:-}
$1"
}

# _chroot_mounts R: the mount points at or under R, deepest first, decoded; fails on a name it cannot handle
_chroot_mounts() {
	local raw m
	while IFS= read -r raw; do
		case $raw in *'\012'*) echo "!! chroot: a mount under $1 has a newline in its name" >&2; return 1 ;; esac
		printf -v m '%b' "$raw"
		printf '%s\n' "$m"
	done < <(awk -v r="$1" '$2 == r || index($2, r "/") == 1 { print $2 }' /proc/mounts | sort -r)
}

chroot_open() {
	local r b src dest opt enc rc=0
	_chroot_canon "$1" || return 1
	r=$CHROOT_R; shift
	_chroot_lock "$r" || return 1
	mkdir -p "$r/proc" "$r/sys" "$r/dev"
	mount -t proc proc "$r/proc"; mount -t sysfs -o ro sysfs "$r/sys"; mount --rbind /dev "$r/dev"
	mount --make-rslave "$r/dev" 2>/dev/null || true	# unmounting the copy must not reach /dev's own submounts
	for b in "$@"; do
		src=${b%%:*} dest=${b#*:} opt=
		case $dest in *:ro) opt="-o ro"; dest=${dest%:ro} ;; esac
		dest=${dest#/}
		# a symlink in the unpacked tree that leads out of ROOT would put the mount where close never looks:
		# checked before mkdir, so a refused destination creates nothing outside ROOT
		case $(realpath -m -- "$r/$dest") in "$r"/*) ;; *) echo "!! chroot_open: $dest leads out of $r" >&2; return 1 ;; esac
		mkdir -p "$r/$dest"
		# shellcheck disable=SC2086
		mount --bind $opt "$src" "$r/$dest" || rc=1
		enc="$r/$dest"; enc=${enc//\\/\\134}; enc=${enc// /\\040}; enc=${enc//$'\t'/\\011}; enc=${enc//$'\n'/\\012}	# /proc/mounts' spelling
		ENC=$enc awk '$2 == ENVIRON["ENC"] { f = 1 } END { exit !f }' /proc/mounts || { echo "!! chroot_open: nothing mounted at $r/$dest" >&2; return 1; }
	done
	return $rc
}

chroot_run() { local r=$1; shift; chroot "$r" /usr/bin/env -i "$@"; }

_chroot_unmount_r() {	# the canonical, locked R
	local r=$1 m
	# -R first; plain -l after it, for a mount whose children the kernel locks (a user namespace's copy of /dev)
	while IFS= read -r m; do umount -R -l -- "$m" 2>/dev/null || umount -l -- "$m" 2>/dev/null || true; done < <(_chroot_mounts "$r")
	if [ -n "$(_chroot_mounts "$r")" ] || awk -v r="$r" '$2 == r || index($2, r "/") == 1 { f = 1 } END { exit !f }' /proc/mounts; then
		echo "!! still mounted under $r" >&2
		return 1
	fi
}

chroot_unmount() {
	local rc=0
	_chroot_canon "$1" || { rc=$?; [ $rc = 2 ] && return 0; return 1; }
	_chroot_lock "$CHROOT_R" || return 1
	_chroot_unmount_r "$CHROOT_R"
}

chroot_close() {	# ROOT resolved once; the same path is unmounted, checked and removed
	local rc=0 r
	_chroot_canon "$1" || { rc=$?; [ $rc = 2 ] && return 0; return 1; }
	r=$CHROOT_R
	_chroot_lock "$r" || return 1
	_chroot_unmount_r "$r" || return 1
	rm -rf --one-file-system -- "$r"
}
