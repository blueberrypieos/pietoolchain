#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build libcc_kext.a, the compiler runtime the kernel and kexts link
# (-lcc_kext), from compiler-rt's integer builtins, into the kernel SDK's
# usr/lib. PIE_KERNEL_SOURCES/llvm-project is LLVM's source tree (at least
# compiler-rt/lib/builtins); see kernel-build-common.sh.
set -euo pipefail
TOOLCHAIN="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
KERNEL="$TOOLCHAIN/kernel"
CC="$TOOLCHAIN/bin/clang"
LIBTOOL="$TOOLCHAIN/prefix/bin/aarch64-apple-darwin-libtool"
BUILD=cc_kext
ARCHS="${ARCHS:-arm64}"
AR="${PIE_LLVM_AR:-llvm-ar-21}"
source "$KERNEL/kernel-build-common.sh"
pie_kernel_setup llvm-project
B="$SRC/compiler-rt/lib/builtins"
RESOURCE="$("$CC" -print-resource-dir)"
COMMON=(-mcpu=generic -mmacosx-version-min=26.0 -mkernel -O2 -std=c11
	-fno-builtin -nostdinc -isystem "$RESOURCE/include")
if [ "${PIE_REPRODUCIBLE:-0}" = 1 ]; then
	COMMON+=(-ffile-prefix-map="$SRC"=/usr/src/llvm-project
		-ffile-prefix-map="$TOOLCHAIN"=/usr/src/pie/toolchain
		-ffile-prefix-map="$RESOURCE"=/usr/lib/llvm-21/lib/clang/21
		-fdebug-compilation-dir=/usr/src/pie/toolchain)
fi
FUNCS="absvdi2 absvsi2 absvti2 addvdi3 addvsi3 addvti3 ashldi3 ashlti3
ashrdi3 ashrti3 bswapdi2 bswapsi2 clzdi2 clzsi2 clzti2 cmpdi2 cmpti2
ctzdi2 ctzsi2 ctzti2 divdi3 divmoddi4 divmodsi4 divsi3 divti3 ffsdi2
ffssi2 ffsti2 lshrdi3 lshrti3 moddi3 modsi3 modti3 muldi3 mulodi4 mulosi4
muloti4 multi3 mulvdi3 mulvsi3 mulvti3 negdi2 negti2 negvdi2 negvsi2
negvti2 paritydi2 paritysi2 parityti2 popcountdi2 popcountsi2 popcountti2
subvdi3 subvsi3 subvti3 ucmpdi2 ucmpti2 udivdi3 udivmoddi4 udivmodsi4
udivmodti4 udivsi3 udivti3 umoddi3 umodsi3 umodti3"
mkdir -p "$BUILD"
objs=()
for arch in $ARCHS; do
	mkdir -p "$BUILD/$arch"
	for f in $FUNCS; do
		o="$BUILD/$arch/$f.o"
		"$CC" -arch "$arch" "${COMMON[@]}" -fvisibility=hidden -c "$B/$f.c" -o "$o"
		objs+=("$o")
	done
	o="$BUILD/$arch/profile_unsupported.o"
	"$CC" -arch "$arch" "${COMMON[@]}" -c "$KERNEL/cc_kext/profile_unsupported.c" -o "$o"
	objs+=("$o")
done
install -d "$SDK/usr/lib"
pie_kernel_archive "$SDK/usr/lib/libcc_kext.a" "${objs[@]}"
