# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT  := xxhash
TAG      := v0.8.4
REPO_URL := https://github.com/Cyan4973/xxHash
include ../common.mk

VERSION := $(TAG:v%=%)
MAJOR   := $(firstword $(subst ., ,$(VERSION)))
DYLIB   := $(OBJ)/libxxhash.$(MAJOR).dylib

all: $(DYLIB)

$(OBJ)/xxhash.o: $(SRCROOT)/.fetched $(CURDIR)/Makefile
	@mkdir -p $(OBJ)
	$(CC) $(filter-out -Os,$(BASE_CFLAGS)) -O3 -std=c99 -Wall -Wextra \
		-c $(SRCROOT)/xxhash.c -o $@

$(DYLIB): $(OBJ)/xxhash.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libxxhash.$(MAJOR).dylib \
		-compatibility_version $(MAJOR) -current_version $(VERSION) $< -o $@

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libxxhash.$(MAJOR).dylib $(SDK)/usr/lib/libxxhash.dylib

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/xxhash.h $(SRCROOT)/xxh3.h,usr/include)
	install -d $(SDK)/usr/lib/pkgconfig
	sed -e 's|@PREFIX@|/usr|; s|@EXECPREFIX@|$${prefix}|; s|@INCLUDEDIR@|$${prefix}/include|' \
		-e 's|@LIBDIR@|$${exec_prefix}/lib|; s|@VERSION@|$(VERSION)|' \
		$(SRCROOT)/libxxhash.pc.in > $(SDK)/usr/lib/pkgconfig/libxxhash.pc

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
