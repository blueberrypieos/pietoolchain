# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := OpenPAM
TAG     := OpenPAM-35
include ../common.mk

S       := $(SRCROOT)/openpam
SOURCES := $(shell python3 ../tools/pbxproj-sources.py $(SRCROOT)/openpam.xcodeproj "Build Library" 2>/dev/null)
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
FLAGS   := $(BASE_CFLAGS) -DLIB_MAJ=2 -DOPENPAM_MODULES_DIR='"/usr/lib/pam/;/usr/local/lib/pam/"' \
	-D_GNU_SOURCE -D__APPLE_MDM_SUPPORT__ -I$(S)/include -I$(S)/lib \
	-isystem $(SDK)/usr/local/include
DYLIB   := $(OBJ)/libpam.2.dylib
MODULES := $(addprefix $(OBJ)/,pam_deny.so.2 pam_permit.so.2 pam_unix.so.2)

all: $(DYLIB) $(MODULES)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libpam.2.dylib \
		-compatibility_version 3 -current_version 3 $(OBJS) -o $@

define module
$(OBJ)/$(1).so.2: $(OBJ)/openpam/modules/$(1)/$(1).o $(DYLIB)
	$$(CC) $$(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/pam/$(1).so.2 $$^ -o $$@
endef
$(foreach m,pam_deny pam_permit pam_unix,$(eval $(call module,$(m))))

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libpam.2.dylib $(SDK)/usr/lib/libpam.dylib
	ln -sf libpam.2.dylib $(ROOT)/usr/lib/libpam.dylib
	install -d $(ROOT)/usr/lib/pam $(ROOT)/etc/pam.d
	install -m 755 $(MODULES) $(ROOT)/usr/lib/pam/
	install -m 444 $(SRCROOT)/pam.d/other $(ROOT)/etc/pam.d/
	$(call install_xcode_man,$(SRCROOT)/openpam.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(wildcard $(S)/include/security/*.h),usr/include/security)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
