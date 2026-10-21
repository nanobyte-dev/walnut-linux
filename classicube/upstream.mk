# Upstream pins of classicube/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- ClassiCube (classicube/) ------------------------------------------------
# A pinned commit (SDL3 backend), plus the texture pack it needs (plain HTTP, so checksummed)
CLASSICUBE_COMMIT   := aa47929825a4647354588818fb0b002b2f808279
CLASSICUBE_SHA256   := 911e2b179728cbbb3c5f6d90f324f38ebbe6f68c57cb96dd66741fb15e5ab488
CLASSICUBE_URL      := https://github.com/ClassiCube/ClassiCube/archive/$(CLASSICUBE_COMMIT).tar.gz
