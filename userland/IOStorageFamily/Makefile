# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := IOStorageFamily
TAG     := IOStorageFamily-337.100.1
include ../common.mk

HDRS := IOStorage.h IOMedia.h IOBlockStorageDevice.h IOBlockStorageDriver.h \
	IOMediaBSDClient.h IOPartitionScheme.h IOFilterScheme.h IOGUIDPartitionScheme.h \
	IOFDiskPartitionScheme.h IOApplePartitionScheme.h IOAppleLabelScheme.h \
	IOStorageCardCharacteristics.h IOStorageControllerCharacteristics.h \
	IOStorageDeviceCharacteristics.h IOStorageProtocolCharacteristics.h

all: installhdrs

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/,$(HDRS)),System/Library/Frameworks/IOKit.framework/Headers/storage)

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs clean
