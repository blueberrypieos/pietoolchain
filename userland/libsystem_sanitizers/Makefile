# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_sanitizers
include ../common.mk

DYLIB := $(OBJ)/libsystem_sanitizers.dylib

all: $(DYLIB)

$(OBJ)/sanitizers.o: sanitizers.c
	@mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJ)/sanitizers.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_sanitizers.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$< -L$(SDK)/usr/lib/system -lsystem_kernel -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
