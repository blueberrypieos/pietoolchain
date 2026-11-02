#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Make the SDK that pietoolchain-sdk packages (toolchain/sdk), in steps:
#
#   toolchain/make-sdk.sh base       AvailabilityVersions + overlay headers
#   toolchain/make-sdk.sh xnu-hdrs   install xnu's headers (run after
#                                    `xnu-make ... installhdrs`, DSTROOT=$2)
#   toolchain/make-sdk.sh firehose   libfirehose_kernel.a (needs xnu-hdrs)
set -euo pipefail

TOOLCHAIN="$(cd "$(dirname "$0")" && pwd)"
SDK="${SDKROOT:-${PIE_SDKROOT:-$TOOLCHAIN/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk}}"
BUILD="${PIE_SDK_BUILD:-$TOOLCHAIN/build}"
PYTHON="${PYTHON_FOR_BUILD:-python3}"
BUILD_MAKE="${PIE_GMAKE:-make}"
export PATH="$TOOLCHAIN/bin:$PATH"

add_xros_aliases() {
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" xros "$SDK"
}

fix_framework_links() {
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" frameworks "$SDK"
}

base() {
	local av
	av="$(cd "$TOOLCHAIN/.." && pwd)/src/AvailabilityVersions"
	[ -d "$av" ] || { echo "make-sdk.sh: no $av (git submodule update --init)" >&2; exit 1; }
	mkdir -p "$SDK"
	"$BUILD_MAKE" -C "$av" install DSTROOT="$SDK" \
		OBJROOT="$BUILD/AvailabilityVersions" \
		SYMROOT="$BUILD/AvailabilityVersions/sym"
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" merge "$TOOLCHAIN/sdk-overlay" "$SDK"
	add_xros_aliases
	install -d "$SDK/usr/local/include/iBoot" \
		"$SDK/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/AppleFeatures" \
		"$SDK/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform"
	install -d "$SDK/usr/local/include/kernel"
	install -m 644 "$TOOLCHAIN/sdk-overlay/usr/include/TargetConditionals.h" \
		"$SDK/usr/local/include/kernel/TargetConditionals.h"
}

xnu_hdrs() {
	local dst="${1:?usage: make-sdk.sh xnu-hdrs <xnu installhdrs DSTROOT>}"
	"$PYTHON" -I "$TOOLCHAIN/hostcompat/sdk.py" merge "$dst" "$SDK"
	install -m 644 "$TOOLCHAIN/../xnu/EXTERNAL_HEADERS/ptrcheck.h" "$SDK/usr/include/ptrcheck.h"
	fix_framework_links
}

case "${1:-}" in
	base) base ;;
	xnu-hdrs) xnu_hdrs "${2:-}" ;;
	firehose) PIE_KERNEL_SOURCES="$(cd "$TOOLCHAIN/.." && pwd)/src" PIE_KERNEL_BUILD="$BUILD" PIE_SDKROOT="$SDK" \
		"$TOOLCHAIN/kernel/build-firehose.sh" ;;
	*) sed -n '7,10p' "$0"; exit 1 ;;
esac
