#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import json
import os
from pathlib import Path
import stat
import sys


def snapshot(sdk):
    entries = {}
    for root, dirs, files in os.walk(sdk):
        for name in dirs + files:
            path = Path(root) / name
            info = path.lstat()
            if stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
                entries[str(path.relative_to(sdk))] = [
                    info.st_dev, info.st_ino, info.st_mode, info.st_size,
                    info.st_mtime_ns, info.st_ctime_ns,
                    os.readlink(path) if path.is_symlink() else None,
                ]
    return entries


def update(sdk, before, directory, name):
    after = snapshot(sdk)
    changed = {path for path in after if before.get(path) != after[path]}
    directory = Path(directory)
    order_file = directory / "ORDER"
    order = order_file.read_text().splitlines() if order_file.exists() else []
    if name not in order:
        order.append(name)
    for provider in order:
        manifest = directory / provider
        owned = set(manifest.read_text().splitlines()) if manifest.exists() else set()
        owned = (owned & after.keys()) - changed
        if provider == name:
            owned |= changed
        manifest.write_text("".join(path + "\n" for path in sorted(owned)))
    order_file.write_text("".join(provider + "\n" for provider in order))


if __name__ == "__main__":
    if sys.argv[1] == "snapshot":
        json.dump(snapshot(sys.argv[2]), sys.stdout)
    elif sys.argv[1] == "update":
        with open(sys.argv[3]) as source:
            before = json.load(source)
        update(sys.argv[2], before, sys.argv[4], sys.argv[5])
    else:
        sys.exit("usage: record-provider.py snapshot SDK | update SDK BEFORE DIRECTORY NAME")
