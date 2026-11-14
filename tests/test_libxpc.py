# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import os
from pathlib import Path
import subprocess


def test_libxpc_suite():
    root = Path(__file__).resolve().parents[1]
    subprocess.run(["sh", "userland/libxpc/tests/run.sh"], cwd=root,
                   env=dict(os.environ, CC="clang-21"), check=True, timeout=280)
