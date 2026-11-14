# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

import pytest


ROOT = Path(__file__).resolve().parents[1]


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def executable(path, content):
    write(path, "#!" + sys.executable + "\n" + content)
    path.chmod(0o755)


def wait_for(path):
    deadline = time.monotonic() + 5
    while not path.exists():
        assert time.monotonic() < deadline, str(path)
        time.sleep(0.01)


@pytest.mark.parametrize("paused_copy", ["sdk", "root"])
@pytest.mark.parametrize("project_name,configured", [
    ("Libnotify", False), ("mDNSResponder", False), ("CommonCrypto", False),
    ("llvm-runtimes", False), ("llvm-runtimes", True),
])
def test_links_wait_for_atomic_library_install_with_parallel_make(tmp_path, project_name, configured, paused_copy):
    tree = tmp_path / "tree"
    project = tree / "userland" / project_name
    for name in (f"userland/{project_name}/Makefile", "userland/common.mk", "userland/tools/man.mk"):
        target = tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    source = tree / "build/src" / project_name
    for name in ("notify.h", "notify_keys.h", "notify_private.h", "notifyutil/notifyutil.c",
                 "notifyd/notify.conf.MacOSX", "mDNSShared/dns_sd.h", "mDNSShared/dns_sd_private.h",
                 "mDNSShared/mDNSResponder.8", "include/Private/CommonBaseXX.h",
                 "include/Private/CommonCRC.h", "include/Private/CommonNumerics.h", ".fetched"):
        write(source / name, "fixture\n")
    objects = tree / "build/userland" / project_name
    write(objects / "exports.txt", "fixture exports\n")
    write(objects / "ccregression-src/.copied", "fixture\n")
    library_name = {"Libnotify": "libsystem_notify.dylib", "mDNSResponder": "libsystem_dnssd.dylib",
                    "CommonCrypto": "libcommonCrypto.dylib", "llvm-runtimes": "libunwind.dylib"}[project_name]
    library = objects / library_name
    write(library, "finished library\n")
    write(objects / "notifyutil.o", "fixture object\n")
    sdk = tree / "toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/system" / library_name
    staged = tree / "build/pkgroot" / project_name / "usr/lib/system" / library_name
    write(sdk, "previous library\n")
    write(staged, "previous library\n")
    write(Path(str(sdk) + ".stub"), "stub marker\n")
    installer = tmp_path / "slow-install"
    linker = tmp_path / "check-link"
    executable(installer, '''import os
from pathlib import Path
import subprocess
import sys
import time
destination = Path(sys.argv[-1])
if destination.parent == Path(os.environ["PAUSED_LIBRARY"]).parent:
    destination.write_bytes(b"")
    Path(os.environ["INSTALL_STARTED"]).touch()
    deadline = time.monotonic() + 10
    while not Path(os.environ["INSTALL_RELEASE"]).exists():
        assert time.monotonic() < deadline
        time.sleep(0.01)
subprocess.run([os.environ["REAL_INSTALL"], *sys.argv[1:]], check=True)
''')
    executable(linker, '''import os
from pathlib import Path
import sys
import time
if sys.argv[1:] == ["independent"]:
    deadline = time.monotonic() + 10
    while not Path(os.environ["INSTALL_STARTED"]).exists():
        assert time.monotonic() < deadline
        time.sleep(0.01)
    Path(os.environ["INDEPENDENT_DONE"]).touch()
else:
    assert "install-lib" not in sys.argv
    assert "install-unwind" not in sys.argv
    if Path(sys.argv[0]).name == "check-cmake":
        directory = Path(sys.argv[sys.argv.index("-B") + 1])
        directory.mkdir(parents=True, exist_ok=True)
        outputs = [directory / "configure"]
    elif Path(sys.argv[0]).name == "check-ninja":
        directory = Path(sys.argv[sys.argv.index("-C") + 1]) / "lib"
        directory.mkdir(parents=True, exist_ok=True)
        outputs = [directory / name for name in ("libc++.1.0.dylib", "libc++abi.1.0.dylib")]
    else:
        outputs = [Path(sys.argv[sys.argv.index("-o") + 1])]
    for output in outputs:
        Path(os.environ["LINK_MARKERS"], output.name).touch()
        if output.name == "notifyutil":
            assert str(output.with_suffix(".o")) in sys.argv
    assert Path(os.environ["SDK_LIBRARY"]).read_bytes() == b"finished library\\n"
    assert Path(os.environ["ROOT_LIBRARY"]).read_bytes() == b"finished library\\n"
    assert not Path(os.environ["SDK_LIBRARY"] + ".stub").exists()
    for output in outputs:
        output.write_bytes(b"fixture executable\\n")
''')
    cmake, ninja = tmp_path / "check-cmake", tmp_path / "check-ninja"
    shutil.copy2(linker, cmake)
    shutil.copy2(linker, ninja)
    write(source / "mDNSPosix/Makefile", ".PHONY: Daemon\nDaemon:\n\tmkdir -p $(BUILDDIR)\n"
          "\t$(LD) -o $(BUILDDIR)/mdnsd\n")
    if configured:
        write(Path(str(objects) + "-libs") / "configured", "fixture\n")
    write(project / "driver.mk", "include Makefile\n.PHONY: fetch independent\nfetch:\n"
          "independent:\n\t$(CC) independent\n")
    started, release, independent, markers = [tmp_path / name for name in ("started", "release", "independent", "links")]
    markers.mkdir()
    environment = dict(os.environ, SDK_LIBRARY=str(sdk), ROOT_LIBRARY=str(staged),
                       INSTALL_STARTED=str(started), INSTALL_RELEASE=str(release),
                       INDEPENDENT_DONE=str(independent), LINK_MARKERS=str(markers),
                       REAL_INSTALL=shutil.which("install"),
                       PAUSED_LIBRARY=str(sdk if paused_copy == "sdk" else staged))
    log = tmp_path / "make.log"
    with log.open("w") as output:
        goals = ["install", "regression"] if project_name == "CommonCrypto" else ["install"]
        process = subprocess.Popen(["make", "-j4", "-f", "driver.mk", *goals, "independent",
                                    "TAG=", "OBJS=", "NOTIFYD_OBJS=", "CLIENT_OBJS=", "UNWIND_OBJS=",
                                    "REGRESSION_TESTS=", "install_xcode_man=", "CMAKE=" + str(cmake),
                                    "NINJA=" + str(ninja), "CC=" + str(linker), "INSTALL=" + str(installer)],
                                   cwd=project, env=environment, stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            wait_for(started)
            wait_for(independent)
            assert process.poll() is None
            assert sdk.read_bytes() == (b"previous library\n" if paused_copy == "sdk" else b"finished library\n")
            assert staged.read_bytes() == b"previous library\n"
            assert Path(str(sdk) + ".stub").exists()
            assert not list(markers.iterdir())
        finally:
            release.touch()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise
    assert process.returncode == 0, log.read_text()
    expected = {"Libnotify": {"notifyd", "notifyutil"}, "mDNSResponder": {"mdnsd"},
                "CommonCrypto": {"ccregression"}, "llvm-runtimes": {"libc++.1.0.dylib", "libc++abi.1.0.dylib"}}
    if project_name == "llvm-runtimes" and not configured:
        expected[project_name].add("configure")
    assert {path.name for path in markers.iterdir()} == expected[project_name]
    assert sdk.read_bytes() == staged.read_bytes() == library.read_bytes()
    assert sdk.stat().st_mode & 0o777 == staged.stat().st_mode & 0o777 == 0o755
    assert not Path(str(sdk) + ".new").exists()
    assert not Path(str(staged) + ".new").exists()


def test_install_both_preserves_library_bytes_and_removes_stubs(tmp_path):
    for name in ("userland/common.mk", "userland/tools/man.mk"):
        target = tmp_path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    project = tmp_path / "userland/fixture"
    libraries = [tmp_path / "objects" / name for name in ("one.dylib", "two.dylib")]
    sdk, staged = tmp_path / "sdk", tmp_path / "root"
    for number, library in enumerate(libraries):
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_bytes(bytes(range(256)) * (number + 1))
        write(sdk / "usr/lib" / (library.name + ".stub"), "stub marker\n")
        write(staged / "usr/lib" / (library.name + ".stub"), "staging marker\n")
    write(project / "Makefile", "PROJECT := fixture\ninclude ../common.mk\n"
          "install:\n\t$(call install_both,$(LIBRARIES),usr/lib)\n")
    subprocess.run(["make", "-j4", "install", "SDK=" + str(sdk), "ROOT=" + str(staged),
                    "LIBRARIES=" + " ".join(str(path) for path in libraries)],
                   cwd=project, check=True, capture_output=True, timeout=15)
    for library in libraries:
        for destination in (sdk, staged):
            target = destination / "usr/lib" / library.name
            assert target.read_bytes() == library.read_bytes()
            assert target.stat().st_mode & 0o777 == 0o755
            assert not Path(str(target) + ".new").exists()
        assert not (sdk / "usr/lib" / (library.name + ".stub")).exists()
        assert (staged / "usr/lib" / (library.name + ".stub")).read_text() == "staging marker\n"
