# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
from pathlib import Path
import subprocess


def test_toolchain_suite():
    root = Path(__file__).resolve().parents[1]
    subprocess.run(["python3", "-m", "unittest", "discover", "-s", "toolchain/tests", "-p", "*.py"],
                   cwd=root, check=True, timeout=100)
