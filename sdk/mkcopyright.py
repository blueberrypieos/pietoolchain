#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
usage: mkcopyright.py PIE PROVIDERS

PROVIDERS is providers.py's output.
"""
import collections
import os
import re
import sys

UBUNTU = {
    "Apache-2.0": "Apache-2.0",
    "CC0-1.0": "CC0-1.0",
    "GFDL-1.2": "GFDL-1.2",
    "GFDL-1.3": "GFDL-1.3",
    "GPL-1.0": "GPL-1",
    "GPL-2.0": "GPL-2",
    "GPL-3.0": "GPL-3",
    "LGPL-2.0": "LGPL-2",
    "LGPL-2.1": "LGPL-2.1",
    "LGPL-3.0": "LGPL-3",
    "MPL-1.1": "MPL-1.1",
    "MPL-2.0": "MPL-2.0",
}


def source(pie, provider):
    for path in (os.path.join(pie, "userland", provider, "copyright"),
                 os.path.join(pie, "sdk", "copyright", provider)):
        if os.path.exists(path):
            return path
    sys.exit("mkcopyright.py: no copyright file for %s" % provider)


def with_texts(pie, text):
    have = os.listdir(os.path.join(pie, "sdk", "licenses"))
    needed = []

    def repl(m):
        lid = m.group(3)
        for spdx in sorted(UBUNTU, key=len, reverse=True):
            if lid == spdx or lid.startswith(spdx + "-"):
                return m.group(0)[:-len(lid)] + UBUNTU[spdx]
        for known in sorted(have, key=len, reverse=True):
            if lid.startswith(known):
                if known not in needed:
                    needed.append(known)
                at = "at" + m.group(2) if m.group(1) else ""
                return "%sthe end of this file (%s)%s" % (at, known, lid[len(known):])
        sys.exit("mkcopyright.py: no sdk/licenses text for %s" % lid)

    text = re.sub(r"(?:\b(in)(\s+))?/usr/share/common-licenses/([A-Za-z0-9+-]+(?:\.[A-Za-z0-9+-]+)*)",
                  repl, text)
    for lid in sorted(needed):
        text += "\n" + "=" * 76 + "\nLicence text: %s\n" % lid + "=" * 76 + "\n\n"
        text += open(os.path.join(pie, "sdk", "licenses", lid)).read().rstrip("\n") + "\n"
    return text


def main():
    pie, providers = sys.argv[1], sys.argv[2]
    count = collections.OrderedDict()
    for line in open(providers):
        name = line.split("\t", 1)[0]
        count[name] = count.get(name, 0) + 1
    out = ["""pietoolchain-sdk: the darwin-arm64 SDK for pietoolchain

Source: https://github.com/blueberrypieos/pietoolchain

The SDK headers, libraries and frameworks, built from the projects below or
taken from the packages of PIE's ports. Each section is that project's or
package's copyright file; the texts of uncommon licences are at the end."""]
    for name, n in count.items():
        out.append("\n" + "=" * 76)
        heading = name
        debs = os.path.join(pie, "userland", name, "debs")
        if os.path.exists(debs):
            path = open(debs).readline().split()[0]
            src, ver = path.split("/")[2], os.path.basename(path).split("_")[1]
            mk = open(os.path.join(pie, "userland", name, "Makefile")).read()
            repo = re.search(r"^PIE_REPO\s*:=\s*(\S+)", mk, re.M)
            if repo and repo.group(1) != src:
                heading = "%s (%s %s, https://github.com/blueberrypieos/%s)" % (
                    name, src, ver, repo.group(1))
            else:
                heading = "%s (%s %s)" % (name, src, ver)
        out.append("%s: %d file%s" % (heading, n, "" if n == 1 else "s"))
        out.append("=" * 76 + "\n")
        out.append(open(source(pie, name)).read().strip("\n"))
    sys.stdout.write(with_texts(pie, "\n".join(out) + "\n"))


if __name__ == "__main__":
    main()
