# pietoolchain

The darwin-arm64 SDK and libSystem runtimes, built on Ubuntu 26.04.
This source package builds:

- `pietoolchain-sdk`: headers, libraries and frameworks for building ports.
- `pie-sdk`: the same SDK on the device, in `/usr/lib/pie/sdk`, for a compiler
  that runs there.
- The 29 libSystem runtime packages for darwin-arm64.

The host tools move to
[pietoolchain-host](https://github.com/blueberrypieos/pietoolchain-host).
It provides the compiler wrappers, cctools, the linker, `mig` and
`/usr/share/pietoolchain`. Its temporary `pietoolchain` compatibility
package depends on the host tools and SDK.

## Packaging a port

A port's packaging repository lists `pietoolchain` in its Build-Depends.
Its `debian/rules` includes `/usr/share/pietoolchain/rules.mk`, followed by
the port's own build recipe, and the package is built with
`dpkg-buildpackage -a darwin-arm64`. The recipe installs into one root;
when `debian/control` names more than one package, each package after the
first takes the files that `debian/PACKAGE.files` lists (one glob per
line, such as `usr/include/*`), and the first gets the rest. The reusable GitHub workflow in
[blueberrypieos/workflows](https://github.com/blueberrypieos/workflows)
installs the host tools and SDK and runs that build.

A port that builds against another port's `-dev` package names it in
Build-Depends. The build machine needs the darwin-arm64 architecture
(`dpkg --add-architecture darwin-arm64`) and the ports' repository as an
apt source for it. `/usr/share/pietoolchain/pie-cross-deps` then installs
the build dependencies. It resolves the darwin-arm64 ones on their own,
apart from the build machine's packages, and unpacks them into
`/var/lib/pietoolchain/cross-root`. The rest it installs on the build
machine as usual.

The build is then `dpkg-buildpackage -a darwin-arm64 -d`. `rules.mk`
builds against `SYSROOT`: the SDK with those packages' headers, libraries
and pkg-config files over it. The recipe finds them through
`PIE_PKG_CONFIG`, the target's pkg-config. `MESON_CROSS` and
`CMAKE_TOOLCHAIN` are a meson cross file and a CMake toolchain file for
the target.

## Contents

| Path | Contents |
|---|---|
| `toolchain/bin/clang` (`cc`, `clang++`) | A wrapper that runs Ubuntu's clang-21 for arm64 Darwin and links with ld64. With `PIE_REPRODUCIBLE=1`, it maps source paths to `/usr/src/pie`. |
| `toolchain/xnu-make`, `toolchain/bin/{xcrun,host-cc,awk,install}`, `toolchain/hostcompat`, the host headers | Run xnu's make on Linux: the values and tools the xnu makefiles expect. |
| `toolchain/make-sdk.sh`, `toolchain/sdk-overlay` | Make the SDK that `pietoolchain-sdk` packages: AvailabilityVersions, the overlay headers, xnu's headers and libfirehose_kernel. |
| `src/`, `xnu/` | The upstream sources, as git submodules. |
| `userland/`, `xnu-patches/` | How each libSystem project is built, and our patches to it. A project with a `debs` file installs its files from the pinned packages of a PIE port instead (`userland/debs.mk`). |
| `build.sh` | Uses the installed host tools, then builds libSystem and the base libraries into the SDK, and installs the libraries of PIE's ports from their packages. |
| `sdk/` | The SDK packages (`mkdebs.py`), the copyright files of the upstream sources, and the lists of the files the source package keeps. |
| `debian/` | The packaging. |

## Building

On Ubuntu 26.04:

```sh
git submodule update --init --recursive
sudo apt-get install dpkg-dev build-essential
sudo apt-get build-dep ./
PIE_FULL_SOURCE=1 dpkg-buildpackage -a darwin-arm64 -b -us -uc  # ../pietoolchain-sdk_0.3.2_all.deb, pie-sdk and the runtime packages
```

Install `pietoolchain-host` 0.3.0 or newer from the resolute archive before
building. `dpkg-buildpackage -A` builds only the SDK. The darwin-arm64 build
also produces the runtimes. Runtime Debian revisions are set by
`X-Pie-Version` in `debian/control`.

`X-Pie-ABI-Minimum` records each runtime's minimum compatible package version.
Runtime dependencies use this minimum. Packaging rebuilds keep it unchanged.
Raise it when runtime libraries start using an interface absent from that version.
Verify the exported interfaces before selecting a minimum.
pietoolchain-sdk and pie-sdk keep exact pins to the current `X-Pie-Version` values.
Applications that need a specific fix declare that newer runtime requirement.

`build.sh` links `debian/build/toolchain/prefix` and `hostinc` to the
installed `/usr/lib/pietoolchain/toolchain`. `PIE_HOST_TOOLCHAIN` may select
another installed tree. SDK helpers and wrappers run from `debian/build`.
The generated SDK stays beside those links at `debian/build/toolchain/sdk`.
The `c++`, `host-c++`, `plutil` and `ld-repro` helpers resolve through
the installed host package.
Compiler path maps and embedded source paths keep their existing values.
See [docs/build-tree.md](docs/build-tree.md).

The darwin-arm64 build takes the pinned packages of PIE's ports
(`userland/*/debs`) from `/var/cache/pietoolchain/debs` (`PIE_DEB_CACHE`),
or downloads them from https://blueberrypieos.com/apt (`PIE_APT`), and
checks their SHA-256.

`PIE_FULL_SOURCE=1` builds from the full checkout. The source package
keeps the files the build reads and the records of the vendored code. A
build from it runs without that variable. The release workflow makes the
source package with
[workflows](https://github.com/blueberrypieos/workflows)' `tools/pie-source`
and builds each set of packages from its `.dsc`.

## Verifying a release

```sh
gh attestation verify FILE --repo blueberrypieos/pietoolchain
```

## Licences

This repository's own files are under the BSD 2-Clause licence
([LICENSE](LICENSE)). The upstream projects it builds keep their own
licences, which `debian/copyright` and each package's copyright file list.
