#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
"""
iig - IOKit Interface Generator, reimplemented for building xnu on Linux.

xnu runs iig over iokit/DriverKit/*.iig to produce the DriverKit class
headers that the kernel's IOKit headers include and the *.iig.cpp files that
are compiled into the kernel. This tool produces that output for the kernel
side:

  * <Class>_Methods / <Class>_KernelMethods macros that
    OSDeclare*StructorsWithDispatch() splices into each class,
  * per-method message IDs, IORPC message/reply layouts, <Class>_<M>_Args,
  * full class definitions for NATIVE classes (and kernel proxies for
    dext classes),
  * an implementation file with RPC stubs, _Invoke unmarshalling,
    _Dispatch / MetaClass::Dispatch and OSAction factories.

Usage (as invoked by the xnu makefiles):
  iig --def X.iig --header X.h --impl X.iig.cpp [--framework-name DriverKit]
      [-- <clang arguments>]
"""

import argparse
import hashlib
import os
import re
import sys

if os.environ.get("PIE_CLANG_BINDINGS"):
    sys.path.insert(0, os.environ["PIE_CLANG_BINDINGS"])
import clang.cindex as ci

LIBCLANG_CANDIDATES = [
    os.environ.get("PIE_LIBCLANG", ""),
    "/usr/lib/llvm-21/lib/libclang.dylib",
    "/usr/lib/llvm-21/lib/libclang-21.so.1",
    "/usr/lib/llvm-21/lib/libclang.so",
]
CLANG_RESOURCE_INCLUDE = os.environ.get(
    "PIE_CLANG_RESOURCE_INCLUDE", "/usr/lib/llvm-21/lib/clang/21/include")

K = ci.CursorKind
T = ci.TypeKind

# Methods the kernel never implements even though the class is implemented by
# the kernel; the kernel only ever calls these into a dext.
NO_KERNEL_IMPL = {
    ("IOUserClient", "CopyClientMemoryForType"),
}

# Static methods the kernel defines itself (not via <Class>::<M>_Impl).
KERNEL_DEFINED_STATICS = {
    ("OSAction", "Create"),
    ("OSAction", "CreateWithTypeName"),
}

# Object lifecycle methods: never RPCs. Hand-written kernel classes declare
# them themselves; NATIVE class definitions declare init/free directly.
LIFECYCLE = {"init", "free", "retain", "release"}


def die(msg):
    sys.stderr.write("iig: error: %s\n" % msg)
    sys.exit(1)


def load_libclang():
    for lib in LIBCLANG_CANDIDATES:
        if lib and os.path.exists(lib):
            ci.Config.set_library_file(lib)
            return
    die("libclang not found (set PIE_LIBCLANG)")


def annotations(cursor):
    return [c.spelling for c in cursor.get_children() if c.kind == K.ANNOTATE_ATTR]


def id_for(owner, method):
    """Stable 64-bit message ID; bit 63 is reserved (kIORPCMessageIDKernel)."""
    h = hashlib.sha256(("pie-iig:%s::%s" % (owner, method)).encode()).digest()
    return int.from_bytes(h[:8], "big") & 0x7fffffffffffffff


def strip_const(spelling):
    s = spelling.strip()
    if s.startswith("const "):
        s = s[len("const "):]
    if s.endswith(" const"):
        s = s[: -len(" const")]
    return s.strip()


def declarator(t, name):
    """Render a declaration of `name` with clang type `t`."""
    if t.kind == T.CONSTANTARRAY:
        return declarator(t.element_type, "%s[%d]" % (name, t.element_count))
    if t.kind == T.INCOMPLETEARRAY:
        return declarator(t.element_type, "%s[]" % name)
    sp = t.spelling
    if sp.endswith("*") or sp.endswith("&"):
        return "%s%s" % (sp, name)
    return "%s %s" % (sp, name)


def array_info(t):
    """(element_type, count, is_const) if t is (a typedef of) a constant array."""
    c = t.get_canonical()
    if c.kind == T.CONSTANTARRAY:
        el = c.element_type
        # Canonicalization can drop the element's const; trust the spelling.
        is_const = "const" in t.spelling.split("[")[0].split() or el.is_const_qualified()
        return el, c.element_count, is_const
    return None


class Param:
    def __init__(self, cursor, model):
        self.name = cursor.spelling
        self.type = cursor.type
        self.ann = annotations(cursor)
        self.cursor = cursor
        self.kind = None          # see classify()
        self.field = None         # message field declaration
        self.serializable = False
        self.classname = None     # for object params
        self.count = 0
        self.classify(model)

    @property
    def target(self):
        return "target" in self.ann

    def decl(self):
        return declarator(self.type, self.name)

    def impl_decl(self):
        """Declaration used for _Impl, _Args and handlers (array params decay)."""
        if self.kind == "objarray":
            return "%s ** const %s" % (self.classname, self.name)
        return self.decl()

    def classify(self, model):
        t = self.type
        c = t.get_canonical()
        sp = t.spelling
        ai = array_info(t)

        if strip_const(sp) == "mach_port_t":
            self.kind = "port"
            return
        if c.kind == T.BLOCKPOINTER:
            # Only meaningful within one address space (kernel-to-kernel).
            self.kind = "in"
            self.field = "%s %s" % (strip_const(sp), self.name)
            return
        if ai is not None:
            el, count, is_const = ai
            if el.kind == T.POINTER and model.is_object(el.get_pointee()):
                self.kind = "objarray"
                self.count = count
                self.classname = model.object_name(el.get_pointee())
                self.serializable = model.is_serializable(self.classname)
                return
            self.kind = "inarray" if is_const else "outarray"
            self.count = count
            if t.kind == T.CONSTANTARRAY:
                self.field = declarator(t.element_type, "%s[%d]" % (self.name, count))
                self.field = re.sub(r"^const ", "", self.field)
            else:
                self.field = "%s %s" % (strip_const(sp), self.name)
            return
        if c.kind == T.POINTER:
            pt = c.get_pointee()
            if pt.kind == T.POINTER and model.is_object(pt.get_pointee()):
                self.kind = "outobj"
                self.classname = model.object_name(pt.get_pointee())
                self.serializable = model.is_serializable(self.classname)
                return
            if model.is_object(pt):
                self.kind = "inobj"
                self.classname = model.object_name(pt)
                self.serializable = model.is_serializable(self.classname)
                return
            if pt.kind in (T.VOID, T.FUNCTIONPROTO, T.FUNCTIONNOPROTO):
                self.kind = "local"
                return
            if pt.kind in (T.CHAR_S, T.CHAR_U, T.SCHAR, T.UCHAR):
                self.kind = "local"
                return
            pointee = t.get_pointee() if t.kind == T.POINTER else pt
            if pt.is_const_qualified():
                self.kind = "instruct"
            else:
                self.kind = "inout"
            self.field = "%s %s" % (strip_const(pointee.spelling), self.name)
            return
        if c.kind in (T.RECORD,):
            self.kind = "local"
            return
        self.kind = "in"
        self.field = "%s %s" % (strip_const(sp), self.name)


