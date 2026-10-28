# Badwolf's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's release,
# as upstream.mk pins it. package.conf ORIG=$BADWOLF_ORIG
BADWOLF_ORIG := $(DOWNLOADS)/badwolf-$(BADWOLF_VERSION).tar.gz
ORIG_VARS += BADWOLF_ORIG
$(BADWOLF_ORIG):
	$(call fetch,$(BADWOLF_URL),$(BADWOLF_SHA256))
badwolf-src: $(BADWOLF_ORIG)
