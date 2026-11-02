# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libmd
TAG     := libmd-7
include ../common.mk

GEN     := $(OBJ)/gen
HL      := md4 md5 sha sha1 sha224 sha256 sha384 sha512
OBJS    := $(patsubst %,$(OBJ)/%hl.o,$(HL)) $(OBJ)/sha0c.o
DYLIB   := $(OBJ)/libmd.dylib
FLAGS   := $(BASE_CFLAGS) -I$(SRCROOT)/include -I$(SRCROOT)/libmd -isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(GEN)/.done: $(SRCROOT)/.fetched
	mkdir -p $(GEN)
	env SRCROOT=$(SRCROOT) BUILT_PRODUCTS_DIR=$(GEN) sh $(SRCROOT)/xcodescripts/generate_sources.sh
	touch $@
$(patsubst %,$(GEN)/%hl.c,$(HL)): $(GEN)/.done ;

$(OBJ)/%.o: $(GEN)/%.c
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/sha0c.o: $(SRCROOT)/libmd/sha0c.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libmd.dylib \
		-compatibility_version 1 -current_version 1 $(OBJS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/libmd/,md4.h md5.h sha.h) \
		$(addprefix $(SRCROOT)/include/,sha224.h sha256.h sha384.h sha512.h),usr/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
