# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_kernel
include ../common.mk

SRC      := $(PIE)/xnu/libsyscall
PRODUCTS := $(OBJ)/products
MIGHDR   := $(PRODUCTS)/mig_hdr
STATIC_SOURCES  := $(shell cat static.list)
DYNAMIC_SOURCES := $(shell cat dynamic.list)

CFLAGS := $(BASE_CFLAGS) -std=gnu11 \
	-fdollars-in-identifiers -fno-stack-protector -fno-stack-check -fno-builtin-calloc \
	-DLIBSYSCALL_INTERFACE -D__DARWIN_VERS_1050=1 -DNO_SYSCALL_LEGACY \
	-DCF_OPEN_SOURCE -DCF_EXCLUDE_CSTD_HEADERS -DDEBUG -D_FORTIFY_SOURCE=0 \
	-Wno-int-conversion \
	-I$(SRC) -I$(SRC)/mach -I$(SRC)/os -I$(SRC)/wrappers -I$(SRC)/wrappers/string \
	-I$(SRC)/wrappers/libproc -I$(SRC)/wrappers/libproc/spawn \
	-I$(PRODUCTS)/internal_hdr/include -I$(MIGHDR)/local/include -I$(MIGHDR)/include \
	-I$(OBJ)/mig -I$(OBJ)/extra_include \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders
CFLAGS := $(filter-out -fstack-protector-strong,$(CFLAGS))
MIGFLAGS := -novouchers -arch $(ARCH) \
	-I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-I$(SDK)/usr/local/include -I$(SDK)/usr/include -DKOBJECT_SERVER

DEFS     := $(filter %.defs,$(STATIC_SOURCES))
MIG_C    := $(patsubst %.defs,$(OBJ)/mig/%User.c,$(notdir $(DEFS))) $(OBJ)/mig/excServer.c
STATIC_OBJS := $(patsubst %,$(OBJ)/obj/%.o,$(basename $(filter-out %.defs,$(STATIC_SOURCES)))) \
	$(patsubst $(OBJ)/mig/%.c,$(OBJ)/obj/mig/%.o,$(MIG_C))
DYNAMIC_OBJS := $(patsubst %,$(OBJ)/dynobj/%.o,$(basename $(DYNAMIC_SOURCES)))

DYLIB := $(OBJ)/libsystem_kernel.dylib

all: $(DYLIB)

$(MIGHDR)/.done:
	install -D -m 644 $(PIE)/xnu/osfmk/arm64/machine_machdep.h $(OBJ)/extra_include/arm64/machine_machdep.h
	mkdir -p $(OBJ)/migobj $(PRODUCTS)
	cd $(SRC) && env OBJROOT=$(OBJ)/migobj SRCROOT=$(SRC) BUILT_PRODUCTS_DIR=$(PRODUCTS) \
		SDKROOT=$(SDK) ARCHS=$(ARCH) PLATFORM_NAME=macosx \
		bash xcodescripts/mach_install_mig.sh
	touch $@

$(OBJ)/sys/stubs.list:
	mkdir -p $(OBJ)/sys
	ARCHS=$(ARCH) perl $(SRC)/xcodescripts/create-syscalls.pl $(SRC)/../bsd/kern/syscalls.master \
		$(SRC)/custom $(SRC)/Platforms MacOSX $(OBJ)/sys

$(OBJ)/syscalls.a: $(OBJ)/sys/stubs.list $(MIGHDR)/.done
	mkdir -p $(OBJ)/sysobj
	set -e; objs=; for s in $$(cat $<); do \
		o=$(OBJ)/sysobj/$$(basename $$s .s).o; \
		$(CC) -x assembler-with-cpp -c $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) \
			-I$(SDK)/usr/include -I$(SDK)/usr/local/include \
			-I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
			$$s -o $$o; objs="$$objs $$o"; \
	done; $(LIBTOOL) -static -o $@ $$objs

$(OBJ)/mig/%User.c: $(SRC)/mach/%.defs $(MIGHDR)/.done
	mkdir -p $(OBJ)/mig
	cd $(OBJ)/mig && $(MIG) $(MIGFLAGS) -header $*.h -user $*User.c -sheader /dev/null -server /dev/null $<
