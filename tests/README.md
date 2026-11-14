<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Copyright (c) 2026 The Blueberry Pie OS Authors -->

# Host suites

Install `python3-pytest dpkg-dev xz-utils clang-21 libclang-rt-21-dev debhelper` on
Ubuntu 26.04. Run `pytest-3 tests </dev/null` from this checkout.
No SDK, built toolchain or source submodules are needed. The CI host-tests
job uses these packages and runs independently of the package build.
Attestation and release wait for that job.

The build-tree fixtures check source preservation, build paths and cleanup.
The compiler case checks stable object bytes and recorded paths for
original and generated sources. Cleanup preserves upstream backup files.
Source reads use `PIE_SOURCES` without copying `src` into the build tree.
The SETUP case resolves host headers through `hostinc`.
Native cases check helper cleanup for architecture-only and combined
host and SDK targets.

The install cases cover Libnotify, mDNSResponder, CommonCrypto and LLVM
runtimes. They pause SDK and staging copies during parallel builds.
Links wait for the completed copies while independent targets run.
They also check library bytes, modes and `.stub` handling.
LLVM cases cover fresh and existing CMake configurations.

With the workflows tools, run `pie check /path/to/pietoolchain`.
`debian/pie-checks.toml` selects the same entry points as CI:

* `toolchain` discovers every Python test under `toolchain/tests`.
  The reproducibility case invokes the checked-in wrapper with the host
  clang-21. It compiles an object and does not link a target program.
* `sdk` discovers `sdk/test_*.py` and checks runtime licence texts, provider
  versions, dependencies and source copyright merging. Packaging fixtures
  include mkdebs.py and a small debian/control. The umask case checks
  byte-identical debs and their versioned Provides field.
* `build-tree` checks the build and packaging paths with small source trees,
  source metadata snapshots and the host compiler. It also checks
  parallel SDK install and link ordering.
* `libxpc` calls `userland/libxpc/tests/run.sh` with `CC=clang-21`.
  It builds wire, object, connection and pipe cases with host stubs. The
  wire suite also runs with address and undefined behaviour sanitizers.

Missing tools fail the suites. Install the packages before retrying.
These checks do not exercise target kernel IPC. Libinfo lookup and
notification delivery still need target scenarios in the image repository.

The committed `.gitattributes` files keep this test directory and
`debian/pie-checks.toml` out of source archives made by `pie source`.
The exclusion applies to both full and reduced archives. The attribute
files also exclude themselves. CI runs the suites from a checkout.
