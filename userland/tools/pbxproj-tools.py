#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
usage: pbxproj-tools.py PROJECT.xcodeproj [-x TARGET:REASON]... [-s TARGET]...
  -x  leave TARGET out, listing REASON in the header (why it is not built)
  -s  leave TARGET out silently (tests, internal tools, or a target the
      project's makefile builds itself)
"""
import os
import re
import shlex
import subprocess
import sys

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pbxproj-sources.py")


def run(proj, target, phase):
    r = subprocess.run([sys.executable, TOOL, proj, target, phase], capture_output=True, text=True)
    return r.stdout


INHERITED = ("$(inherited)", "${inherited}", "$inherited", '"$(inherited)"')


def setting_unescape(x):
    try:
        words = shlex.split(x)
    except ValueError:
        words = None
    if words is not None and len(words) == 1:
        return words[0]
    return re.sub(r"\\(['\"])", r"\1", x)


def unquote(x):
    x = x.strip()
    if len(x) >= 2 and x[0] == '"' and x[-1] == '"' and x[-2] != "\\":
        x = re.sub(r"\\(.)", r"\1", x[1:-1])
    return setting_unescape(x)


def parse_list(v):
    v = v.strip()
    if v.startswith("("):
        v = v[1:-1]
        return [unquote(x.strip().rstrip(",")) for x in v.split("\n") if x.strip().rstrip(",")]
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"' and v[-2] != "\\":
        v = v[1:-1]
    return [w.replace('\\"', '"') for w in re.findall(r'(?:[^\s"\\]|\\.|"[^"]*")+', v)]


BUILD_PATHS = ("SDKROOT", "SRCROOT", "PROJECT_DIR", "BUILT_PRODUCTS_DIR", "DERIVED_FILE_DIR",
               "DERIVED_SOURCES_DIR", "OBJROOT", "SYMROOT", "DSTROOT")


def expand(v, s, target, depth=0):
    if depth > 5:
        return v
    def rep(m):
        name = m.group(1).split(":")[0]
        if name == "TARGET_NAME":
            return target
        if name in s and name not in BUILD_PATHS:
            return expand(s[name], s, target, depth + 1)
        return m.group(0)
    return re.sub(r"\$\(([A-Za-z_0-9:=]+)\)", rep, v)


def mkquote(x):
    if not re.search(r"[\"'\s\\]", x):
        return x
    if "'" in x:
        return '"%s"' % x.replace("\\", "\\\\").replace('"', '\\"')
    return "'%s'" % x


def read_xcconfig(path, s, conditional=None):
    if conditional is None:
        conditional = set()
    if not os.path.exists(path):
        return
    for line in open(path):
        inc = re.match(r'#include\s+"([^"]+)"', line.strip())
        if inc:
            read_xcconfig(os.path.join(os.path.dirname(path), inc.group(1)), s, conditional)
            continue
        line = line.split("//")[0].strip()
        mm = re.match(r"([A-Z_a-z0-9]+)(\[[^\]]*\])?\s*=\s*(.*)", line)
        if not mm:
            continue
        key, cond, val = mm.groups()
        if cond and "macosx" not in cond:
            continue
        if cond:
            conditional.add(key)
        elif key in conditional:
            continue
        s[key] = val


def settings(proj, target):
    out = run(proj, target, "Settings")
    s = {}
    m = re.match(r"xcconfig: (.*)", out)
    projdir = os.path.dirname(os.path.abspath(proj))
    if m and m.group(1) != "(none)":
        read_xcconfig(os.path.join(projdir, m.group(1)), s)
    body = out.split("\n", 1)[1] if "\n" in out else ""
    found = re.findall(r'^\s*"?([A-Z_a-z0-9]+)((?:\[[^\]]*\])*)"? = (\((?:.|\n)*?\)|[^;]*);', body, re.M)
    for key, cond, val in sorted(found, key=lambda f: bool(f[1])):
        if cond and "macosx" not in cond:
            continue
        if cond and key in s:
            val = inherit(val, s[key])
        s[key] = val
    return s


def inherit(val, base):
    items = parse_raw(val)
    if not any(x in INHERITED for x in items):
        return val
    out = []
    for x in items:
        out.extend(parse_raw(base) if x in INHERITED else [x])
    return "(\n" + "".join("%s,\n" % x for x in out) + ")"


def parse_raw(v):
    v = v.strip()
    if v.startswith("("):
        return [x.strip().rstrip(",") for x in v[1:-1].split("\n") if x.strip().rstrip(",")]
    return [v]


def install_mode(flag, mode=0o755):
    flag = flag.strip().strip('"')
    if re.fullmatch(r"[0-7]+", flag):
        return int(flag, 8)
    for clause in flag.split(","):
        m = re.fullmatch(r"([ugoa]*)([-+=])([rwxXst]*)", clause.strip())
        if not m:
            continue
        who, op, perms = m.groups()
        who = who or "a"
        if "a" in who:
            who = "ugo"
        bits = 0
        for c in who:
            shift = {"u": 6, "g": 3, "o": 0}[c]
            for p in perms:
                if p in "rwx" or (p == "X" and mode & 0o111):
                    bits |= {"r": 4, "w": 2, "x": 1, "X": 1}[p] << shift
                elif p == "s" and c in "ug":
                    bits |= 0o4000 if c == "u" else 0o2000
                elif p == "t":
                    bits |= 0o1000
        if op == "+":
            mode |= bits
        elif op == "-":
            mode &= ~bits
        else:
            keep = 0
            for c in "ugo":
                if c not in who:
                    keep |= 0o7 << {"u": 6, "g": 3, "o": 0}[c]
            mode = (mode & keep) | bits
    return mode


def links(text):
    objs = {}
    for m in re.finditer(r"\n\t\t([0-9A-F]{24})(?: /\* .*? \*/)? = \{\n(.*?)\n\t\t\};", text, re.S):
        objs[m.group(1)] = m.group(2)
    out = []
    for oid, body in objs.items():
        if "isa = PBXAggregateTarget" not in body:
            continue
        name = re.search(r"name = \"?([^\";]+)\"?;", body).group(1)
        conf = {}
        cl = objs.get(re.search(r"buildConfigurationList = ([0-9A-F]{24})", body).group(1), "")
        for cid in re.findall(r"[0-9A-F]{24}", re.search(r"buildConfigurations = \((.*?)\);", cl, re.S).group(1)):
            cb = objs.get(cid, "")
            if re.search(r"name = Release;", cb):
                for k, v in re.findall(r"^\s*([A-Z_]+) = \"?([^\";]*)\"?;", cb, re.M):
                    conf[k] = v
        inst = conf.get("INSTALL_PATH", "/usr/bin").lstrip("/")
        prod = conf.get("PRODUCT_NAME", "$(TARGET_NAME)").replace("$(TARGET_NAME)", name)
        for pid in re.findall(r"[0-9A-F]{24}", re.search(r"buildPhases = \((.*?)\);", body, re.S).group(1)):
            pb = objs.get(pid, "")
            if "hardlink.sh" not in pb and not re.search(
                    r'ln -fh?v? \\"\$\{SCRIPT_INPUT_FILE_0\}\\" \\"\$\{SCRIPT_OUTPUT_FILE_0\}\\"', pb):
                continue
            ins = re.findall(r"\$\(DSTROOT\)/([^\",\n]*)", re.search(r"inputPaths = \((.*?)\);", pb, re.S).group(1))
            outs = re.findall(r"\$\(DSTROOT\)/([^\",\n]*)", re.search(r"outputPaths = \((.*?)\);", pb, re.S).group(1))
            for i, o in zip(ins, outs):
                sub = lambda x: x.replace("$(INSTALL_PATH)", inst).replace("$(PRODUCT_NAME)", prod).replace("//", "/").lstrip("/")
                out.append("%s:%s" % (sub(i), sub(o)))
    return sorted(set(out))


def man_pages(proj, target):
    out = []
    for line in run(proj, target, "Man").split("\n"):
        if "\t" in line:
            path, sect = line.split("\t")
            out.append("%s:%s" % (path, sect))
    return out


def product_types(text):
    out = {}
    for m in re.finditer(r"\n\t\t[0-9A-F]{24}(?: /\* .*? \*/)? = \{\n(\t\t\tisa = PBXNativeTarget;.*?)\n\t\t\};", text, re.S):
        body = m.group(1)
        name = re.search(r"\bname = \"?([^\";]+)\"?;", body)
        ptype = re.search(r"productType = \"([^\"]+)\";", body)
        if name and ptype:
            out[name.group(1)] = ptype.group(1)
    return out


def main():
    args = sys.argv[1:]
    proj = args.pop(0)
    skip, why = set(), {}
    while args:
        opt = args.pop(0)
        val = args.pop(0)
        if opt == "-x":
            t, _, reason = val.partition(":")
            skip.add(t)
            why[t] = reason
        elif opt == "-s":
            skip.add(val)
    text = open(os.path.join(proj, "project.pbxproj")).read()
    targets = sorted(set(re.findall(r'PBXNativeTarget "([^"]+)"', text)))
    ptypes = product_types(text)
    for t in targets:
        if ptypes.get(t, "com.apple.product-type.tool") != "com.apple.product-type.tool":
            skip.add(t)
    name = os.path.basename(proj).replace(".xcodeproj", "")
    out = ["# SPDX-License-Identifier: BSD-2-Clause",
           "# Copyright (c) 2026 The Blueberry Pie OS Authors",
           "#",
           "# Generated by userland/tools/pbxproj-tools.py from %s.xcodeproj." % name,
           "# Each tool's sources, install directory, flags and libraries."]
    if why:
        out.append("# Not built yet:")
        for t in sorted(why):
            out.append("#   %s (%s)" % (t, why[t]))
    out.append("")
    for t in targets:
        if t in skip:
            continue
        srcs = [x.split("\t")[0] for x in run(proj, t, "Sources").split("\n") if x.strip()]
        srcs = [x for x in srcs if not re.match(r"(.*/)?tests/", x)]
        srcs = [x for x in srcs if not x.endswith((".h", ".hpp"))]
        if not srcs:
            continue
        s = settings(proj, t)
        s = {k: expand(v, s, t) for k, v in s.items()}
        d = s.get("INSTALL_PATH", "/usr/bin").strip('"').lstrip("/")
        inherited = INHERITED
        flags = ["-D" + x for x in parse_list(s.get("GCC_PREPROCESSOR_DEFINITIONS", ""))
                 if x and x not in inherited]
        flags += [x for x in parse_list(s.get("OTHER_CFLAGS", "")) if x not in inherited]
        for h in parse_list(s.get("HEADER_SEARCH_PATHS", "")) + parse_list(s.get("USER_HEADER_SEARCH_PATHS", "")):
            h = re.sub(r"\$\{(\w+)\}", r"$(\1)", h)
            h = h.replace("$(SYSTEM_LIBRARY_DIR)", "/System/Library")
            if h in ("$(SRCROOT)", "$(PROJECT_DIR)"):
                flags.append("-I$(SRCROOT)")
                continue
            h = h.replace("$(SRCROOT)/", "").replace("$(PROJECT_DIR)/", "")
            if h.startswith("/System/") or h.startswith("/usr/"):
                h = "$(SDKROOT)" + h
            if h.startswith("$(SDKROOT)"):
                flags.append("-I" + h.replace("$(SDKROOT)//", "$(SDKROOT)/"))
            elif h and "$" not in h and not h.startswith("/"):
                flags.append("-I$(SRCROOT)/" + h)
        libs = []
        for f in run(proj, t, "Frameworks").split("\n"):
            m = re.search(r"lib([A-Za-z0-9_.+-]+?)\.(tbd|dylib|a)$", f.strip())
            if m:
                libs.append("-l" + m.group(1))
            m = re.search(r"([A-Za-z0-9_]+)\.framework$", f.strip())
            if m:
                libs.append("-framework " + m.group(1))
        for x in parse_list(s.get("OTHER_LDFLAGS", "")):
            if x.startswith("-l"):
                libs.append(x)
        out.append("TOOLS += %s" % t)
        out.append("%s_SRCS := %s" % (t, " ".join(srcs)))
        out.append("%s_DIR := %s" % (t, d))
        if flags:
            out.append("%s_FLAGS := %s" % (t, " ".join(mkquote(f) for f in flags)))
        if libs:
            out.append("%s_LIBS := %s" % (t, " ".join(dict.fromkeys(libs))))
        if "INSTALL_MODE_FLAG" in s:
            mode = install_mode(s["INSTALL_MODE_FLAG"])
            if mode & 0o6000:
                out.append("%s_MODE := %o" % (t, mode))
        mans = man_pages(proj, t)
        if mans:
            out.append("%s_MANS := %s" % (t, " ".join(mans)))
        out.append("")
    for l in links(text):
        if os.path.basename(l.split(":")[0]) not in skip:
            out.append("LINKS += %s" % l)
    for a in sorted(set(re.findall(r'PBXAggregateTarget "([^"]+)"', text))):
        if a not in skip:
            for m in man_pages(proj, a):
                out.append("MANS += %s" % m)
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
