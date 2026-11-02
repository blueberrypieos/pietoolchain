#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""Fill in the build settings a kext's Info.plist names.

    kextplist.py IN OUT [NAME=VALUE...] [-x SETTINGS...]

A kext's Info.plist can name build settings as $(NAME) and $NAME
(PRODUCT_BUNDLE_IDENTIFIER, a version, a copyright string). This writes IN
with each one given as NAME=VALUE, or set in a settings file (-x; NAME =
VALUE lines, which may name settings set before them), replaced, and
nothing else changed. A setting left unset is an
error, as is a result that is not a property list.
"""
import plistlib
import re
import sys

REF = re.compile(r"\$\((\w+)\)|\$(\w+)")


def expand(value, settings, what):
    def one(m):
        name = m.group(1) or m.group(2)
        if name not in settings:
            sys.exit("kextplist.py: %s names $(%s), which is not set" % (what, name))
        return settings[name]
    return REF.sub(one, value)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    src, out, rest = args[0], args[1], args[2:]
    settings = {}
    while rest:
        a = rest.pop(0)
        if a == "-x":
            for line in open(rest.pop(0), encoding="utf-8"):
                line = line.split("//", 1)[0].strip()
                if "=" in line:
                    k, v = (x.strip() for x in line.split("=", 1))
                    settings[k] = expand(v, settings, "an .xcconfig")
        else:
            k, v = a.split("=", 1)
            settings[k] = v
    text = expand(open(src, encoding="utf-8").read(), settings, src)
    plistlib.loads(text.encode("utf-8"))
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    main()
