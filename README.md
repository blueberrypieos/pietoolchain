# pietoolchain

An LLVM/Clang cross-compiler toolchain for darwin-arm64, packaged for
Ubuntu 26.04, and the darwin-arm64 SDK it builds against. One source
package builds:

- `pietoolchain`: a clang-21 wrapper, cctools and the ld64 linker,
  `mig`, and make rules for building Debian packages of darwin-arm64 ports.
  It runs on the Linux build machine.
- `pietoolchain-sdk`: the darwin-arm64 headers, libraries and frameworks
  that ports build against. `pietoolchain` depends on it.
- The libSystem runtime packages (`libsystem-c`, `dyld`, `libcxx` and the
  others), darwin-arm64 packages built with the SDK.

## Packaging a port

A port's packaging repository lists `pietoolchain` in its Build-Depends.
Its `debian/rules` includes `/usr/share/pietoolchain/rules.mk`, followed by
the port's own build recipe, and the package is built with
`dpkg-buildpackage -a darwin-arm64`. The recipe installs into one root;
when `debian/control` names more than one package, each package after the
first takes the files that `debian/PACKAGE.files` lists (one glob per
line, such as `usr/include/*`), and the first gets the rest. The reusable GitHub workflow in
[blueberrypieos/workflows](https://github.com/blueberrypieos/workflows)
installs `pietoolchain` and `pietoolchain-sdk` and runs that build.

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
| `toolchain/bin/clang` (`cc`, `c++`, `clang++`) | A wrapper that runs Ubuntu's clang-21 for arm64 Darwin and links with ld64. With `PIE_REPRODUCIBLE=1`, it maps source paths to `/usr/src/pie`. |
| `toolchain/build-cctools.sh` | Builds cctools and ld64 from `src/cctools-port`, with `src/apple-libdispatch` linked in statically. `otool` is llvm-21's `llvm-otool`. |
| `toolchain/build-mig.sh`, `toolchain/mig/hostinc` | Builds `mig` and `migcom` (the Mach interface generator) from `src/bootstrap_cmds`, with the few Mach headers migcom needs on the host. `mig` preprocesses with pietoolchain's `cc`. |
| `toolchain/xnu-make`, `toolchain/bin/{xcrun,host-cc,host-c++,plutil,awk,install,ld-repro,iig}`, `toolchain/hostcompat`, `toolchain/iig` | Run xnu's make on Linux: the values and tools the xnu makefiles expect. `iig` generates DriverKit interfaces. |
| `toolchain/kernel` | Build a kernel's SDK and libraries and its kexts: `make-sdk.sh`, `build-*-kext.sh`, `build-cckext.sh`, `build-firehose.sh`, `kext.mk` and the kext tools. The kernel package passes the sources in. |
| `toolchain/make-sdk.sh`, `toolchain/sdk-overlay` | Make the SDK that `pietoolchain-sdk` packages: AvailabilityVersions, the overlay headers, xnu's headers and libfirehose_kernel. |
| `share/rules.mk` | The compiler flags, the dpkg build targets, reproducible-build settings and the `.deb` layout. |
| `share/tools.mk`, `share/pbxproj-*.py` | Rules for building tools from project files. |
| `share/pie-source-guard` | Checks that a port's source package holds only the files `debian/copyright` keeps. |
| `src/`, `xnu/` | The upstream sources, as git submodules. |
| `userland/`, `xnu-patches/` | How each libSystem project is built, and our patches to it. |
| `build.sh` | Builds the toolchain, then libSystem and the base libraries into the SDK. |
| `sdk/` | The SDK packages (`mkdebs.py`), the copyright files of the upstream sources, and the lists of the files the source package keeps. |
| `debian/` | The packaging. |

## Building

On Ubuntu 26.04:

```sh
git submodule update --init --recursive
sudo apt-get install dpkg-dev build-essential
sudo apt-get build-dep ./
PIE_FULL_SOURCE=1 dpkg-buildpackage -B -us -uc                  # ../pietoolchain_0.1.0_amd64.deb
PIE_FULL_SOURCE=1 dpkg-buildpackage -a darwin-arm64 -b -us -uc  # ../pietoolchain-sdk_0.1.0_all.deb and the runtime packages
```

`PIE_FULL_SOURCE=1` builds from the full checkout. The source package
keeps only the files of `src/` and `xnu/` that the build reads, and a
build from it runs without that variable. The release workflow makes the
source package with [workflows](https://github.com/blueberrypieos/workflows)'
`tools/pie-source` and builds each set of packages from its `.dsc`.

## Verifying a release

```sh
gh attestation verify FILE --repo blueberrypieos/pietoolchain
```

## Licences

This repository's own files are under the BSD 2-Clause licence
([LICENSE](LICENSE)). The upstream projects it builds keep their own
licences, which `debian/copyright` and each package's copyright file list.
