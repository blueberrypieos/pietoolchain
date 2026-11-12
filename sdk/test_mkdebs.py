#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("mkdebs", Path(__file__).with_name("mkdebs.py"))
mkdebs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkdebs)


class RuntimeDepends(unittest.TestCase):
    def test_versioned(self):
        versions = {"libxpc": "0-2", "libcorecrypto": "1:0.2.0-2", "libsystem-info": "600-2"}
        self.assertEqual(
            mkdebs.runtime_depends({"libxpc", "libbsm", "libcorecrypto"}, versions),
            "libbsm, libcorecrypto (>= 1:0.2.0-2), libxpc (>= 0-2)")

    def test_none(self):
        self.assertEqual(mkdebs.runtime_depends(set(), {"libxpc": "0-2"}), "")


if __name__ == "__main__":
    unittest.main()
