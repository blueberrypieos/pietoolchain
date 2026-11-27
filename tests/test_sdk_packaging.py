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


def test_device_sdk_is_a_darwin_package_with_exact_runtime_pins():
    module = load("mkdebs")
    pars = {p.get("Package"): p for p in module.stanzas(str(ROOT / "debian/control"))}
    sdk = pars["pie-sdk"]
    assert sdk["Architecture"] == "darwin-arm64"
    assert "Provides" not in sdk and "X-Pie-Project" not in sdk
    assert sdk["Depends"].startswith("${sdk:Depends}")
    pins = module.sdk_provides(str(ROOT))
    runtimes = [p for p in pars.values() if "X-Pie-Project" in p]
    assert pins.count("(= ") == len(runtimes)


def fake_pie(tmp_path, elf):
    pie = tmp_path / "pie"
    (pie / "sdk").mkdir(parents=True)
    (pie / "debian/build").mkdir(parents=True)
    (pie / "sdk/README.source").write_text("one\n")
    (pie / "sdk/README.source.ports").write_text("two\n")
    script = pie / "sdk/mksdk.sh"
    payload = "printf '\\177ELF' > \"$S/usr/lib/tool\"" if elf else "true"
    script.write_text(
        "#!/bin/sh\nset -e\n[ \"$1 $2\" = \"-d -r\" ]\nR=$3\n"
        "S=\"$R/usr/lib/pietoolchain/toolchain/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk\"\n"
        "mkdir -p \"$S/usr/include\" \"$S/usr/lib\" \"$R/usr/share/doc/pietoolchain-sdk\"\n"
        "echo '#define X 1' > \"$S/usr/include/x.h\"\n"
        "printf '\\317\\372\\355\\376' > \"$S/usr/lib/libx.dylib\"\n"
        + payload + "\n"
        "echo copyright > \"$R/usr/share/doc/pietoolchain-sdk/copyright\"\n")
    script.chmod(0o755)
    return pie


def test_device_sdk_moves_the_sdk_and_its_copyright(tmp_path, monkeypatch):
    module = load("mkdebs")
    pie = fake_pie(tmp_path, elf=False)
    made = {}
    monkeypatch.setattr(module, "sdk_provides", lambda p: "libx (= 1)")
    monkeypatch.setattr(module, "finish", lambda p, pkg, dst, **kw: made.update(pkg=pkg, dst=dst, **kw))
    module.device_sdk(str(pie))
    dst = Path(made["dst"])
    assert made["pkg"] == "pie-sdk"
    assert made["substvars"] == [("sdk:Depends", "libx (= 1)")]
    assert (dst / "usr/lib/pie/sdk/usr/include/x.h").is_file()
    assert (dst / "usr/lib/pie/sdk/usr/lib/libx.dylib").is_file()
    assert (dst / "usr/share/doc/pie-sdk/copyright").read_text() == "copyright\n"
    assert (dst / "usr/share/doc/pie-sdk/README.source").read_text() == "one\ntwo\n"
    assert not (dst / "usr/lib/pietoolchain").exists()
    assert not (pie / "debian/build/pie-sdk").exists()


def test_device_sdk_refuses_host_files(tmp_path, monkeypatch):
    module = load("mkdebs")
    pie = fake_pie(tmp_path, elf=True)
    monkeypatch.setattr(module, "sdk_provides", lambda p: "")
    monkeypatch.setattr(module, "finish", lambda *a, **kw: pytest.fail("packaged host files"))
    with pytest.raises(SystemExit):
        module.device_sdk(str(pie))


def test_device_copyright_bundles_every_text(texts):
    module = load("mkcopyright")
    (texts / "sdk/licenses/Apache-2.0").write_text("apache\n")
    (texts / "sdk/licenses/GPL-2.0-or-later").write_text("gpl\n")
    text = ("The full text is in /usr/share/common-licenses/Apache-2.0.\n"
            "The full text is in /usr/share/common-licenses/GPL-2.0-or-later.\n")
    host = module.with_texts(str(texts), text)
    assert "/usr/share/common-licenses/Apache-2.0" in host and "/usr/share/common-licenses/GPL-2." in host
    device = module.with_texts(str(texts), text, device=True)
    assert "/usr/share/common-licenses/" not in device
    assert device.count("Licence text: Apache-2.0") == 1
    assert device.count("Licence text: GPL-2.0-or-later") == 1
    assert device.endswith("gpl\n")


@pytest.mark.parametrize("args, title, texts_line", [
    ([], "pietoolchain-sdk: ", "the texts of uncommon licences are at the end."),
    (["--device"], "pie-sdk: ", "the texts of the licences they name are at the end.")])
def test_copyright_title_names_the_package(texts, args, title, texts_line):
    notice = texts / "sdk/copyright/fixture"
    notice.parent.mkdir(parents=True)
    notice.write_text("Fixture notice\n")
    providers = texts / "providers"
    providers.write_text("fixture\tusr/include/x.h\n")
    out = subprocess.check_output(["python3", str(ROOT / "sdk/mkcopyright.py"), *args, str(texts), str(providers)],
                                  text=True)
    assert out.startswith(title)
    assert texts_line in out
