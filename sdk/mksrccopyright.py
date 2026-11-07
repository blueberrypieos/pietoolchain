#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
usage: mksrccopyright.py PIE [--check]

Writes PIE's debian/copyright from the copyright file and source-files list
of each source tree (userland/P/copyright and userland/P/source-files, or
sdk/copyright/T and sdk/source-files/T):
- its first paragraph has Files-Excluded: src/* xnu/*, and Files-Included:
  the files of each tree that the source package keeps;
- each tree's paragraphs follow, with their paths under the tree.

It also writes debian/pietoolchain.copyright, the copyright of the
pietoolchain package: our files and the trees pietoolchain is built from
(PIETOOLCHAIN_TREES).

--check compares with the files there and fails if they differ. It also
checks the files the source package keeps: every file of a tree must have a
paragraph of that tree, and every pattern of debian/copyright must apply to
some file.
"""
import os
import re
import sys

FORMAT = "https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
TOOLCHAIN = {"AvailabilityVersions": "AvailabilityVersions", "apple-libdispatch": "apple-libdispatch",
             "bootstrap_cmds": "bootstrap_cmds", "cctools-port": "cctools-port",
             "llvm-project": "llvm-project"}

OURS = "2026 The Blueberry Pie OS Authors"
# The trees the pietoolchain package's programs and headers come from.
PIETOOLCHAIN_TREES = ("src/cctools-port", "src/apple-libdispatch", "src/bootstrap_cmds")
OWN = [
    ("userland/libplatform/arm64/optimized-routines/*", "2012-2024 Arm Limited",
     "MIT OR Apache-2.0 with LLVM exception"),
    ("userland/CommonCrypto/patches/*\n"
     " userland/Libinfo/patches/0001-* userland/Libinfo/patches/0003-*\n"
     " userland/Libinfo/patches/0004-* userland/Libinfo/patches/0005-*\n"
     " userland/Libinfo/patches/0006-* userland/Libm/patches/*\n"
     " userland/Libsystem/patches/* userland/copyfile/patches/*\n"
     " userland/dyld/patches/* userland/hfs/patches/* userland/libmalloc/patches/*\n"
     " userland/syslog/patches/* xnu-patches/*", OURS, "APSL-2.0"),
    ("userland/launchd/patches/* userland/libdispatch/patches/*\n"
     " userland/mDNSResponder/patches/*", OURS, "Apache-2.0"),
    ("userland/Libinfo/patches/0002-*", OURS, "LicenseRef-BSD-4-Clause-Bruce-Simpson"),
    ("userland/libplatform/patches/*", OURS, "BSD-3-Clause"),
    ("userland/openlibm/patches/*", OURS, "MIT"),
]

UBUNTU = {"Apache-2.0": "Apache-2.0", "CC0-1.0": "CC0-1.0", "GFDL-1.2": "GFDL-1.2",
          "GFDL-1.3": "GFDL-1.3", "GPL-1.0": "GPL-1", "GPL-2.0": "GPL-2", "GPL-3.0": "GPL-3",
          "LGPL-2.0": "LGPL-2", "LGPL-2.1": "LGPL-2.1", "LGPL-3.0": "LGPL-3",
          "MPL-1.1": "MPL-1.1", "MPL-2.0": "MPL-2.0"}
LICENCE_PATH = r"/usr/share/common-licenses/([A-Za-z0-9+-]+(?:\.[A-Za-z0-9+-]+)*)"


def with_texts(pie, par):
    def ubuntu(lid):
        for spdx in sorted(UBUNTU, key=len, reverse=True):
            if lid == spdx or lid.startswith(spdx + "-"):
                return UBUNTU[spdx]
        return None

    def text(lid):
        f = os.path.join(pie, "sdk", "licenses", lid)
        if not os.path.exists(f):
            sys.exit("mksrccopyright.py: no sdk/licenses text for %s" % lid)
        return "\n".join(" " + l if l.strip() else " ." for l in open(f).read().strip("\n").split("\n"))

    def full(m):
        return m.group(0) if ubuntu(m.group(1)) else text(m.group(1))

    par = re.sub(r"^ The full text is in\s+" + LICENCE_PATH + r"\.$", full, par, flags=re.M)
    for lid in dict.fromkeys(re.findall(LICENCE_PATH, par)):
        if not ubuntu(lid):
            par += "\n .\n" + text(lid)
    return re.sub(LICENCE_PATH, lambda m: "/usr/share/common-licenses/" + (ubuntu(m.group(1)) or m.group(1)), par)


def paragraphs(text):
    return [p for p in re.split(r"\n\s*\n", text.strip("\n")) if p.strip()]


def field(par, name):
    m = re.search(r"^%s:[ \t]*(.*(?:\n[ \t].*)*)" % name, par, re.M)
    return m.group(1) if m else None


def licence_names(expr):
    expr = re.sub(r"\s+", " ", expr.split("\n", 1)[0])
    names = re.split(r"\s+(?:and|or|AND|OR)\s+|[(),]", expr)
    out = []
    for n in names:
        n = n.strip()
        if n:
            out.append(re.sub(r"\s+(with|WITH)\s+", " with ", n))
    return out


def body(par):
    """The text of a standalone License paragraph, without its first line."""
    return re.sub(r"^License:[^\n]*\n?", "", par[par.index("License:"):], count=1)


def same_text(a, b):
    def norm(t):
        return " ".join(l.strip() for l in t.split("\n") if l.strip() not in ("", "."))
    return norm(body(a)) == norm(body(b))


# Licences a component's own text replaces inline (none: texts that name
# their holders have licences of their own, sdk-licence-texts.py).
OWN_TEXTS = set()
# Licences whose stand-alone text is the generic one in sdk/licenses, which
# names no holder: each file's own notice names its holders.
GENERIC = {"BSD-1-Clause", "BSD-2-Clause", "BSD-3-Clause", "public-domain"}
# Licences whose texts name who to acknowledge or whose wording varies with
# the holder: each text has a LicenseRef of its own.
NAMED = {"BSD-4-Clause", "HPND"}
# Earlier APSL versions: their files are used under APSL-2.0, which section 7
# of each permits (sdk-licence-texts.py adds the Comment).
APSL1 = {"APSL-1.0", "APSL-1.1", "APSL-1.1+", "APSL-1.2"}


def generic(pie, name):
    text = open(os.path.join(pie, "sdk", "licenses", name)).read().strip("\n")
    return "License: " + name + "\n" + "\n".join(" " + l if l.strip() else " ." for l in text.split("\n"))


def upper_operators(par):
    """AND and OR in upper case in a paragraph's License expression."""
    def fix(m):
        expr = re.sub(r"\s+and\s+", " AND ", m.group(2))
        return m.group(1) + re.sub(r"\s+or\s+", " OR ", expr)
    return re.sub(r"^(License:[ \t]*)(.*)$", fix, par, count=1, flags=re.M)


def inline_own(par, own, texts):
    """debian/copyright prints one text per licence name, the first one
    found. A Files paragraph whose component has a different text for one
    of the OWN_TEXTS licences carries that text inline."""
    lic = field(par, "License")
    if "\n" in lic.strip("\n"):
        return par
    extra = [own[n] for n in licence_names(lic)
             if n in OWN_TEXTS and n in own and n in texts and not same_text(own[n], texts[n])]
    if not extra:
        return par
    return par.rstrip("\n") + "\n" + "\n .\n".join(body(t).rstrip("\n") for t in extra)


def vendored(pie):
    out = []
    for p in sorted(os.listdir(os.path.join(pie, "userland"))):
        mk = os.path.join(pie, "userland", p, "Makefile")
        if not os.path.exists(mk) or not re.search(r"^TAG\s*:?=", open(mk).read(), re.M):
            continue
        s = open(mk).read()
        url = re.search(r"^REPO_URL\s*\??:?=\s*(\S+)", s, re.M)
        repo = re.search(r"^REPO\s*\??:?=\s*(\S+)", s, re.M)
        name = os.path.basename(url.group(1))[:-4] if url and url.group(1).endswith(".git") else \
            os.path.basename(url.group(1)) if url else (repo.group(1) if repo else p)
        out.append(("src/" + name, os.path.join("userland", p, "copyright")))
    for d, f in sorted(TOOLCHAIN.items()):
        out.append(("src/" + d, os.path.join("sdk", "copyright", f)))
    out.append(("xnu", os.path.join("sdk", "copyright", "xnu")))
    seen, uniq = set(), []
    for tree, f in out:
        if tree not in seen:
            seen.add(tree)
            uniq.append((tree, f))
    return uniq


def source_files(f):
    if f.startswith("userland/"):
        return os.path.join(os.path.dirname(f), "source-files")
    return os.path.join("sdk", "source-files", os.path.basename(f))


def matcher(pattern):
    body = "".join(".*" if c == "*" else "." if c == "?" else re.escape(c) for c in pattern)
    return re.compile(body + r"\Z", re.S)


def kept_files(pie, included, trees):
    """The files of the source tree, without what Files-Included leaves out
    of the trees (a git checkout has them all; a source package does not)."""
    rx = [matcher(p) for p in included if "*" in p or "?" in p]
    exact = {p for p in included if "*" not in p and "?" not in p}
    out = []
    for d, dirs, files in os.walk(pie):
        rel = os.path.relpath(d, pie)
        if rel == ".":
            dirs[:] = [x for x in dirs if x not in ("debian", "build", ".git", ".pc")]
        elif rel == "toolchain":
            dirs[:] = [x for x in dirs if x not in ("build", "prefix", "sdk")]
        for f in files + [x for x in dirs if os.path.islink(os.path.join(d, x))]:
            path = os.path.normpath(os.path.join(rel, f))
            if path.startswith(tuple(t + "/" for t in trees)) and path not in exact and not any(r.match(path) for r in rx):
                continue
            out.append(path)
    return out


def check_files(pie, header_included, trees, tree_pars, all_pars):
    errors = []
    files = kept_files(pie, header_included, trees)
    for tree, pars in tree_pars:
        mine = [f for f in files if f.startswith(tree + "/")]
        compiled = [[matcher(p) for p in field(par, "Files").split()] for par in pars]
        for f in mine:
            if not any(r.match(f) for rs in compiled for r in rs):
                errors.append("%s has no paragraph of %s" % (f, tree))
    pats = [(i, p, matcher(p)) for i, par in enumerate(all_pars) for p in field(par, "Files").split()]
    last = {}
    for f in files:
        for i, p, r in pats:
            if r.match(f):
                last[f] = (i, p)
    used = set(last.values())
    for i, p, r in pats:
        if (i, p) not in used:
            errors.append("pattern %s applies to no file" % p)
    for p in header_included:
        r = matcher(p)
        if not any(r.match(f) for f in files):
            errors.append("Files-Included %s matches no file" % p)
    return errors


def main():
    pie, check = sys.argv[1], "--check" in sys.argv
    texts, trees, included = {}, [], []
    for tree, f in vendored(pie):
        if not os.path.isdir(os.path.join(pie, tree)):
            sys.exit("mksrccopyright.py: no %s" % tree)
        lst = os.path.join(pie, source_files(f))
        if not os.path.exists(lst):
            sys.exit("mksrccopyright.py: no %s" % source_files(f))
        included += [tree + "/" + l.strip() for l in open(lst) if l.strip()]
        pars, own = [], {}
        for par in paragraphs(open(os.path.join(pie, f)).read()):
            lic = field(par, "License")
            if lic is None:
                continue
            files = field(par, "Files")
            if files is None:
                name = re.sub(r"\s+(with|WITH)\s+", " with ", lic.split("\n", 1)[0].strip())
                texts.setdefault(name, par)
                own[name] = par
                continue
            pats = files.split()
            if pats[0].startswith(("userland/", "xnu-patches/")):
                continue
            pats = [p if p.startswith(tree + "/") else tree + "/" + p for p in pats]
            body = "Files: " + "\n ".join(pats)
            pars.append(upper_operators(re.sub(r"^Files:[ \t]*.*(?:\n[ \t]+.*)*", lambda m: body, par, count=1,
                                               flags=re.M)))
        trees.append((tree, [inline_own(par, own, texts) for par in pars]))
    header = ["Format: " + FORMAT, "Upstream-Name: pietoolchain",
              "Source: https://github.com/blueberrypieos/pietoolchain"]
    text, all_pars = render(pie, header + ["Files-Excluded: src/* xnu/*", "Files-Included:"] +
                            [" " + p for p in included], OWN, trees, texts)
    binary, _ = render(pie, header, [], [(t, p) for t, p in trees if t in PIETOOLCHAIN_TREES], texts)
    outputs = [(os.path.join(pie, "debian", "copyright"), text),
               (os.path.join(pie, "debian", "pietoolchain.copyright"), binary)]
    if check:
        for path, want in outputs:
            if not os.path.exists(path) or open(path).read() != want:
                sys.exit("mksrccopyright.py: debian/%s is not up to date (sdk/mksrccopyright.py %s)"
                         % (os.path.basename(path), pie))
        errors = check_files(pie, included, [t for t, p in trees], trees, all_pars)
        if errors:
            sys.exit("mksrccopyright.py:\n  " + "\n  ".join(errors[:50]) + ("\n  ... %d more" % (len(errors) - 50) if len(errors) > 50 else ""))
        return
    for path, want in outputs:
        open(path, "w").write(want)


def render(pie, header, own_pars, trees, texts):
    """A copyright file: HEADER, our files, the OWN_PARS and the TREES'
    paragraphs, then the texts of the licences they name."""
    out = header + ["", "Files: *", "Copyright: 2026 The Blueberry Pie OS Authors", "License: BSD-2-Clause"]
    used = {"BSD-2-Clause"}
    all_pars = ["Files: *\nCopyright: 2026 The Blueberry Pie OS Authors\nLicense: BSD-2-Clause"]
    for files, copyright, licence in own_pars:
        par = "Files: " + files + "\nCopyright: " + copyright + "\nLicense: " + licence
        out += ["", par]
        all_pars.append(par)
        used.update(licence_names(licence))
    for tree, pars in trees:
        for par in pars:
            lic = field(par, "License")
            if "\n" in lic.strip("\n"):
                par = with_texts(pie, par)
            else:
                used.update(licence_names(lic))
            out += ["", par]
            all_pars.append(par)
    names, missing = set(), []
    named = sorted(used & NAMED)
    if named:
        sys.exit("mksrccopyright.py: %s name no holder; use the LicenseRef of the file's own text" % ", ".join(named))
    old = sorted(used & APSL1)
    if old:
        sys.exit("mksrccopyright.py: %s: files are used under APSL-2.0 (section 7 of APSL 1.x)" % ", ".join(old))
    for n in used:
        if n in texts or n in GENERIC:
            names.add(n)
            continue
        for part in re.split(r" with ", n):
            if part in texts:
                names.add(part)
            elif part.lower() not in ("public-domain", "none"):
                missing.append(part)
    for n in sorted(names):
        out += ["", generic(pie, n) if n in GENERIC else with_texts(pie, texts[n])]
    if missing:
        sys.exit("mksrccopyright.py: no licence text for: " + ", ".join(sorted(set(missing))))
    return "\n".join(out) + "\n", all_pars


if __name__ == "__main__":
    main()
