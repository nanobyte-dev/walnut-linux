#!/bin/bash
# pkg-dir.sh NAME | --all [--order] | --order: our source packages (a directory with a package.conf;
# docs/LAYOUT.md), relative to the repository's top; the work is tools/pkgconf.py's (its header has the rules).
#   NAME     the directory of package NAME; exit 1 when there is none
#   --all    every package directory, one per line (the Makefile's PKG_DIRS)
#   --order  every package name in build order; --all --order the directories in that order
exec python3 -I "$(dirname "$0")/pkgconf.py" dir "$@"
