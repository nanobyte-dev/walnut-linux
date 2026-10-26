# classicube's orig tarball (included by the Makefile; docs/PACKAGING.md "Upstream tarballs"): upstream's tarball at
# the commit upstream.mk pins, repacked (+dfsg) without the paths in dfsg-exclude.txt (package.conf says
# why). package.conf ORIG=$CC_ORIG. Also the game's sounds and music, fetched and converted as its
# launcher would (sounds.sh, make classicube-audio): classicube-data's orig tarball (licensed/) packs them
CC_DIR := $(call pkgdir,classicube)
CC_TARBALL := $(DOWNLOADS)/ClassiCube-$(CLASSICUBE_COMMIT).tar.gz
CC_DFSG_EXCLUDE := $(shell sed -e 's/\#.*//' -e '/^[[:space:]]*$$/d' $(CC_DIR)/dfsg-exclude.txt)
CC_ORIG := $(BUILD)/classicube/classicube-$(CLASSICUBE_COMMIT)+dfsg.tar.gz
ORIG_VARS += CC_ORIG
$(CC_TARBALL):
	$(call fetch,$(CLASSICUBE_URL),$(CLASSICUBE_SHA256))
$(CC_ORIG): $(CC_TARBALL) $(CC_DIR)/orig.mk $(CC_DIR)/dfsg-exclude.txt
	@mkdir -p $(dir $@)
	gzip -dc $< | tar --delete $(addprefix ClassiCube-$(CLASSICUBE_COMMIT)/,$(CC_DFSG_EXCLUDE)) | gzip -n9 > $@.tmp
	mv $@.tmp $@
classicube-src: $(CC_ORIG)
CC_AUDIO := $(BUILD)/classicube/audio
classicube-audio: $(CC_AUDIO)/default2.zip
$(CC_AUDIO)/default2.zip: $(CC_TARBALL) $(CC_DIR)/sounds.sh | $(GAMES_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e CC_TARBALL=$(CC_TARBALL) $(GAMES_BUILDER_TAG) $(CC_DIR)/sounds.sh
