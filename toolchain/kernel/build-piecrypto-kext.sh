#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libpiecrypto_kernel.a, the part of piecrypto the kernel takes (its
# sources.mk's PIECRYPTO_KERNEL_SRCS and PIECRYPTO_KERNEL_ARM64_SRCS), into
# the kernel SDK's usr/local/lib/kernel. Needs xnu's headers in the SDK
# (make-sdk.sh xnu-hdrs) and PIE_KERNEL_SOURCES/piecrypto
# (kernel-build-common.sh).
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=libpiecrypto_kernel
ARCH="${ARCH:-arm64}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup piecrypto
KF="$SDK/System/Library/Frameworks/Kernel.framework"
mkvar() {
	printf 'print:\n\t@echo $(%s)\n' "$1" |
		"$KERNEL_MAKE" -s --no-print-directory -f "$SRC/sources.mk" -f - PIECRYPTO="$SRC" print
}
SOURCES=$(mkvar PIECRYPTO_KERNEL_SRCS)
ARM64_SOURCES=$(mkvar PIECRYPTO_KERNEL_ARM64_SRCS)
# shellcheck disable=SC2207
CPPFLAGS=($(mkvar PIECRYPTO_KERNEL_CPPFLAGS))
# shellcheck disable=SC2207
ARM64_CFLAGS=($(mkvar PIECRYPTO_KERNEL_ARM64_CFLAGS))
COMMON=(-arch "$ARCH" -isysroot "$SDK" -mmacosx-version-min=26.0
	-Os -g -mkernel -nostdinc -std=gnu11
	-DKERNEL=1 -DKERNEL_PRIVATE=1 -DPRIVATE=1 -DAPPLE -DNeXT
	"${CPPFLAGS[@]}" -I"$KF/PrivateHeaders" -I"$KF/Headers"
	-idirafter "$SDK/usr/local/include/kernel" -idirafter "$SDK/usr/include"
	-idirafter "$("$CC" -print-resource-dir)/include")
PORTABLE=(-mcpu=generic -mgeneral-regs-only)
SIMD=(-mcpu=generic "${ARM64_CFLAGS[@]}")
rm -rf "$BUILD"
mkdir -p "$BUILD"
objs=()
compile() {
	local src=$1 rel obj
	shift
	rel="${src#"$SRC"/}"
	obj="$BUILD/${rel//\//_}"
	obj="${obj%.c}.o"
	"$CC" "${REPRO_FLAGS[@]}" "${COMMON[@]}" "$@" -c "$src" -o "$obj"
	objs+=("$obj")
}
for src in $SOURCES; do
	compile "$src" "${PORTABLE[@]}"
done
for src in $ARM64_SOURCES; do
	compile "$src" "${SIMD[@]}"
done
install -d "$SDK/usr/local/lib/kernel"
pie_kernel_archive "$SDK/usr/local/lib/kernel/libpiecrypto_kernel.a" "${objs[@]}"
