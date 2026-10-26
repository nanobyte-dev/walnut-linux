# Upstream pins of warzone2100/ (included by the Makefile after config.mk; a change here is a change to the
# package, so its version goes up). Downloads are checked against these sha256s.
# --- Warzone 2100 (warzone2100/): release 4.3.5, the last with an OpenGL 2.1 / GLES 2 renderer ---
WARZONE_VERSION     := 4.3.5
WARZONE_SHA256      := 01d608f6f9638e14d7c857df40ad339c1bfc207a05daafe7157ad8652a3405c3
WARZONE_URL         := https://github.com/Warzone2100/warzone2100/releases/download/$(WARZONE_VERSION)/warzone2100_src.tar.xz