class Method:
    def __init__(self, cls, cursor, model):
        self.cls = cls
        self.cursor = cursor
        self.name = cursor.spelling
        self.ann = annotations(cursor)
        self.static = cursor.is_static_method()
        self.virtual = cursor.is_virtual_method()
        self.pure = cursor.is_pure_virtual_method()
        self.const = cursor.is_const_method()
        toks = [t.spelling for t in cursor.get_tokens()]
        self.override = "override" in toks
        self.ret = cursor.result_type.spelling
        self.params = [Param(p, model) for p in cursor.get_arguments()]
        self.owner = cls.name   # class whose ID this method uses
        self.type_ann = None
        for a in self.ann:
            if a.startswith("type="):
                self.type_ann = a[len("type="):]

    @property
    def localonly(self):
        return "localonly" in self.ann

    @property
    def invokereply(self):
        return "invokereply" in self.ann

    @property
    def oneway(self):
        return self.ret == "void"

    @property
    def has_result(self):
        return self.ret not in ("void", "kern_return_t", "IOReturn")

    @property
    def marshallable(self):
        return all(p.kind != "local" for p in self.params)

    @property
    def target_param(self):
        for p in self.params:
            if p.target:
                return p
        return None

    def is_rpc(self):
        return not self.localonly and self.marshallable

    def skipped_in_kernel(self):
        """Virtual LOCALONLY methods of hand-written kernel classes exist only in
        DriverKit; declaring them would leave undefined vtable entries."""
        return self.localonly and self.virtual and self.cls.mode == "handwritten"

    def has_call_variant(self):
        """Static LOCAL methods also get <M>_Call, the always-local entry point."""
        return self.static and "local" in self.ann and self.is_rpc() and self.kernel_impl()

    def is_reply_method(self):
        """REPLY: the kernel answers a pending INVOKEREPLY call with this message."""
        return "reply" in self.ann and self.is_rpc() and not self.static and self.owner == self.cls.name

    def kernel_impl(self):
        """Whether the kernel provides <Class>::<M>_Impl and dispatches it."""
        if not self.cls.kernel_implemented or self.localonly or self.pure:
            return False
        if (self.owner, self.name) in NO_KERNEL_IMPL or (self.cls.name, self.name) in NO_KERNEL_IMPL:
            return False
        if self.target_param and not ({"reply", "kernel"} & set(self.ann)):
            return False
        return True

    def param_list(self, defaults=False, with_super=False):
        # INVOKEREPLY only changes the _Impl side (it receives the IORPC).
        ps = [p.decl() for p in self.params]
        if with_super:
            ps.append("OSDispatchMethod supermethod = NULL" if defaults else "OSDispatchMethod supermethod")
        return ", ".join(ps)

    def impl_params(self):
        ps = [p.impl_decl() for p in self.params]
        if self.invokereply:
            ps = ["const IORPC rpc"] + ps
        return ", ".join(ps)

    def arg_names(self):
        return ", ".join(p.name for p in self.params)

    def impl_call_args(self):
        """Arguments forwarding a stub's parameters to <M>_Impl."""
        return ", ".join("(%s **) %s" % (p.classname, p.name) if p.kind == "objarray" else p.name
                         for p in self.params)

    @property
    def prefix(self):
        return "%s_%s" % (self.owner, self.name)


class Class:
    def __init__(self, cursor, model):
        self.cursor = cursor
        self.name = cursor.spelling
        self.ann = annotations(cursor)
        self.bases = [b for b in cursor.get_children() if b.kind == K.CXX_BASE_SPECIFIER]
        self.super = self.bases[0].type.spelling.replace("class ", "") if self.bases else None
        self.extends = None
        for a in self.ann:
            if a.startswith("extends="):
                self.extends = a[len("extends="):]
        self.methods = []
        self.model = model
        for m in cursor.get_children():
            if m.kind == K.CXX_METHOD:
                self.methods.append(Method(self, m, model))
        self.mode = None
        self.kernel_implemented = False

    @property
    def native(self):
        return "native" in self.ann

    @property
    def abstract(self):
        return False


def same_signature(a, b):
    if a.is_static_method() != b.is_static_method():
        return False
    pa = [p.type.get_canonical().spelling for p in a.get_arguments()]
    pb = [p.type.get_canonical().spelling for p in b.get_arguments()]
    return pa == pb


