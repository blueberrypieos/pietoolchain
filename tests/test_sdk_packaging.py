# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import importlib.util
from pathlib import Path

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
        "Package: libone\nX-Pie-Project: one\nX-Pie-Version: 2\n\n"
        "Package: libtwo\nX-Pie-Project: two\nX-Pie-Version: 3\n")
    assert module.sdk_provides(str(tmp_path)) == "libone (= 2), libtwo (= 3)"
    assert module.runtime_depends({"libtwo", "external", "libone"}, {"libone": "2", "libtwo": "3"}) == "external, libone (>= 2), libtwo (>= 3)"


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
