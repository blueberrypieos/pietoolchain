# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libxml2
TAG     := libxml2-39.10
include ../common.mk

XCODEPROJ := $(SRCROOT)/libxml2.xcodeproj
sources = $(filter %.c,$(shell python3 ../tools/pbxproj-sources.py $(XCODEPROJ) $(1) 2>/dev/null))
GEN := $(SRCROOT)/Pregenerated Files
FLAGS := $(BASE_CFLAGS) -std=gnu99 -DNDEBUG -DHAVE_CONFIG_H -DLIBXML_STATIC \
	-I'$(GEN)/include' -I$(SRCROOT)/libxml2/include -I$(SRCROOT)/libxml2 -isystem $(SDK)/usr/local/include
DYLIB := $(OBJ)/libxml2.2.dylib

all: $(DYLIB) $(OBJ)/bin/xmllint $(OBJ)/bin/xmlcatalog

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(OBJ)/libxml2.a: $(patsubst %.c,$(OBJ)/%.o,$(call sources,xml2))
	$(LIBTOOL) -static -o $@ $^

$(DYLIB): $(OBJ)/libxml2.a
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libxml2.2.dylib \
		-compatibility_version 10 -current_version 10.9 \
		-Wl,-force_load,$< -Wl,-exported_symbols_list,'$(GEN)/libxml2.exp' -licucore -lz -o $@

$(OBJ)/bin/%: $(OBJ)/libxml2/%.o $(DYLIB)
	@mkdir -p $(dir $@)
	$(CC) $(BASE_LDFLAGS) $^ -o $@

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libxml2.2.dylib $(SDK)/usr/lib/libxml2.dylib
	ln -sf libxml2.2.dylib $(ROOT)/usr/lib/libxml2.dylib
	install -d $(ROOT)/usr/bin
	install -m 755 $(OBJ)/bin/xmllint $(OBJ)/bin/xmlcatalog $(ROOT)/usr/bin/
	$(call install_xcode_man,$(SRCROOT)/libxml2.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(filter-out %/xmlversion.h,$(wildcard $(SRCROOT)/libxml2/include/libxml/*.h)),usr/include/libxml2/libxml)
	install -m 644 '$(GEN)/include/libxml/xmlversion.h' $(SDK)/usr/include/libxml2/libxml/
	install -d $(SDK)/usr/lib/pkgconfig
	install -m 644 libxml-2.0.pc $(SDK)/usr/lib/pkgconfig/

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