class Model:
    def __init__(self, tu, main_file, framework):
        self.tu = tu
        self.main = os.path.abspath(main_file)
        self.framework = framework
        self.serializable = set()
        self.class_decls = {}
        for c in tu.cursor.walk_preorder():
            if c.kind == K.CLASS_DECL:
                if "serializable" in annotations(c):
                    self.serializable.add(c.spelling)
                if c.is_definition():
                    self.class_decls[c.spelling] = c
        self.classes = []
        for c in tu.cursor.get_children():
            if c.kind in (K.CLASS_DECL, K.STRUCT_DECL) and c.is_definition() and self.in_main(c):
                if c.kind == K.CLASS_DECL and (self.derives_from_osobject(c) or "extends=" in "".join(annotations(c))):
                    self.classes.append(Class(c, self))
        by_name = {c.name: c for c in self.classes}
        # Merge EXTENDS classes into the class they extend.
        self.extenders = [c for c in self.classes if c.extends]
        for e in self.extenders:
            base = by_name.get(e.extends)
            if base is None:
                die("%s extends unknown class %s" % (e.name, e.extends))
            for m in e.methods:
                m.cls = base
                m.owner = base.name
                base.methods.append(m)
        self.classes = [c for c in self.classes if not c.extends]
        for c in self.classes:
            self.assign_mode(c)
            for m in c.methods:
                m.owner = self.id_owner(c, m)

    def in_main(self, c):
        f = c.location.file
        return f is not None and os.path.abspath(f.name) == self.main

    def derives_from_osobject(self, c):
        if c.spelling == "OSObject":
            return True
        for b in c.get_children():
            if b.kind == K.CXX_BASE_SPECIFIER:
                ref = b.referenced
                if ref is not None and self.derives_from_osobject(ref.get_definition() or ref):
                    return True
        return False

    def is_object(self, t):
        t = t.get_canonical()
        if t.kind != T.RECORD:
            return False
        d = t.get_declaration()
        return d.kind == K.CLASS_DECL

    def object_name(self, t):
        return t.get_canonical().get_declaration().spelling

    def is_serializable(self, name):
        return name in self.serializable

    def assign_mode(self, c):
        if c.name == "OSObject":
            c.mode = "handwritten"
        elif c.native:
            c.mode = "native"
        elif "kernel" in c.ann:
            # KERNEL classes are implemented by hand-written kernel classes,
            # in DriverKit and elsewhere (iokit/Tests/TestServices).
            c.mode = "handwritten"
        else:
            c.mode = "proxy"
        c.kernel_implemented = "kernel" in c.ann

    def id_owner(self, c, m):
        """The class that first declares method m (walking the base chain)."""
        owner = c.name
        decl = c.cursor
        while True:
            base = None
            for b in decl.get_children():
                if b.kind == K.CXX_BASE_SPECIFIER:
                    ref = b.referenced
                    base = ref.get_definition() if ref is not None else None
                    break
            if base is None:
                break
            for mm in base.get_children():
                if mm.kind == K.CXX_METHOD and mm.spelling == m.name and same_signature(mm, m.cursor):
                    owner = base.spelling
                    for a in annotations(mm):
                        if a in ("invokereply", "reply") and a not in m.ann:
                            m.ann.append(a)
            decl = base
        return owner


# ---------------------------------------------------------------------------
# Header generation


def msg_definitions(c, out):
    # Messages are defined once, by the class that first declares a method;
    # overrides reuse the declaring class's layout and ID.
    for m in c.methods:
        if m.name not in LIFECYCLE and m.is_rpc() and m.owner == c.name:
            emit_msg(m, out)


def obj_params(m, direction):
    kinds = ("inobj", "objarray") if direction == "in" else ("outobj",)
    return [p for p in m.params if p.kind in kinds]


def emit_msg(m, out):
    P = m.prefix
    w = out.append
    w("#define %s_ID 0x%016xULL" % (P, id_for(m.owner, m.name)))
    w("")
    ins = obj_params(m, "in")
    ports = [p for p in m.params if p.kind == "port"]
    nrefs = 1 + sum(p.count if p.kind == "objarray" else 1 for p in ins)
    w("#pragma pack(push, 4)")
    w("struct %s_Msg_Content {" % P)
    w("\tIORPCMessage __hdr;")
    w("\tOSObjectRef  __object;")
    for p in ins:
        if p.kind == "objarray":
            w("\tOSObjectRef  %s[%d];" % (p.name, p.count))
        else:
            w("\tOSObjectRef  %s;" % p.name)
    for p in m.params:
        if p.kind in ("in", "inarray", "instruct", "inout"):
            w("\t%s;" % p.field)
    w("};")
    w("#pragma pack(pop)")
    w("#pragma pack(4)")
    w("struct %s_Msg {" % P)
    w("\tIORPCMessageMach           mach;")
    w("\tmach_msg_port_descriptor_t __object__descriptor;")
    for p in ins:
        if p.kind == "objarray":
            w("\tmach_msg_port_descriptor_t %s__descriptor[%d];" % (p.name, p.count))
        else:
            w("\tmach_msg_port_descriptor_t %s__descriptor;" % p.name)
    for p in ports:
        w("\tmach_msg_port_descriptor_t %s__descriptor;" % p.name)
    w("\t%s_Msg_Content content;" % P)
    w("};")
    w("#pragma pack()")
    w("#define %s_Msg_ObjRefs (%d)" % (P, nrefs))
    w("#define %s_Msg_Ports (%d)" % (P, len(ports)))
    w("")
    outs = obj_params(m, "out")
    w("#pragma pack(push, 4)")
    w("struct %s_Rpl_Content {" % P)
    w("\tIORPCMessage __hdr;")
    for p in outs:
        w("\tOSObjectRef  %s;" % p.name)
    if m.has_result:
        w("\t%s __result;" % strip_const(m.ret))
    for p in m.params:
        if p.kind in ("inout", "outarray"):
            w("\t%s;" % p.field)
    w("};")
    w("#pragma pack(pop)")
    w("#pragma pack(4)")
    w("struct %s_Rpl {" % P)
    w("\tIORPCMessageMach           mach;")
    for p in outs:
        w("\tmach_msg_port_descriptor_t %s__descriptor;" % p.name)
    w("\t%s_Rpl_Content content;" % P)
    w("};")
    w("#pragma pack()")
    w("#define %s_Rpl_ObjRefs (%d)" % (P, len(outs)))
    w("")