$(OBJ)/mig/%User.c: $(SRC)/mach/servers/%.defs $(MIGHDR)/.done
	mkdir -p $(OBJ)/mig
	cd $(OBJ)/mig && $(MIG) $(MIGFLAGS) -header $*.h -user $*User.c -sheader /dev/null -server /dev/null $<
$(OBJ)/mig/excServer.c: $(SRC)/mach/exc.defs $(OBJ)/mig/excUser.c
	cd $(OBJ)/mig && $(MIG) $(MIGFLAGS) -header /dev/null -user /dev/null -sheader excServer.h -server excServer.c $<

$(OBJ)/obj/%.o: $(SRC)/%.c $(MIGHDR)/.done $(MIG_C)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/obj/%.o: $(SRC)/%.s $(MIGHDR)/.done
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/obj/mig/%.o: $(OBJ)/mig/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/dynobj/%.o: $(SRC)/%.c $(MIGHDR)/.done
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/libsystem_kernel.a: $(STATIC_OBJS)
	$(LIBTOOL) -static -o $@ $^

$(DYLIB): $(DYNAMIC_OBJS) $(OBJ)/libsystem_kernel.a $(OBJ)/syscalls.a
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_kernel.dylib \
		-compatibility_version 1 -current_version 12377.121.6 \
		-Wl,-umbrella,System \
		$(DYNAMIC_OBJS) -Wl,-all_load $(OBJ)/libsystem_kernel.a $(OBJ)/syscalls.a \
		-o $@

PUBLIC_HEADERS  := $(addprefix $(SRC)/,$(shell awk -F'\t' '$$2 == "Public" {print $$1}' headers.list))
PRIVATE_HEADERS := $(addprefix $(SRC)/,$(shell awk -F'\t' '$$2 == "Private" {print $$1}' headers.list))

installhdrs: $(MIGHDR)/.done
	$(call copy_headers,$(PUBLIC_HEADERS),usr/include)
	$(call copy_headers,$(PRIVATE_HEADERS),usr/local/include)
	install -d $(SDK)/usr/include/mach $(SDK)/usr/local/include/mach $(SDK)/usr/include/servers
	cp -f $(MIGHDR)/include/mach/*.h $(SDK)/usr/include/mach/
	cp -f $(MIGHDR)/include/servers/*.h $(SDK)/usr/include/servers/
	cp -f $(MIGHDR)/local/include/mach/*.h $(SDK)/usr/local/include/mach/
	$(call copy_headers,$(SRC)/os/proc.h,usr/include/os)
	$(call copy_headers,$(SRC)/os/tsd.h,usr/local/include/os)
	$(call copy_headers,$(SRC)/mach/mach/vm_page_size.h,usr/local/include/mach)
	$(call copy_headers,$(SRC)/wrappers/libproc/libproc.h,usr/include)

$(OBJ)/dyld/libsystem_kernel.a: $(OBJ)/libsystem_kernel.a $(OBJ)/syscalls.a $(SOURCE_NOTICE_OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	$(LIBTOOL) -static -no_warning_for_no_symbols -o $@ $^

XNU_MAN := $(OBJ)/xnu-man
$(XNU_MAN)/.done:
	rm -rf $(XNU_MAN)
	cd $(PIE)/xnu && $(TOOLCHAIN)/xnu-make ARCH_CONFIGS=ARM64 BUILD_LTO=0 KERNEL_CONFIGS=RELEASE \
		MACHINE_CONFIGS=VMAPPLE ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 OBJROOT=$(XNU_MAN)/obj \
		SYMROOT=$(XNU_MAN)/sym DSTROOT=$(XNU_MAN)/dst install_textfiles > $(XNU_MAN).log
	touch $@

install: $(DYLIB) $(OBJ)/dyld/libsystem_kernel.a installhdrs $(XNU_MAN)/.done
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib/dyld
	install -m 644 $(OBJ)/dyld/libsystem_kernel.a $(SDK)/usr/local/lib/dyld/
	install -d $(ROOT)/usr/share/man
	cp -R $(XNU_MAN)/dst/usr/share/man/. $(ROOT)/usr/share/man/

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
.SECONDARY:
