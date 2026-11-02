#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
Check a kext executable before it goes anywhere near a kernel: that the
kernel's run-time linker (kxld) can link it, and that it calls the kernel
only through the symbol sets it declares.

usage: kextcheck.py [-L DIR]... EXECUTABLE INFO.PLIST SYSTEM.KEXT...

- An arm64 MH_KEXT_BUNDLE, with no dyld information (chained fixups, the
  export trie, LC_DYLD_INFO): kxld reads only the classic relocations.
- Its relocations are all 64-bit pointers (ARM64_RELOC_UNSIGNED), which
  kxld's arm64 relocator handles: external ones for the slots of the
  kernel functions and data it uses (ld64 links arm64 kexts with stubs
  that load them), local ones for its own pointers.
- No segment is both writable and executable.
- It defines _kmod_info, which kxld fills in.
- Every symbol it leaves undefined is exported by a library named in its
  Info.plist's OSBundleLibraries: an interface kext in a System.kext
  (mksystemkext.sh), checked against each one given (one per
  SoC kernel), or a kext in a -L directory (its executable's symbols).
  Only direct libraries count, as with kxld. The exception: virtual
  methods that a C++ class inherits, named only by its vtable's slots,
  which kxld takes from the superclass's vtable in the kernel instead
  (vtable patching).
"""
import os
import plistlib
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_KEXT_BUNDLE = 11
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xB
DYLD_COMMANDS = {
    0x22: "LC_DYLD_INFO", 0x80000022: "LC_DYLD_INFO_ONLY",
    0x80000033: "LC_DYLD_EXPORTS_TRIE", 0x80000034: "LC_DYLD_CHAINED_FIXUPS",
}
ARM64_RELOC_UNSIGNED = 0
VM_PROT_WRITE, VM_PROT_EXECUTE = 2, 4
N_STAB, N_TYPE, N_EXT, N_UNDF = 0xE0, 0x0E, 0x01, 0x0


class MachO:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        magic, self.cputype, _, self.filetype, ncmds, _, _, _ = \
            struct.unpack_from("<8I", self.data, 0)
        if magic != MH_MAGIC_64:
            raise ValueError(f"{path}: not a 64-bit Mach-O")
        self.commands = []
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<2I", self.data, off)
            self.commands.append((cmd, off, size))
            off += size

    def segments(self):
        for cmd, off, _ in self.commands:
            if cmd == LC_SEGMENT_64:
                name = self.data[off + 8:off + 24].rstrip(b"\0").decode()
                maxprot, initprot, nsects = struct.unpack_from("<3I", self.data, off + 56)
                sects = []
                for i in range(nsects):
                    s = off + 72 + i * 80
                    sects.append((self.data[s:s + 16].rstrip(b"\0").decode(),
                                  struct.unpack_from("<2I", self.data, s + 56)))
                yield name, maxprot, initprot, sects

    def symbols(self):
        """(name, n_type, n_sect, n_value) for each symbol."""
        for cmd, off, _ in self.commands:
            if cmd != LC_SYMTAB:
                continue
            symoff, nsyms, stroff, _ = struct.unpack_from("<4I", self.data, off + 8)
            for i in range(nsyms):
                strx, ntype, nsect, _, value = struct.unpack_from("<IBBhQ", self.data, symoff + 16 * i)
                end = self.data.index(b"\0", stroff + strx)
                yield self.data[stroff + strx:end].decode(), ntype, nsect, value

    def relocations(self):
        """(kind, r_type, r_length, r_pcrel, r_extern, address, symbolnum)
        for each relocation."""
        for cmd, off, _ in self.commands:
            if cmd != LC_DYSYMTAB:
                continue
            extreloff, nextrel, locreloff, nlocrel = struct.unpack_from("<4I", self.data, off + 64)
            for kind, base, n in (("external", extreloff, nextrel), ("local", locreloff, nlocrel)):
                for i in range(n):
                    address, info = struct.unpack_from("<iI", self.data, base + 8 * i)
                    yield (kind, info >> 28, (info >> 25) & 3, (info >> 24) & 1, (info >> 27) & 1,
                           address, info & 0xFFFFFF)
        for _, _, _, sects in self.segments():
            for sect, (reloff, nreloc) in sects:
                if nreloc:
                    yield ("section " + sect, -1, 0, 0, 0, 0, 0)


def bundle_executable(bundle):
    """Info.plist dict and executable path of a kext bundle (flat or Contents/)."""
    for plist, exedir in (("Contents/Info.plist", "Contents/MacOS"), ("Info.plist", "")):
        p = os.path.join(bundle, plist)
        if os.path.exists(p):
            with open(p, "rb") as f:
                info = plistlib.load(f)
            exe = info.get("CFBundleExecutable")
            return info, os.path.join(bundle, exedir, exe) if exe else None
    return None, None


def library_kexts(dirs):
    """Bundle identifier -> (Info.plist, executable) of the kexts in dirs."""
    found = {}
    for d in dirs:
        for root, subdirs, _ in os.walk(d):
            for s in subdirs:
                if s.endswith(".kext"):
                    info, exe = bundle_executable(os.path.join(root, s))
                    if info and "CFBundleIdentifier" in info:
                        found.setdefault(info["CFBundleIdentifier"], (info, exe))
    return found


def main(argv):
    libdirs = []
    while len(argv) > 1 and argv[0] == "-L":
        libdirs.append(argv[1])
        argv = argv[2:]
    if len(argv) < 3:
        sys.exit(__doc__.strip())
    exe_path, plist_path, systems = argv[0], argv[1], argv[2:]
    errors = []
    m = MachO(exe_path)
    name = os.path.basename(exe_path)

    if m.cputype != CPU_TYPE_ARM64 or m.filetype != MH_KEXT_BUNDLE:
        errors.append("not an arm64 MH_KEXT_BUNDLE (link it with -Xlinker -kext)")
    for cmd, _, _ in m.commands:
        if cmd in DYLD_COMMANDS:
            errors.append(f"has {DYLD_COMMANDS[cmd]}, which kxld cannot read")
    relocs = list(m.relocations())
    for kind, rtype, rlength, pcrel, _, _, _ in relocs:
        if rtype != ARM64_RELOC_UNSIGNED or rlength != 3 or pcrel:
            errors.append(f"{kind} relocation of type {rtype} (length {rlength}, pcrel {pcrel}): "
                          "kxld links only 64-bit pointers in a linked kext")
            break
    for seg, maxprot, initprot, _ in m.segments():
        for prot in (maxprot, initprot):
            if prot & VM_PROT_WRITE and prot & VM_PROT_EXECUTE:
                errors.append(f"segment {seg} is writable and executable")
                break

    syms = list(m.symbols())
    defined = {n for n, t, _, _ in syms if not t & N_STAB and t & N_TYPE != N_UNDF}
    undefined = sorted({n for n, t, _, _ in syms if not t & N_STAB and t & N_TYPE == N_UNDF and t & N_EXT})

    # Undefined symbols named only by vtable slots (from each __ZTV symbol
    # to the next symbol in its section): inherited virtual methods.
    starts = sorted((v, nsect) for n, t, nsect, v in syms if not t & N_STAB and nsect)
    vtables = []
    for n, t, nsect, v in syms:
        if not t & N_STAB and nsect and n.startswith("__ZTV"):
            ends = [a for a, ns in starts if ns == nsect and a > v]
            vtables.append((v, min(ends) if ends else float("inf")))
    in_vtable, elsewhere = set(), set()
    for kind, _, _, _, rextern, address, symnum in relocs:
        if kind == "external" and rextern and symnum < len(syms):
            target = syms[symnum][0]
            if any(lo <= address < hi for lo, hi in vtables):
                in_vtable.add(target)
            else:
                elsewhere.add(target)
    inherited = {s for s in in_vtable - elsewhere if s.startswith("__ZN")}
    if "_kmod_info" not in defined:
        errors.append("defines no _kmod_info")

    with open(plist_path, "rb") as f:
        info = plistlib.load(f)
    if info.get("CFBundleExecutable") != name:
        errors.append(f"Info.plist's CFBundleExecutable is not {name}")
    libraries = info.get("OSBundleLibraries", {})
    if not libraries:
        errors.append("Info.plist has no OSBundleLibraries")
    others = library_kexts(libdirs)
    for system in systems:
        plugins = library_kexts([system])
        exported = set()
        for lib in libraries:
            if lib in plugins:
                lexe = plugins[lib][1]
            elif lib in others:
                lexe = others[lib][1]
            else:
                errors.append(f"library {lib} is in neither {system} nor the -L directories")
                continue
            if lexe:
                exported |= {n for n, t, _, _ in MachO(lexe).symbols() if not t & N_STAB}
        missing = [s for s in undefined if s not in exported and s not in inherited]
        for s in missing:
            errors.append(f"undefined symbol {s} is exported by none of its OSBundleLibraries ({system})")

    for e in errors:
        print(f"{name}: {e}", file=sys.stderr)
    if errors:
        sys.exit(1)
    print(f"{name}: {len(undefined) - len(inherited & set(undefined))} kernel symbols, all in "
          f"{', '.join(sorted(libraries))}"
          + (f", and {len(inherited & set(undefined))} inherited virtual methods for kxld to patch"
             if inherited & set(undefined) else "")
          + "; relocations and segments fit kxld")


if __name__ == "__main__":
    main(sys.argv[1:])
