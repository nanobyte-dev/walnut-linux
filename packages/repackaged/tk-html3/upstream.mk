# Upstream pins of packages/repackaged/tk-html3/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- Tkhtml 3.0, TkinterWeb's fork (packages/repackaged/tk-html3/) ----------------------------------
# No release tags on the 3.0 branch: its head commit of 2026-09-19 (package.conf UPVER names it)
TKHTML_COMMIT       := d6a6df30a73df3952c7f7d8d89084841531b99ca
TKHTML_SHA256       := ced64eebf79b845f35ece898927636c7b4d2a875fb2124b2fabcfbce15cc16ac
TKHTML_URL          := https://github.com/Andereoo/TkinterWeb-Tkhtml/archive/$(TKHTML_COMMIT).tar.gz
