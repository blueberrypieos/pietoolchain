# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_featureflags
include ../common.mk

DYLIB := $(OBJ)/libsystem_featureflags.dylib

all: $(DYLIB)

$(OBJ)/featureflags.o: featureflags.c
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJ)/featureflags.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_featureflags.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$< -L$(SDK)/usr/lib/system -lsystem_kernel -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
