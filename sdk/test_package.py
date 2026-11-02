#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


class Package(unittest.TestCase):
    def test_umask(self):
        with tempfile.TemporaryDirectory() as temporary:
            pie = Path(temporary)
            scripts = pie / "sdk"
            scripts.mkdir()
            for name in ("mksdk.sh", "providers.py", "mkcopyright.py"):
                shutil.copy2(Path(__file__).with_name(name), scripts / name)
            (scripts / "licenses").mkdir()
            provider = pie / "userland" / "fixture"
            provider.mkdir(parents=True)
            (provider / "copyright").write_text("Fixture copyright\nLicense: BSD-2-Clause\n")
            manifests = pie / "build" / "providers"
            manifests.mkdir(parents=True)
            (manifests / "ORDER").write_text("fixture\n")
            (manifests / "fixture").write_text("usr/lib/libfixture.a\n")
            sdk = pie / "sysroot"
            (sdk / "usr/lib").mkdir(parents=True)
            (sdk / "usr/lib/libfixture.a").write_text("fixture library\n")
            for path in [sdk, *sdk.rglob("*")]:
                os.utime(path, (1700000000, 1700000000))
            packages = []
            for mask in (0o022, 0o077):
                output = pie / str(mask)
                subprocess.run(
                    [str(scripts / "mksdk.sh"), "-s", str(sdk),
                     "-v", "0.1.0", "-o", str(output)],
                    preexec_fn=lambda: os.umask(mask), check=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                package = output / "pietoolchain-sdk_0.1.0_all.deb"
                packages.append(package.read_bytes())
                control = subprocess.check_output(["dpkg-deb", "--ctrl-tarfile", str(package)])
                with tarfile.open(fileobj=io.BytesIO(control)) as archive:
                    self.assertEqual(archive.getmember(".").mode, 0o755)
                    self.assertEqual(archive.getmember("./control").mode, 0o644)
            self.assertEqual(packages[0], packages[1])


if __name__ == "__main__":
    unittest.main()