def method_decl(m, c, for_handwritten):
    """Declaration(s) of method m inside class c."""
    lines = []
    qual = " const" if m.const else ""
    ov = " APPLE_KEXT_OVERRIDE" if m.override else ""
    # In the kernel every RPC gets a stub (dext-implemented callbacks are
    # sent to the dext), so nothing stays pure and no class is abstract.
    pure = ""
    virt = "virtual " if m.virtual else ""
    stat = "static " if m.static else ""
    if m.localonly or not m.is_rpc():
        lines.append("%s%s%s %s(%s)%s%s%s;" % (stat, virt, m.ret, m.name, m.param_list(), qual, ov, pure))
    else:
        if m.static:
            lines.append("static %s %s(%s);" % (m.ret, m.name, m.param_list()))
        else:
            lines.append("%s%s %s(%s)%s%s%s;" % (virt, m.ret, m.name,
                                                 m.param_list(defaults=True, with_super=True), qual, ov, pure))
    return lines


def class_macros(c, out):
    """<Class>_Methods / <Class>_KernelMethods and <Class>_<M>_Args."""
    w = out.append
    for m in c.methods:
        if m.name not in LIFECYCLE and (m.is_rpc() or m.kernel_impl()):
            args = m.impl_params()
            w("#define %s_%s_Args \\" % (c.name, m.name))
            w("\t\t%s" % (args if args else ""))
            w("")

    methods = []
    methods.append("public:")
    methods.append("virtual kern_return_t Dispatch(const IORPC rpc) APPLE_KEXT_OVERRIDE;")
    methods.append("static kern_return_t _Dispatch(%s * self, const IORPC rpc);" % c.name)
    for m in c.methods:
        if m.name in LIFECYCLE or m.skipped_in_kernel():
            continue
        if c.mode == "proxy" and m.override and not m.static:
            continue
        methods.extend(method_decl(m, c, c.mode == "handwritten"))
        if m.is_rpc() and m.owner == c.name or m.is_rpc() and m.kernel_impl():
            hargs = ["OSMetaClassBase * target"] if not m.static else []
            if m.invokereply:
                hargs.append("const IORPC rpc")
            hargs += [p.impl_decl() for p in m.params]
            methods.append("typedef %s (*%s_Handler)(%s);" % (m.ret, m.name, ", ".join(hargs) or "void"))
            if m.static:
                methods.append("static kern_return_t %s_Invoke(const IORPC rpc, %s_Handler func);" % (m.name, m.name))
            else:
                methods.append("static kern_return_t %s_Invoke(const IORPC rpc, OSMetaClassBase * target, %s_Handler func);"
                               % (m.name, m.name))
                if m.target_param:
                    methods.append("static kern_return_t %s_Invoke(const IORPC rpc, OSMetaClassBase * target, "
                                   "%s_Handler func, const OSMetaClass * targetActionClass);" % (m.name, m.name))
        if m.has_call_variant():
            methods.append("static kern_return_t %s_Call(%s);" % (m.name, m.param_list()))
        if m.type_ann and not m.static:
            methods.append("kern_return_t CreateAction%s(size_t referenceSize, OSAction ** action);" % m.name)
        if m.is_reply_method():
            methods.append("kern_return_t %s(const IORPC rpc, %s);" % (m.name, m.param_list()))
    methods.append("protected:")

    kmethods = ["public:"]
    for m in c.methods:
        if m.name in LIFECYCLE:
            continue
        if m.kernel_impl() and (m.is_rpc() or not m.marshallable):
            stat = "static " if m.static else ""
            qual = " const" if m.const else ""
            kmethods.append("%s%s %s_Impl(%s)%s;" % (stat, m.ret, m.name, m.impl_params(), qual))
    kmethods.append("protected:")

    w("#define %s_Methods \\" % c.name)
    w(" \\\n".join("\t" + l for l in methods))
    w("")
    w("#define %s_KernelMethods \\" % c.name)
    w(" \\\n".join("\t" + l for l in kmethods))
    w("")


def class_definition(c, out):
    """Full definition for NATIVE classes and dext proxies."""
    w = out.append
    w("struct %s_IVars;" % c.name)
    w("")
    w("class %s : public %s" % (c.name, c.super))
    w("{")
    if c.abstract:
        w("\tOSDeclareAbstractStructorsWithDispatch(%s);" % c.name)
    else:
        w("\tOSDeclareDefaultStructorsWithDispatch(%s);" % c.name)
    w("")
    w("public:")
    w("\ttypedef %s super;" % c.super)
    w("#pragma clang diagnostic push")
    w("#pragma clang diagnostic ignored \"-Wshadow-field\"")
    w("\tstruct %s_IVars * ivars;" % c.name)
    w("#pragma clang diagnostic pop")
    if c.mode == "native":
        for m in c.methods:
            if m.name in LIFECYCLE:
                ov = " APPLE_KEXT_OVERRIDE" if m.override else ""
                w("\t%s%s %s(%s)%s;" % ("virtual " if m.virtual else "", m.ret, m.name, m.param_list(), ov))
    w("};")
    w("")


def rewrite_text(text):
    # <Framework/X.iig> -> <Framework/X.h>, "X.iig" -> "X.h"
    return re.sub(r'(#\s*include\s*[<"][^>"]*?)\.iig([>"])', r"\1.h\2", text)


def generate_header(model, src, base):
    pieces = []
    pos = 0
    guard_open = "\n#if !__IIG && KERNEL\n"
    replacements = []
    for c in model.classes:
        replacements.append((c, c.cursor.extent.start.offset, c.cursor.extent.end.offset))
    for e in model.extenders:
        replacements.append((e, e.cursor.extent.start.offset, e.cursor.extent.end.offset))
    replacements.sort(key=lambda r: r[1])

    out = ["/* Generated by iig from %s.iig -- do not edit. */" % base, "",
           "#pragma clang diagnostic push",
           "#pragma clang diagnostic ignored \"-Wdocumentation\"", ""]
    for c, start, end in replacements:
        pieces.append(rewrite_text(src[pos:start]))
        # swallow the ';' closing the class declaration
        m = re.match(r"\s*;", src[end:])
        if m:
            end += m.end()
        body = []
        if c in model.extenders:
            body.append("/* %s: methods merged into %s */" % (c.name, c.extends))
        else:
            body.append("class %s;" % c.name)
            body.append("")
            body.append("#if !__DOCUMENTATION__")
            body.append("")
            msg_definitions(c, body)
            class_macros(c, body)
            if c.mode in ("native", "proxy"):
                class_definition(c, body)
            body.append("#endif /* !__DOCUMENTATION__ */")
        pieces.append("\n".join(body) + "\n")
        pos = end
    pieces.append(rewrite_text(src[pos:]))
    out.append("".join(pieces))
    out.append("#pragma clang diagnostic pop")
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Implementation generation


