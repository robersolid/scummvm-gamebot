# webOS IPK packaging, modeled on the webosbrew RetroArch flow.
# Requires ares-package from @webos-tools/cli in the PATH and the
# webosbrew native toolchain staging dir in WEBOS_STAGING_DIR.

WEBOS_APP_ID ?= org.scummvm.webos
WEBOS_STAGING_DIR ?= $(shell $(CXX) --print-sysroot)
WEBOS_DIST := webos-dist

ipk: all
	$(RM) -r $(WEBOS_DIST)
	$(MKDIR) -p $(WEBOS_DIST)/lib $(WEBOS_DIST)/data
	$(CP) $(EXECUTABLE) $(WEBOS_DIST)/scummvm
	$(STRIP) $(WEBOS_DIST)/scummvm
	$(CP) $(srcdir)/dists/webos/appinfo.json $(WEBOS_DIST)/
	-$(CP) $(srcdir)/dists/webos/icon.png $(WEBOS_DIST)/
	-$(CP) $(srcdir)/dists/webos/icon160.png $(WEBOS_DIST)/
	$(CP) $(srcdir)/gui/themes/*.zip $(srcdir)/gui/themes/*.dat $(WEBOS_DIST)/data/
	-$(CP) $(srcdir)/dists/engine-data/*.dat $(srcdir)/dists/engine-data/*.zip $(srcdir)/dists/engine-data/*.tbl $(srcdir)/dists/engine-data/*.cpt $(WEBOS_DIST)/data/
	# runtime libs needed on TV
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libstdc++.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libatomic.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/lib/libatomic.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/lib/libgcc_s.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libjpeg.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libpng16.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libfreetype.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libz.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libtheora*.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libogg.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libvorbis*.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libmad.so* $(WEBOS_DIST)/lib/
	-$(CP) -d $(WEBOS_STAGING_DIR)/usr/lib/libSDL2_net-2.0.so* $(WEBOS_DIST)/lib/
ifdef SDL_PREFIX
	-$(CP) -d $(SDL_PREFIX)/lib/libSDL2-2.0.so* $(WEBOS_DIST)/lib/
endif
	-$(CP) -d $(srcdir)/toolchains/SDL2-webos/lib/libSDL2-2.0.so* $(WEBOS_DIST)/lib/
	ares-package $(WEBOS_DIST)

launch: ipk
	ares-install $(WEBOS_APP_ID)_*.ipk
	ares-launch $(WEBOS_APP_ID)

.PHONY: ipk launch
