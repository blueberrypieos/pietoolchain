# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_sandbox
include ../common.mk

DYLIB := $(OBJ)/libsystem_sandbox.dylib

all: $(DYLIB)

$(OBJ)/sandbox.o: sandbox.c sandbox.h sandbox_private.h rootless.h | installhdrs
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJ)/sandbox.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_sandbox.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$< -L$(SDK)/usr/lib/system -lsystem_malloc -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)

installhdrs:
	install -d $(SDK)/usr/include $(SDK)/usr/local/include/sandbox
	install -m 644 sandbox.h $(SDK)/usr/include/sandbox.h
	install -m 644 sandbox_private.h $(SDK)/usr/local/include/sandbox/private.h
	install -m 644 rootless.h $(SDK)/usr/local/include/rootless.h

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
