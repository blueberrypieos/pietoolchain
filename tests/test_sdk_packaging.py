# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import importlib.util
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "sdk" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def texts(tmp_path):
    directory = tmp_path / "sdk/licenses"
    directory.mkdir(parents=True)
    (directory / "BSD-2-Clause").write_text("first line\n\nlast line\n")
    return tmp_path


def test_runtime_licence_text_is_bundled_exactly(texts):
    module = load("mkdebs")
    result = module.with_texts(str(texts), "License: BSD-2-Clause\n The full text is in\n /usr/share/common-licenses/BSD-2-Clause.\n")
    assert result == "License: BSD-2-Clause\n first line\n .\n last line\n"
    assert "/usr/share/common-licenses/" not in result


@pytest.mark.parametrize("reference", [" The full text is in /usr/share/common-licenses/Absent.",
                                        " See /usr/share/common-licenses/BSD-2-Clause.",
                                        " The full text cannot be resolved."])
def test_runtime_refuses_missing_or_ambiguous_text(texts, reference):
    with pytest.raises(SystemExit):
        load("mkdebs").with_texts(str(texts), "License: BSD-2-Clause\n" + reference + "\n")


def test_sdk_texts_are_appended_once(texts):
    result = load("mkcopyright").with_texts(str(texts), "See /usr/share/common-licenses/BSD-2-Clause.\nAgain /usr/share/common-licenses/BSD-2-Clause.\n")
    assert result.count("Licence text: BSD-2-Clause") == 1
    assert result.endswith("first line\n\nlast line\n")


def test_provider_versions_and_runtime_dependencies(tmp_path):
    module = load("mkdebs")
    (tmp_path / "debian").mkdir()
    (tmp_path / "debian/control").write_text(
        "Source: fixture\n\nPackage: sdk\nArchitecture: all\n\n"
        "Package: libone\nX-Pie-Project: one\nX-Pie-Version: 2\nX-Pie-ABI-Minimum: 1\n\n"
        "Package: libtwo\nX-Pie-Project: two\nX-Pie-Version: 3\nX-Pie-ABI-Minimum: 1\n")
    assert module.sdk_provides(str(tmp_path)) == "libone (= 2), libtwo (= 3)"
    packages = [p for p in module.stanzas(str(tmp_path / "debian/control")) if "X-Pie-Project" in p]
    minimums = module.runtime_minimums(packages)
    assert module.runtime_depends({"libtwo", "external", "libone"}, minimums) == "external, libone (>= 1), libtwo (>= 1)"


def test_source_copyright_merges_matching_holders_without_changing_coverage():
    module = load("mksrccopyright")
    paragraphs = ["Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/",
                  "Files: one\nCopyright: 2026 Example\nLicense: BSD-2-Clause",
                  "Files: two\nCopyright: 2026 Example\nLicense: BSD-2-Clause"]
    result = module.join_all(paragraphs, ["one", "two"])
    assert len(result) == 2
    before = module.last_matches(paragraphs, ["one", "two"])
    after = module.last_matches(result, ["one", "two"])
    assert set(before) == set(after) == {"one", "two"}
    for name in before:
        assert module.rest_of(paragraphs[before[name][0]]) == module.rest_of(result[after[name][0]])
    assert set(module.field(result[1], "Files").split()) == {"one", "two"}


@pytest.mark.parametrize("minimum", [None, "invalid", "4"])
def test_runtime_rejects_missing_invalid_or_future_minimum(minimum):
    package = {"Package": "libfixture", "X-Pie-Version": "3"}
    if minimum is not None:
        package["X-Pie-ABI-Minimum"] = minimum
    with pytest.raises(SystemExit):
        load("mkdebs").runtime_minimums([package])


def test_runtime_archive_dependencies_survive_packaging_revisions(tmp_path, monkeypatch):
    module = load("mkdebs")
    pie = tmp_path / "source"
    (pie / "debian").mkdir(parents=True)
    (pie / "debian/changelog").write_text(
        "fixture (1) unstable; urgency=medium\n\n  * Build fixture.\n\n"
        " -- Maxwell <maxwell@blueberrypieos.com>  Wed, 25 Nov 2026 06:30:00 +0000\n")
    tool = pie / "debian/build/toolchain/prefix/bin/aarch64-apple-darwin-otool"
    tool.parent.mkdir(parents=True)
    tool.write_text(
        "#!/usr/bin/python3\nfrom pathlib import Path\nimport sys\n"
        "print(sys.argv[-1] + ':')\n"
        "if Path(sys.argv[-1]).name == 'libfirst.dylib':\n"
        " print('  /usr/lib/libsecond.dylib (compatibility version 1.0.0, current version 1.0.0)')\n")
    tool.chmod(0o755)
    for project in ("first", "second"):
        root = pie / "debian/build/build/pkgroot" / project / "usr/lib"
        root.mkdir(parents=True)
        (root / ("lib" + project + ".dylib")).write_bytes(b"\xcf\xfa\xed\xfe")
        notice = pie / "userland" / project / "copyright"
        notice.parent.mkdir(parents=True)
        notice.write_text("Fixture notice\nLicense: BSD-2-Clause\n")
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "1700000000")
    monkeypatch.setenv("DEB_HOST_ARCH", "darwin-arm64")
    for release, minimum in (("2", "1"), ("3", "1"), ("4", "2")):
        control = ("Source: fixture\nSection: libs\nPriority: required\n"
                   "Maintainer: Maxwell <maxwell@blueberrypieos.com>\n\n")
        for project in ("first", "second"):
            control += ("Package: lib" + project + "\nArchitecture: darwin-arm64\n"
                        "Essential: yes\nDepends: ${runtime:Depends}\nX-Pie-Project: " + project +
                        "\nX-Pie-Version: " + release + "\nX-Pie-ABI-Minimum: " + minimum +
                        "\nDescription: fixture runtime library\n\n")
        (pie / "debian/control").write_text(control)
        module.arch(str(pie))
        package = tmp_path / ("libfirst_" + release + "_darwin-arm64.deb")
        assert subprocess.check_output(["dpkg-deb", "-f", str(package), "Depends"], text=True).strip() == "libsecond (>= " + minimum + ")"
        assert subprocess.check_output(["dpkg-deb", "-f", str(package), "Essential"], text=True).strip() == "yes"
        assert module.sdk_provides(str(pie)) == "libfirst (= " + release + "), libsecond (= " + release + ")"