def desc_type(p):
    return "MACH_MSG_OOL_DESCRIPTOR" if p.serializable else "MACH_MSG_PORT_DESCRIPTOR"


def gen_stub(c, m, w):
    P = m.prefix
    owner_cls = c.name
    if m.static:
        # In the kernel a static RPC runs locally; dexts reach *_Impl through
        # MetaClass::Dispatch instead.
        if m.kernel_impl() and (c.name, m.name) not in KERNEL_DEFINED_STATICS:
            w(m.ret)
            w("%s::%s(%s)" % (owner_cls, m.name, m.param_list()))
            w("{")
            w("\treturn %s_Impl(%s);" % (m.name, m.impl_call_args()))
            w("}")
            w("")
        if m.has_call_variant():
            w("kern_return_t")
            w("%s::%s_Call(%s)" % (owner_cls, m.name, m.param_list()))
            w("{")
            w("\treturn %s_Impl(%s);" % (m.name, m.impl_call_args()))
            w("}")
            w("")
        return
    tp = m.target_param
    params = m.param_list(with_super=True)
    qual = " const" if m.const else ""
    w("%s" % m.ret)
    w("%s::%s(%s)%s" % (owner_cls, m.name, params, qual))
    w("{")
    w("\tkern_return_t ret;")
    w("\tunion {")
    w("\t\t%s_Msg msg;" % P)
    w("\t\tstruct {")
    w("\t\t\t%s_Rpl rpl;" % P)
    w("\t\t\tmach_msg_max_trailer_t trailer;")
    w("\t\t} rpl;")
    w("\t} buf;")
    w("\tstruct %s_Msg * msg = &buf.msg;" % P)
    if not m.oneway:
        w("\tstruct %s_Rpl * rpl = &buf.rpl.rpl;" % P)
    w("")
    w("\tmemset(msg, 0, sizeof(struct %s_Msg));" % P)
    w("\tmsg->mach.msgh.msgh_size = sizeof(*msg);")
    w("\tmsg->content.__hdr.flags = %s;" % ("kIORPCMessageOneway" if m.oneway else "0"))
    w("\tmsg->content.__hdr.msgid = %s_ID;" % P)
    target_expr = tp.name if tp else "this"
    w("\tmsg->content.__object = (OSObjectRef) %s;" % (target_expr if tp else "(const OSObject *) this"))
    w("\tmsg->content.__hdr.objectRefs = %s_Msg_ObjRefs;" % P)
    w("\tmsg->mach.msgh_body.msgh_descriptor_count = %s_Msg_ObjRefs + %s_Msg_Ports;" % (P, P))
    w("\tmsg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;")
    for p in m.params:
        if p.kind == "inobj":
            w("\tmsg->%s__descriptor.type = %s;" % (p.name, desc_type(p)))
            w("\tmsg->content.%s = (OSObjectRef) %s;" % (p.name, p.name))
        elif p.kind == "objarray":
            w("\tfor (unsigned int i = 0; i < %d; i++) {" % p.count)
            w("\t\tmsg->%s__descriptor[i].type = %s;" % (p.name, desc_type(p)))
            w("\t\tmsg->content.%s[i] = (OSObjectRef) %s[i];" % (p.name, p.name))
            w("\t}")
        elif p.kind == "port":
            disp = "MACH_MSG_TYPE_COPY_SEND"
            if "MACH_MSG_TYPE_MAKE_SEND" in p.ann:
                disp = "MACH_MSG_TYPE_MAKE_SEND"
            w("\tmsg->%s__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;" % p.name)
            w("\tmsg->%s__descriptor.disposition = %s;" % (p.name, disp))
            w("\tmsg->%s__descriptor.name = %s;" % (p.name, p.name))
        elif p.kind == "in":
            w("\tmsg->content.%s = %s;" % (p.name, p.name))
        elif p.kind == "inarray":
            el = array_info(p.type)[0]
            if el.get_canonical().kind in (T.CHAR_S, T.CHAR_U, T.SCHAR, T.UCHAR):
                w("\tstrlcpy((char *) &msg->content.%s[0], (const char *) %s, sizeof(msg->content.%s));"
                  % (p.name, p.name, p.name))
            else:
                w("\tmemcpy(&msg->content.%s[0], &%s[0], sizeof(msg->content.%s));" % (p.name, p.name, p.name))
        elif p.kind in ("instruct", "inout"):
            w("\tif (%s) {" % p.name)
            w("\t\tmsg->content.%s = *%s;" % (p.name, p.name))
            w("\t}")
    w("")
    w("\tIORPC rpc = { .message = &buf.msg.mach, .reply = &buf.rpl.rpl.mach, "
      ".sendSize = sizeof(buf.msg), .replySize = sizeof(buf.rpl), .kernelContent = &buf.msg.content.__hdr };")
    inv = "%s->Invoke(rpc)" % tp.name if tp else "((OSMetaClassBase *) this)->Invoke(rpc)"
    w("\tif (supermethod) {")
    w("\t\tret = supermethod((OSMetaClassBase *) %s, rpc);" % ("this" if not tp else tp.name))
    w("\t} else {")
    w("\t\tret = %s;" % inv)
    w("\t}")
    if m.oneway:
        w("\t(void) ret;")
        w("}")
        w("")
        return
    w("")
    w("\tif (kIOReturnSuccess == ret) {")
    w("\t\tdo {")
    w("\t\t\tif (rpl->mach.msgh.msgh_size != sizeof(*rpl)) {")
    w("\t\t\t\tret = kIOReturnIPCError;")
    w("\t\t\t\tbreak;")
    w("\t\t\t}")
    w("\t\t\tif (rpl->content.__hdr.msgid != %s_ID) {" % P)
    w("\t\t\t\tret = kIOReturnIPCError;")
    w("\t\t\t\tbreak;")
    w("\t\t\t}")
    w("\t\t\tif (rpl->content.__hdr.objectRefs != %s_Rpl_ObjRefs) {" % P)
    w("\t\t\t\tret = kIOReturnIPCError;")
    w("\t\t\t\tbreak;")
    w("\t\t\t}")
    for p in m.params:
        if p.kind == "outobj":
            pointee = strip_const(p.type.get_pointee().spelling)
            w("\t\t\tif (%s) {" % p.name)
            w("\t\t\t\t*%s = (%s) rpl->content.%s;" % (p.name, pointee, p.name))
            w("\t\t\t}")
        elif p.kind == "inout":
            w("\t\t\tif (%s) {" % p.name)
            w("\t\t\t\t*%s = rpl->content.%s;" % (p.name, p.name))
            w("\t\t\t}")
        elif p.kind == "outarray":
            w("\t\t\tmemcpy(&%s[0], &rpl->content.%s[0], sizeof(rpl->content.%s));" % (p.name, p.name, p.name))
    w("\t\t} while (false);")
    w("\t}")
    if m.has_result:
        w("\tif (kIOReturnSuccess != ret) {")
        w("\t\treturn (%s) 0;" % m.ret)
        w("\t}")
        w("\treturn rpl->content.__result;")
    else:
        w("\treturn ret;")
    w("}")
    w("")


