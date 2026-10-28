# tkinterweb's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): the PyPI
# sdist upstream.mk pins. package.conf ORIG=$TKW_ORIG
TKW_ORIG := $(DOWNLOADS)/tkinterweb-$(TKINTERWEB_VERSION).tar.gz
ORIG_VARS += TKW_ORIG
$(TKW_ORIG):
	$(call fetch,$(TKINTERWEB_URL),$(TKINTERWEB_SHA256))
tkinterweb-src: $(TKW_ORIG)
