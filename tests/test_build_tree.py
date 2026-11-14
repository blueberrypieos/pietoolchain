# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


def snapshot(tree, packages=()):
    state = {}
    for path in [tree, *tree.rglob("*")]:
        name = path.relative_to(tree).as_posix()
        if name == "debian/build" or name.startswith("debian/build/"):
            continue
        if name == "debian/files" or path.parent == tree / "debian" and name.endswith(".substvars"):
            continue
        if any(name == "debian/" + p or name.startswith("debian/" + p + "/") for p in packages):
            continue
        info = path.lstat()
        content = os.readlink(path) if path.is_symlink() else (
            hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None)
        state[name] = (info.st_mode, info.st_uid, info.st_gid,
                       info.st_mtime_ns if name != "debian" else None, content)
    return state


def fixture_tree(tree):
    tree.mkdir()
    for name in ("build.sh", "debian/rules", "debian/changelog",
                 "toolchain/make-sdk.sh", "toolchain/hostcompat/sdk.py",
                 "toolchain/kernel/kernel-build-common.sh", "userland/common.mk",
                 "userland/tools/man.mk"):
        target = tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    shutil.copytree(ROOT / "sdk", tree / "sdk", ignore=shutil.ignore_patterns("__pycache__"))
    write(tree / "debian/control", "Source: pietoolchain\nSection: devel\nPriority: optional\n"
          "Maintainer: Example <maintainer@example.org>\n\n"
          "Package: pietoolchain\nArchitecture: linux-any\nDescription: fixture toolchain\n\n"
          "Package: pietoolchain-sdk\nArchitecture: all\nProvides: ${sdk:Provides}\n"
          "Description: fixture SDK\n\n"
          "Package: libfixture\nArchitecture: darwin-arm64\nX-Pie-Project: Libc\n"
          "X-Pie-Version: 1\nDepends: ${runtime:Depends}\nDescription: fixture library\n")
    write(tree / "xnu/Makefile", "fixture=original\n")
    write(tree / "xnu/EXTERNAL_HEADERS/ptrcheck.h", "fixture header\n")
    write(tree / "xnu-patches/fixture.patch", "--- a/Makefile\n+++ b/Makefile\n@@ -1 +1 @@\n"
          "-fixture=original\n+fixture=patched\n")
    write(tree / "toolchain/build-cctools.sh", '#!/bin/bash\nset -eu\n'
          't=$(cd "$(dirname "$0")" && pwd)\n'
          'build=${BUILD:-$t/build}; prefix=${PREFIX:-$t/prefix}\n'
          'mkdir -p "$build" "$prefix/bin" "$prefix/lib/cctools-host"\n'
          'touch "$build/cctools-built"\n'
          'printf "#!/bin/sh\\nexit 0\\n" > "$prefix/bin/aarch64-apple-darwin-otool"\n'
          'chmod 755 "$prefix/bin/aarch64-apple-darwin-otool"\n'
          'touch "$prefix/lib/cctools-host/libstuff.a" "$prefix/lib/cctools-host/libmacho.a"\n',
          executable=True)
    write(tree / "toolchain/build-mig.sh", '#!/bin/bash\nset -eu\n'
          't=$(cd "$(dirname "$0")" && pwd)\n'
          'build=${BUILD:-$t/build}; prefix=${PREFIX:-$t/prefix}\n'
          'mkdir -p "$build/mig" "$prefix/bin" "$prefix/libexec"\n'
          'printf "#!/bin/sh\\nexit 0\\n" > "$prefix/bin/mig"\n'
          'cp "$prefix/bin/mig" "$prefix/libexec/migcom"\n'
          'chmod 755 "$prefix/bin/mig" "$prefix/libexec/migcom"\n', executable=True)
    write(tree / "toolchain/xnu-make", '#!/bin/bash\nset -eu\n'
          't=$(cd "$(dirname "$0")" && pwd)\n'
          'for header in mach-o/arch.h mach-o/swap.h; do '
          'cmp "$t/hostinc/$header" "$PIE_SOURCES/cctools-port/cctools/include/$header"; done\n'
          'test "$(cat Makefile)" = fixture=patched\n'
          'for arg; do case "$arg" in OBJROOT=*|SYMROOT=*|DSTROOT=*) '
          'mkdir -p "${arg#*=}";; esac; done\n'
          'for arg; do case "$arg" in DSTROOT=*) '
          'mkdir -p "${arg#*=}/usr/include"; '
          'echo fixture > "${arg#*=}/usr/include/kernel.h";; esac; done\n', executable=True)
    write(tree / "toolchain/kernel/build-firehose.sh", '#!/bin/bash\nset -eu\n'
          'TOOLCHAIN=$(cd "$(dirname "$0")/.." && pwd)\n'
          'BUILD=libfirehose_kernel\n'
          'source "$TOOLCHAIN/kernel/kernel-build-common.sh"\n'
          'pie_kernel_setup libdispatch\n'
          'mkdir -p "$BUILD" "$SDK/usr/local/lib/kernel"\n'
          'echo fixture > "$SDK/usr/local/lib/kernel/libfirehose_kernel.a"\n', executable=True)
    (tree / "src/libdispatch").mkdir(parents=True)
    (tree / "toolchain/hostinc").symlink_to("../src/cctools-port/cctools/include")
    for header in ("mach-o/arch.h", "mach-o/swap.h"):
        write(tree / "src/cctools-port/cctools/include" / header, "fixture header\n")
    write(tree / "src/AvailabilityVersions/Makefile", "install:\n"
          "\tmkdir -p $(OBJROOT) $(SYMROOT) $(DSTROOT)/usr/include\n"
          "\tpython3 -c 'import fixture'\n"
          "\tcp AvailabilityInternal.h $(DSTROOT)/usr/include/\n")
    write(tree / "src/AvailabilityVersions/fixture.py", "value = 1\n")
    write(tree / "src/AvailabilityVersions/AvailabilityInternal.h",
          "#define __API_AVAILABLE_PLATFORM_xros(x) fixture\n")
    (tree / "toolchain/sdk-overlay").mkdir()
    write(tree / "toolchain/sdk-overlay/usr/include/TargetConditionals.h", "fixture header\n")
    script = (tree / "build.sh").read_text()
    projects = set()
    for group in ("HEADERS", "LIBRARIES", "PROGRAMS"):
        projects.update(p.split(":")[0] for p in re.search(group + '="([^"]+)"', script)[1].split())
    projects.add("stubs")
    for project in projects:
        write(tree / "userland" / project / "Makefile", f"PROJECT := {project}\n"
              "include ../common.mk\n"
              "installhdrs install install-libc install-lib install-unwind:\n"
              "\tmkdir -p $(ROOT)/usr/lib $(SDK)/usr/lib $(OBJ) $(SRCROOT)\n"
              "\tprintf 'fixture library\\n' > $(SDK)/usr/lib/$(PROJECT).a\n"
              "\tcp $(SDK)/usr/lib/$(PROJECT).a $(ROOT)/usr/lib/\n"
              "\ttouch $(OBJ)/built\n")
        write(tree / "userland" / project / "copyright",
              "Files: *\nCopyright: 2026 Example\nLicense: APSL-2.0\n")
        if project == "Libc":
            makefile = tree / "userland" / project / "Makefile"
            makefile.write_text(makefile.read_text() +
                                "\tprintf '\\317\\372\\355\\376fixture\\n' > $(ROOT)/usr/lib/libfixture.dylib\n"
                                "\tcp $(ROOT)/usr/lib/libfixture.dylib $(SDK)/usr/lib/\n")
        if project == "copyfile":
            makefile = tree / "userland" / project / "Makefile"
            makefile.write_text("TAG := fixture\n" + makefile.read_text())
            write(tree / "src/copyfile/fixture.h", "fixture header\n")
            write(tree / "src/copyfile/unused.bak", "upstream backup\n")


