# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := cctools
TAG     := cctools-1035.1.102
include ../common.mk

INC := $(SRCROOT)/include/mach-o

MACHO_H := reloc.h fat.h loader.h nlist.h ldsyms.h getsect.h arch.h ranlib.h stab.h swap.h

SOURCES := arch.c get_end.c getsecbyname.c getsegbyname.c i386_swap.c slot_name.c swap.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libmacho.dylib

CFLAGS := $(BASE_CFLAGS) -std=gnu11 -Wno-ambiguous-macro -I$(SRCROOT)/include

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/libmacho/%.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libmacho.dylib \
		-compatibility_version 1 -current_version 1035.1.102 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -Wl,-upward-lcompiler_rt \
		-Wl,-upward-lsystem_malloc -ldyld -Wl,-upward-lsystem_c \
		-Wl,-upward-lsystem_kernel -o $@

installhdrs: fetch
	$(call copy_headers,$(addprefix $(INC)/,$(MACHO_H)),usr/include/mach-o)
	$(call copy_headers,$(INC)/arm64/reloc.h,usr/include/mach-o/arm64)
	$(call copy_headers,$(INC)/arm/reloc.h,usr/include/mach-o/arm)
	$(call copy_headers,$(INC)/x86_64/reloc.h,usr/include/mach-o/x86_64)

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
