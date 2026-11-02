#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build msdosfs.kext, the FAT filesystem, from its kext's sources, Info.plist
# and build settings, with kernel/kext.mk.

# KERNEL_BUILD is where both kernels were built (KERNEL_BUILD/<soc>/obj),
# KEXT_OBJ where the bundle goes, PIE_SDKROOT the kernel SDK, and the
# source is PIE_KERNEL_SOURCES/msdosfs (kernel/kext.mk).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
SRC="${PIE_KERNEL_SOURCES:?set PIE_KERNEL_SOURCES}/msdosfs"
OUT="${KEXT_OBJ:?set KEXT_OBJ}"
KMOD="$SRC/msdosfs.kextproj/msdosfs.kmodproj"
SOURCES="msdosfs_conv.c msdosfs_denode.c msdosfs_fat.c msdosfs_lookup.c msdosfs_vfsops.c msdosfs_vnops.c"
mkdir -p "$OUT"
"$KERNEL/kextplist.py" "$SRC/Info-msdosfs_kext.plist" "$OUT/Info.plist" \
	PRODUCT_BUNDLE_IDENTIFIER=com.apple.filesystems.msdosfs -x "$SRC/msdosfs.xcconfig"
make -s -f "$KERNEL/kext.mk" KEXT=msdosfs KEXT_OBJ="$OUT" KEXT_PLIST="$OUT/Info.plist" \
	KEXT_SOURCES="$(for s in $SOURCES; do printf '%s ' "$KMOD/$s"; done)" \
	KEXT_START=msdosfs_module_start KEXT_STOP=msdosfs_module_stop \
	KEXT_CFLAGS="-DDEBUG=0 -I$KMOD -Wno-error=int-conversion" \
	KERNEL_BUILD="${KERNEL_BUILD:?set KERNEL_BUILD}" all
