#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libIOStorageFamily_kext.a, the storage family (IOStorageFamily),
# into the kernel SDK's usr/local/lib/kernel, and install its headers in
# usr/local/include/kernel/IOKit/storage. Needs xnu's headers in the SDK
# (make-sdk.sh xnu-hdrs), PIE_KERNEL_SOURCES/IOStorageFamily
# (kernel-build-common.sh) and PIE_XNU_SOURCE, the xnu tree.
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=libIOStorageFamily_kext
ARCH="${ARCH:-arm64}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup IOStorageFamily
IIG="$TOOLCHAIN/bin/iig"
KF="$SDK/System/Library/Frameworks/Kernel.framework"
XNU="${PIE_XNU_SOURCE:?set PIE_XNU_SOURCE}"
SOURCES="IORequestsPool.cpp IOUserBlockStorageDevice_kext.cpp IOAppleLabelScheme.cpp
	IOApplePartitionScheme.cpp IOBlockStorageDevice.cpp IOBlockStorageDriver.cpp
	IOFDiskPartitionScheme.cpp IOFilterScheme.cpp IOGUIDPartitionScheme.cpp IOMedia.cpp
	IOMediaBSDClient.cpp IOPartitionScheme.cpp IOStorage.cpp IORequest.cpp"
rm -rf "$BUILD"
mkdir -p "$BUILD/include/BlockStorageDeviceDriverKit" "$BUILD/include/IOKit/storage"
for h in "$SRC"/*.h; do
	ln -s "$h" "$BUILD/include/IOKit/storage/"
done
COMMON=(-arch "$ARCH" -mcpu=generic -isysroot "$SDK" -mmacosx-version-min=26.0
	-Os -g -mkernel -nostdinc
	-DKERNEL=1 -DKERNEL_PRIVATE=1 -DPRIVATE=1 -DAPPLE -DNeXT
	-I"$BUILD/include" -I"$BUILD/include/BlockStorageDeviceDriverKit" -I"$SRC"
	-I"$KF/PrivateHeaders" -I"$KF/Headers"
	-idirafter "$SDK/usr/local/include/kernel" -idirafter "$SDK/usr/include"
	-idirafter "$XNU/iokit")
CXXFLAGS=(-x c++ -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions
	-fno-register-global-dtors-with-atexit -mllvm -disable-atexit-based-global-dtor-lowering)
"$IIG" --def "$SRC/BlockStorageDeviceDriverKit/IOUserBlockStorageDevice.iig" \
	--header "$BUILD/include/BlockStorageDeviceDriverKit/IOUserBlockStorageDevice.h" \
	--impl "$BUILD/IOUserBlockStorageDevice.iig.cpp" \
	--framework-name BlockStorageDeviceDriverKit -- "${COMMON[@]}" -x c++ -std=gnu++17 \
	-F"$SDK/System/Library/Frameworks" -D__IIG=1 -DDRIVERKIT_PRIVATE=1
objs=()
for src in $SOURCES; do
	obj="$BUILD/${src%.*}.o"
	"$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" "${CXXFLAGS[@]}" -c "$SRC/$src" -o "$obj"
	objs+=("$obj")
done
"$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" "${CXXFLAGS[@]}" -DDRIVERKIT_PRIVATE=1 -c "$BUILD/IOUserBlockStorageDevice.iig.cpp" \
	-o "$BUILD/IOUserBlockStorageDevice.iig.o"
objs+=("$BUILD/IOUserBlockStorageDevice.iig.o")
install -d "$SDK/usr/local/lib/kernel"
pie_kernel_archive "$SDK/usr/local/lib/kernel/libIOStorageFamily_kext.a" "${objs[@]}"
install -d "$SDK/usr/local/include/kernel/IOKit/storage" \
	"$SDK/usr/local/include/kernel/BlockStorageDeviceDriverKit"
install -m 644 "$SRC"/*.h "$SDK/usr/local/include/kernel/IOKit/storage/"
install -m 644 "$BUILD/include/BlockStorageDeviceDriverKit/IOUserBlockStorageDevice.h" \
	"$SDK/usr/local/include/kernel/BlockStorageDeviceDriverKit/"
