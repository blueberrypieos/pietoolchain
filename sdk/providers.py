#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
usage: providers.py PIE FILES

FILES lists paths relative to the SDK, one per line.
"""
import os
import sys


def main():
    pie, files = sys.argv[1], sys.argv[2]
    pdir = os.path.join(pie, "build", "providers")
    overlay = os.path.join(pie, "toolchain", "sdk-overlay")
    with open(os.path.join(pdir, "ORDER")) as f:
        order = [line.strip() for line in f if line.strip()]
    owner = {}
    for name in order:
        with open(os.path.join(pdir, name)) as f:
            for line in f:
                path = line.rstrip("\n")
                if path:
                    owner[path] = name
    missing = []
    for line in open(files):
        path = line.rstrip("\n")
        if os.path.lexists(os.path.join(overlay, path)):
            print("sdk-overlay\t" + path)
        elif path in owner:
            print(owner[path] + "\t" + path)
        else:
            missing.append(path)
    if missing:
        sys.exit("providers.py: %d files have no provider, e.g. %s"
                 % (len(missing), ", ".join(missing[:5])))


if __name__ == "__main__":
    main()
