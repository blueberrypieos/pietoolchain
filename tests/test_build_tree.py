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
    write(tree / "debian/changelog", "pietoolchain (0.2.0) unstable; urgency=medium\n\n"
          "  * Fixture SDK.\n\n -- Example <maintainer@example.org>  Tue, 14 Nov 2023 22:13:20 +0000\n")
    write(tree / "debian/control", "Source: pietoolchain\nSection: devel\nPriority: optional\n"
          "Maintainer: Example <maintainer@example.org>\n\n"
          "Package: pietoolchain-sdk\nArchitecture: all\nProvides: ${sdk:Provides}\n"
          "Description: fixture SDK\n\n"
          "Package: libfixture\nArchitecture: darwin-arm64\nX-Pie-Project: Libc\n"
          "X-Pie-Version: 1\nX-Pie-ABI-Minimum: 1\nDepends: ${runtime:Depends}\nDescription: fixture library\n\n"
          "Package: pie-sdk\nArchitecture: darwin-arm64\nDepends: ${sdk:Depends}\n"
          "Description: fixture device SDK\n")
    write(tree / "xnu/Makefile", "fixture=original\n")
    write(tree / "xnu/EXTERNAL_HEADERS/ptrcheck.h", "fixture header\n")
    write(tree / "xnu-patches/fixture.patch", "--- a/Makefile\n+++ b/Makefile\n@@ -1 +1 @@\n"
          "-fixture=original\n+fixture=patched\n")
    installed = tree.parent / "installed/toolchain"
    for name in ("aarch64-apple-darwin-ld", "mig"):
        write(installed / "prefix/bin" / name, '#!/bin/sh\nprintf "installed tool\\n"\n', executable=True)
    write(installed / "prefix/bin/aarch64-apple-darwin-otool", '#!/bin/sh\nexit 0\n', executable=True)
    write(installed / "prefix/libexec/migcom", '#!/bin/sh\nexit 0\n', executable=True)
    for name in ("libstuff.a", "libmacho.a"):
        write(installed / "prefix/lib/cctools-host" / name, "installed library\n")
    for header in ("mach-o/arch.h", "mach-o/swap.h"):
        write(installed / "hostinc" / header, "installed header\n")
    write(tree / "toolchain/xnu-make", '#!/bin/bash\nset -eu\n'
          't=$(cd "$(dirname "$0")" && pwd)\n'
          'for header in mach-o/arch.h mach-o/swap.h; do '
          'cmp "$t/hostinc/$header" "$PIE_HOST_TOOLCHAIN/hostinc/$header"; done\n'
          'test "$("$t/prefix/bin/mig")" = "installed tool"\n'
          'test "$("$t/prefix/bin/aarch64-apple-darwin-ld")" = "installed tool"\n'
          'cmp "$t/prefix/lib/cctools-host/libstuff.a" "$PIE_HOST_TOOLCHAIN/prefix/lib/cctools-host/libstuff.a"\n'
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
    installed_before = snapshot(tmp_path / "installed")
    environment = dict(os.environ, SOURCE_DATE_EPOCH="1700000000", PIE_SDK_PACKAGE_VERSION="0.2.0",
                       DEB_HOST_ARCH="darwin-arm64",
                       PIE_HOST_TOOLCHAIN=str(tmp_path / "installed/toolchain"))
    environment.pop("PYTHONDONTWRITEBYTECODE", None)
    subprocess.run([tree / "build.sh"], cwd=tmp_path, env=environment,
                   check=True, capture_output=True, timeout=60)
    assert snapshot(tree) == before
    work = tree / "debian/build"
    assert (work / "xnu/Makefile").read_text() == "fixture=patched\n"
    assert (work / "build/hdrs/obj").is_dir()
    assert not (work / "toolchain/build/mig").exists()
    assert (work / "build/userland/Libc/built").exists()
    assert (work / "build/src/Libc").is_dir()
    assert not (work / "src").exists()
    assert not (work / "src").is_symlink()
    assert (work / "toolchain/hostinc").resolve(strict=True) == tmp_path / "installed/toolchain/hostinc"
    assert (work / "toolchain/prefix").resolve(strict=True) == tmp_path / "installed/toolchain/prefix"
    assert not (tree / "toolchain/hostinc").exists()
    assert snapshot(tmp_path / "installed") == installed_before
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
    assert snapshot(tmp_path / "installed") == installed_before
    for kind in ("arch", "indep"):
        subprocess.run(["python3", tree / "sdk/mkdebs.py", tree, kind], cwd=tree,
                       env=environment, check=True, capture_output=True, timeout=30)
    assert (tmp_path / "libfixture_1_darwin-arm64.deb").is_file()
    assert (tmp_path / "pietoolchain-sdk_0.2.0_all.deb").is_file()
    device = list(tmp_path.glob("pie-sdk_*_darwin-arm64.deb"))
    assert len(device) == 1
    listing = subprocess.check_output(["dpkg-deb", "-c", device[0]], text=True)
    assert "./usr/lib/pie/sdk/usr/include/" in listing
    assert "./usr/lib/pietoolchain/" not in listing
    fields = subprocess.check_output(["dpkg-deb", "-f", device[0], "Depends", "Provides"], text=True)
    assert fields.startswith("Depends: libfixture (= 1)") and "Provides" not in fields
    assert snapshot(tree, ("libfixture", "pietoolchain-sdk", "pie-sdk")) == before
    subprocess.run([tree / "debian/rules", "clean"], cwd=tree, check=True, capture_output=True)
    assert not work.exists()
    assert snapshot(tree) == before


def test_toolchain_stage_reuses_installed_inputs(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    installed = tmp_path / "installed/toolchain"
    environment = dict(os.environ, PIE_HOST_TOOLCHAIN=str(installed))
    before = snapshot(installed)
    for _ in range(2):
        subprocess.run([tree / "build.sh", "toolchain"], env=environment,
                       check=True, capture_output=True, timeout=30)
        work = tree / "debian/build"
        assert (work / "toolchain/prefix").is_symlink()
        assert not (installed / "prefix/prefix").exists()
        assert (work / "toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/kernel.h").exists()
        assert (work / "toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/local/lib/kernel/libfirehose_kernel.a").exists()
    assert snapshot(installed) == before
    (installed / "prefix/bin/mig").unlink()
    result = subprocess.run([tree / "build.sh", "toolchain"], env=environment, capture_output=True)
    assert result.returncode != 0
    assert b"missing host toolchain input:" in result.stderr


def test_native_arch_targets_do_not_build_host_tools(tmp_path):
    tree = tmp_path / "source"
    fixture_tree(tree)
    before = snapshot(tree)
    for target in ("build-arch", "binary-arch"):
        subprocess.run([tree / "debian/rules", target, "DEB_HOST_ARCH_OS=linux"], cwd=tree,
                       check=True, capture_output=True, timeout=30)
    assert snapshot(tree) == before
    assert not (tree / "debian/build").exists()


def test_xcrun_resolves_installed_helpers_and_keeps_local_compilers(tmp_path):
    local = tmp_path / "toolchain"
    installed = tmp_path / "installed/toolchain"
    (local / "bin").mkdir(parents=True)
    shutil.copy2(ROOT / "toolchain/bin/xcrun", local / "bin/xcrun")
    names = ("host-c++", "plutil", "ld-repro", "c++")
    for name in names:
        write(installed / "bin" / name, '#!/bin/sh\nprintf "installed helper\\n"\n', executable=True)
    write(local / "bin/clang++", '#!/bin/sh\nprintf "local compiler\\n"\n', executable=True)
    environment = dict(os.environ, PIE_HOST_TOOLCHAIN=str(installed))
    before = snapshot(installed)
    for name in names:
        found = subprocess.check_output([local / "bin/xcrun", "-find", name],
                                        env=environment, text=True).strip()
        assert found == str(installed / "bin" / name)
        assert Path(found).is_file()
        result = subprocess.check_output([local / "bin/xcrun", name], env=environment, text=True)
        assert result == "installed helper\n"
        assert not (local / "bin" / name).exists()
    found = subprocess.check_output([local / "bin/xcrun", "-find", "clang++"],
                                    env=environment, text=True).strip()
    assert found == str(local / "bin/clang++")
    sdk = subprocess.check_output([local / "bin/xcrun", "-show-sdk-path"],
                                  env=environment, text=True).strip()
    assert sdk == str(local / "sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
    assert snapshot(installed) == before
    (installed / "bin/c++").unlink()
    result = subprocess.run([local / "bin/xcrun", "-find", "c++"],
                            env=environment, capture_output=True)
    assert result.returncode != 0


def test_xnu_make_uses_installed_helpers(tmp_path):
    local = tmp_path / "toolchain"
    installed = tmp_path / "installed/toolchain"
    local.mkdir()
    shutil.copy2(ROOT / "toolchain/xnu-make", local / "xnu-make")
    for name in ("plutil", "ld-repro"):
        write(installed / "bin" / name, '#!/bin/sh\nexit 0\n', executable=True)
    write(local / "bin/make", '#!/bin/sh\nprintf "%s\\n" "${PIE_TARGET_LD:-}" '
          '"${PIE_REAL_TARGET_LD:-}" "$@"\n', executable=True)
    before = snapshot(installed)
    for objroot in ("", str(tmp_path / "obj")):
        environment = dict(os.environ, PIE_HOST_TOOLCHAIN=str(installed), PIE_TCSH="/bin/true",
                           PIE_REPRODUCIBLE="1", SOURCE_DATE_EPOCH="1700000000",
                           PIE_REPRO_OBJROOT=objroot)
        for name in ("PIE_TARGET_LD", "PIE_REAL_TARGET_LD"):
            environment.pop(name, None)
        result = subprocess.check_output([local / "xnu-make", "installhdrs"],
                                         env=environment, text=True).splitlines()
        assert "PLUTIL=" + str(installed / "bin/plutil") in result[2:]
        assert "XCRUN=" + str(local / "bin/xcrun") in result[2:]
        assert "HOST_CC=" + str(local / "bin/host-cc") in result[2:]
        assert result[0] == (str(installed / "bin/ld-repro") if objroot else "")
        assert result[1] == (str(local / "prefix/bin/aarch64-apple-darwin-ld") if objroot else "")
        assert not (local / "bin/plutil").exists()
        assert not (local / "bin/ld-repro").exists()
    assert snapshot(installed) == before


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