def gen_invoke(c, m, w):
    P = m.prefix
    if m.static:
        w("kern_return_t")
        w("%s::%s_Invoke(const IORPC _rpc, %s_Handler func)" % (c.name, m.name, m.name))
    else:
        w("kern_return_t")
        w("%s::%s_Invoke(const IORPC _rpc, OSMetaClassBase * target, %s_Handler func)" % (c.name, m.name, m.name))
    w("{")
    w("\tconst %s_Msg_Content * content = (const %s_Msg_Content *) _rpc.kernelContent;" % (P, P))
    w("")
    w("\tif (!content || content->__hdr.objectRefs != %s_Msg_ObjRefs) {" % P)
    w("\t\treturn kIOReturnIPCError;")
    w("\t}")
    w("\tif (_rpc.sendSize < sizeof(%s_Msg)) {" % P)
    w("\t\treturn kIOReturnIPCError;")
    w("\t}")
    call = []
    port_index = 0
    for p in m.params:
        if p.kind == "inobj":
            w("\t%s * %s = OSDynamicCast(%s, (OSObject *) content->%s);" % (p.classname, p.name, p.classname, p.name))
            w("\tif (!%s && content->%s) {" % (p.name, p.name))
            w("\t\treturn kIOReturnBadArgument;")
            w("\t}")
            call.append("(%s) %s" % (strip_const(p.type.spelling), p.name))
        elif p.kind == "objarray":
            el = array_info(p.type)[0]
            w("\t%s * %s_array[%d];" % (p.classname, p.name, p.count))
            w("\tfor (unsigned int i = 0; i < %d; i++) {" % p.count)
            w("\t\t%s_array[i] = OSDynamicCast(%s, (OSObject *) content->%s[i]);" % (p.name, p.classname, p.name))
            w("\t\tif (!%s_array[i] && content->%s[i]) {" % (p.name, p.name))
            w("\t\t\treturn kIOReturnBadArgument;")
            w("\t\t}")
            w("\t}")
            call.append("%s_array" % p.name)
        elif p.kind == "outobj":
            w("\t%s %s = NULL;" % (strip_const(p.type.get_pointee().spelling), p.name))
            call.append("&%s" % p.name)
        elif p.kind == "port":
            w("\tmach_port_t %s = (mach_port_t) _rpc.message->objects[%s_Msg_ObjRefs + %d].name;"
              % (p.name, P, port_index))
            port_index += 1
            call.append(p.name)
        elif p.kind == "in":
            call.append("content->%s" % p.name)
        elif p.kind == "inarray":
            call.append("content->%s" % p.name)
        elif p.kind == "instruct":
            call.append("&content->%s" % p.name)
        elif p.kind == "inout":
            w("\t%s = content->%s;" % (p.field, p.name))
            call.append("&%s" % p.name)
        elif p.kind == "outarray":
            w("\t%s;" % p.field)
            w("\tmemset(&%s, 0, sizeof(%s));" % (p.name, p.name))
            call.append(p.name)
    args = []
    if not m.static:
        args.append("target")
    if m.invokereply:
        args.append("_rpc")
    args += call
    w("")
    if m.ret == "void":
        w("\tfunc(%s);" % ", ".join(args))
        w("\treturn kIOReturnSuccess;")
        w("}")
        w("")
        return
    if m.has_result:
        w("\t%s __result = func(%s);" % (strip_const(m.ret), ", ".join(args)))
    else:
        w("\tkern_return_t ret = func(%s);" % ", ".join(args))
        w("\tif (kIOReturnSuccess != ret) {")
        w("\t\treturn ret;")
        w("\t}")
    w("")
    w("\tif (_rpc.replySize < sizeof(%s_Rpl)) {" % P)
    for p in m.params:
        if p.kind == "outobj":
            w("\t\tOSSafeReleaseNULL(%s);" % p.name)
    w("\t\treturn kIOReturnIPCError;")
    w("\t}")
    w("\t%s_Rpl * rpl = (%s_Rpl *) _rpc.reply;" % (P, P))
    w("\trpl->mach.msgh.msgh_id = kIORPCVersionCurrentReply;")
    w("\trpl->mach.msgh.msgh_size = sizeof(*rpl);")
    w("\trpl->mach.msgh_body.msgh_descriptor_count = %s_Rpl_ObjRefs;" % P)
    w("\trpl->content.__hdr.msgid = %s_ID;" % P)
    w("\trpl->content.__hdr.flags = 0;")
    w("\trpl->content.__hdr.objectRefs = %s_Rpl_ObjRefs;" % P)
    for p in m.params:
        if p.kind == "outobj":
            w("\trpl->%s__descriptor.type = %s;" % (p.name, desc_type(p)))
            w("\trpl->content.%s = (OSObjectRef) %s;" % (p.name, p.name))
        elif p.kind == "inout":
            w("\trpl->content.%s = %s;" % (p.name, p.name))
        elif p.kind == "outarray":
            w("\tmemcpy(&rpl->content.%s, &%s, sizeof(rpl->content.%s));" % (p.name, p.name, p.name))
    if m.has_result:
        w("\trpl->content.__result = __result;")
    w("\treturn kIOReturnSuccess;")
    w("}")
    w("")


