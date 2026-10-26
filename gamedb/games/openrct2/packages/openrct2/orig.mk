# openrct2's orig tarballs (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's release
# tarball and asset zips, as upstream.mk pins them. The zips keep their upstream names (debian/rules
# refers to them), one directory per asset version. The orig tarball is upstream's without its own
# debian/ directory (+ds, as Debian does): ours replaces it. package.conf ORIG and COMPONENTS
ORCT2_DL := $(DOWNLOADS)/OpenRCT2-$(OPENRCT2_VERSION).tar.gz
ORCT2_ORIG := $(DOWNLOADS)/openrct2_$(OPENRCT2_VERSION)+ds.orig.tar.gz
ORCT2_OBJECTS := $(DOWNLOADS)/openrct2-objects-$(OPENRCT2_OBJECTS_VERSION)/objects.zip
ORCT2_TITLESEQ := $(DOWNLOADS)/openrct2-titleseq-$(OPENRCT2_TITLESEQ_VERSION)/title-sequences.zip
ORCT2_OPENSFX := $(DOWNLOADS)/openrct2-opensfx-$(OPENRCT2_OPENSFX_VERSION)/opensound.zip
ORCT2_OPENMUSIC := $(DOWNLOADS)/openrct2-openmusic-$(OPENRCT2_OPENMUSIC_VERSION)/openmusic.zip
ORIG_VARS += ORCT2_ORIG ORCT2_OBJECTS ORCT2_TITLESEQ ORCT2_OPENSFX ORCT2_OPENMUSIC
$(ORCT2_DL):
	$(call fetch,$(OPENRCT2_URL),$(OPENRCT2_SHA256))
$(ORCT2_OBJECTS):
	$(call fetch,$(OPENRCT2_OBJECTS_URL),$(OPENRCT2_OBJECTS_SHA256))
$(ORCT2_TITLESEQ):
	$(call fetch,$(OPENRCT2_TITLESEQ_URL),$(OPENRCT2_TITLESEQ_SHA256))
$(ORCT2_OPENSFX):
	$(call fetch,$(OPENRCT2_OPENSFX_URL),$(OPENRCT2_OPENSFX_SHA256))
$(ORCT2_OPENMUSIC):
	$(call fetch,$(OPENRCT2_OPENMUSIC_URL),$(OPENRCT2_OPENMUSIC_SHA256))
$(ORCT2_ORIG): $(ORCT2_DL)
	gzip -dc $< | tar --delete --wildcards 'OpenRCT2-$(OPENRCT2_VERSION)/debian' | gzip -n9 > $@.tmp
	mv $@.tmp $@
openrct2-src: $(ORCT2_ORIG) $(ORCT2_OBJECTS) $(ORCT2_TITLESEQ) $(ORCT2_OPENSFX) $(ORCT2_OPENMUSIC)
