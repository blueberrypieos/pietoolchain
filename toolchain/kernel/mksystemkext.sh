#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Assemble System.kext, a kernel's interface kexts (its KPI symbol sets),
# from its build: each PlugIns/NAME.kext gets its Info.plist and, as its
# executable, the build's NAME.symbolset. kextload sends the ones a kext
# names in OSBundleLibraries to the kernel with it, and kext.mk checks a
# kext's undefined symbols against them.
#
#   mksystemkext.sh CONFIGDIR OUTDIR
#
# CONFIGDIR is a kernel build's config directory
# (OBJROOT/DEVELOPMENT_ARM64_<SOC>/config); OUTDIR/System.kext is made
# again.
set -euo pipefail
[ $# = 2 ] || { sed -n 4,15p "$0"; exit 2; }
CONFIG="$1" OUT="$2"
[ -f "$CONFIG/System.kext/Info.plist" ] || { echo "mksystemkext.sh: no System.kext in $CONFIG" >&2; exit 1; }

S="$OUT/System.kext"
rm -rf "$S"
install -d "$S/PlugIns"
install -m 644 "$CONFIG/System.kext/Info.plist" "$S/Info.plist"
for k in "$CONFIG"/System.kext/PlugIns/*.kext; do
	name=$(basename "$k" .kext)
	exe=$(python3 -c 'import plistlib, sys; print(plistlib.load(open(sys.argv[1], "rb")).get("CFBundleExecutable", ""))' "$k/Info.plist")
	install -d "$S/PlugIns/$name.kext"
	install -m 644 "$k/Info.plist" "$S/PlugIns/$name.kext/Info.plist"
	if [ -n "$exe" ]; then
		[ -f "$CONFIG/$exe.symbolset" ] || { echo "mksystemkext.sh: $name.kext: no $exe.symbolset" >&2; exit 1; }
		install -m 644 "$CONFIG/$exe.symbolset" "$S/PlugIns/$name.kext/$exe"
	fi
done
