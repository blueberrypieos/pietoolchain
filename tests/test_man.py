# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PBXPROJ_MAN = ROOT / "userland/tools/pbxproj-man.py"

SCRIPT = r'''grep -v "^#" "$SRCROOT/man/manpages.lst" | while read src dst links; do
	d="$DSTROOT/usr/share/man/man1"; mkdir -p "$d"; cp "$SRCROOT/man/$src" "$d/$dst"
	for l in $links; do ln -sf "$src" "$d/$l"; done
done
cp "$SRCROOT/conf/value.txt" "$DSTROOT/usr/share/man/man1/value.1"
'''


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def pbx_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'


def project(tmp_path):
    proj = tmp_path / "proj"
    write(proj / "Test.xcodeproj/project.pbxproj", """// !$*UTF8*$!
{
	objects = {
		AAAAAAAAAAAAAAAAAAAAAAA1 /* Test */ = {
			isa = PBXNativeTarget;
			buildPhases = (
				AAAAAAAAAAAAAAAAAAAAAAA2 /* Install man pages */,
			);
			name = Test;
		};
		AAAAAAAAAAAAAAAAAAAAAAA2 /* Install man pages */ = {
			isa = PBXShellScriptBuildPhase;
			name = "Install man pages";
			shellPath = /bin/sh;
			shellScript = %s;
		};
	};
}
""" % pbx_string(SCRIPT))
    write(proj / "man/manpages.lst", "# manpage tables\na.1 a.1 a-link.1\nb.1 b.1 b-link.1 b-other.1\n")
    write(proj / "man/a.1", "page a\n")
    write(proj / "man/b.1", "page b\n")
    write(proj / "conf/value.txt", "theirs\n")
    write(tmp_path / "ours.txt", "ours\n")
    return proj


def run_man(tmp_path, *options):
    proj = project(tmp_path)
    dst = tmp_path / "dst"
    r = subprocess.run([sys.executable, str(PBXPROJ_MAN), "-d", str(dst), *options,
                        str(proj / "Test.xcodeproj"), "Test"], capture_output=True, text=True)
    return proj, dst / "usr/share/man/man1", r


def test_man_phase_without_options_installs_every_listed_page(tmp_path):
    proj, man1, r = run_man(tmp_path)
    assert r.returncode == 0, r.stderr
    assert sorted(os.listdir(man1)) == ["a-link.1", "a.1", "b-link.1", "b-other.1", "b.1", "value.1"]
    assert (man1 / "value.1").read_text() == "theirs\n"


def test_omit_leaves_out_the_page_and_its_links(tmp_path):
    proj, man1, r = run_man(tmp_path, "--man-list", "man/manpages.lst", "--omit", "b.1")
    assert r.returncode == 0, r.stderr
    assert sorted(os.listdir(man1)) == ["a-link.1", "a.1", "value.1"]
    assert os.readlink(man1 / "a-link.1") == "a.1"
    assert (proj / "man/manpages.lst").read_text().count("b.1") == 2


def test_replace_gives_the_man_phase_our_file(tmp_path):
    proj, man1, r = run_man(tmp_path, "--replace", "conf/value.txt=%s" % (tmp_path / "ours.txt"))
    assert r.returncode == 0, r.stderr
    assert (man1 / "value.1").read_text() == "ours\n"
    assert (proj / "conf/value.txt").read_text() == "theirs\n"


