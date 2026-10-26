# darkplaces-gl2's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): Debian buster's
# DarkPlaces 0~20180412~beta1 tarball, as upstream.mk pins it. package.conf ORIG=$DP_ORIG
DP_ORIG := $(DOWNLOADS)/darkplaces_$(DARKPLACES_VERSION).orig.tar.xz
ORIG_VARS += DP_ORIG
$(DP_ORIG):
	$(call fetch,$(DARKPLACES_URL),$(DARKPLACES_SHA256))
darkplaces-gl2-src: $(DP_ORIG)
