# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libplatform
TAG     := libplatform-375.120.2
include ../common.mk

SOURCES := $(shell cat sources.list)

# STRING=arm64 (the default) builds arm64/'s assembly string functions.
# STRING=generic builds the generic C ones alone.
STRING ?= arm64
ifeq ($(STRING),arm64)
ASM_SOURCES := $(sort $(wildcard arm64/*.S arm64/optimized-routines/*.S))
SOURCES := $(filter-out $(addprefix src/string/generic/,bzero.c memchr.c memcmp.c \
	memcmp_zero.c memmove.c memset_pattern.c strchr.c strcmp.c strcpy.c strlen.c \
	strncmp.c strnlen.c),$(SOURCES))
else ifeq ($(STRING),generic)
ASM_SOURCES :=
else
$(error STRING is arm64 or generic, not $(STRING))
endif
STRING_STAMP := $(OBJ)/string-$(STRING)
$(shell mkdir -p $(OBJ) && rm -f $(filter-out $(STRING_STAMP),$(wildcard $(OBJ)/string-*)) && \
	touch -a $(STRING_STAMP))

OBJS    := $(patsubst %,$(OBJ)/obj/%.o,$(basename $(SOURCES) $(ASM_SOURCES)))
DYLIB   := $(OBJ)/libsystem_platform.dylib

CFLAGS = $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) -std=gnu11 \
	-fno-stack-protector -fdollars-in-identifiers -momit-leaf-frame-pointer \
	-D_FORTIFY_SOURCE=0 -DCONFIG_MTE=0 $(OSATOMIC_DEFS) \
	-I$(SRCROOT)/private -I$(SRCROOT)/include -I$(SRCROOT)/internal $(EXTRA_INCLUDES) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-Wno-int-conversion $(EXTRA_CFLAGS)
OSATOMIC_DEFS = -DOSATOMIC_USE_INLINED=0 -DOSATOMIC_DEPRECATED=0 \
	-DOSSPINLOCK_USE_INLINED=1 -DOS_UNFAIR_LOCK_INLINE=0

$(OBJ)/obj/src/os/%.o $(OBJ)/dyld/src/os/%.o: OSATOMIC_DEFS = -DOSATOMIC_USE_INLINED=0 \
	-DOSATOMIC_DEPRECATED=0 -DOSSPINLOCK_USE_INLINED=0 -DOSSPINLOCK_DEPRECATED=0
$(OBJ)/obj/src/os/%.o $(OBJ)/dyld/src/os/%.o: EXTRA_INCLUDES = -I$(SRCROOT)/src/os/resolver
$(OBJ)/obj/src/os/%.o: EXTRA_CFLAGS = -fvisibility=hidden
$(OBJ)/dyld/src/os/%.o: EXTRA_CFLAGS = -fvisibility=hidden $(DYLD_DEFS)
$(OBJ)/obj/src/atomics/%.o $(OBJ)/dyld/src/atomics/%.o: OSATOMIC_DEFS = -DOSATOMIC_USE_INLINED=0 \
	-DOSATOMIC_DEPRECATED=0

DYLD_DEFS := -DVARIANT_DYLD=1 -DVARIANT_NO_RESOLVERS=1 -DVARIANT_STATIC=1
DYLD_OBJS := $(patsubst %,$(OBJ)/dyld/%.o,$(basename $(SOURCES) $(ASM_SOURCES)))
DYLD_LIB  := $(OBJ)/libplatform_dyld.a
$(OBJ)/dyld/%.o: EXTRA_CFLAGS = $(DYLD_DEFS)

all: $(DYLIB) $(DYLD_LIB)

$(OBJ)/dyld/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/dyld/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ASFLAGS = $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) -g -I$(CURDIR)/arm64
$(OBJ)/obj/arm64/%.o: arm64/%.S arm64/asmdefs.h
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
$(OBJ)/dyld/arm64/%.o: arm64/%.S arm64/asmdefs.h
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $(DYLD_DEFS) -c $< -o $@

$(DYLD_LIB): $(DYLD_OBJS) $(STRING_STAMP) $(SOURCE_NOTICE_OBJ)
	rm -f $@
	$(LIBTOOL) -static -no_warning_for_no_symbols -o $@ $(DYLD_OBJS) $(SOURCE_NOTICE_OBJ)

$(OBJ)/obj/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/obj/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DYLIB): $(OBJS) $(STRING_STAMP)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_platform.dylib \
		-compatibility_version 1 -current_version 375.120.2 \
		-Wl,-umbrella,System -Wl,-alias_list,$(SRCROOT)/xcodeconfig/libplatform.aliases \
		$(OBJS) -L$(SDK)/usr/local/lib -lCrashReporterClient \
		-L$(SDK)/usr/lib/system -lsystem_kernel -o $@

install: $(DYLIB) $(DYLD_LIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib/dyld
	install -m 644 $(DYLD_LIB) $(SDK)/usr/local/lib/dyld/
	$(call install_man_list,$(SRCROOT)/man/manpages.lst,$(SRCROOT)/man)

installhdrs: $(SRCROOT)/.fetched
	$(call copy_tree,$(SRCROOT)/include,usr/include,-not -path './exclavekit/*' -not -name 'string_x86.h')
	$(call copy_tree,$(SRCROOT)/private,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