def test_replace_of_a_file_the_project_lacks(tmp_path):
    proj = project(tmp_path)
    (proj / "conf/value.txt").unlink()
    (proj / "conf").rmdir()
    dst = tmp_path / "dst"
    r = subprocess.run([sys.executable, str(PBXPROJ_MAN), "-d", str(dst),
                        "--replace", "conf/value.txt=%s" % (tmp_path / "ours.txt"),
                        str(proj / "Test.xcodeproj"), "Test"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert (dst / "usr/share/man/man1/value.1").read_text() == "ours\n"
    assert not (proj / "conf").exists()


def test_omit_of_an_unlisted_page_fails(tmp_path):
    proj, man1, r = run_man(tmp_path, "--man-list", "man/manpages.lst", "--omit", "c.1")
    assert r.returncode != 0
    assert "lists no c.1" in r.stderr


def test_install_man_list_copies_pages_and_links_to_their_page(tmp_path):
    src = tmp_path / "src"
    write(src / "man2/a.2", "page a\n")
    write(src / "man2/b.2", "page b\n")
    write(tmp_path / "man.list", "# list\nman2/a.2 a.2 a-link.2\nman2/a.2 a-copy.2\nman2/b.2 b.2\n")
    write(tmp_path / "Makefile", "include %s\nall:\n\t$(call install_man_list,%s,%s,444)\n"
          % (ROOT / "userland/tools/man.mk", tmp_path / "man.list", src))
    root = tmp_path / "root"
    r = subprocess.run(["make", "-s", "-C", str(tmp_path), "ROOT=" + str(root)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    man2 = root / "usr/share/man/man2"
    assert sorted(os.listdir(man2)) == ["a-copy.2", "a-link.2", "a.2", "b.2"]
    assert os.readlink(man2 / "a-link.2") == "a.2"
    assert not (man2 / "a-copy.2").is_symlink() and (man2 / "a-copy.2").read_text() == "page a\n"
    assert (man2 / "b.2").stat().st_mode & 0o777 == 0o444


def copy_project(tmp_path):
    proj = tmp_path / "proj"
    write(proj / "Test.xcodeproj/project.pbxproj", """// !$*UTF8*$!
{
	objects = {
		AAAAAAAAAAAAAAAAAAAAAAA1 = {
			isa = PBXNativeTarget;
			buildPhases = (AAAAAAAAAAAAAAAAAAAAAAA2,);
			name = Test;
		};
		AAAAAAAAAAAAAAAAAAAAAAA2 = {
			isa = PBXCopyFilesBuildPhase;
			dstPath = /usr/share/man/man1;
			files = (AAAAAAAAAAAAAAAAAAAAAAA5, AAAAAAAAAAAAAAAAAAAAAAA6,);
		};
		AAAAAAAAAAAAAAAAAAAAAAA3 = {isa = PBXFileReference; path = man/a.1; sourceTree = SOURCE_ROOT; };
		AAAAAAAAAAAAAAAAAAAAAAA4 = {isa = PBXFileReference; path = man/b.1; sourceTree = SOURCE_ROOT; };
		AAAAAAAAAAAAAAAAAAAAAAA5 = {isa = PBXBuildFile; fileRef = AAAAAAAAAAAAAAAAAAAAAAA3; };
		AAAAAAAAAAAAAAAAAAAAAAA6 = {isa = PBXBuildFile; fileRef = AAAAAAAAAAAAAAAAAAAAAAA4; };
	};
}
""")
    write(proj / "man/a.1", "page a\n")
    write(proj / "man/b.1", "page b\n")
    return proj


def test_copy_phase_omissions_match_full_and_reduced_trees(tmp_path):
    proj = copy_project(tmp_path)
    original = (proj / "Test.xcodeproj/project.pbxproj").read_bytes()
    outputs = []
    for name in ("full", "reduced"):
        if name == "reduced":
            (proj / "man/b.1").unlink()
        dst = tmp_path / name
        result = subprocess.run([sys.executable, str(PBXPROJ_MAN), "-d", str(dst), "--omit", "b.1",
                                 str(proj / "Test.xcodeproj"), "Test"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "no man/b.1" not in result.stderr
        output = dst / "usr/share/man/man1"
        outputs.append({f.name: f.read_bytes() for f in output.iterdir()})
        assert (proj / "Test.xcodeproj/project.pbxproj").read_bytes() == original
    assert outputs == [{"a.1": b"page a\n"}] * 2


def test_copy_phase_listing_and_unknown_omission(tmp_path):
    proj = copy_project(tmp_path)
    command = [sys.executable, str(PBXPROJ_MAN), "--omit"]
    project_args = [str(proj / "Test.xcodeproj"), "Test"]
    result = subprocess.run(command + ["man/b.1"] + project_args, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout == "man/a.1:man1\n"
    result = subprocess.run(command + ["c.1"] + project_args, capture_output=True, text=True)
    assert result.returncode != 0
    assert "copy phases lists no c.1" in result.stderr
