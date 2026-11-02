# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := openlibm
TAG     := v0.8.8
REPO_URL := https://github.com/JuliaMath/openlibm
SRCROOT  = $(PIE)/build/src/openlibm
include ../common.mk

SOURCES := $(shell cat sources.list)
OLM_OBJS := $(patsubst %,$(OBJ)/olm/%.o,$(basename $(SOURCES)))
OBJS  := $(OLM_OBJS) $(OBJ)/fenv.o $(OBJ)/darwin.o
DYLIB := $(OBJ)/libsystem_m.dylib

OLM_CFLAGS := $(filter-out -Os,$(BASE_CFLAGS)) -O2 -fno-builtin -fno-strict-aliasing \
	-std=c99 -I$(SRCROOT) -I$(SRCROOT)/include -I$(SRCROOT)/aarch64 -I$(SRCROOT)/src \
	-I$(SRCROOT)/ld128 -DASSEMBLER -D__BSD_VISIBLE -Wno-implicit-function-declaration
CFLAGS := $(filter-out -Os,$(BASE_CFLAGS)) -O2 -std=gnu11 -Wall -Wextra

all: $(DYLIB)

$(OBJ)/olm/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(OLM_CFLAGS) -c $< -o $@
$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/aliases.list: sources.list $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	cd $(SRCROOT) && cat $(SOURCES) | \
		grep -oE 'openlibm_(weak|strong)_reference\( *[A-Za-z0-9_]+ *, *[A-Za-z0-9_]+ *\)' | \
		sed -E 's/.*\( *([A-Za-z0-9_]+) *, *([A-Za-z0-9_]+) *\)/_\1 _\2/' | sort -u > $@

$(DYLIB): $(OBJS) $(OBJ)/aliases.list
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_m.dylib \
		-compatibility_version 1 -current_version 0.8.8 -Wl,-umbrella,System \
		$(OBJS) -Wl,-alias_list,$(OBJ)/aliases.list \
		-L$(SDK)/usr/lib/system -lcompiler_rt -Wl,-upward-lsystem_c -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)
	$(MAKE) -C ../Libm fetch
	$(call install_xcode_man,$(PIE)/build/src/Libm/Libm.xcodeproj)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
