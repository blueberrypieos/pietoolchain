#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
Make the binary packages debian/control names, for debian/rules.

usage: mkdebs.py PIE arch|indep

indep: pietoolchain-sdk, from the SDK.
arch: each package with an X-Pie-Project field, from that project's staging
root, build/pkgroot/<project>.
"""
import hashlib
import os
import re
import shutil
import stat
import subprocess
import sys

ARCH = "darwin-arm64"
LINKED = {"etc", "tmp", "var"}
SCRIPTS = {"preinst", "postinst", "prerm", "postrm", "config"}
MACHO_MAGICS = (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")
OTHERS = {"OpenBSM": "libbsm"}


LICENCE_PATH = re.compile(r"/usr/share/common-licenses/([A-Za-z0-9+-]+(?:\.[A-Za-z0-9+-]+)*)\.?$")


def with_texts(pie, text):
    """A runtime package's copyright, with the full text from sdk/licenses in
    place of each "The full text ... is in /usr/share/common-licenses/X."."""
    def full(lid):
        f = os.path.join(pie, "sdk", "licenses", lid)
        if not os.path.exists(f):
            die("no sdk/licenses text for %s" % lid)
        return [" " + l if l.strip() else " ." for l in open(f).read().strip("\n").split("\n")]

    out = []
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith(" The full text"):
            j = i
            while j < len(lines) and not LICENCE_PATH.search(lines[j].strip()):
                j += 1
            if j == len(lines) or j > i + 1:
                die("cannot read the licence reference: %s" % line.strip())
            out.extend(full(LICENCE_PATH.search(lines[j].strip()).group(1)))
            i = j + 1
            continue
        if "/usr/share/common-licenses/" in line:
            die("a licence reference that is not a full-text sentence: %s" % line.strip())
        out.append(line)
        i += 1
    return "\n".join(out)

def die(msg):
    sys.exit("mkdebs.py: " + msg)


def stanzas(path):
    out = []
    for par in re.split(r"\n\s*\n", open(path).read().strip()):
        fields, last = {}, None
        for line in par.splitlines():
            if line[:1].isspace() and last:
                fields[last] += "\n" + line
            elif ":" in line:
                last, value = line.split(":", 1)
                fields[last] = value.strip()
        out.append(fields)
    return out


def files_of(root):
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not (dirpath == root and d == "DEBIAN")]
        for name in filenames:
            out.append("/" + os.path.relpath(os.path.join(dirpath, name), root))
        for name in dirnames:
            if os.path.islink(os.path.join(dirpath, name)):
                out.append("/" + os.path.relpath(os.path.join(dirpath, name), root))
    return sorted(out)


def stage(src, dst):
    for name in sorted(os.listdir(src)):
        if name == "DEBIAN":
            continue
        target = os.path.join(dst, "private", name) if name in LINKED else os.path.join(dst, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        subprocess.run(["cp", "-a", os.path.join(src, name), target], check=True)


def fix_modes(root):
    for dirpath, dirnames, filenames in os.walk(root):
        for name in dirnames + filenames:
            p = os.path.join(dirpath, name)
            st = os.lstat(p)
            if not stat.S_ISLNK(st.st_mode):
                os.chmod(p, stat.S_IMODE(st.st_mode) & ~0o022)
    os.chmod(root, 0o755)


def is_macho(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    with open(path, "rb") as f:
        return f.read(4) in MACHO_MAGICS


def load_commands(otool, path):
    out = subprocess.run([otool, "-L", path], check=True, capture_output=True,
                         text=True).stdout.splitlines()[1:]
    deps = []
    for line in out:
        m = re.match(r"\s+(\S.*?) \(compatibility version [^)]*\)$", line)
        if m:
            deps.append((m.group(1), ", weak" in line))
    return deps


def finish(pie, pkg, dst, version=None, fields=(), substvars=()):
    epoch = os.environ["SOURCE_DATE_EPOCH"]
    os.makedirs(os.path.join(dst, "DEBIAN"), exist_ok=True)
    fix_modes(dst)
    conffiles = [p for p in files_of(dst) if p.startswith("/private/etc/")
                 and not os.path.islink(dst + p)]
    if conffiles:
        with open(os.path.join(dst, "DEBIAN", "conffiles"), "w") as f:
            f.write("".join(p + "\n" for p in conffiles))
    with open(os.path.join(dst, "DEBIAN", "md5sums"), "w") as f:
        for p in files_of(dst):
            if os.path.isfile(dst + p) and not os.path.islink(dst + p):
                with open(dst + p, "rb") as g:
                    f.write("%s  %s\n" % (hashlib.md5(g.read()).hexdigest(), p[1:]))
    for name in os.listdir(os.path.join(dst, "DEBIAN")):
        os.chmod(os.path.join(dst, "DEBIAN", name), 0o755 if name in SCRIPTS else 0o644)
    cmd = ["dpkg-gencontrol", "-p" + pkg, "-P" + dst]
    if version:
        cmd.append("-v" + version)
    # debhelper's ${misc:Depends}, which nothing here adds to.
    cmd += ["-D%s=%s" % kv for kv in fields] + ["-Vmisc:Depends="] + ["-V%s=%s" % kv for kv in substvars]
    subprocess.run(cmd, cwd=pie, check=True)
    subprocess.run(["find", dst, "-exec", "touch", "-h", "-d", "@" + epoch, "{}", "+"], check=True)
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zxz", "--build", dst, os.path.dirname(pie)],
                   check=True, env=dict(os.environ, SOURCE_DATE_EPOCH=epoch))


def sdk_provides(pie):
    return ", ".join("%s (= %s)" % (s["Package"], s["X-Pie-Version"])
                     for s in stanzas(os.path.join(pie, "debian", "control")) if "X-Pie-Project" in s)


def indep(pie):
    dst = os.path.join(pie, "debian", "pietoolchain-sdk")
    shutil.rmtree(dst, ignore_errors=True)
    subprocess.run([os.path.join(pie, "sdk", "mksdk.sh"), "-r", dst], check=True)
    doc = os.path.join(dst, "usr", "share", "doc", "pietoolchain-sdk")
    os.makedirs(doc, exist_ok=True)
    with open(os.path.join(doc, "README.source"), "w") as f:
        f.write(open(os.path.join(pie, "sdk", "README.source")).read())
        f.write(open(os.path.join(pie, "sdk", "README.source.ports")).read())
    finish(pie, "pietoolchain-sdk", dst, substvars=[("sdk:Provides", sdk_provides(pie))])


def arch(pie):
    otool = os.path.join(pie, "toolchain", "prefix", "bin", "aarch64-apple-darwin-otool")
    pkgs = [s for s in stanzas(os.path.join(pie, "debian", "control")) if "X-Pie-Project" in s]
    staged = {}
    for s in pkgs:
        pkg, project = s["Package"], s["X-Pie-Project"]
        root = os.path.join(pie, "build", "pkgroot", project)
        if not os.path.isdir(root) or not os.listdir(root):
            die("%s: no staging root %s (./build.sh toolchain userland)" % (pkg, root))
        dst = os.path.join(pie, "debian", pkg)
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(dst)
        stage(root, dst)
        doc = os.path.join(dst, "usr", "share", "doc", pkg)
        os.makedirs(doc, exist_ok=True)
        with open(os.path.join(doc, "copyright"), "w") as f:
            f.write(with_texts(pie, open(os.path.join(pie, "userland", project, "copyright")).read()))
        fields = []
        debian = os.path.join(root, "DEBIAN")
        if os.path.isdir(debian):
            os.makedirs(os.path.join(dst, "DEBIAN"), exist_ok=True)
            for name in sorted(os.listdir(debian)):
                if name in SCRIPTS:
                    shutil.copyfile(os.path.join(debian, name), os.path.join(dst, "DEBIAN", name))
                elif name == "pie-fields":
                    for line in open(os.path.join(debian, name)):
                        if ":" in line:
                            k, v = line.split(":", 1)
                            fields.append((k.strip(), v.strip()))
                else:
                    die("%s: unknown %s in its staging root's DEBIAN" % (pkg, name))
        staged[pkg] = (s, dst, fields)

    owner = {}
    for pkg, (_, dst, _) in staged.items():
        for p in files_of(dst):
            if p in owner:
                die("%s is in both %s and %s" % (p, owner[p], pkg))
            owner[p] = pkg
    for project, pkg in OTHERS.items():
        root = os.path.join(pie, "build", "pkgroot", project)
        for p in files_of(root):
            if p.startswith("/usr/lib/") and p.endswith(".dylib"):
                owner.setdefault(p, pkg)

    def resolve(name):
        if name in owner:
            return owner[name]
        parts = name.strip("/").split("/")
        for i in range(len(parts) - 1, 0, -1):
            link = "/" + "/".join(parts[:i])
            if link in owner and owner[link] in staged:
                target = os.readlink(staged[owner[link]][1] + link)
                return resolve(os.path.normpath(os.path.join(os.path.dirname(link), target, *parts[i:])))
        return None

    unresolved, depends = [], {}
    for pkg, (s, dst, fields) in sorted(staged.items()):
        depends[pkg] = set()
        for p in files_of(dst):
            if not is_macho(dst + p):
                continue
            for name, weak in load_commands(otool, dst + p):
                dep = resolve(name)
                if dep is None and not weak:
                    unresolved.append("%s (%s): %s" % (p, pkg, name))
                elif dep and dep != pkg:
                    depends[pkg].add(dep)
    if unresolved:
        die("libraries in no package:\n  " + "\n  ".join(unresolved))
    for pkg, (s, dst, fields) in sorted(staged.items()):
        finish(pie, pkg, dst, s["X-Pie-Version"], fields,
               [("runtime:Depends", ", ".join(sorted(depends[pkg])))])


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("arch", "indep"):
        sys.exit(__doc__.strip().split("\n\n")[1])
    pie = os.path.abspath(sys.argv[1])
    if "SOURCE_DATE_EPOCH" not in os.environ:
        die("SOURCE_DATE_EPOCH is not set")
    (arch if sys.argv[2] == "arch" else indep)(pie)


if __name__ == "__main__":
    main()
