#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# usage: run.sh
#
# Builds libxpc's wire format, objects, connections and pipes for the build
# machine, with stub Mach and dispatch calls, and runs wire_test, wire_test
# built with AddressSanitizer and UndefinedBehaviorSanitizer, and conn_test.
# CC is clang by default. It must take -fblocks and
# -fsanitize=address,undefined.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
pie=$(cd "$here/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
${CC:-clang} -std=gnu11 -fblocks -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
	-I "$here/include" -I "$pie/toolchain/sdk-overlay/usr/include" \
	-I "$pie/toolchain/sdk-overlay/usr/local/include" \
	-o "$out/wire_test" "$here/wire_test.c" "$here/../object.c"
${CC:-clang} -std=gnu11 -fblocks -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
	-I "$here/include" -I "$pie/toolchain/sdk-overlay/usr/include" \
	-I "$pie/toolchain/sdk-overlay/usr/local/include" \
	-o "$out/wire_test_asan" "$here/wire_test.c" "$here/../object.c"
${CC:-clang} -std=gnu11 -fblocks -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
	-I "$here/include" -I "$pie/toolchain/sdk-overlay/usr/include" \
	-I "$pie/toolchain/sdk-overlay/usr/local/include" \
	-o "$out/conn_test" "$here/conn_test.c" "$here/blocks.c" "$here/../wire.c" \
	"$here/../object.c" "$here/../pipe.c"
"$out/wire_test"
ASAN_OPTIONS=detect_leaks=0 "$out/wire_test_asan"
"$out/conn_test"
