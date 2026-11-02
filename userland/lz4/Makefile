# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT  := lz4
TAG      := v1.10.0
REPO_URL := https://github.com/lz4/lz4
include ../common.mk

VERSION := $(TAG:v%=%)
MAJOR   := $(firstword $(subst ., ,$(VERSION)))
DYLIB   := $(OBJ)/liblz4.$(MAJOR).dylib
SRCS    := lz4.c lz4file.c lz4frame.c lz4hc.c xxhash.c
OBJS    := $(addprefix $(OBJ)/,$(SRCS:.c=.o))
HEADERS := lz4.h lz4hc.h lz4frame.h

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/.fetched $(CURDIR)/Makefile
	@mkdir -p $(OBJ)
	$(CC) $(filter-out -Os,$(BASE_CFLAGS)) -O3 -Wall -Wextra -DXXH_NAMESPACE=LZ4_ \
		-c $(SRCROOT)/lib/$*.c -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/liblz4.$(MAJOR).dylib \
		-compatibility_version $(MAJOR) -current_version $(VERSION) $^ -o $@

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf liblz4.$(MAJOR).dylib $(SDK)/usr/lib/liblz4.dylib

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/lib/,$(HEADERS)),usr/include)
	install -d $(SDK)/usr/lib/pkgconfig
	sed -e 's|@PREFIX@|/usr|; s|@LIBDIR@|$${prefix}/lib|; s|@INCLUDEDIR@|$${prefix}/include|' \
		-e 's|@VERSION@|$(VERSION)|' $(SRCROOT)/lib/liblz4.pc.in > $(SDK)/usr/lib/pkgconfig/liblz4.pc

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
