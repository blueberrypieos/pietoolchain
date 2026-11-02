# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Shared by the kernel library scripts, which source it:
#   pie_kernel_setup PROJECT   SRC, BUILD, SDK and REPRO_FLAGS for PROJECT
#   pie_kernel_archive OUT OBJ...
#
# PIE_SDKROOT is the kernel SDK (make-sdk.sh), PIE_KERNEL_SOURCES the
# directory of the projects' sources (PIE_KERNEL_SOURCES/PROJECT), and
# PIE_KERNEL_BUILD the build directory.
pie_kernel_setup() {
	local project=$1
	SRC="${PIE_KERNEL_SOURCES:?set PIE_KERNEL_SOURCES}/$project"
	BUILD="${PIE_KERNEL_BUILD:?set PIE_KERNEL_BUILD}/$BUILD"
	SDK="${PIE_SDKROOT:?set PIE_SDKROOT}"
	PYTHON="${PYTHON_FOR_BUILD:-python3}"
	KERNEL_MAKE=make
	[ -d "$SRC" ] || { echo "no $SRC" >&2; exit 1; }
	REPRO_FLAGS=()
	if [ "${PIE_REPRODUCIBLE:-0}" = 1 ]; then
		: "${SOURCE_DATE_EPOCH:?reproducible kernel libraries require SOURCE_DATE_EPOCH}"
		export TZ=UTC LC_ALL=C ZERO_AR_DATE=1
		REPRO_FLAGS=(-ffp-contract=off -fdebug-compilation-dir=/usr/src/pie
			"-ffile-prefix-map=$TOOLCHAIN=/usr/src/pie/toolchain"
			"-ffile-prefix-map=$SRC=/usr/src/pie/src/$project"
			"-ffile-prefix-map=$BUILD=/usr/src/pie/build/$project"
			"-ffile-prefix-map=$SDK=/usr/src/pie/sdk")
	fi
}

pie_kernel_archive() {
	local output=$1
	shift
	if [ "${PIE_REPRODUCIBLE:-0}" = 1 ]; then
		rm -f "$output.new"
		"${PIE_LLVM_AR:-llvm-ar-21}" rcsD "$output.new" "$@"
		install -m 644 "$output.new" "$output"
		rm "$output.new"
	else
		"$LIBTOOL" -static -o "$output" "$@"
	fi
}
