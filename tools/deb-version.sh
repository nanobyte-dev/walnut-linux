#!/bin/bash
# deb-version.sh <pkg>: prints N for <pkg>'s "+ps3.N" version, the date and the short hash of that commit:
# N = the commit count up to the last commit that touched the package's directory or the shared
# packaging tooling (so versions only grow); a dirty tree gives "<N+1>~dev<stamp>" (make deploy only).
set -euo pipefail
PKG=$1
PD=$(tools/pkg-dir.sh "$PKG")
paths=("$PD/" tools/deb-prepare.sh tools/deb-build.sh tools/deb-chroot.sh build-containers/Dockerfile.deb)
# package.conf VERSION_PATHS: more sources the package carries (e.g. ps3-boot's initramfs, tools/ssdimg):
# a file, a directory, or a pattern read as GitHub's path filters read it (github-ci/gen-workflows.py
# passes the same strings): * within one path component, ** across them; so a pattern is a git
# :(glob) pathspec (plain git lets * cross /)
VERSION_PATHS=$(python3 -I tools/pkgconf.py words "$PKG" VERSION_PATHS)
set -f	# the patterns reach git as they are, not expanded here
for v in $VERSION_PATHS; do
	case $v in *'*'*) paths+=(":(glob)$v") ;; *) paths+=("$v") ;; esac
done
set +f
last=$(git log -1 --format=%H -- "${paths[@]}")
n=$(git rev-list --count "$last")
date=$(git log -1 --format=%cD "$last")
# LINUX_VARIANT (the lockdep kernel, tools/linux-config.sh): always a test build, so the release names of its
# kernel packages and of the ps3rsx-modules built for it are its own, never those of the plain debug build of
# the same commit (a ps3rsx built for another kernel config must not load into it)
variant=
[ "$PKG" != linux ] || [ -z "${LINUX_VARIANT:-}" ] || variant=1
if [ -n "$variant" ] || [ -n "$(git status --porcelain -- "${paths[@]}")" ]; then
	n="$((n + 1))~dev$(date -u +%Y%m%d%H%M%S)"
	date=$(date -uR)
fi
echo "$n"
echo "$date"
git log -1 --format=%h "$last"	# the commit the version stands for (dirty trees: plus changes)
