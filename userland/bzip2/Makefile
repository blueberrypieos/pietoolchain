# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := bzip2
TAG     := bzip2-49
include ../common.mk

S        := $(SRCROOT)/bzip2
LIB_SRCS := blocksort.c bzlib.c compress.c crctable.c decompress.c arm64/crc32vec.s huffman.c randtable.c
LIB_OBJS := $(patsubst %,$(OBJ)/lib/%.o,$(basename $(LIB_SRCS)))
DYLIB    := $(OBJ)/libbz2.1.0.dylib
FLAGS    := $(BASE_CFLAGS) -I$(S) -isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders

all: $(DYLIB) $(OBJ)/bzip2 $(OBJ)/bzip2recover

$(OBJ)/lib/%.o: $(S)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/lib/arm64/crc32vec.o: $(S)/arm64/crc32vec.s $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -mcpu=generic+aes -c $< -o $@

$(DYLIB): $(LIB_OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libbz2.1.0.dylib \
		-compatibility_version 1 -current_version 1.0.8 \
		-Wl,-unexported_symbols_list,$(SRCROOT)/unexports $(LIB_OBJS) -o $@

$(OBJ)/%.tool.o: $(S)/%.c
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%: $(OBJ)/%.tool.o $(DYLIB)
	$(CC) $(BASE_LDFLAGS) $< $(DYLIB) -o $@

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libbz2.1.0.dylib $(SDK)/usr/lib/libbz2.dylib
	ln -sf libbz2.1.0.dylib $(ROOT)/usr/lib/libbz2.dylib
	install -d $(ROOT)/usr/bin
	install -m 755 $(OBJ)/bzip2 $(OBJ)/bzip2recover $(ROOT)/usr/bin/
	ln -f $(ROOT)/usr/bin/bzip2 $(ROOT)/usr/bin/bunzip2
	ln -f $(ROOT)/usr/bin/bzip2 $(ROOT)/usr/bin/bzcat
	install -m 755 $(S)/bzdiff $(S)/bzmore $(ROOT)/usr/bin/
	ln -sf bzdiff $(ROOT)/usr/bin/bzcmp
	ln -sf bzmore $(ROOT)/usr/bin/bzless
	$(call install_xcode_man,$(SRCROOT)/bzip2.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(S)/bzlib.h,usr/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
