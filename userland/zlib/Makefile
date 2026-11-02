# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := zlib
TAG     := zlib-100.120.1
include ../common.mk

SOURCES := AddOn/zopt_inffast.c AddOn/ZAssembly/adler32vec_arm64.s AddOn/ZAssembly/crc32lt_arm64.s \
	$(addprefix zlib/,adler32.c compress.c crc32.c deflate.c infback.c inflate.c inftrees.c \
	trees.c uncompr.c zutil.c inffast.c gzclose.c gzlib.c gzread.c gzwrite.c)
OBJS    := $(patsubst %,$(OBJ)/%.o,$(basename $(SOURCES)))
DYLIB   := $(OBJ)/libz.1.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu11 -DUSE_MMAP -DVEC_OPTIMIZE -DINFFAST_OPT -I$(SRCROOT)/zlib \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/AddOn/ZAssembly/crc32lt_arm64.o: FLAGS += -mcpu=generic+aes

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libz.1.dylib \
		-compatibility_version 1 -current_version 1.2.12 \
		-Wl,-exported_symbols_list,$(SRCROOT)/libz.exp $(OBJS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libz.1.dylib $(SDK)/usr/lib/libz.dylib
	ln -sf libz.1.dylib $(ROOT)/usr/lib/libz.dylib
	$(call install_xcode_man,$(SRCROOT)/zlib.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/zlib/zlib.h $(SRCROOT)/zlib/zconf.h,usr/include)
	install -d $(SDK)/usr/lib/pkgconfig
	install -m 644 zlib.pc $(SDK)/usr/lib/pkgconfig/

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
