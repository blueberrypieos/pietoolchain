# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Libsystem
TAG     := Libsystem-1356
include ../common.mk

LSYS   := $(SDK)/usr/lib/system
CONFIG := $(OBJ)/config.h
ARGS   := $(OBJ)/linker_arguments.txt
DYLIB  := $(OBJ)/libSystem.B.dylib
OBJS   := $(OBJ)/init.o $(OBJ)/CompatibilityHacks.o

all: $(DYLIB)

$(CONFIG) $(ARGS) &: $(SRCROOT)/.fetched $(wildcard $(LSYS)/*.dylib)
	@mkdir -p $(OBJ)
	: > $(CONFIG); : > $(ARGS); : > $(OBJ)/missing.txt
	set -e; cat $(SRCROOT)/requiredlibs $(SRCROOT)/optionallibs | sort | while read line; do \
		found=; \
		for lib in $$line; do \
			if [ -e $(LSYS)/lib$$lib.dylib ] && ! [ -e $(LSYS)/lib$$lib.dylib.stub ]; then \
				found=$$lib; break; \
			fi; \
		done; \
		if [ -n "$$found" ]; then \
			echo "#define HAVE_$$(echo $$found | sed 's/_sim//' | tr a-z A-Z) 1" >> $(CONFIG); \
			echo "-Wl,-reexport-l$$found" >> $(ARGS); \
		elif grep -qx "$$line" $(SRCROOT)/requiredlibs; then \
			echo "$$line" >> $(OBJ)/missing.txt; \
		fi; \
	done
	@if [ -s $(OBJ)/missing.txt ]; then echo "libSystem: missing required members:" $$(cat $(OBJ)/missing.txt); fi

CFLAGS := $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) -fno-stack-protector \
	-std=gnu11 -include $(CONFIG) -DCURRENT_VARIANT_normal=1 \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders

$(OBJ)/%.o: $(SRCROOT)/%.c $(CONFIG)
	$(CC) $(CFLAGS) -c $< -o $@

$(DYLIB): $(OBJS) $(ARGS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nodefaultlibs -Wl,-search_paths_first \
		-install_name /usr/lib/libSystem.B.dylib \
		-compatibility_version 1 -current_version 1356 \
		$(OBJS) -L$(LSYS) $$(cat $(ARGS)) -o $@

BSD_LIBS := c info m pthread dbm poll dl rpcsvc proc gcc_s.1

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib)
	for d in $(SDK) $(ROOT); do \
		ln -sf libSystem.B.dylib $$d/usr/lib/libSystem.dylib; \
		for l in $(BSD_LIBS); do ln -sf libSystem.dylib $$d/usr/lib/lib$$l.dylib; done; \
	done

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
