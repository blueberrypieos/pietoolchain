#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libpthread_kext.a, the kernel half of libpthread (kern/*.c), into
# the kernel SDK's usr/local/lib/kernel. Needs xnu's headers in the SDK
# (make-sdk.sh xnu-hdrs) and PIE_KERNEL_SOURCES/libpthread
# (kernel-build-common.sh).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=libpthread_kext
ARCHS="${ARCHS:-arm64}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup libpthread
KF="$SDK/System/Library/Frameworks/Kernel.framework"
mkdir -p "$BUILD"
objs=()
for arch in $ARCHS; do
	for src in kern_init kern_support kern_synch; do
		obj="$BUILD/$src.$arch.o"
		"$CC" "${REPRO_FLAGS[@]}" -arch "$arch" -mcpu=generic -isysroot "$SDK" -mmacosx-version-min=26.0 \
			-std=gnu11 -Os -g -mkernel -nostdinc \
			-Wno-error=int-conversion \
			-DKERNEL=1 -DKERNEL_PRIVATE=1 -DPRIVATE=1 \
			-DXNU_KERNEL_PRIVATE -DMACH_KERNEL_PRIVATE -DABSOLUTETIME_SCALAR_TYPE \
			-DNEEDS_SCHED_CALL_T -D__PTHREAD_EXPOSE_INTERNALS__ \
			-Dcurrent_uthread=pthread_kext_current_uthread -Dpthread_kern=pthread_kext_kern \
			-I"$SRC/private" -I"$SRC/include" -I"$SRC" \
			-I"$KF/PrivateHeaders" -I"$KF/Headers" -idirafter "$SDK/usr/include" \
			-c "$SRC/kern/$src.c" -o "$obj"
		objs+=("$obj")
	done
done
install -d "$SDK/usr/local/lib/kernel"
pie_kernel_archive "$SDK/usr/local/lib/kernel/libpthread_kext.a" "${objs[@]}"
