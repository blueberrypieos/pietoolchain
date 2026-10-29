#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build cctools and the ld64 linker, from cctools-port, for targeting arm64
# Darwin. On Linux, ld64 needs libdispatch and BlocksRuntime; they come from
# apple-libdispatch and are linked in statically. Both sources are git
# submodules in toolchain/src. otool is LLVM's llvm-otool.
#
#   PREFIX  the install directory (default toolchain/prefix)
#   BUILD   the build directory (default toolchain/build)
set -euo pipefail

TOOLCHAIN="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$TOOLCHAIN/prefix}"
BUILD="${BUILD:-$TOOLCHAIN/build}"
SRC="$TOOLCHAIN/src"
CC="${CC:-clang-21}"
CXX="${CXX:-clang++-21}"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-21}"
JOBS="${JOBS:-$(nproc)}"

for f in cctools-port/cctools/configure.ac apple-libdispatch/CMakeLists.txt; do
	[ -f "$SRC/$f" ] || { echo "build-cctools.sh: no $SRC/$f (git submodule update --init)" >&2; exit 1; }
done

cmake -S "$SRC/apple-libdispatch" -B "$BUILD/apple-libdispatch" -G Ninja \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PREFIX"
ninja -C "$BUILD/apple-libdispatch" -j"$JOBS" install

# cctools-port builds in its source directory, so build a copy of it, after
# generating its autotools files with autogen.sh.
rm -rf "$BUILD/cctools"
cp -a "$SRC/cctools-port/cctools" "$BUILD/cctools"
cd "$BUILD/cctools"
./autogen.sh
# The notice APSL 2.0 section 2.3 requires in each tool: where its source is.
cat > "$BUILD/apsl-notice.c" <<'EOF'
__attribute__((used)) const char pietoolchain_apsl_notice[] =
	"This program contains Covered Code under the Apple Public Source License 2.0. "
	"Its Source Code is available under that License from "
	"https://github.com/blueberrypieos/pietoolchain and its cctools-port and "
	"apple-libdispatch submodules.";
EOF
"$CC" -O2 -g0 -c "$BUILD/apsl-notice.c" -o "$BUILD/apsl-notice.o"
# The static libdispatch needs BlocksRuntime and pthreads linked after it.
CC="$CC" CXX="$CXX" CFLAGS="-I$PREFIX/include" CXXFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" \
	LIBS="$BUILD/apsl-notice.o -lBlocksRuntime -lpthread" ./configure --prefix="$PREFIX" --target=aarch64-apple-darwin \
		--with-llvm-config="$(command -v "$LLVM_CONFIG")"
make -j"$JOBS"
make install
ln -s "$("$LLVM_CONFIG" --bindir)/llvm-otool" "$PREFIX/bin/aarch64-apple-darwin-otool"

# ld -v keeps writing after head exits, which would fail under pipefail.
"$PREFIX/bin/aarch64-apple-darwin-ld" -v 2>&1 | head -1 || true
