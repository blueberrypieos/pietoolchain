#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""Check that the clang wrapper maps source paths and fixes the build date
for reproducible builds."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Reproducible(unittest.TestCase):
    def test_compilation_paths_and_date(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            objects = []
            for name in ('one', 'longer-second-directory'):
                tree = base / name
                tree.mkdir()
                source = tree / 'source.c'
                source.write_text('const char *file = __FILE__; const char *date = __DATE__ " " __TIME__; int answer(void) { return 42; }\n')
                environment = dict(os.environ, PIE_REPRODUCIBLE='1', SOURCE_DATE_EPOCH='1737072000',
                                   PIE_REPRO_SOURCE=str(tree))
                output = tree / 'source.o'
                subprocess.run([ROOT / 'bin/clang', '-mcpu=generic', '-g', '-c', source, '-o', output], env=environment, check=True)
                objects.append(output.read_bytes())
            self.assertEqual(*objects)
            self.assertIn(b'/usr/src/pie/src/source.c', objects[0])
            self.assertNotIn(str(base).encode(), objects[0])


if __name__ == '__main__':
    unittest.main()
