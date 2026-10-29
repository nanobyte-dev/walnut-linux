# devilutionx's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's offline
# source release, as upstream.mk pins it. package.conf ORIG=$DVX_ORIG
DVX_ORIG := $(DOWNLOADS)/devilutionx_$(DEVILUTIONX_VERSION).orig.tar.xz
ORIG_VARS += DVX_ORIG
$(DVX_ORIG):
	$(call fetch,$(DEVILUTIONX_URL),$(DEVILUTIONX_SHA256))
devilutionx-src: $(DVX_ORIG)
