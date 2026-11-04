# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := IOKitUser
TAG     := IOKitUser-100231.120.3
include ../common.mk

FW      := System/Library/Frameworks/IOKit.framework
GEN     := $(OBJ)/derived
SOURCES := IOKitLib.c IOCFSerialize.c IOCFUnserialize.tab.c IOCFURLAccess.c \
	IODataQueueClient.c IOCircularDataQueue.c IOMIGMachPort.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(OBJ)/iokitmig64.o $(OBJ)/IOTrap.o \
	$(OBJ)/IOPowerSources.o
FLAGS   := $(BASE_CFLAGS) -DAPPLE -D_ANSI_C_SOURCE -Dvolatile=__volatile -DHAVE_CFPLUGIN \
	-DHAVE_CFURLACCESS -I$(GEN) -I$(SRCROOT) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include -Wno-deprecated-declarations -Wno-error=int-conversion
DYLIB   := $(OBJ)/IOKit
PUBLIC_HEADERS := IOCFSerialize.h IOCFUnserialize.h IOCFPlugIn.h IOCFURLAccess.h IOCFBundle.h \
	IODataQueueClient.h IOKitLib.h
PRIVATE_HEADERS := IOKitLibPrivate.h IOMIGMachPort.h
PS_HEADERS := IOPowerSources.h IOPSKeys.h

all: $(DYLIB)

$(GEN)/iokitmig64.c: $(SDK)/usr/include/device/device.defs $(SRCROOT)/.fetched
	@mkdir -p $(GEN)
	$(MIG) -arch $(ARCH) -I$(SDK)/usr/include -DIOKIT -D__LP64__ -novouchers -DKOBJECT_SERVER -server /dev/null \
		-header $(GEN)/iokitmig64.h -user $@ $<
$(GEN)/iokitmig64.h: $(GEN)/iokitmig64.c

$(OBJ)/%.o: $(SRCROOT)/%.c $(GEN)/iokitmig64.h | installhdrs
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/IOTrap.o: $(SRCROOT)/IOTrap.s $(SRCROOT)/.fetched
	$(CC) $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) \
		-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders -c $< -o $@
$(OBJ)/iokitmig64.o: $(GEN)/iokitmig64.c
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/IOPowerSources.o: extra/IOPowerSources.c | installhdrs
	$(CC) $(FLAGS) -I$(SRCROOT)/ps.subproj -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /$(FW)/Versions/A/IOKit \
		-compatibility_version 1 -current_version 275 $(OBJS) \
		-F$(SDK)/System/Library/Frameworks -framework CoreFoundation -o $@

define framework
	install -d $(1)/$(FW)/Versions/A/Resources
	ln -sfn A $(1)/$(FW)/Versions/Current
	ln -sfn Versions/Current/IOKit $(1)/$(FW)/IOKit
	ln -sfn Versions/Current/Resources $(1)/$(FW)/Resources
	install -m 644 $(SRCROOT)/CustomInfo.plist $(1)/$(FW)/Versions/A/Resources/Info.plist
	install -m 755 $(DYLIB) $(1)/$(FW)/Versions/A/IOKit
endef
install: all
	$(call framework,$(SDK))
	$(call framework,$(ROOT))

installhdrs: fetch
	install -d $(SDK)/$(FW)/Versions/A/Headers/ps $(SDK)/$(FW)/Versions/A/PrivateHeaders
	cd $(SRCROOT) && install -m 644 $(PUBLIC_HEADERS) $(SDK)/$(FW)/Versions/A/Headers/
	cd $(SRCROOT)/ps.subproj && install -m 644 $(PS_HEADERS) $(SDK)/$(FW)/Versions/A/Headers/ps/
	cd $(SRCROOT) && install -m 644 $(PRIVATE_HEADERS) $(SDK)/$(FW)/Versions/A/PrivateHeaders/

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
