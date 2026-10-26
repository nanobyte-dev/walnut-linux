# zlib-ng's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's release tag,
# as upstream.mk pins it. package.conf ORIG=$ZNG_ORIG
ZNG_ORIG := $(DOWNLOADS)/zlib-ng-$(ZLIBNG_VERSION).tar.gz
ORIG_VARS += ZNG_ORIG
$(ZNG_ORIG):
	$(call fetch,$(ZLIBNG_URL),$(ZLIBNG_SHA256))
zlib-ng-src: $(ZNG_ORIG)
