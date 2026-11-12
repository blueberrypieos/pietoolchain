# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := hfs
TAG     := hfs-715.120.4
include ../common.mk
SDKROOT := $(SDK)

TOOLS := mount_hfs newfs_hfs fsck_hfs
mount_hfs_SRCS := mount_hfs/mount_hfs.c
mount_hfs_DIR := sbin
mount_hfs_LIBS := -lutil
newfs_hfs_SRCS := newfs_hfs/newfs_hfs.c newfs_hfs/makehfs.c newfs_hfs/hfs_endian.c
newfs_hfs_DIR := sbin
newfs_hfs_LIBS := -lutil
FSCK_DFALIB := BlockCache BTree BTreeAllocate BTreeMiscOps BTreeNodeOps BTreeScanner \
	BTreeTreeOps CatalogCheck dirhardlink HardLinkCheck hfs_endian SAllocate SBTree \
	SCatalog SControl SDevice SExtents SKeyCompare SRebuildBTree SRepair SStubs SUtils \
	SVerify1 SVerify2 VolumeBitmapCheck
fsck_hfs_SRCS := $(addprefix fsck_hfs/,fsck_hfs.c fsck_messages.c utilities.c) \
	$(addprefix lib_fsck_hfs/,cache.c check.c lib_fsck_hfs.c fsck_debug.c fsck_strings.c \
	fsck_hfs_strings.c fsck_journal.c) \
	$(addprefix lib_fsck_hfs/dfalib/,$(addsuffix .c,$(FSCK_DFALIB))) fsck_hfs_vers.c
fsck_hfs_DIR := sbin
fsck_hfs_FLAGS := -DBSD=1 -DCONFIG_HFS_TRIM=1 -DDEBUG_BUILD=0 -include stdarg.h -include unistd.h -fcommon \
	-I$(SRCROOT)/fsck_hfs -I$(SRCROOT)/lib_fsck_hfs -I$(SRCROOT)/lib_fsck_hfs/dfalib
fsck_hfs_LIBS := -lutil
TOOLS_LDFLAGS := -L$(SDK)/usr/local/lib

include ../tools/tools.mk

$(OBJ)/fsck_hfs/fsck_hfs_vers.c:
	@mkdir -p $(dir $@)
	printf 'const unsigned char fsck_hfsVersionString[] = "@(#)PROGRAM:fsck_hfs  PROJECT:$(TAG)\\n";\nconst double fsck_hfsVersionNumber = 0;\n' > $@

all: installhdrs $(TOOL_BINS)

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/core/,hfs_format.h hfs_mount.h hfs_unistr.h),usr/include/hfs)

install: install-tools
	$(call install_xcode_man,$(SRCROOT)/hfs.xcodeproj,$(TOOLS))

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs install clean
