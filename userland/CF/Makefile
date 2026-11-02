# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := CF
TAG     := CF-1153.18
include ../common.mk

VERSION := 1153.18
HDR := $(OBJ)/include
FW  := System/Library/Frameworks/CoreFoundation.framework
SOURCES := $(notdir $(wildcard $(SRCROOT)/*.c))
OBJS := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(OBJ)/objc-gc.o
FLAGS := $(BASE_CFLAGS) -x c -std=gnu99 -fcommon -Wno-trigraphs -Wno-deprecated -fconstant-cfstrings \
	-fexceptions -DCF_BUILDING_CF=1 -DDEPLOYMENT_TARGET_MACOSX=1 \
	-DU_SHOW_DRAFT_API=1 \
	-DU_SHOW_CPLUSPLUS_API=0 -DVERSION=$(VERSION) -I$(HDR) -I$(CURDIR)/extra -I$(CURDIR)/objc-gc \
	-isystem $(SDK)/usr/local/include -include $(SRCROOT)/CoreFoundation_Prefix.h
DYLIB := $(OBJ)/CoreFoundation

PUBLIC_HEADERS := CFArray.h CFBag.h CFBase.h CFBinaryHeap.h CFBitVector.h CFBundle.h CFByteOrder.h \
	CFCalendar.h CFCharacterSet.h CFData.h CFDate.h CFDateFormatter.h CFDictionary.h CFError.h \
	CFLocale.h CFMessagePort.h CFNumber.h CFNumberFormatter.h CFPlugIn.h CFPlugInCOM.h \
	CFPreferences.h CFPropertyList.h CFRunLoop.h CFSet.h CFSocket.h CFStream.h CFString.h \
	CFStringEncodingExt.h CFTimeZone.h CFTree.h CFURL.h CFURLAccess.h CFUUID.h \
	CFUserNotification.h CFXMLNode.h CFXMLParser.h CFAvailability.h CFUtilities.h CoreFoundation.h \
	CFMachPort.h
PRIVATE_HEADERS := CFBundlePriv.h CFCharacterSetPriv.h CFError_Private.h CFLogUtilities.h CFPriv.h \
	CFRuntime.h CFStorage.h CFStreamAbstract.h CFStreamPriv.h CFStreamInternal.h \
	CFStringDefaultEncoding.h CFStringEncodingConverter.h CFStringEncodingConverterExt.h \
	CFUniChar.h CFUnicodeDecomposition.h CFUnicodePrecomposition.h ForFoundationOnly.h \
	CFBurstTrie.h CFICULogging.h

all: $(DYLIB)

$(HDR)/.copied: $(SRCROOT)/.fetched
	mkdir -p $(HDR)/CoreFoundation
	cp $(SRCROOT)/*.h $(HDR)/CoreFoundation/
	touch $@

$(OBJ)/%.o: $(SRCROOT)/%.c $(HDR)/.copied
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/objc-gc.o: objc-gc/objc-gc.c $(wildcard objc-gc/*.h objc-gc/objc/*.h)
	@mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -I$(CURDIR)/objc-gc -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /$(FW)/Versions/A/CoreFoundation \
		-Wl,-init,___CFInitialize -compatibility_version 150 -current_version $(VERSION) \
		-Wl,-alias_list,$(SRCROOT)/SymbolAliases \
		-Wl,-sectcreate,__UNICODE,__csbitmaps,$(SRCROOT)/CFCharacterSetBitmaps.bitmap \
		-Wl,-sectcreate,__UNICODE,__properties,$(SRCROOT)/CFUniCharPropertyDatabase.data \
		-Wl,-sectcreate,__UNICODE,__data,$(SRCROOT)/CFUnicodeData-L.mapping \
		-Wl,-segprot,__UNICODE,r,r $(OBJS) -licucore -o $@

define framework
	rm -rf $(1)/$(FW)
	install -d $(1)/$(FW)/Versions/A/Resources $(1)/$(FW)/Versions/A/Headers \
		$(1)/$(FW)/Versions/A/PrivateHeaders
	ln -sf A $(1)/$(FW)/Versions/Current
	ln -sf Versions/Current/Resources $(1)/$(FW)/Resources
	ln -sf Versions/Current/Headers $(1)/$(FW)/Headers
	ln -sf Versions/Current/PrivateHeaders $(1)/$(FW)/PrivateHeaders
	ln -sf Versions/Current/CoreFoundation $(1)/$(FW)/CoreFoundation
	install -m 644 $(SRCROOT)/Info.plist $(1)/$(FW)/Versions/A/Resources/
	install -m 755 $(DYLIB) $(1)/$(FW)/Versions/A/CoreFoundation
endef
install: all
	$(call framework,$(SDK))
	$(call framework,$(ROOT))
	cd $(SRCROOT) && install -m 644 $(PUBLIC_HEADERS) $(SDK)/$(FW)/Versions/A/Headers/
	cd $(SRCROOT) && install -m 644 $(PRIVATE_HEADERS) $(SDK)/$(FW)/Versions/A/PrivateHeaders/

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
