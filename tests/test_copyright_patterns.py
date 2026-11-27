# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import os
import runpy
import subprocess
import sys
from pathlib import Path

import pytest


@pytest.mark.parametrize('pattern,name', [(r'file\?.c', 'file?.c'), (r'file\*.c', 'file*.c'),
                                          (r'file\\.c', 'file\\.c')])
def test_source_patterns(pattern, name, tmp_path):
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    assert sdk['matcher'](pattern).match(name)
    (tmp_path / 'src').mkdir()
    (tmp_path / 'src' / name).touch()
    assert sdk['kept_files'](str(tmp_path), ['src/' + pattern]) == ['src/' + name]


def test_unread_own_files_are_excluded(tmp_path):
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    (tmp_path / 'build.sh').touch()
    (tmp_path / 'unread.txt').touch()
    (tmp_path / 'src').mkdir()
    (tmp_path / 'src/read.c').touch()
    (tmp_path / 'src/unread.c').touch()
    assert sorted(sdk['kept_files'](str(tmp_path), ['build.sh', 'src/read.c'])) == ['build.sh', 'src/read.c']


def test_unused_tree_may_be_absent(tmp_path):
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    (tmp_path / 'build.sh').touch()
    assert sdk['package_files'](str(tmp_path), ['build.sh'], ['src/unused']) == ['build.sh']
    with pytest.raises(SystemExit):
        sdk['package_files'](str(tmp_path), ['src/required/input.c'], ['src/required'])


def test_pruning_preserves_selected_fields():
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    pars = ['Format: example',
            'Files: *\nCopyright: 2026 Example\nLicense: BSD-2-Clause',
            'Files: selected.c absent.c\nCopyright: 2025 Other\nLicense: Expat',
            'Files: unused.c\nCopyright: 2024 Unused\nLicense: Expat']
    result = sdk['prune_patterns'](pars, ['selected.c', 'build.sh'])
    assert len(result) == 3
    assert sdk['field'](result[-1], 'Files') == 'selected.c'
    assert 'Other' in result[-1] and 'Unused' not in '\n'.join(result)
    before = sdk['last_matches'](pars, ['selected.c', 'build.sh'])
    after = sdk['last_matches'](result, ['selected.c', 'build.sh'])
    for name in before:
        assert sdk['rest_of'](pars[before[name][0]]) == sdk['rest_of'](result[after[name][0]])


def test_git_metadata_is_never_an_input(tmp_path):
    sdk = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'sdk/mksrccopyright.py'))
    (tmp_path / 'src/one/.git').mkdir(parents=True)
    (tmp_path / 'src/one/.git/HEAD').write_text('reference')
    (tmp_path / 'src/one/input.c').touch()
    (tmp_path / 'src/two').mkdir()
    (tmp_path / 'src/two/.git').write_text('gitdir: elsewhere')
    (tmp_path / 'src/two/input.c').touch()
    assert sorted(sdk['kept_files'](str(tmp_path), ['src/*'])) == ['src/one/input.c', 'src/two/input.c']


@pytest.mark.parametrize('full,status', [(False, 1), (True, 0)])
def test_guard_checks_reduced_sources_and_accepts_full_analysis(tmp_path, full, status):
    (tmp_path / 'debian').mkdir()
    (tmp_path / 'debian/copyright').write_text('Format: example\nFiles-Excluded: *\nFiles-Included: input.c\n')
    (tmp_path / 'input.c').touch()
    (tmp_path / 'unread.c').touch()
    environment = dict(os.environ)
    environment.pop('PIE_FULL_SOURCE', None)
    if full:
        environment['PIE_FULL_SOURCE'] = '1'
    guard = Path(__file__).resolve().parents[1] / 'sdk/pie-source-guard'
    result = subprocess.run([str(guard), str(tmp_path)], env=environment, capture_output=True)
    assert result.returncode == status


def test_full_analysis_still_requires_a_manifest(tmp_path):
    guard = Path(__file__).resolve().parents[1] / 'sdk/pie-source-guard'
    environment = dict(os.environ, PIE_FULL_SOURCE='1')
    result = subprocess.run([str(guard), str(tmp_path)], env=environment, capture_output=True)
    assert result.returncode != 0
