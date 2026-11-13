# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import runpy
from pathlib import Path

import pytest


@pytest.mark.parametrize('pattern,name', [(r'file\?.c', 'file?.c'), (r'file\*.c', 'file*.c'),
                                          (r'file\\.c', 'file\\.c')])
def test_source_patterns(pattern, name, tmp_path):
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    assert sdk['matcher'](pattern).match(name)
    (tmp_path / 'src').mkdir()
    (tmp_path / 'src' / name).touch()
    assert sdk['kept_files'](str(tmp_path), ['src/' + pattern], ['src']) == ['src/' + name]
