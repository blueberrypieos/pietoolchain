#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "record_provider", Path(__file__).with_name("record-provider.py"))
record = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record)


class Providers(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name) / "sdk"
        self.manifests = Path(self.temp.name) / "providers"
        self.sdk.mkdir()
        self.manifests.mkdir()

    def install(self, provider, action):
        before = record.snapshot(self.sdk)
        action()
        record.update(self.sdk, before, self.manifests, provider)

    def owned(self, provider):
        return (self.manifests / provider).read_text().splitlines()

    def test_replace_stub_and_repeat_provider(self):
        lib = self.sdk / "libsystem_m.dylib"
        self.install("openlibm", lambda: (self.sdk / "math.h").write_text("header"))
        self.install("stubs", lambda: lib.write_text("stub"))
        self.install("openlibm", lambda: lib.write_text("real"))
        self.assertEqual(self.owned("openlibm"), ["libsystem_m.dylib", "math.h"])
        self.assertEqual(self.owned("stubs"), [])
        self.assertEqual((self.manifests / "ORDER").read_text(), "openlibm\nstubs\n")

    def test_preserved_mtime_replacement(self):
        lib = self.sdk / "lib"
        self.install("stubs", lambda: lib.write_text("stub"))
        times = lib.stat()

        def replace():
            lib.write_text("real")
            os.utime(lib, ns=(times.st_atime_ns, times.st_mtime_ns))

        self.install("real", replace)
        self.assertEqual(self.owned("real"), ["lib"])
        self.assertEqual(self.owned("stubs"), [])

    def test_incremental_noop_preserves_owners(self):
        self.install("first", lambda: (self.sdk / "header").write_text("same"))
        self.install("second", lambda: None)
        self.install("first", lambda: None)
        self.assertEqual(self.owned("first"), ["header"])
        self.assertEqual(self.owned("second"), [])

    def test_symlink_replacement_and_removal(self):
        link = self.sdk / "link"
        self.install("first", lambda: link.symlink_to("absent"))

        def replace():
            link.unlink()
            link.symlink_to("elsewhere")

        self.install("second", replace)
        self.assertEqual(self.owned("first"), [])
        self.assertEqual(self.owned("second"), ["link"])
        self.install("first", link.unlink)
        self.assertEqual(self.owned("second"), [])


if __name__ == "__main__":
    unittest.main()
