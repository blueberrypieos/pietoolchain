# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libmalloc
TAG     := libmalloc-812.100.31
include ../common.mk

SOURCES := $(filter-out %.d,$(shell cat sources.list))
OBJS    := $(patsubst %,$(OBJ)/obj/%.o,$(basename $(SOURCES)))
DTRACE_H := $(OBJ)/dtrace/magmallocProvider.h
DYLIB   := $(OBJ)/libsystem_malloc.dylib

CFLAGS := $(BASE_CFLAGS) -std=gnu11 -momit-leaf-frame-pointer \
	-D_FORTIFY_SOURCE=0 -DOSATOMIC_USE_INLINED=1 -DOS_UNFAIR_LOCK_INLINE=1 \
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DNDEBUG \
	-DOS_VARIANT_RESOLVED=1 -DOS_VARIANT_NOTRESOLVED=1 \
	-I$(OBJ)/dtrace -I$(SRCROOT)/include -I$(SRCROOT)/private -I$(SRCROOT)/resolver \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-Wno-format-invalid-specifier -Wno-format-extra-args

LDLIBS := -L$(SDK)/usr/local/lib -lCrashReporterClient \
	-L$(SDK)/usr/lib/system -lcompiler_rt -ldyld -lsystem_kernel \
	-lsystem_platform -lsystem_pthread -Wl,-upward-lsystem_featureflags \
	-Wl,-upward-lsystem_c -Wl,-upward-lsystem_blocks -Wl,-upward-lcorecrypto

all: $(DYLIB)

$(DTRACE_H): $(SRCROOT)/src/magmallocProvider.d
	mkdir -p $(dir $@)
	$(PIE)/userland/tools/dtrace-h.py $< $@

$(OBJ)/obj/%.o: $(SRCROOT)/%.c $(DTRACE_H)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/obj/%.o: $(SRCROOT)/%.s $(DTRACE_H)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_malloc.dylib \
		-compatibility_version 1 -current_version 812.100.31 \
		-Wl,-umbrella,System \
		-Wl,-interposable_list,$(SRCROOT)/xcodeconfig/interposable.list \
		$(OBJS) $(LDLIBS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/libmalloc.xcodeproj)

installhdrs: $(SRCROOT)/.fetched
	$(call copy_tree,$(SRCROOT)/include,usr/include)
	$(call copy_tree,$(SRCROOT)/private,usr/local/include,-not -name '*exclaves*')

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
