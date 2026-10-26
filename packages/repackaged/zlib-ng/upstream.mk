# Upstream pins of packages/repackaged/zlib-ng/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- zlib-ng in zlib-compat mode (packages/repackaged/zlib-ng/) ------------------------------------
# A libz.so.1 in a directory of its own that a game opts into (LD_LIBRARY_PATH); not in Debian
ZLIBNG_VERSION      := 2.3.3
ZLIBNG_SHA256       := f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
ZLIBNG_URL          := https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$(ZLIBNG_VERSION).tar.gz
