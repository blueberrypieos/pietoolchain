# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT  := xz
TAG      := v5.8.1
REPO_URL := https://github.com/tukaani-project/xz
include ../common.mk

BINUTILS := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin
DYLIB    := $(OBJ)/liblzma.5.8.1.dylib

$(OBJ)/configured: $(SRCROOT)/.fetched
	cmake -G Ninja -S $(SRCROOT) -B $(OBJ) -DCMAKE_SYSTEM_NAME=Darwin \
		-DCMAKE_OSX_SYSROOT=$(SDK) -DCMAKE_OSX_ARCHITECTURES=$(ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=26.5 -DCMAKE_C_COMPILER=$(CC) -DCMAKE_C_FLAGS="-mcpu=generic" \
		-DCMAKE_AR=$(BINUTILS)-ar -DCMAKE_RANLIB=$(BINUTILS)-ranlib \
		-DCMAKE_INSTALL_NAME_TOOL=$(BINUTILS)-install_name_tool -DCMAKE_NM=$(BINUTILS)-nm \
		-DCMAKE_STRIP=$(BINUTILS)-strip -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_SHARED_LIBS=ON \
		-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_NAME_DIR=/usr/lib \
		-DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON -DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_SCRIPTS=OFF -DBUILD_TESTING=OFF
	touch $@

all: $(OBJ)/configured
	ninja -C $(OBJ) liblzma xz

install: all
	$(call install_both,$(DYLIB),usr/lib)
	set -e; for d in $(SDK) $(ROOT); do \
		mv $$d/usr/lib/liblzma.5.8.1.dylib $$d/usr/lib/liblzma.5.dylib; \
		ln -sf liblzma.5.dylib $$d/usr/lib/liblzma.dylib; \
	done
	$(call copy_headers,$(SRCROOT)/src/liblzma/api/lzma.h,usr/include)
	$(call copy_tree,$(SRCROOT)/src/liblzma/api/lzma,usr/include/lzma)
	install -d $(ROOT)/usr/bin
	install -m 755 $(OBJ)/xz $(ROOT)/usr/bin/xz
	set -e; for l in unxz xzcat lzma unlzma lzcat; do ln -f $(ROOT)/usr/bin/xz $(ROOT)/usr/bin/$$l; done
	install -d $(ROOT)/usr/share/man/man1
	install -m 644 $(SRCROOT)/src/xz/xz.1 $(ROOT)/usr/share/man/man1/
	set -e; for l in unxz xzcat lzma unlzma lzcat; do ln -f $(ROOT)/usr/share/man/man1/xz.1 $(ROOT)/usr/share/man/man1/$$l.1; done

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
