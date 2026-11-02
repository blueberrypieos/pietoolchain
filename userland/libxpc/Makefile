# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libxpc
include ../common.mk

SOURCES := object.c plist.c connection.c bootstrap.c services.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libxpc.dylib

all: $(DYLIB)

$(OBJ)/%.o: %.c internal.h pie_bootstrap.h
	@mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -fblocks -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib -install_name /usr/lib/system/libxpc.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -lsystem_c -lsystem_malloc -lsystem_kernel \
		-lsystem_platform -lsystem_blocks -ldispatch -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

installhdrs:
	install -d $(SDK)/usr/local/include/pie
	install -m 644 pie_bootstrap.h $(SDK)/usr/local/include/pie/bootstrap.h

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
