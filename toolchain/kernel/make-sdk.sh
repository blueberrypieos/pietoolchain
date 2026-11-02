#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Make the kernel SDK, PIE_SDKROOT, in steps:
#
#   make-sdk.sh new        a writable copy of pietoolchain-sdk's SDK, with
#                          AvailabilityVersions (PIE_KERNEL_SOURCES/
#                          AvailabilityVersions) and the include directories
#                          the kernel build names
#   make-sdk.sh xnu-hdrs DSTROOT
#                          add xnu's headers (xnu-make installhdrs DSTROOT=)
#   make-sdk.sh firehose   add libfirehose_kernel.a (build-firehose.sh)
#
# PIE_KERNEL_BUILD is the build directory.
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
SDK="${PIE_SDKROOT:?set PIE_SDKROOT}"
PYTHON="${PYTHON_FOR_BUILD:-python3}"
export PATH="$TOOLCHAIN/bin:$PATH"

new() {
	local av="${PIE_KERNEL_SOURCES:?set PIE_KERNEL_SOURCES}/AvailabilityVersions"
	local build="${PIE_KERNEL_BUILD:?set PIE_KERNEL_BUILD}/AvailabilityVersions"
	rm -rf "$SDK"
	mkdir -p "$(dirname "$SDK")"
	cp -a "$TOOLCHAIN/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk/." "$SDK/"
	chmod -R u+w "$SDK"
	make -C "$av" install DSTROOT="$SDK" OBJROOT="$build" SYMROOT="$build/sym"
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" xros "$SDK"
	install -d "$SDK/usr/local/include/iBoot" \
		"$SDK/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/AppleFeatures" \
		"$SDK/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform" \
		"$SDK/usr/local/include/kernel"
}

xnu_hdrs() {
	local dst="${1:?usage: make-sdk.sh xnu-hdrs DSTROOT}"
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" merge "$dst" "$SDK"
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" frameworks "$SDK"
}

case "${1:-}" in
	new) new ;;
	xnu-hdrs) xnu_hdrs "${2:-}" ;;
	firehose) "$KERNEL/build-firehose.sh" ;;
	*) sed -n 4,15p "$0"; exit 2 ;;
esac