def gen_reply_overload(c, m, w):
    """<M>(rpc, args...): write the <M> message into rpc.reply, answering a
    pending INVOKEREPLY call (e.g. CheckForWork -> InterruptOccurred)."""
    P = m.prefix
    tp = m.target_param
    w("kern_return_t")
    w("%s::%s(const IORPC rpc, %s)" % (c.name, m.name, m.param_list()))
    w("{")
    w("\tstruct %s_Msg * msg = (struct %s_Msg *) rpc.reply;" % (P, P))
    w("")
    w("\tif (rpc.replySize < sizeof(*msg)) {")
    w("\t\treturn kIOReturnIPCError;")
    w("\t}")
    w("\tmemset(msg, 0, sizeof(*msg));")
    w("\tmsg->mach.msgh.msgh_id = kIORPCVersionCurrentReply;")
    w("\tmsg->mach.msgh.msgh_size = sizeof(*msg);")
    w("\tmsg->mach.msgh_body.msgh_descriptor_count = %s_Msg_ObjRefs + %s_Msg_Ports;" % (P, P))
    w("\tmsg->content.__hdr.flags = kIORPCMessageOneway;")
    w("\tmsg->content.__hdr.msgid = %s_ID;" % P)
    w("\tmsg->content.__hdr.objectRefs = %s_Msg_ObjRefs;" % P)
    w("\tmsg->content.__object = (OSObjectRef) %s;" % (tp.name if tp else "(const OSObject *) this"))
    w("\tmsg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;")
    for p in m.params:
        if p.kind == "inobj":
            w("\tmsg->%s__descriptor.type = %s;" % (p.name, desc_type(p)))
            w("\tmsg->content.%s = (OSObjectRef) %s;" % (p.name, p.name))
        elif p.kind == "in":
            w("\tmsg->content.%s = %s;" % (p.name, p.name))
        elif p.kind in ("instruct", "inout"):
            w("\tif (%s) {" % p.name)
            w("\t\tmsg->content.%s = *%s;" % (p.name, p.name))
            w("\t}")
        elif p.kind == "inarray":
            w("\tmemcpy(&msg->content.%s[0], &%s[0], sizeof(msg->content.%s));" % (p.name, p.name, p.name))
        else:
            die("%s::%s: REPLY parameter %s not supported" % (c.name, m.name, p.name))
    w("\treturn kIOReturnSuccess;")
    w("}")
    w("")


def gen_trampoline(c, m, w):
    """Plain function adapting a <Class>_<M>_Handler to the _Impl member."""
    if m.static:
        return
    ps = ["OSMetaClassBase * target"]
    if m.invokereply:
        ps.append("const IORPC rpc")
    ps += [p.impl_decl() for p in m.params]
    args = (["rpc"] if m.invokereply else []) + [p.name for p in m.params]
    w("static %s" % m.ret)
    w("%s_%s_Trampoline(%s)" % (c.name, m.name, ", ".join(ps)))
    w("{")
    call = "((%s *) target)->%s_Impl(%s)" % (c.name, m.name, ", ".join(args))
    w("\t%s%s;" % ("" if m.ret == "void" else "return ", call))
    w("}")
    w("")


