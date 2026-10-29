#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
Write a tools.mk for a project made up of command-line tools, such as
shell_cmds, file_cmds or text_cmds. For each native target in the project
file, it writes the target's sources, install directory and compiler
settings as make variables, which tools.mk turns into build rules.

usage: pbxproj-tools.py PROJECT [-x TARGET:REASON]... [-s TARGET]...
  PROJECT is the project directory, the one that holds project.pbxproj
  -x  leave TARGET out, and list it with REASON (why it isn't built) in the
      generated file's header
  -s  leave TARGET out without listing it (for tests, internal tools, or a
      target that the project's own makefile builds)

For each target T it writes:
  TOOLS     += T
  T_SRCS    := the target's sources (paths relative to the project)
  T_DIR     := its INSTALL_PATH (default /usr/bin), without the leading /
  T_FLAGS   := -D for each of its GCC_PREPROCESSOR_DEFINITIONS, its
               OTHER_CFLAGS, and -I for each of its HEADER_SEARCH_PATHS
               inside the project
  T_LIBS    := -l for each library it links and -framework for each
               framework (from its Frameworks phase and OTHER_LDFLAGS)
  T_MODE    := its install mode in octal, when its INSTALL_MODE_FLAG makes
               it setuid or setgid (su, passwd, ping, ...)
  T_MANS    := the man pages it installs (its Copy Files phases into
               /usr/share/man), as path:manN
The settings come from the target's Release configuration and its xcconfig
file. Only plain assignments are read, and [sdk=macosx*] variants take
precedence.

Only tool targets (com.apple.product-type.tool) are listed. Aggregate
targets that hard-link a tool, such as file_cmds' unlink and chgrp (with
xcodescripts/hardlink.sh, or an inline ln -f from the script phase's input
to its output), become LINKS += SOURCE:LINK entries, with both paths
relative to the root. The man pages of aggregate targets (the links' pages,
or a project's separate man page target) become MANS += path:manN entries.
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


# $inherited also means $(inherited).
INHERITED = ("$(inherited)", "${inherited}", "$inherited", '"$(inherited)"')


def setting_unescape(x):
    """A build setting item as the project's build reads it: shell quoting removed
    (SYSDIR='"/etc"' is SYSDIR="/etc"; \\" is a quote)."""
    try:
        words = shlex.split(x)
    except ValueError:
        words = None
    if words is not None and len(words) == 1:
        return words[0]
    return re.sub(r"\\(['\"])", r"\1", x)


def unquote(x):
    """An item of a project-file list: surrounding quotes removed, the
    project file's string escapes undone, then the build setting's."""
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
    # Words separated by spaces; \" is a literal quote.
    return [w.replace('\\"', '"') for w in re.findall(r'(?:[^\s"\\]|\\.|"[^"]*")+', v)]


# Paths of the build, not settings: left for the makefile (SDKROOT is a
# setting too, naming the SDK, e.g. macosx.internal).
BUILD_PATHS = ("SDKROOT", "SRCROOT", "PROJECT_DIR", "BUILT_PRODUCTS_DIR", "DERIVED_FILE_DIR",
               "DERIVED_SOURCES_DIR", "OBJROOT", "SYMROOT", "DSTROOT")


def expand(v, s, target, depth=0):
    """Expand $(NAME) from the settings; unknown names are left for make."""
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
    """A word for a make recipe (run by sh)."""
    if not re.search(r"[\"'\s\\]", x):
        return x
    if "'" in x:
        return '"%s"' % x.replace("\\", "\\\\").replace('"', '\\"')
    return "'%s'" % x


def read_xcconfig(path, s, conditional=None):
    """An xcconfig file's assignments into S, those of the files it
    #includes (relative to it) where the #include is; a later assignment
    wins, but an [sdk=macosx*] variant over a plain one."""
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
    """The target's build settings: its xcconfig's, then the target's own."""
    out = run(proj, target, "Settings")
    s = {}
    m = re.match(r"xcconfig: (.*)", out)
    projdir = os.path.dirname(os.path.abspath(proj))
    if m and m.group(1) != "(none)":
        read_xcconfig(os.path.join(projdir, m.group(1)), s)
    body = out.split("\n", 1)[1] if "\n" in out else ""
    found = re.findall(r'^\s*"?([A-Z_a-z0-9]+)((?:\[[^\]]*\])*)"? = (\((?:.|\n)*?\)|[^;]*);', body, re.M)
    # The plain values first, then the macOS ones over them; $(inherited)
    # in a conditional value is the plain value of the same level.
    for key, cond, val in sorted(found, key=lambda f: bool(f[1])):
        if cond and "macosx" not in cond:
            continue
        if cond and key in s:
            val = inherit(val, s[key])
        s[key] = val
    return s


def inherit(val, base):
    """VAL with its $(inherited) item replaced by BASE (both raw values)."""
    items = parse_raw(val)
    if not any(x in INHERITED for x in items):
        return val
    out = []
    for x in items:
        out.extend(parse_raw(base) if x in INHERITED else [x])
    return "(\n" + "".join("%s,\n" % x for x in out) + ")"


def parse_raw(v):
    """A raw value's items, still quoted as the project file has them."""
    v = v.strip()
    if v.startswith("("):
        return [x.strip().rstrip(",") for x in v[1:-1].split("\n") if x.strip().rstrip(",")]
    return [v]


def install_mode(flag, mode=0o755):
    """The mode a tool is installed with: chmod FLAG on the linker's 0755
    (octal, or symbolic clauses such as u+s,go-w,a+rX)."""
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
    """SOURCE:LINK pairs of the aggregate targets that run hardlink.sh."""
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
            # hardlink.sh, or an inline ln -f of the phase's input to its output.
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
    """The man pages a target installs, as path:manN."""
    out = []
    for line in run(proj, target, "Man").split("\n"):
        if "\t" in line:
            path, sect = line.split("\t")
            out.append("%s:%s" % (path, sect))
    return out


def product_types(text):
    """Each native target's product type."""
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
    # Only command-line tools; libraries are the project makefile's.
    for t in targets:
        if ptypes.get(t, "com.apple.product-type.tool") != "com.apple.product-type.tool":
            skip.add(t)
    name = os.path.basename(os.path.normpath(proj))
    out = ["# Generated by pbxproj-tools.py from %s." % name,
           "# Each tool's sources, install directory, flags and libraries."]
    if why:
        out.append("# Not built yet:")
        for t in sorted(why):
            out.append("#   %s (%s)" % (t, why[t]))
    out.append("")
    for t in targets:
        if t in skip:
            continue
        # Test sources (darwintest) that a tool's Sources phase lists by
        # mistake are dropped.
        srcs = [x.split("\t")[0] for x in run(proj, t, "Sources").split("\n") if x.strip()]
        srcs = [x for x in srcs if not re.match(r"(.*/)?tests/", x)]
        # Headers a Sources phase lists are not compiled.
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
                # Absolute system paths are looked up in the SDK.
                h = "$(SDKROOT)" + h
            if h.startswith("$(SDKROOT)"):
                # An SDK directory; the makefile defines SDKROOT.
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
        # Not the links to a tool that is left out.
        if os.path.basename(l.split(":")[0]) not in skip:
            out.append("LINKS += %s" % l)
    for a in sorted(set(re.findall(r'PBXAggregateTarget "([^"]+)"', text))):
        if a not in skip:
            for m in man_pages(proj, a):
                out.append("MANS += %s" % m)
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
