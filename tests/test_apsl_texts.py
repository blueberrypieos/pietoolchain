# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import hashlib
from pathlib import Path
import runpy

import pytest


ROOT = Path(__file__).resolve().parents[1]
TEXTS = (
    ("APSL-2.0", "e5881019d8766c1e88a5fe1dbca4ba40c78011d41fcb18f6e9f50df60182685b"),
    ("LicenseRef-APSL-2.0-2007", "212846e0145aa50fb3a5aef254a370311a93acf6c1e792e47e0068d64c8c3885"),
    ("LicenseRef-APSL-2.0-2007-copyfile", "4407579a41ee5062dd5451539a05b495e699c4f1c4965c8ad65c9bc3b38d9860"),
)


@pytest.mark.parametrize("name,digest", TEXTS)
def test_exact_apsl_text_and_availability(name, digest):
    text = (ROOT / "sdk/licenses" / name).read_bytes()
    introduction, shipped = text.split(b"\n\n", 1)
    assert introduction == (
        b"This package contains Covered Code under the Apple Public Source License\n"
        b"2.0. The Source Code of the Covered Code is available under the terms of\n"
        b"that License from https://github.com/blueberrypieos/pietoolchain.")
    assert hashlib.sha256(shipped).hexdigest() == digest
    source = runpy.run_path(str(ROOT / "sdk/mksrccopyright.py"))
    paragraph = source["generic"](ROOT, name)
    assert "\t" not in paragraph
    reference = "License: " + name + "\n The full text is in /usr/share/common-licenses/" + name + "."
    assert source["with_texts"](ROOT, reference) == paragraph
    decoded = "\n".join("" if line == " ." else line[1:]
                        for line in paragraph.splitlines()[1:]) + "\n"
    assert decoded == text.decode().expandtabs().rstrip("\n") + "\n"


@pytest.mark.parametrize("project,name", (
    ("Libc", "APSL-2.0"),
    ("Libsystem", "LicenseRef-APSL-2.0-2007"),
    ("copyfile", "LicenseRef-APSL-2.0-2007-copyfile"),
))
def test_runtime_text_matches_component(project, name):
    runtime = runpy.run_path(str(ROOT / "sdk/mkdebs.py"))
    copyright = runtime["with_texts"](
        ROOT, (ROOT / "userland" / project / "copyright").read_text())
    paragraph = copyright.split("\n\nLicense: " + name + "\n", 1)[1].split("\n\n", 1)[0]
    decoded = "\n".join("" if line == " ." else line[1:]
                        for line in paragraph.splitlines()) + "\n"
    assert decoded == (ROOT / "sdk/licenses" / name).read_text().rstrip("\n") + "\n"


@pytest.mark.parametrize("name,digest", TEXTS)
def test_sdk_preserves_apsl_bytes(name, digest):
    text = (ROOT / "sdk/licenses" / name).read_bytes()
    assert hashlib.sha256(text.split(b"\n\n", 1)[1]).hexdigest() == digest
    sdk = runpy.run_path(str(ROOT / "sdk/mkcopyright.py"))
    copyright = sdk["with_texts"](ROOT, "See /usr/share/common-licenses/" + name + ".\n")
    appended = copyright.split("Licence text: " + name + "\n" + "=" * 76 + "\n\n", 1)[1]
    assert appended.encode() == text.rstrip(b"\n") + b"\n"
