#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libhfs_kext.a, the HFS+ filesystem (hfs's core/ and hfs_encodings/),
# into the kernel SDK's usr/local/lib/kernel. Needs xnu's headers in the SDK
# (make-sdk.sh xnu-hdrs) and PIE_KERNEL_SOURCES/hfs (kernel-build-common.sh).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=libhfs_kext
ARCH="${ARCH:-arm64}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup hfs
KF="$SDK/System/Library/Frameworks/Kernel.framework"
SOURCES="BTree.c BTreeAllocate.c BTreeMiscOps.c BTreeNodeOps.c BTreeNodeReserve.c
	BTreeScanner.c BTreeTreeOps.c BTreeWrapper.c CatalogUtilities.c FileExtentMapping.c
	FileIDsServices.c MacOSStubs.c UnicodeWrappers.c VolumeAllocation.c hfs_attrlist.c
	hfs_btreeio.c hfs_catalog.c hfs_chash.c hfs_cnode.c hfs_cprotect.c hfs_endian.c
	hfs_extents.c hfs_fsinfo.c hfs_hotfiles.c hfs_iokit.cpp hfs_journal.c
	hfs_link.c hfs_lookup.c hfs_notification.c hfs_quota.c hfs_readwrite.c hfs_resize.c
	hfs_search.c hfs_vfsops.c hfs_vfsutils.c hfs_vnops.c hfs_xattr.c rangelist.c"
ENCODING_SOURCES="hfs_encodinghint.c hfs_encodings.c"
COMMON=(-arch "$ARCH" -mcpu=generic -isysroot "$SDK" -mmacosx-version-min=26.0
	-Os -g -mkernel -nostdinc
	-DKERNEL=1 -DKERNEL_PRIVATE=1 -DPRIVATE=1 -DAPPLE -DNeXT
	-include "$SRC/core/kext-config.h"
	-I"$SRC/core" -I"$KF/PrivateHeaders" -I"$KF/Headers"
	-idirafter "$SDK/usr/local/include/kernel" -idirafter "$SDK/usr/include"
	-Wno-error=int-conversion)
mkdir -p "$BUILD"
objs=()
for src in $SOURCES; do
	obj="$BUILD/${src%.*}.o"
	case "$src" in
	*.cpp) "$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" -x c++ -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions \
		-fno-register-global-dtors-with-atexit -mllvm -disable-atexit-based-global-dtor-lowering \
		-c "$SRC/core/$src" -o "$obj" ;;
	*) "$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" -std=gnu11 -c "$SRC/core/$src" -o "$obj" ;;
	esac
	objs+=("$obj")
done
for src in $ENCODING_SOURCES; do
	obj="$BUILD/${src%.*}.o"
	"$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" -I"$SRC/hfs_encodings" -std=gnu11 -Dhfs_allocated=hfs_encodings_allocated \
		-c "$SRC/hfs_encodings/$src" -o "$obj"
	objs+=("$obj")
done
install -d "$SDK/usr/local/lib/kernel"
pie_kernel_archive "$SDK/usr/local/lib/kernel/libhfs_kext.a" "${objs[@]}"
