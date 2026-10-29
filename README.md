# pietoolchain

An LLVM/Clang cross-compiler toolchain for darwin-arm64, packaged for
Ubuntu 26.04. It contains a clang-21 wrapper, Apple's cctools and ld64
linker, and make rules for building Debian packages of darwin-arm64 ports.
The darwin-arm64 headers and libraries that ports build against come from
the separate `pietoolchain-sdk` package, which `pietoolchain` depends on.

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

## Contents

| Path | Contents |
|---|---|
| `toolchain/bin/clang` (`cc`, `c++`, `clang++`) | A wrapper that runs Ubuntu's clang-21 for arm64 Darwin and links with ld64. With `PIE_REPRODUCIBLE=1`, it maps source paths to `/usr/src/pie`. |
| `toolchain/build-cctools.sh` | Builds cctools and ld64 from the `cctools-port` submodule, with `apple-libdispatch` linked in statically. `otool` is llvm-21's `llvm-otool`. |
| `toolchain/src` | The git submodules of cctools-port and apple-libdispatch. |
| `share/rules.mk` | The compiler flags, the dpkg build targets, reproducible-build settings and the `.deb` layout. |
| `share/tools.mk`, `share/pbxproj-*.py` | Rules for building tools from project files. |
| `debian/` | The packaging for `pietoolchain` itself. |

## Building

```sh
git submodule update --init
gbp buildpackage                          # ../build-area/pietoolchain_0.2.0-1_amd64.deb
```
