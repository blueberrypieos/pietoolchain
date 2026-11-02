#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build IONetworkingFamily.kext, the networking family, from its kext's
# sources and Info.plist, with kernel/kext.mk, and install its
# headers in the kernel SDK's usr/local/include/kernel/IOKit/network.

# KERNEL_BUILD is where both kernels were built (KERNEL_BUILD/<soc>/obj),
# KEXT_OBJ where the bundle goes, PIE_SDKROOT the kernel SDK, and the
# source is PIE_KERNEL_SOURCES/IONetworkingFamily (kernel/kext.mk).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
SRC="${PIE_KERNEL_SOURCES:?set PIE_KERNEL_SOURCES}/IONetworkingFamily"
SDK="${PIE_SDKROOT:?set PIE_SDKROOT}"
OUT="${KEXT_OBJ:?set KEXT_OBJ}"
SOURCES="IOEthernetController.cpp IOEthernetInterface.cpp IOKernelDebugger.cpp
	IOMbufMemoryCursor.cpp IONetworkController.cpp IONetworkData.cpp
	IONetworkInterface.cpp IONetworkMedium.cpp IONetworkStack.cpp
	IONetworkUserClient.cpp IOOutputQueue.cpp IOPacketQueue.cpp"
mkdir -p "$OUT"
rm -rf "$OUT/include"
mkdir -p "$OUT/include/IOKit/network"
for h in "$SRC"/*.h; do
	ln -s "$h" "$OUT/include/IOKit/network/"
done
"$KERNEL/kextplist.py" "$SRC/Info.plist" "$OUT/Info.plist" \
	PRODUCT_BUNDLE_IDENTIFIER=com.apple.iokit.IONetworkingFamily
make -s -f "$KERNEL/kext.mk" KEXT=IONetworkingFamily KEXT_OBJ="$OUT" KEXT_PLIST="$OUT/Info.plist" \
	KEXT_SOURCES="$(for s in $SOURCES; do printf '%s ' "$SRC/$s"; done)" \
	KEXT_CXXFLAGS="-D__PRIVATE_SPI__ -DIONETWORK_AUTO_NAME=1 -I$OUT/include -I$SRC" \
	KERNEL_BUILD="${KERNEL_BUILD:?set KERNEL_BUILD}" all
install -d "$SDK/usr/local/include/kernel/IOKit/network"
install -m 644 "$SRC"/*.h "$SDK/usr/local/include/kernel/IOKit/network/"
