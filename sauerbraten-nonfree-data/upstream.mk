# Upstream pins of sauerbraten-nonfree-data/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- Sauerbraten's models and sounds missing from cube2-data (sauerbraten-nonfree-data/) --------
# The 2020_12_29 Linux release (SourceForge publishes the MD5 only; the SHA256 is of our download)
SAUER_RELEASE_FILE   := sauerbraten_2020_12_29_linux.tar.bz2
SAUER_RELEASE_MD5    := b50d018f7dd6259f1fc12b56028a51b5
SAUER_RELEASE_SHA256 := cdba7c4a47cefd30d0afdd6a912199a1384319cf1619923cb7189e72e468be70
SAUER_RELEASE_URL    := https://sourceforge.net/projects/sauerbraten/files/sauerbraten/2020_11_29/$(SAUER_RELEASE_FILE)/download
