#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
Print the files of one target in a project file, as paths relative to the
project's directory (resolved through the project's group tree).

usage: pbxproj-sources.py PROJECT TARGET [PHASE]
  PROJECT is the project directory, the one that holds project.pbxproj.
  PHASE is Sources (the default), Headers, Frameworks, Man or Settings:
    Sources     the source files; one with its own compiler flags
                (COMPILER_FLAGS) is printed as "path<TAB>flags"
    Headers     the headers, each as "path<TAB>Public|Private|Project"
    Frameworks  the libraries and frameworks the target links
    Man         the man pages the target installs (its Copy Files phases
                into /usr/share/man/manN), each as "path<TAB>manN"; this
                also works for aggregate targets
    Settings    the target's Release configuration instead of files: its
                xcconfig file and build settings

  Sources also include the target's files in file-system-synchronized
  groups (object version 77): every source file under a group that the target lists,
  except the group's membership exceptions for the target, plus the files
  that other groups' exceptions add to the target. Files limited to
  platforms other than macOS, or moved to another build phase, are left
  out.
"""
import os
import re
import sys

ID = r"[0-9A-F]{24}"


def load(path):
    s = open(path + "/project.pbxproj", encoding="utf-8").read()
    objs = {}
    # One-line objects: ID /* comment */ = {isa = ...; ...; };
    for m in re.finditer(r"\n\t\t(" + ID + r")(?: /\* .*? \*/)? = \{(isa = [^\n]*?)\};(?=\n)", s):
        objs[m.group(1)] = m.group(2)
    # Multi-line objects.
    for m in re.finditer(r"\n\t\t(" + ID + r")(?: /\* .*? \*/)? = \{\n(.*?)\n\t\t\};", s, re.S):
        objs.setdefault(m.group(1), m.group(2))
    return objs


def attr(body, key):
    m = re.search(r"(?:^|[\s{;])" + key + r' = ("(?:[^"\\]|\\.)*"|[^;]+);', body)
    if not m:
        return None
    return re.sub(r"\s*/\*.*?\*/", "", m.group(1)).strip().strip('"')


SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".s", ".S")


def listattr(body, key):
    """Items of a parenthesised list attribute, without comments or quotes."""
    m = re.search(key + r" = \((.*?)\);", body, re.S)
    if not m:
        return []
    items = re.sub(r"/\*.*?\*/", "", m.group(1)).split(",")
    return [i.strip().strip('"') for i in items if i.strip()]


def synced_sources(objs, tid, tbody, projdir):
    """Sources of target `tid` that come from file-system-synchronized groups."""
    own = set(re.findall(ID, re.search(r"fileSystemSynchronizedGroups = \((.*?)\);", tbody, re.S).group(1))) \
        if "fileSystemSynchronizedGroups" in tbody else set()
    out = []
    for gid, gbody in objs.items():
        if "isa = PBXFileSystemSynchronizedRootGroup" not in gbody:
            continue
        gpath = attr(gbody, "path")
        excepted, added, dropped = set(), set(), set()
        for eid in listattr(gbody, "exceptions"):
            eid = re.search(ID, eid).group(0)
            eb = objs.get(eid, "")
            if "PBXFileSystemSynchronizedBuildFileExceptionSet" in eb:
                if attr(eb, "target") != tid:
                    continue
                excepted |= set(listattr(eb, "membershipExceptions"))
                pf = re.search(r"platformFiltersByRelativePath = \{(.*?)\n\t\t\t\};", eb, re.S)
                if pf:
                    for f, plats in re.findall(r'\s*"?([^"=\n]+?)"? = \((.*?)\);', pf.group(1), re.S):
                        if "macos" not in plats:
                            dropped.add(f.strip())
            elif "PBXFileSystemSynchronizedGroupBuildPhaseMembershipExceptionSet" in eb:
                # Files in another build phase of a target are not Sources.
                dropped |= set(listattr(eb, "membershipExceptions"))
        if gid in own:
            root = os.path.join(projdir, gpath)
            for dirpath, dirs, files in os.walk(root):
                dirs.sort()
                for f in sorted(files):
                    rel = os.path.relpath(os.path.join(dirpath, f), root)
                    if rel.endswith(SOURCE_EXTS) and rel not in excepted and rel not in dropped:
                        out.append(os.path.join(gpath, rel))
        else:
            for rel in sorted(excepted):
                if rel.endswith(SOURCE_EXTS) and rel not in dropped:
                    out.append(os.path.join(gpath, rel))
    return out


def main():
    proj, target = sys.argv[1], sys.argv[2]
    phase = sys.argv[3] if len(sys.argv) > 3 else "Sources"
    objs = load(proj)
    parent = {}
    for oid, body in objs.items():
        if re.search(r"isa = (PBXGroup|PBXVariantGroup)", body):
            ch = re.search(r"children = \((.*?)\);", body, re.S)
            for c in re.findall(ID, ch.group(1)) if ch else []:
                parent[c] = oid

    def fullpath(oid):
        parts = []
        while oid:
            body = objs.get(oid, "")
            p = attr(body, "path")
            tree = attr(body, "sourceTree")
            if p:
                parts.append(p)
            if tree and tree != "<group>":
                if tree not in ("SOURCE_ROOT", "<absolute>"):
                    parts.append("$(" + tree + ")")
                break
            oid = parent.get(oid)
        return "/".join(reversed(parts))

    for oid, body in objs.items():
        native = "isa = PBXNativeTarget" in body
        if (native or (phase == "Man" and "isa = PBXAggregateTarget" in body)) and \
                attr(body, "name") == target:
            if phase == "Settings":
                cl = objs[attr(body, "buildConfigurationList")]
                ids = re.findall(ID, re.search(r"buildConfigurations = \((.*?)\);", cl, re.S).group(1))
                confs = [objs[c] for c in ids]
                conf = next((c for c in confs if attr(c, "name") == "Release"), confs[0])
                base = attr(conf, "baseConfigurationReference")
                print("xcconfig: %s" % (fullpath(base) if base else "(none)"))
                bs = re.search(r"buildSettings = \{(.*?)\n\t\t\t\};", conf, re.S)
                for line in (bs.group(1).strip().splitlines() if bs else []):
                    print("  " + line.strip())
                return
            if phase == "Sources":
                for path in synced_sources(objs, oid, body, os.path.dirname(os.path.abspath(proj))):
                    print(path)
            phases = re.search(r"buildPhases = \((.*?)\);", body, re.S).group(1)
            for ph in re.findall(ID, phases):
                pb = objs.get(ph, "")
                if phase == "Man":
                    # Any share/man/manN (/usr/share/man, $(INSTALL_PATH)/../share/man,
                    # $(GZIP_PREFIX)/share/man), but not /usr/local's (internal pages).
                    dst = re.search(r"dstPath = \"?([^;\"]*?)share/man/(man[^/\";]+)/?\"?;", pb)
                    if dst and "/usr/local" in dst.group(1):
                        dst = None
                    if "isa = PBXCopyFilesBuildPhase" not in pb or not dst:
                        continue
                    for f in re.findall(ID, re.search(r"files = \((.*?)\);", pb, re.S).group(1)):
                        print("%s\t%s" % (fullpath(attr(objs[f], "fileRef")), dst.group(2)))
                    continue
                if "isa = PBX" + phase + "BuildPhase" not in pb:
                    continue
                for f in re.findall(ID, re.search(r"files = \((.*?)\);", pb, re.S).group(1)):
                    path = fullpath(attr(objs[f], "fileRef"))
                    if phase == "Headers":
                        # Header visibility: Public, Private or Project (none).
                        a = re.search(r"ATTRIBUTES = \((.*?)\)", objs[f], re.S)
                        vis = [x.strip().strip(",") for x in (a.group(1).split() if a else [])]
                        vis = [v for v in vis if v in ("Public", "Private")]
                        print("%s\t%s" % (path, vis[0] if vis else "Project"))
                    else:
                        m = re.search(r'COMPILER_FLAGS = ("(?:[^"\\]|\\.)*"|[^;]+);', objs[f])
                        flags = m.group(1).strip('"').replace('\\"', '"') if m else ""
                        print("%s\t%s" % (path, flags) if flags else path)
            return
    sys.exit("no target %s" % target)


if __name__ == "__main__":
    main()
