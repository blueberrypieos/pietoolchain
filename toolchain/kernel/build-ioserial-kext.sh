#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build IOSerialFamily.kext, the serial port family, from its kext's
# sources and Info.plist, with kernel/kext.mk, and install its headers in
# the kernel SDK: usr/local/include/kernel/IOKit/serial, and IOSerialKeys.h
# and ioss.h in IOKit.framework's Headers/serial.

# KERNEL_BUILD is where both kernels were built (KERNEL_BUILD/<soc>/obj),
# KEXT_OBJ where the bundle goes, PIE_SDKROOT the kernel SDK, and the
# source is PIE_KERNEL_SOURCES/IOSerialFamily (kernel/kext.mk).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
SRC="${PIE_KERNEL_SOURCES:?set PIE_KERNEL_SOURCES}/IOSerialFamily"
SDK="${PIE_SDKROOT:?set PIE_SDKROOT}"
OUT="${KEXT_OBJ:?set KEXT_OBJ}"
K="$SRC/IOSerialFamily.kmodproj"
mkdir -p "$OUT"
rm -rf "$OUT/include"
mkdir -p "$OUT/include/IOKit/serial"
for h in "$K"/*.h; do
	ln -s "$h" "$OUT/include/IOKit/serial/"
done
install -m 644 "$K/IOSerialFamily-Info.plist" "$OUT/Info.plist"
make -s -f "$KERNEL/kext.mk" KEXT=IOSerialFamily KEXT_OBJ="$OUT" KEXT_PLIST="$OUT/Info.plist" \
	KEXT_SOURCES="$K/IOSerialBSDClient.cpp $K/IOSerialStreamSync.cpp" \
	KEXT_CXXFLAGS="-I$OUT/include -I$K -Wno-format-security" \
	KERNEL_BUILD="${KERNEL_BUILD:?set KERNEL_BUILD}" all
install -d "$SDK/usr/local/include/kernel/IOKit/serial"
install -m 644 "$K"/*.h "$SDK/usr/local/include/kernel/IOKit/serial/"
install -d "$SDK/System/Library/Frameworks/IOKit.framework/Versions/A/Headers/serial"
install -m 644 "$K/IOSerialKeys.h" "$K/ioss.h" "$SDK/System/Library/Frameworks/IOKit.framework/Versions/A/Headers/serial/"
