#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
#   ./build.sh [STAGE...]
#
# Stages, in order (default: toolchain userland sdk):
#   toolchain  cctools and ld64, mig, the SDK's base headers
#              (AvailabilityVersions), xnu's headers and firehose
#   userland   libSystem and the base libraries, each built into the SDK
#              (toolchain/sdk), or installed there from the packages of PIE's
#              ports (userland/*/debs), and into its staging root,
#              build/pkgroot/<project>
#   sdk        build/pietoolchain-sdk_VERSION_all.deb (sdk/mksdk.sh):
#              VERSION is $PIE_SDK_PACKAGE_VERSION, else the date of the SDK's
#              newest file
set -euo pipefail

PIE="$(cd "$(dirname "$0")" && pwd)"
T="$PIE/toolchain"
[ $# -gt 0 ] && STAGES=("$@") || STAGES=(toolchain userland sdk)

say() { printf '\n==== %s\n' "$*"; }

if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
	if [ -f "$PIE/debian/changelog" ]; then
		SOURCE_DATE_EPOCH=$(dpkg-parsechangelog -l "$PIE/debian/changelog" -STimestamp)
	else
		SOURCE_DATE_EPOCH=$(git -C "$PIE" log -1 --format=%ct)
	fi
fi
export SOURCE_DATE_EPOCH PIE_REPRODUCIBLE=1 PIE_REPRO_SOURCE="$PIE" ZERO_AR_DATE=1 TZ=UTC LC_ALL=C

SDK="$T/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
provide() {
	local name=$1 before
	shift
	mkdir -p "$PIE/build/providers"
	before=$(mktemp)
	python3 "$PIE/sdk/record-provider.py" snapshot "$SDK" > "$before"
	"$@"
	python3 "$PIE/sdk/record-provider.py" update "$SDK" "$before" "$PIE/build/providers" "$name"
	rm -f "$before"
}

patch_xnu() {
	local p
	[ -f "$PIE/xnu/Makefile" ] || return 0
	for p in "$PIE"/xnu-patches/*.patch; do
		[ -e "$p" ] || continue
		patch -d "$PIE/xnu" -p1 -R -s -f --dry-run < "$p" >/dev/null 2>&1 && continue
		patch -d "$PIE/xnu" -p1 -s -f --no-backup-if-mismatch < "$p"
	done
}

xnu_make() {
	(cd "$PIE/xnu" && "$T/xnu-make" ARCH_CONFIGS=ARM64 BUILD_LTO=0 "$@")
}

stage_toolchain() {
	say "toolchain: cctools, mig, SDK"
	[ -f "$PIE/xnu/Makefile" ] || { echo "build.sh: no xnu (git submodule update --init)" >&2; exit 1; }
	"$T/build-cctools.sh"
	"$T/build-mig.sh"
	provide AvailabilityVersions "$T/make-sdk.sh" base
	xnu_make KERNEL_CONFIGS=RELEASE MACHINE_CONFIGS=VMAPPLE \
		ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
		OBJROOT="$PIE/build/hdrs/obj" SYMROOT="$PIE/build/hdrs/sym" \
		DSTROOT="$PIE/build/hdrs/dst" installhdrs
	provide xnu "$T/make-sdk.sh" xnu-hdrs "$PIE/build/hdrs/dst"
	provide libdispatch-firehose "$T/make-sdk.sh" firehose
}

HEADERS="libsystem_kernel architecture cctools dyld libplatform libpthread libmalloc
	libclosure libdispatch Libc Libm llvm-runtimes launchd libxpc OpenBSM Libinfo
	Libnotify syslog libsystem_configuration mDNSResponder libresolv Librpcsvc hfs
	IOStorageFamily CarbonHeaders copyfile removefile libutil zlib bzip2 CommonCrypto
	libmd libxo libedit libcorecrypto libsystem_sandbox libsystem_coreservices compat"
LIBRARIES="CrashReporterClient libplatform libpthread libsystem_featureflags libcorecrypto
	libmalloc libclosure compiler-rt Libc:install-libc openlibm cctools dyld_policy dyld
	libsystem_trace libdispatch libsystem_sanitizers libxpc Libc Libnotify:install-lib syslog
	libsystem_configuration mDNSResponder:install-lib Libinfo removefile copyfile CommonCrypto
	libsystem_sandbox libsystem_coreservices llvm-runtimes:install-unwind Libsystem
	llvm-runtimes libresolv
	libutil zlib bzip2 xz xxhash lz4 openssl libiconv ICU libxml2 CF IOKitUser ncurses
	libedit libmd libxo OpenBSM libsysmon libsbuf OpenPAM libarchive libipsec"
PROGRAMS="mDNSResponder Libnotify"

fetch() {
	if grep -q '^TAG' "$PIE/userland/$1/Makefile"; then
		make -C "$PIE/userland/$1" fetch
	fi
}

declare -A STAGED
fresh_root() {
	if [ -z "${STAGED[$1]:-}" ]; then
		rm -rf "$PIE/build/pkgroot/$1"
		STAGED[$1]=1
	fi
}

stage_userland() {
	local p target
	for p in $HEADERS; do
		say "userland: $p headers"
		fetch "$p"
		provide "$p" make -C "$PIE/userland/$p" installhdrs
	done
	say "userland: libsystem_kernel"
	fresh_root libsystem_kernel
	provide libsystem_kernel make -C "$PIE/userland/libsystem_kernel" -j"$(nproc)" install
	say "userland: stubs"
	provide stubs make -C "$PIE/userland/stubs" install
	for p in $LIBRARIES $PROGRAMS; do
		target=install
		case "$p" in
		*:*) target="${p#*:}"; p="${p%%:*}" ;;
		esac
		say "userland: $p $target"
		fetch "$p"
		fresh_root "$p"
		provide "$p" make -C "$PIE/userland/$p" -j"$(nproc)" "$target"
	done
}

stage_sdk() {
	say "sdk: pietoolchain-sdk"
	"$PIE/sdk/mksdk.sh" ${PIE_SDK_PACKAGE_VERSION:+-v "$PIE_SDK_PACKAGE_VERSION"}
}

patch_xnu
for s in "${STAGES[@]}"; do
	"stage_$s"
done
say "done: ${STAGES[*]}"
