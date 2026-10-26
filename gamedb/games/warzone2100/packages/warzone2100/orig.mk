# warzone2100's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's release
# tarball, as upstream.mk pins it. package.conf ORIG=$WZ_ORIG
WZ_ORIG := $(DOWNLOADS)/warzone2100_$(WARZONE_VERSION).orig.tar.xz
ORIG_VARS += WZ_ORIG
$(WZ_ORIG):
	$(call fetch,$(WARZONE_URL),$(WARZONE_SHA256))
warzone2100-src: $(WZ_ORIG)
