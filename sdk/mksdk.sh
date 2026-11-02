#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# usage: sdk/mksdk.sh [-s SDK] [-v VERSION] [-o DIR]
#
#   -s SDK      the build SDK (default
#               toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk)
#   -v VERSION  the package's version (default the date below, YYYYMMDD, UTC)
#   -o DIR      the output directory (default build)
#   -r ROOT     only stage the package's files and copyright file in ROOT,
#               for debian/rules (sdk/mkdebs.py makes the package)
#
# Files are sorted, owned by root and dated SOURCE_DATE_EPOCH (else by the
# SDK's newest file).
set -euo pipefail
umask 022
PIE="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$PIE/toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
OUT="$PIE/build"
VERSION=
ROOT=
while getopts 's:v:o:r:' opt; do
	case "$opt" in
		s) SDK="$OPTARG" ;;
		v) VERSION="$OPTARG" ;;
		o) OUT="$OPTARG" ;;
		r) ROOT="$OPTARG" ;;
		*) sed -n 5,15p "$0"; exit 2 ;;
	esac
done
[ -d "$SDK/usr/lib" ] || { echo "mksdk.sh: $SDK is no SDK" >&2; exit 1; }

stub=$(find "$SDK" -name '*.stub' -print -quit)
[ -z "$stub" ] || { echo "mksdk.sh: $SDK has bootstrap stubs, like $stub: build the userland first" >&2; exit 1; }

date=${SOURCE_DATE_EPOCH:-$(find "$SDK" -printf '%T@\n' | sort -n | tail -n 1 | cut -d. -f1)}
VERSION="${VERSION:-$(date -u -d "@$date" +%Y%m%d)}"
NAME="pietoolchain-sdk_${VERSION}_all"
mkdir -p "$OUT"
W="$(mktemp -d "$OUT/.mksdk.XXXXXX")"
trap 'rm -rf "$W"' EXIT

(cd "$SDK" && find . \( -type f -o -type l \) -printf '%P\n') | LC_ALL=C sort |
	grep -E '^(SDKSettings\.[a-z]+|usr/include/.*|usr/local/include/.*|usr/lib/.*|usr/local/lib/.*|System/Library/Frameworks/.*)$' \
	> "$W/files"
python3 "$PIE/sdk/providers.py" "$PIE" "$W/files" > "$W/providers"

D="${ROOT:-$W/root}"
S="$D/usr/lib/pietoolchain/toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
DOC="$D/usr/share/doc/pietoolchain-sdk"
mkdir -p "$S" "$DOC"
(cd "$SDK" && tar -cf - --no-recursion -T "$W/files") | (cd "$S" && tar -xf -)
python3 "$PIE/sdk/mkcopyright.py" "$PIE" "$W/providers" > "$DOC/copyright"
[ -z "$ROOT" ] || exit 0
mkdir -p "$D/DEBIAN"

provides=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1] + "/sdk"); import mkdebs; print(mkdebs.sdk_provides(sys.argv[1]))' "$PIE")
cat > "$D/DEBIAN/control" <<EOF
Package: pietoolchain-sdk
Version: $VERSION
Architecture: all
Multi-Arch: foreign
Provides: $provides
Maintainer: Maxwell <maxwell@blueberrypieos.com>
Installed-Size: $(du -sk --apparent-size "$D/usr" | cut -f1)
Section: devel
Priority: optional
Homepage: https://github.com/blueberrypieos/pietoolchain
Description: darwin-arm64 SDK for pietoolchain
 The SDK headers, libraries and frameworks for pietoolchain to build against.
EOF
(cd "$D" && find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums)
chmod -R go-w "$D"
find "$D" -exec touch -h -d "@$date" {} +
SOURCE_DATE_EPOCH=$date dpkg-deb --root-owner-group -Zxz --build "$D" "$OUT/$NAME.deb.part" >/dev/null
mv "$OUT/$NAME.deb.part" "$OUT/$NAME.deb"
(cd "$OUT" && sha256sum "$NAME.deb")
