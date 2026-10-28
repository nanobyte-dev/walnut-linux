# Upstream pins of packages/patched/desktop/badwolf/ (included by the Makefile after config.mk; a change here is a
# change to the package, so its version goes up). Downloads are checked against these sha256s.
# --- Badwolf, Walnut's web browser (packages/patched/desktop/badwolf/) ----------------------------------
# The release tarball, signed by upstream with signify (2025.pub); the sha256 is of the verified file
BADWOLF_VERSION     := 1.4.0
BADWOLF_SHA256      := 9542978a77fef179e386d803cc3dad340202c354c09201d4200c93b0c95b59d3
BADWOLF_URL         := https://distfiles.hacktivis.me/releases/badwolf/badwolf-$(BADWOLF_VERSION).tar.gz
