# tk-html3's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): GitHub's
# archive of the commit upstream.mk pins. package.conf ORIG=$TKHTML_ORIG
TKHTML_ORIG := $(DOWNLOADS)/TkinterWeb-Tkhtml-$(TKHTML_COMMIT).tar.gz
ORIG_VARS += TKHTML_ORIG
$(TKHTML_ORIG):
	$(call fetch,$(TKHTML_URL),$(TKHTML_SHA256))
tk-html3-src: $(TKHTML_ORIG)
