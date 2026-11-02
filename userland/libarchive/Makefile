# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libarchive
TAG     := libarchive-167.120.3
include ../common.mk

XCODEPROJ := $(SRCROOT)/libarchive.xcodeproj
sources = $(filter %.c,$(shell python3 ../tools/pbxproj-sources.py $(XCODEPROJ) $(1) 2>/dev/null))
LIB_SRCS := $(filter-out %/archive_check_entitlement.c,$(call sources,libarchive))
FE_SRCS  := $(call sources,libarchive_fe)
TAR_SRCS := $(call sources,tar)
CPIO_SRCS := $(call sources,cpio)

A := $(SRCROOT)/libarchive
FLAGS := $(BASE_CFLAGS) '-DPLATFORM_CONFIG_H="$(SRCROOT)/config.h"' -I$(CURDIR) \
	-I$(A)/libarchive -I$(A)/libarchive_fe -I$(SDK)/usr/include/libxml2 -Wno-deprecated-non-prototype
DYLIB := $(OBJ)/libarchive.2.dylib

all: $(DYLIB) $(OBJ)/bin/bsdtar $(OBJ)/bin/cpio

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/entitlement.o: entitlement.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

LIB_OBJS := $(patsubst %.c,$(OBJ)/%.o,$(LIB_SRCS)) $(OBJ)/entitlement.o
$(DYLIB): $(LIB_OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libarchive.2.dylib \
		-compatibility_version 9 -current_version 9.2 \
		-Wl,-unexported_symbols_list,$(SRCROOT)/unexports \
		$^ -lz -lbz2 -llzma -liconv -lxml2 -o $@

$(OBJ)/libarchive_fe.a: $(patsubst %.c,$(OBJ)/%.o,$(FE_SRCS))
	$(LIBTOOL) -static -o $@ $^

$(OBJ)/bin/bsdtar: $(patsubst %.c,$(OBJ)/%.o,$(TAR_SRCS)) $(OBJ)/libarchive_fe.a $(DYLIB)
	@mkdir -p $(dir $@)
	$(CC) $(BASE_LDFLAGS) $^ -o $@
$(OBJ)/bin/cpio: $(patsubst %.c,$(OBJ)/%.o,$(CPIO_SRCS)) $(OBJ)/libarchive_fe.a $(DYLIB)
	@mkdir -p $(dir $@)
	$(CC) $(BASE_LDFLAGS) $^ -o $@

install: all
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libarchive.2.dylib $(SDK)/usr/lib/libarchive.dylib
	ln -sf libarchive.2.dylib $(ROOT)/usr/lib/libarchive.dylib
	$(call copy_headers,$(A)/libarchive/archive.h $(A)/libarchive/archive_entry.h,usr/local/include)
	install -d $(ROOT)/usr/bin
	install -m 755 $(OBJ)/bin/bsdtar $(OBJ)/bin/cpio $(ROOT)/usr/bin/
	ln -sf bsdtar $(ROOT)/usr/bin/tar
	$(call install_xcode_man,$(SRCROOT)/libarchive.xcodeproj)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