def test_build_and_packages_leave_source_unchanged(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    before = snapshot(tree)
    environment = dict(os.environ, SOURCE_DATE_EPOCH="1700000000", PIE_SDK_PACKAGE_VERSION="0.2.0",
                       DEB_HOST_ARCH="darwin-arm64")
    environment.pop("PYTHONDONTWRITEBYTECODE", None)
    subprocess.run([tree / "build.sh"], cwd=tmp_path, env=environment,
                   check=True, capture_output=True, timeout=60)
    assert snapshot(tree) == before
    work = tree / "debian/build"
    assert (work / "xnu/Makefile").read_text() == "fixture=patched\n"
    assert (work / "build/hdrs/obj").is_dir()
    assert (work / "toolchain/build/mig").is_dir()
    assert (work / "build/userland/Libc/built").exists()
    assert (work / "build/src/Libc").is_dir()
    assert not (work / "src").exists()
    assert not (work / "src").is_symlink()
    assert (work / "toolchain/hostinc").resolve(strict=True) == tree / "src/cctools-port/cctools/include"
    assert os.readlink(tree / "toolchain/hostinc") == "../src/cctools-port/cctools/include"
    assert (work / "build/src/copyfile/fixture.h").read_text() == "fixture header\n"
    assert (work / "build/notices/Libc").is_file()
    assert (work / "build/providers/ORDER").is_file()
    assert (work / "build/pietoolchain-sdk_0.2.0_all.deb").is_file()
    assert not list(tree.glob("**/__pycache__"))
    package = work / "build/pietoolchain-sdk_0.2.0_all.deb"
    first = package.read_bytes()
    subprocess.run([tree / "build.sh", "sdk"], cwd=tmp_path, env=environment,
                   check=True, capture_output=True, timeout=30)
    assert package.read_bytes() == first
    assert snapshot(tree) == before
    for kind in ("arch", "indep"):
        subprocess.run(["python3", tree / "sdk/mkdebs.py", tree, kind], cwd=tree,
                       env=environment, check=True, capture_output=True, timeout=30)
    assert (tmp_path / "libfixture_1_darwin-arm64.deb").is_file()
    assert (tmp_path / "pietoolchain-sdk_0.2.0_all.deb").is_file()
    assert snapshot(tree, ("libfixture", "pietoolchain-sdk")) == before
    subprocess.run([tree / "debian/rules", "clean"], cwd=tree, check=True, capture_output=True)
    assert not work.exists()
    assert snapshot(tree) == before


def test_native_targets_remove_debhelper_state(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    shutil.copytree(ROOT / "toolchain/bin", tree / "toolchain/bin")
    shutil.copytree(ROOT / "share", tree / "share")
    (tree / "toolchain/iig").mkdir()
    write(tree / "debian/hostinc", "")
    write(tree / "src/llvm-project/llvm/cmake/config.guess", "#!/bin/sh\ntimestamp='2000-01-01'\n")
    for name in ("comparison.ll.bak", "comparison.ll~", "#comparison.ll#"):
        write(tree / "src/llvm-project/llvm/test/fixture" / name, "upstream fixture\n")
    write(tree / "debian/build/stamp-check", "")
    before = snapshot(tree)
    environment = dict(os.environ, DEB_BUILD_OPTIONS="nocheck nostrip", DH_COMPAT="13")
    for target in ("build-arch", "binary-arch", "binary"):
        subprocess.run([tree / "debian/rules", target, "DEB_HOST_ARCH_OS=linux"], cwd=tree,
                       env=environment, check=True, capture_output=True, timeout=30)
        assert snapshot(tree, ("pietoolchain", "pietoolchain-sdk")) == before
        assert not (tree / "debian/.debhelper").exists()
        assert (tree / "debian/build/stamp-host").exists()
    assert (tmp_path / "pietoolchain_0.2.0_amd64.deb").is_file()
    assert (tmp_path / "pietoolchain-sdk_0.2.0_all.deb").is_file()
    assert (tree / "debian/files").is_file()
    subprocess.run([tree / "debian/rules", "clean"], cwd=tree, check=True, capture_output=True)
    assert not (tree / "debian/build").exists()
    assert snapshot(tree) == before


def test_host_cleanup_preserves_sources_and_package_outputs(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    for name in ("comparison.ll.bak", "comparison.ll~", "#comparison.ll#", "data.debhelper"):
        write(tree / "src/llvm-project/llvm/test/fixture" / name, "upstream fixture\n")
    preserved = ("debian/build/object.o", "debian/pietoolchain/usr/bin/tool",
                 "debian/pietoolchain.substvars", "debian/files", "debian/source.bak")
    for name in preserved:
        write(tree / name, "build output\n")
    before = snapshot(tree)
    state = ("debian/.debhelper/state", "debian/debhelper-build-stamp",
             "debian/pietoolchain.debhelper.log", "debian/pietoolchain.postinst.debhelper")
    for name in state:
        write(tree / name, "helper state\n")
    subprocess.run([tree / "debian/rules", "clean-host"], cwd=tree, check=True, capture_output=True)
    assert snapshot(tree) == before
    assert not (tree / "debian/.debhelper").exists()
    assert all(not (tree / name).exists() for name in state)
    assert all((tree / name).read_text() == "build output\n" for name in preserved)


def test_userland_uses_original_sources_and_stable_prefixes(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    before = snapshot(tree)
    work = tree / "debian/build"
    shutil.copytree(tree / "userland", work / "userland")
    (work / "debian").mkdir()
    shutil.copy2(tree / "debian/control", work / "debian/control")
    environment = dict(os.environ, PIE_SOURCES=str(tree / "src"))
    probe = "paths:\n\t@printf '%s\\n' '$(SRC)' '$(LLVM)' '$(SOURCE_CFLAGS)' '$(BASE_CFLAGS)' '$(CMAKE_FLAGS)'\n"
    for project in ("compiler-rt", "llvm-runtimes"):
        shutil.copy2(ROOT / "userland" / project / "Makefile", work / "userland" / project / "Makefile")
        values = subprocess.check_output(["make", "-s", "--no-print-directory", "-C",
                                          work / "userland" / project, "--eval", probe, "paths"],
                                         env=environment, text=True).splitlines()
        assert values[2] == f"-ffile-prefix-map={tree}/src=/usr/src/pie/src/src"
        assert values[2] in values[3]
        if project == "compiler-rt":
            assert values[0] == str(tree / "src/llvm-project/compiler-rt/lib/builtins")
        else:
            assert values[1] == str(tree / "src/llvm-project")
            assert values[4].count(values[2]) == 2
    assert snapshot(tree) == before


def test_compilation_keeps_existing_recorded_paths(tmp_path):
    outputs = []
    for name, inside in (("previous", ""), ("relocated", "debian/build")):
        source = tmp_path / name
        work = source / inside
        shutil.copytree(ROOT / "toolchain/bin", work / "toolchain/bin")
        sources = source / "src"
        for path in ("userland/common.mk", "userland/tools/man.mk"):
            target = work / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, target)
        project = work / "userland/fixture"
        write(project / "Makefile", "PROJECT := fixture\ninclude ../common.mk\n"
              "flags:\n\t@printf '%s\\n' '$(SOURCE_CFLAGS)'\n")
        flag = subprocess.check_output(["make", "-s", "--no-print-directory", "-C", project, "flags"],
                                       env=dict(os.environ, PIE_SOURCES=str(sources)), text=True).strip()
        units = (sources / "fixture/source.c", work / "build/src/fixture/generated.c")
        for unit in units:
            write(unit, 'const char *path = __FILE__; int answer(void) { return 42; }\n')
        group = []
        for number, unit in enumerate(units):
            output = work / f"unit-{number}.o"
            subprocess.run([work / "toolchain/bin/clang", "-g", "-c", unit, "-o", output,
                            flag],
                           env=dict(os.environ, PIE_REPRODUCIBLE="1", PIE_REPRO_SOURCE=str(work),
                                    SOURCE_DATE_EPOCH="1700000000"), check=True)
            group.append(output.read_bytes())
        outputs.append(group)
    assert outputs[0] == outputs[1]
    assert b"/usr/src/pie/src/src/fixture/source.c" in outputs[0][0]
    assert b"/usr/src/pie/src/build/src/fixture/generated.c" in outputs[0][1]
    assert all(b"debian/build" not in output for output in outputs[1])