def generate_impl(model, src, base, framework):
    out = []
    w = out.append
    w("/* Generated by iig from %s.iig -- do not edit. */" % base)
    w("")
    w("#include <libkern/c++/OSContainers.h>")
    w("#include <IOKit/IOLib.h>")
    if framework:
        w("#include <%s/%s.h>" % (framework, base))
        if any(m.type_ann for c in model.classes for m in c.methods):
            w("#include <%s/OSAction.h>" % framework)
    else:
        w('#include "%s.h"' % base)
    # @iig implementation ... @iig end blocks
    for mm in re.finditer(r"@iig implementation\n(.*?)@iig end", src, re.S):
        w(mm.group(1).rstrip())
    w("")
    w("#if KERNEL")
    w("")
    for c in model.classes:
        if c.mode in ("native", "proxy"):
            if c.abstract:
                w("OSDefineMetaClassAndAbstractStructors(%s, %s);" % (c.name, c.super))
            else:
                w("OSDefineMetaClassAndStructors(%s, %s);" % (c.name, c.super))
            w("")
        # Dispatch
        w("kern_return_t")
        w("%s::Dispatch(const IORPC rpc)" % c.name)
        w("{")
        w("\treturn _Dispatch(this, rpc);")
        w("}")
        w("")
        dispatched = [m for m in c.methods
                      if m.name not in LIFECYCLE and not m.static and m.is_rpc() and m.kernel_impl()]
        for m in dispatched:
            gen_trampoline(c, m, w)
        w("kern_return_t")
        w("%s::_Dispatch(%s * self, const IORPC rpc)" % (c.name, c.name))
        w("{")
        w("\tkern_return_t ret = kIOReturnUnsupported;")
        w("\tIORPCMessage * msg = rpc.kernelContent;")
        w("")
        w("\tif (!msg) {")
        w("\t\treturn kIOReturnIPCError;")
        w("\t}")
        w("\tswitch (msg->msgid) {")
        for m in dispatched:
            w("\tcase %s_ID:" % m.prefix)
            w("\t\tret = %s::%s_Invoke(rpc, self, &%s_%s_Trampoline);" % (c.name, m.name, c.name, m.name))
            w("\t\tbreak;")
        w("\tdefault:")
        if c.super and c.name != "OSObject":
            w("\t\tret = %s::_Dispatch(self, rpc);" % c.super)
        else:
            w("\t\tret = self->OSMetaClassBase::Dispatch(rpc);")
        w("\t\tbreak;")
        w("\t}")
        w("\treturn ret;")
        w("}")
        w("")
        # MetaClass::Dispatch (static methods)
        sdispatched = [m for m in c.methods
                       if m.name not in LIFECYCLE and m.static and m.is_rpc() and m.kernel_impl()]
        w("kern_return_t")
        w("%s::MetaClass::Dispatch(const IORPC rpc)" % c.name)
        w("{")
        w("\tkern_return_t ret = kIOReturnUnsupported;")
        w("\tIORPCMessage * msg = rpc.kernelContent;")
        w("")
        w("\tif (!msg) {")
        w("\t\treturn kIOReturnIPCError;")
        w("\t}")
        w("\tswitch (msg->msgid) {")
        for m in sdispatched:
            w("\tcase %s_ID:" % m.prefix)
            w("\t\tret = %s::%s_Invoke(rpc, &%s::%s_Impl);" % (c.name, m.name, c.name, m.name))
            w("\t\tbreak;")
        w("\tdefault:")
        w("\t\tret = OSMetaClassBase::Dispatch(rpc);")
        w("\t\tbreak;")
        w("\t}")
        w("\treturn ret;")
        w("}")
        w("")
        # stubs, invokes, action factories
        for m in c.methods:
            if m.name in LIFECYCLE:
                continue
            if c.mode == "proxy" and m.override and not m.static:
                continue
            if m.is_rpc():
                gen_stub(c, m, w)
                if m.owner == c.name or m.kernel_impl():
                    gen_invoke(c, m, w)
                    if m.target_param and not m.static:
                        w("kern_return_t")
                        w("%s::%s_Invoke(const IORPC rpc, OSMetaClassBase * target, %s_Handler func, "
                          "const OSMetaClass * targetActionClass)" % (c.name, m.name, m.name))
                        w("{")
                        w("\t(void) targetActionClass;")
                        w("\treturn %s_Invoke(rpc, target, func);" % m.name)
                        w("}")
                        w("")
                if m.is_reply_method():
                    gen_reply_overload(c, m, w)
            elif not m.localonly and not m.marshallable and m.kernel_impl() and not m.static and not m.pure:
                # Not marshallable (blocks): only callable within the kernel.
                qual = " const" if m.const else ""
                w(m.ret)
                w("%s::%s(%s)%s" % (c.name, m.name, m.param_list(), qual))
                w("{")
                w("\t%s%s_Impl(%s);" % ("" if m.ret == "void" else "return ", m.name, m.arg_names()))
                w("}")
                w("")
            if m.type_ann and not m.static:
                tcls, _, tmeth = m.type_ann.partition("::")
                aname = "OSAction_%s_%s" % (c.name, m.name)
                w("class %s : public OSAction" % aname)
                w("{")
                w("\tOSDeclareDefaultStructors(%s);" % aname)
                w("};")
                w("OSDefineMetaClassAndStructors(%s, OSAction);" % aname)
                w("")
                w("kern_return_t")
                w("%s::CreateAction%s(size_t referenceSize, OSAction ** action)" % (c.name, m.name))
                w("{")
                w("\tkern_return_t ret;")
                w("\tOSString * typeName = OSString::withCString(\"%s\");" % m.type_ann)
                w("\tret = OSAction::CreateWithTypeName(this, %s_ID, %s_%s_ID, referenceSize, typeName, action);"
                  % (m.prefix, tcls, tmeth))
                w("\tOSSafeReleaseNULL(typeName);")
                w("\treturn ret;")
                w("}")
                w("")
    w("#endif /* KERNEL */")
    w("")
    return "\n".join(out)


# ---------------------------------------------------------------------------


def main():
    argv = sys.argv[1:]
    clang_args = []
    if "--" in argv:
        i = argv.index("--")
        argv, clang_args = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(prog="iig")
    ap.add_argument("--def", dest="defn", required=True)
    ap.add_argument("--header", required=True)
    ap.add_argument("--impl", required=True)
    ap.add_argument("--framework-name", default=None)
    ap.add_argument("--deployment-target", default=None)
    ap.add_argument("--log", default=None)
    args, unknown = ap.parse_known_args(argv)

    load_libclang()
    src = open(args.defn, encoding="utf-8").read()
    base = os.path.basename(args.defn)
    if base.endswith(".iig"):
        base = base[:-4]

    parse_args = ["--target=arm64-apple-macos", "-fblocks", "-isystem", CLANG_RESOURCE_INCLUDE,
                  "-Wno-everything"] + clang_args
    if "-x" not in clang_args:
        parse_args = ["-x", "c++", "-std=gnu++2b", "-D__IIG=1"] + parse_args
    idx = ci.Index.create()
    tu = idx.parse(args.defn, args=parse_args,
                   options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    for d in errors:
        sys.stderr.write("iig: %s\n" % d)
    if errors and os.environ.get("PIE_IIG_STRICT", "1") == "1":
        die("%d error(s) parsing %s" % (len(errors), args.defn))

    model = Model(tu, args.defn, args.framework_name)
    header = generate_header(model, src, base)
    impl = generate_impl(model, src, base, args.framework_name)
    with open(args.header, "w") as f:
        f.write(header)
    with open(args.impl, "w") as f:
        f.write(impl)


if __name__ == "__main__":
    main()
