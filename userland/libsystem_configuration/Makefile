# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_configuration
REPO    := configd
TAG     := configd-1405.120.5
include ../common.mk

DYLIB := $(OBJ)/libsystem_configuration.dylib

all: $(DYLIB)

installhdrs: $(SRCROOT)/.fetched
	install -d $(SDK)/usr/local/include
	install -m 644 $(SRCROOT)/dnsinfo/dnsinfo.h $(SDK)/usr/local/include/

$(OBJ)/configuration.o: configuration.c installhdrs
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -isystem $(SDK)/usr/local/include -c $< -o $@

$(DYLIB): $(OBJ)/configuration.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_configuration.dylib \
		-compatibility_version 1 -current_version 1405 -Wl,-umbrella,System \
		$< -L$(SDK)/usr/lib/system -lsystem_kernel -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs install clean
