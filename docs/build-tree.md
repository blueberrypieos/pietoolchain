<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Copyright (c) 2026 The Blueberry Pie OS Authors -->

# Building outside the source tree

Run `./build.sh` from the checkout, or use `debian/rules`.
The script copies the build helpers and xnu into `debian/build`.
Toolchain and userland stages run the copied scripts and makefiles.
Upstream projects are read in place through `PIE_SOURCES`, which names
`src` in the checkout. Projects that need writable sources copy them
under `debian/build`. The xnu patches apply to its copy.
Python bytecode writes are disabled.

The SDK is in `debian/build/toolchain/sdk`.
The toolchain prefix and objects sit beside it.
Runtime staging roots, userland objects and provider records are in
`debian/build/build`. Standalone SDK packages go there too.
Debian binary packages are written beside the source directory.
The copied `toolchain/hostinc` link resolves through `PIE_SOURCES`
to the cctools headers used by xnu's SETUP tools.

The compiler maps the build root to `/usr/src/pie/src`.
A separate map records `PIE_SOURCES` as `/usr/src/pie/src/src`.
Userland and CMake compiler flags carry that map.
Kernel library maps use `/usr/src/pie/toolchain`, `/usr/src/pie/src`,
`/usr/src/pie/build` and `/usr/src/pie/sdk`.
The linker strips the build root from object paths.
These maps keep recorded paths independent of the build directory.
Provider records use SDK-relative paths. Package manifests use installed
paths. Neither records the physical build directory.
Trace maps pair copied helper directories with their source directories.
Project copy maps name the original directories under `src`.

Native targets build under `debian/build`.
They disable the source-wide autotools configuration update.
The cctools build runs `autogen.sh` in its copy.
After each successful native target, `clean-host` removes
`debian/.debhelper`, `debian/debhelper-build-stamp`,
`debian/*.debhelper.log` and `debian/*.debhelper`.
It preserves build outputs, package staging roots, substitution variables
and `debian/files`.
The cleanup uses explicit paths because `dh_clean` deletes backup-like
files across the source tree. Upstream files with those names must survive.

`debian/rules clean` removes `debian/build`, package staging directories,
`debian/files`, `debian/*.substvars` and the helper state above.

The shared `install_both` helper installs SDK and runtime libraries
atomically. It copies each library to a temporary name beside each
destination, then renames it into place. It removes the SDK's `.stub`
marker after both copies finish.
Libnotify's programs, the mDNSResponder daemon and CommonCrypto's
regression program wait for `install-lib`.
The LLVM C++ configuration and links wait for `install-unwind`.
Other prerequisites can run in parallel.
