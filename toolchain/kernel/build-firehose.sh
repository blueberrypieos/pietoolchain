#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libfirehose_kernel.a, libdispatch's kernel-side os_log buffer code,
# into the kernel SDK's usr/local/lib/kernel. Needs xnu's headers in the SDK
# (make-sdk.sh xnu-hdrs) and PIE_KERNEL_SOURCES/libdispatch
# (kernel-build-common.sh).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=libfirehose_kernel
ARCHS="${ARCHS:-arm64}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup libdispatch
install -d "$SDK/usr/local/include/kernel/os"
unifdef -DKERNEL=1 -DOS_FIREHOSE_SPI=1 -DOS_VOUCHER_ACTIVITY_SPI_TYPES=1 -UOS_VOUCHER_ACTIVITY_SPI \
	-o "$SDK/usr/local/include/kernel/os/firehose_buffer_private.h" \
	"$SRC/os/firehose_buffer_private.h" || [ $? -eq 1 ]
mkdir -p "$BUILD"
objs=()
for arch in $ARCHS; do
	obj="$BUILD/firehose_buffer.$arch.o"
	"$CC" "${REPRO_FLAGS[@]}" -arch "$arch" -mcpu=generic -isysroot "$SDK" -mmacosx-version-min=26.0 \
		-std=gnu11 -Os -g -mkernel -nostdinc -Wno-packed \
		-DKERNEL=1 -DDISPATCH_USE_DTRACE=0 \
		-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
		-DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 \
		-I"$SRC" \
		-I"$SDK/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
		-I"$SDK/System/Library/Frameworks/Kernel.framework/Headers" \
		-I"$SDK/usr/local/include/os" \
		-I"$SDK/usr/local/include/firehose" \
		-I"$SDK/usr/local/include/kernel" \
		-c "$SRC/src/firehose/firehose_buffer.c" -o "$obj"
	objs+=("$obj")
done
install -d "$SDK/usr/local/lib/kernel"
pie_kernel_archive "$SDK/usr/local/lib/kernel/libfirehose_kernel.a" "${objs[@]}"
