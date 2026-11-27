#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
usage: pbxproj-man.py [-d DSTROOT] [--man-list FILE] [--omit PAGE]... [--replace FILE=OURS]...
                      PROJECT.xcodeproj [TARGET...]
  Without TARGETs, every native and aggregate target's.
  --man-list FILE   the project's list of man pages, "source dest links..." a line
  --omit PAGE       omit PAGE from copy phases and from a source list, when given
  --replace FILE=OURS
                    the man phase reads OURS as FILE, whether or not the
                    project has FILE or its directory
  FILE is a path under the project directory.
"""
import getpass
import grp
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "pbxproj-sources.py")
ID = r"[0-9A-F]{24}"


def tools_module():
    spec = importlib.util.spec_from_file_location("xcodeproj_tools", os.path.join(HERE, "pbxproj-tools.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def objects(text):
    objs = {}
    for m in re.finditer(r"\n\t\t(" + ID + r")(?: /\* .*? \*/)? = \{\n(.*?)\n\t\t\};", text, re.S):
        objs[m.group(1)] = m.group(2)
    for m in re.finditer(r"\n\t\t(" + ID + r")(?: /\* .*? \*/)? = (\{isa = .*?\});", text):
        objs[m.group(1)] = m.group(2)
    return objs


def unescape(s):
    return s.encode().decode("unicode_escape").encode("latin-1").decode("utf-8")


def script_phases(objs, target):
    for body in objs.values():
        if not re.search(r"isa = PBX(Native|Aggregate)Target;", body):
            continue
        name = re.search(r"\bname = \"?([^\";]+)\"?;", body)
        if not name or name.group(1) != target:
            continue
        phases = re.search(r"buildPhases = \((.*?)\);", body, re.S)
        for pid in re.findall(ID, phases.group(1) if phases else ""):
            pb = objs.get(pid, "")
            if "isa = PBXShellScriptBuildPhase" not in pb:
                continue
            sh = re.search(r"shellPath = ([^;]+);", pb)
            sc = re.search(r'shellScript = "((?:[^"\\]|\\.)*)";', pb)
            nm = re.search(r'\bname = "?([^";]+)"?;', pb)
            ins = re.search(r"inputPaths = \((.*?)\);", pb, re.S)
            inputs = [x.strip().strip(",").strip('"') for x in (ins.group(1).split("\n") if ins else [])]
            if sc:
                yield ((nm.group(1) if nm else ""), (sh.group(1).strip('"') if sh else "/bin/sh"),
                       unescape(sc.group(1)), [x for x in inputs if x])


def installs_man(name, script, inputs, projdir):
    if re.search(r"share/man|MANDIR|man-?pages", name + "\n" + script, re.I):
        return True
    for ref in re.findall(r"xcodescripts/[A-Za-z0-9_.+-]+", script + "\n" + "\n".join(inputs)):
        try:
            if "share/man" in open(os.path.join(projdir, ref), errors="replace").read():
                return True
        except OSError:
            pass
    return False


def overlay(projdir, out, rel, data):
    parts = rel.split("/")
    src, dst = projdir, out
    for part in parts[:-1]:
        src, dst = os.path.join(src, part), os.path.join(dst, part)
        if os.path.islink(dst):
            os.unlink(dst)
            os.makedirs(dst)
            for e in os.listdir(src):
                os.symlink(os.path.join(src, e), os.path.join(dst, e))
        elif not os.path.isdir(dst):
            os.makedirs(dst)
    dst = os.path.join(dst, parts[-1])
    if os.path.lexists(dst):
        os.unlink(dst)
    with open(dst, "wb") as f:
        f.write(data)


def shadow(projdir, out, files=None):
    os.makedirs(out)
    for e in os.listdir(projdir):
        if e != "xcodescripts":
            os.symlink(os.path.join(projdir, e), os.path.join(out, e))
    xs = os.path.join(projdir, "xcodescripts")
    if os.path.isdir(xs):
        os.makedirs(os.path.join(out, "xcodescripts"))
        for e in os.listdir(xs):
            src, dst = os.path.join(xs, e), os.path.join(out, "xcodescripts", e)
            if not os.path.isfile(src):
                os.symlink(src, dst)
                continue
            data = open(src, "rb").read()
            m = re.match(rb"#!\s*(\S+)\s+(-[^\n]*\S)[ \t]*\n", data)
            if m:
                data = b"#!" + m.group(1) + b"\nset " + m.group(2) + b"\n" + data[m.end():]
            with open(dst, "wb") as f:
                f.write(data)
            shutil.copymode(src, dst)
    for rel, data in sorted((files or {}).items()):
        overlay(projdir, out, rel, data)
    return out


def omitted(path, pages):
    out = []
    for line in open(path, "rb").read().split(b"\n"):
        words = line.split()
        if words and not line.startswith(b"#") and words[0].decode() in pages:
            continue
        out.append(line)
    return b"\n".join(out)


def copy_pages(proj, target):
    out = subprocess.run([sys.executable, TOOL, proj, target, "Man"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        path, sect = line.split("\t")
        yield path, sect


def main():
    args = sys.argv[1:]
    dst, man_list, omit, replace = None, None, set(), {}
    while args and args[0].startswith("-"):
        if len(args) < 2 or args[0] not in ("-d", "--man-list", "--omit", "--replace"):
            sys.exit(__doc__.strip())
        opt, val, args = args[0], args[1], args[2:]
        if opt == "-d":
            dst = os.path.abspath(val)
        elif opt == "--man-list":
            man_list = val
        elif opt == "--omit":
            omit.add(val)
        else:
            rel, eq, ours = val.partition("=")
            if not eq:
                sys.exit(__doc__.strip())
            replace[rel] = open(ours, "rb").read()
    proj, targets = args[0], args[1:]
    projdir = os.path.dirname(os.path.abspath(proj))
    files = dict(replace)
    text = open(os.path.join(proj, "project.pbxproj"), encoding="utf-8").read()
    if not targets:
        targets = sorted(set(re.findall(r'PBX(?:Native|Aggregate)Target "([^"]+)"', text)))
    copies = {target: list(copy_pages(proj, target)) for target in targets}
    available = {name for pages in copies.values() for path, _ in pages
                 for name in (path, os.path.basename(path))}
    if man_list:
        available.update(w.split()[0] for w in open(os.path.join(projdir, man_list)).read().split("\n")
                         if w.split() and not w.startswith("#"))
        files[man_list] = omitted(os.path.join(projdir, man_list), omit)
    missing = sorted(omit - available)
    if missing:
        sys.exit("pbxproj-man: %s lists no %s" % (man_list or "copy phases", ", ".join(missing)))
    selected = {target: [(path, sect) for path, sect in pages
                         if path not in omit and os.path.basename(path) not in omit]
                for target, pages in copies.items()}
    seen = set()
    if dst is None:
        for t in targets:
            for path, sect in selected[t]:
                if (path, sect) not in seen:
                    seen.add((path, sect))
                    print("%s:%s" % (path, sect))
        return

    for t in targets:
        for path, sect in selected[t]:
            src = os.path.join(projdir, path)
            if not os.path.exists(src):
                print("pbxproj-man: %s: no %s" % (t, path), file=sys.stderr)
                continue
            d = os.path.join(dst, "usr/share/man", sect)
            os.makedirs(d, exist_ok=True)
            shutil.copy(src, d)

    objs = objects(text)
    tools = tools_module()
    user, group = getpass.getuser(), grp.getgrgid(os.getgid()).gr_name
    scratch = tempfile.mkdtemp(prefix="pbxproj-man.")
    try:
        projdir = shadow(projdir, os.path.join(scratch, "src"), files)
        for d in ("usr/lib", "usr/bin", "usr/sbin", "usr/libexec", "usr/include", "usr/local/lib",
                  "usr/local/include", "usr/share/man"):
            os.makedirs(os.path.join(dst, d), exist_ok=True)
        for t in targets:
            phases = [p for p in script_phases(objs, t) if installs_man(p[0], p[2], p[3], projdir)]
            if not phases:
                continue
            try:
                s = tools.settings(proj, t)
            except Exception:
                s = {}
            ipath = tools.expand(s.get("INSTALL_PATH", "/usr/bin"), s, t).strip('"')
            prod = tools.expand(s.get("PRODUCT_NAME", t), s, t).replace("$(TARGET_NAME)", t).strip('"')
            env = dict(os.environ)
            env.update({
                "PATH": os.path.join(HERE, "bsd") + os.pathsep + env.get("PATH", ""),
                "ACTION": "install", "PLATFORM_NAME": "macosx", "VARIANT_PLATFORM_NAME": "macosx",
                "ARCHS": "arm64", "CURRENT_ARCH": "arm64", "NATIVE_ARCH": "arm64",
                "SRCROOT": projdir, "PROJECT_DIR": projdir, "DSTROOT": dst, "INSTALL_ROOT": dst,
                "INSTALL_PATH": ipath, "INSTALL_DIR": dst + ipath, "TARGET_NAME": t,
                "PRODUCT_NAME": prod, "EXECUTABLE_NAME": prod, "RC_ProjectName": os.path.basename(projdir),
                "INSTALL_OWNER": user, "INSTALL_GROUP": group, "INSTALL_MODE_FLAG": "u+w,go-w,a+rX",
                "ALTERNATE_OWNER": user, "ALTERNATE_GROUP": group, "ALTERNATE_MODE": "u+w,go-w,a+rX",
                "DERIVED_FILES_DIR": scratch, "DERIVED_FILE_DIR": scratch, "BUILT_PRODUCTS_DIR": scratch,
                "TARGET_BUILD_DIR": scratch, "OBJROOT": scratch, "SYMROOT": scratch,
                "TEMP_DIR": scratch, "SDKROOT": env.get("SDKROOT", ""),
            })
            for name, shell, script, inputs in phases:
                words = shell.split()
                if os.path.basename(words[0]) in ("sh", "bash"):
                    words[0] = "/bin/bash"
                elif os.path.basename(words[0]).startswith("python"):
                    words[0] = sys.executable
                penv = dict(env)
                for i, x in enumerate(inputs):
                    x = re.sub(r"\$[({]?(SRCROOT|PROJECT_DIR)[)}]?", projdir, x)
                    penv["SCRIPT_INPUT_FILE_%d" % i] = x
                penv["SCRIPT_INPUT_FILE_COUNT"] = str(len(inputs))
                with tempfile.NamedTemporaryFile("w", suffix=".sh", dir=scratch, delete=False) as f:
                    f.write(script)
                r = subprocess.run(words + [f.name], cwd=projdir, env=penv,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
                if r.returncode:
                    last = (r.stderr.strip().splitlines() or [""])[-1]
                    print("pbxproj-man: %s: %s failed (%d): %s" % (t, name or "a script phase", r.returncode, last),
                          file=sys.stderr)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
