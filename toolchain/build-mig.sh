#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Build mig and migcom, the Mach interface generator, from bootstrap_cmds
# (the git submodule src/bootstrap_cmds), for a Linux host:
# PREFIX/bin/mig and PREFIX/libexec/migcom. mig runs the C preprocessor it
# finds as cc next to it; MIGCC overrides it.
#
#   PREFIX  the install directory (default toolchain/prefix)
#   BUILD   the build directory (default toolchain/build)
set -euo pipefail

TOOLCHAIN="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$TOOLCHAIN/prefix}"
BUILD="${BUILD:-$TOOLCHAIN/build}/mig"
CC="${CC:-clang-21}"
TOP="$(cd "$TOOLCHAIN/.." && pwd)"
M="$TOP/src/bootstrap_cmds/migcom.tproj"
VERSION=bootstrap_cmds-138

[ -f "$M/parser.y" ] || { echo "build-mig.sh: no $M/parser.y (git submodule update --init)" >&2; exit 1; }
rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

bison -y -d "$M/parser.y" -o y.tab.c
flex -o lex.yy.c "$M/lexxer.l"

cat > apsl-notice.c <<'NOTICE'
__attribute__((used)) const char pietoolchain_apsl_notice[] =
	"This program contains Covered Code under the Apple Public Source License 2.0. "
	"Its Source Code is available under that License from "
	"https://github.com/blueberrypieos/pietoolchain and its bootstrap_cmds submodule.";
NOTICE

SOURCES="error global header mig routine server statement string type user utils"
"$CC" -O2 -w -D_DEFAULT_SOURCE -Ulinux -include sys/types.h -DMIG_VERSION="\"$VERSION\"" \
	"-ffile-prefix-map=$TOP=/usr/src/pietoolchain" \
	-I"$TOOLCHAIN/mig/hostinc" -I"$M" -I. -o migcom \
	$(for s in $SOURCES; do echo "$M/$s.c"; done) y.tab.c lex.yy.c apsl-notice.c

install -d "$PREFIX/bin" "$PREFIX/libexec"
install -m 755 migcom "$PREFIX/libexec/migcom"
install -m 755 "$M/mig.sh" "$PREFIX/bin/mig"
