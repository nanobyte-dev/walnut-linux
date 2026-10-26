# Upstream pins of packages/patched/games/openrct2/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- OpenRCT2 (packages/patched/games/openrct2/): release 0.5.6 with our big-endian patches, and the asset zips its CMake would fetch ---
# (upstream's assets.json at v0.5.6: objects, title sequences, OpenSoundEffects, OpenMusic; same sha256)
OPENRCT2_VERSION    := 0.5.6
OPENRCT2_SHA256     := f4732724c1a709cfaff02ddff7fe3b6d027ad7827b343bf6f2a254114d32128b
OPENRCT2_URL        := https://github.com/OpenRCT2/OpenRCT2/archive/refs/tags/v$(OPENRCT2_VERSION).tar.gz
OPENRCT2_OBJECTS_VERSION  := 1.8.0
OPENRCT2_OBJECTS_SHA256   := 949bee5cac9e941be63bb923b792738a507c155a6fea73edab0b23bede21d302
OPENRCT2_OBJECTS_URL      := https://github.com/OpenRCT2/objects/releases/download/v$(OPENRCT2_OBJECTS_VERSION)/objects.zip
OPENRCT2_TITLESEQ_VERSION := 0.5.6
OPENRCT2_TITLESEQ_SHA256  := 37ac28985baf183bae27af72710613595f683dc24892c05989fb6bfd016bdb5d
OPENRCT2_TITLESEQ_URL     := https://github.com/OpenRCT2/title-sequences/releases/download/v$(OPENRCT2_TITLESEQ_VERSION)/title-sequences.zip
OPENRCT2_OPENSFX_VERSION  := 1.0.6
OPENRCT2_OPENSFX_SHA256   := 06b90f3e19c216752df441d551b26a9e3e1ba7755bdd2102504b73bf993608be
OPENRCT2_OPENSFX_URL      := https://github.com/OpenRCT2/OpenSoundEffects/releases/download/v$(OPENRCT2_OPENSFX_VERSION)/opensound.zip
OPENRCT2_OPENMUSIC_VERSION := 1.6.1
OPENRCT2_OPENMUSIC_SHA256 := 994b350d3b180ee1cb9619fe27f7ebae3a1a5232840c4bd47a89f33fa89de1a1
OPENRCT2_OPENMUSIC_URL    := https://github.com/OpenRCT2/OpenMusic/releases/download/v$(OPENRCT2_OPENMUSIC_VERSION)/openmusic.zip
