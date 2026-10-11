#!/usr/bin/env python3
"""LP64 audit: the work list for converting the port to 64-bit.

Reads the preprocessed sources of the build (pc/build/boot64/*.i, written by
`build_boot.py --arch 64`, so the include paths and defines are the build's)
plus the port runtime's C files, and uses clang to:

- dump every record layout twice: as the PS1 sees it (mipsel, ILP32; the
  32-bit x86 build for host-only runtime files) and under LP64 (x86_64);
- walk the x86_64 JSON AST for where each changed struct is instantiated
  (static/global, stack, heap, scratchpad and fixed guest addresses,
  reinterpreted byte buffers, image data imports), literal sizes passed to
  clears/copies/allocations, byte-offset pointer arithmetic with constants,
  and casts between pointers and 32-bit integers;
- scan the text for packed OT links and other 24/32-bit address idioms;
- type the data imports (pc/build/boot/imports.ld) and the generated
  function tables.

Writes pc/build/lp64_audit/audit.json (machine-readable, regenerable) and
pc/docs/LP64_Audit.md (summary, classification and the parallel work split).
Read-only with respect to the build: needs only clang.
"""

import argparse
import bisect
import collections
import json
import multiprocessing
import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lp64_report import group_of  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

CLANG_COMMON = [
    "-fsyntax-only",
    "-w",
    "-ferror-limit=0",
    "-funsigned-char",
    "-fgnu89-inline",
    "-fno-builtin",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=int-conversion",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
    "-Wno-error=incompatible-function-pointer-types",
]
TARGET_PS1 = "mipsel-unknown-linux-gnu"
TARGET_I386 = "i386-unknown-linux-gnu"
TARGET_LP64 = "x86_64-unknown-linux-gnu"

# Calls with a destination pointer and a byte count: name -> (dst, src, size).
SIZE_CALLS = {
    "vs_main_bzero": (0, None, 1),
    "bzero": (0, None, 1),
    "vs_main_memcpy": (0, 1, 2),
    "vs_battle_memcpy": (0, 1, 2),
    "vs_battle_rMemcpy": (0, 1, 2),
    "vs_battle_copyAligned": (0, 1, 2),
    "memcpy": (0, 1, 2),
    "memmove": (0, 1, 2),
    "memset": (0, None, 2),
    "__builtin_memcpy": (0, 1, 2),
    "__builtin_memset": (0, None, 2),
}
ALLOC_CALLS = {"vs_main_allocHeap": 0, "vs_main_allocHeapR": 0, "malloc": 0, "calloc": 1, "realloc": 1}

SCALAR_SIZES = {
    "char": 1, "signed char": 1, "unsigned char": 1, "_Bool": 1,
    "short": 2, "unsigned short": 2, "short int": 2, "unsigned short int": 2,
    "int": 4, "unsigned int": 4, "float": 4,
    "long": 8, "unsigned long": 8, "long int": 8, "unsigned long int": 8,
    "long long": 8, "unsigned long long": 8, "long long int": 8, "double": 8,
    "void": 1,
}
# The same scalars as the PS1 sees them (long and pointers are 4 bytes).
SCALAR_SIZES_PS1 = {**SCALAR_SIZES, "long": 4, "unsigned long": 4, "long int": 4, "unsigned long int": 4}
LONG_TYPE = re.compile(r"^(unsigned )?long( int)?$")
UNSIGNED = re.compile(r"\bunsigned\b|\b_Bool\b")
# __UINTPTR_TYPE__ after gcc -E, as clang prints it. Game code writes u_long
# (a typedef, printed as such, 32-bit on LP64) or long, so a cast spelled
# plain "unsigned long" is a guest-pointer accessor's own full-width
# conversion (psx_get, psx_to_ptr, psx_load, psx_slot_get: pc/src/psx_types.h)
# or a deliberate uintptr cast in port code: full width, so not listed.
ACCESSOR_UINTPTR = "unsigned long"

# 24/32-bit address idioms, scanned as text in src/ and pc/src.
TEXT_PATTERNS = [
    ("ot_mask_24", re.compile(r"0x0*[fF]{6}\b(?!\s*[fF])")),
    ("tag_high_byte", re.compile(r"0x[fF]{2}0{6}\b")),
    ("kseg0_or", re.compile(r"\|\s*0x80000000")),
    ("kseg0_literal", re.compile(r"0x80[0-1][0-9a-fA-F]{5}\b")),
    ("scratchpad_literal", re.compile(r"0x1[fF]80[0-9a-fA-F]{4}\b|getScratchAddr")),
    ("prim_macros", re.compile(r"\b(addPrim|addPrims|setaddr|getaddr|nextPrim|isendprim|termPrim|catPrim|setlen|getlen|ClearOTag|ClearOTagR|DrawOTag)\b")),
    ("addr_shift_24", re.compile(r"(>>|<<)\s*24\b.*\b(tag|addr|prim|ot|OT|next)\b|\b(tag|addr|prim|ot|OT|next)\b.*(>>|<<)\s*24\b")),
    ("ptr_as_u32", re.compile(r"\((u_long|u32|s32|int|u_int|unsigned int|long)\)\s*&?\s*\w+\s*[-+|&]")),
]


# --------------------------------------------------------------------------
# Locations: clang's JSON gives byte offsets into the .i; linemarkers map
# them back to the original file and line.

class LineIndex:
    def __init__(self, data):
        self.starts = []
        self.places = []
        current, line = "<unknown>", 1
        offset = 0
        marker = re.compile(rb'^#\s*(\d+)\s+"([^"]*)"')
        for raw in data.split(b"\n"):
            match = marker.match(raw)
            if match:
                line = int(match.group(1))
                current = os.path.normpath(match.group(2).decode(errors="replace"))
            else:
                self.starts.append(offset)
                self.places.append((current, line))
                line += 1
            offset += len(raw) + 1

    def at(self, offset):
        if offset is None or not self.starts:
            return ("<unknown>", 0)
        i = bisect.bisect_right(self.starts, offset) - 1
        return self.places[max(i, 0)]


def node_offset(node):
    for key in ("loc", "range"):
        loc = node.get(key)
        if not loc:
            continue
        if key == "range":
            loc = loc.get("begin", {})
        if "expansionLoc" in loc:
            loc = loc["expansionLoc"]
        if "offset" in loc:
            return loc["offset"]
    return None


# --------------------------------------------------------------------------
# Record layouts from -fdump-record-layouts.

LAYOUT_LINE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))? \|( *)(.*)$")
LAYOUT_END = re.compile(r"^\s*\| \[sizeof=(\d+),.*align=(\d+)")


def parse_layouts(text):
    layouts = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = block.strip("\n").splitlines()
        entries, name, size, align = [], None, None, None
        for line in lines:
            end = LAYOUT_END.match(line)
            if end:
                size, align = int(end.group(1)), int(end.group(2))
                break
            match = LAYOUT_LINE.match(line)
            if not match:
                continue
            depth = (len(match.group(4)) - 1) // 2
            body = match.group(5)
            if name is None:
                name = body
                continue
            # "type name"; the name is the last token (types may contain spaces).
            type_text, _, field = body.rpartition(" ")
            entries.append(
                {
                    "depth": depth,
                    "offset": int(match.group(1)),
                    "bits": None if match.group(2) is None else (int(match.group(2)), int(match.group(3))),
                    "type": type_text,
                    "name": field,
                }
            )
        if name is None or size is None:
            continue
        name = normalise_record_name(name)
        layouts[name] = {"size": size, "align": align, "entries": entries}
    return layouts


def normalise_record_name(name):
    # Anonymous records print their (presumed) location; normalise the path.
    return re.sub(r"\((unnamed|anonymous) at ([^)]*?):(\d+):(\d+)\)",
                  lambda m: f"({m.group(1)} at {os.path.normpath(m.group(2))}:{m.group(3)}:{m.group(4)})", name)


def unnamed_location(name):
    match = re.search(r"\((?:unnamed|anonymous) at ([^)]*?):(\d+):\d+\)", name)
    return (match.group(1), int(match.group(2))) if match else None


LONG_WORD = re.compile(rb"\b(unsigned\s+|signed\s+)?long(\s+(?:unsigned|signed))?(\s+int)?\b(?!\s+(long|double))")


def long_as_int(data):
    """PS1 sources with every (u_)long 32 bits: the proposed ROOD_PC retype."""
    def sub(match):
        if data[max(0, match.start() - 5):match.start()].rstrip().endswith(b"long"):
            return match.group(0)
        return (match.group(1) or b"") + (match.group(2) or b"") + b" int"
    return LONG_WORD.sub(sub, data)


def strip_type(type_text):
    t = re.sub(r"\b(const|volatile|restrict|__restrict)\b", "", type_text)
    return normalise_record_name(" ".join(t.split()))


ARRAY = re.compile(r"^(.*?)\s*((?:\[\d*\])+)$")


def split_array(type_text):
    match = ARRAY.match(type_text)
    if not match:
        return type_text, 1
    count = 1
    for dim in re.findall(r"\[(\d*)\]", match.group(2)):
        count *= int(dim) if dim else 0
    return match.group(1).strip(), count


def pointee(type_text):
    t = strip_type(type_text)
    if t.endswith("*") and "(*" not in t:
        return t[:-1].strip()
    return None


# --------------------------------------------------------------------------
# Per-unit analysis.

def run_clang(args, data):
    result = subprocess.run(["clang", *args, "-x", "c", "-"], input=data, capture_output=True)
    errors = result.stderr.decode(errors="replace").count(" error: ")
    return result.stdout, errors


class Unit:
    def __init__(self, name, source, data64, data32, ref_target):
        self.name = name
        self.source = source
        self.data64 = data64
        self.data32 = data32
        self.ref_target = ref_target
        self.index = LineIndex(data64)
        self.findings = collections.defaultdict(list)
        self.instances = collections.defaultdict(list)
        self.import_types = {}
        self.decl_refs = collections.Counter()
        self.typedefs = {}
        self.record_locs = {}
        self.function_locs = {}
        self.member_uses = collections.defaultdict(list)

    def where(self, node):
        file, line = self.index.at(node_offset(node))
        return file, line


def type_of(node, desugared=True):
    t = node.get("type", {})
    if desugared and "desugaredQualType" in t:
        return t["desugaredQualType"]
    return t.get("qualType", "")


def kids(node):
    return node.get("inner", [])


def strip_parens(node):
    while node and node.get("kind") in ("ParenExpr",) and kids(node):
        node = kids(node)[0]
    return node


def strip_implicit(node):
    while node and node.get("kind") in ("ParenExpr", "ImplicitCastExpr") and kids(node):
        node = kids(node)[0]
    return node


def strip_casts(node):
    while node and node.get("kind") in ("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr") and kids(node):
        node = kids(node)[0]
    return node


def const_value(node):
    node = strip_implicit(node)
    if node is None:
        return None
    kind = node.get("kind")
    if kind == "IntegerLiteral":
        return int(node["value"])
    if kind == "CharacterLiteral":
        return int(node["value"])
    if kind == "CStyleCastExpr" and node.get("castKind") in ("IntegralCast", "NoOp"):
        return const_value(kids(node)[0])
    if kind == "UnaryOperator" and kids(node):
        v = const_value(kids(node)[0])
        if v is None:
            return None
        return {"-": -v, "~": ~v, "+": v}.get(node.get("opcode"))
    if kind == "BinaryOperator" and len(kids(node)) == 2:
        a, b = const_value(kids(node)[0]), const_value(kids(node)[1])
        if a is None or b is None:
            return None
        op = node.get("opcode")
        try:
            return {
                "+": a + b, "-": a - b, "*": a * b, "|": a | b, "&": a & b,
                "<<": a << b, ">>": a >> b, "^": a ^ b,
                "/": int(a / b) if b else None,  # C truncates toward zero
            }.get(op)
        except (ValueError, OverflowError):
            return None
    return None


def callee_name(call):
    if not kids(call):
        return None
    callee = strip_implicit(kids(call)[0])
    if callee and callee.get("kind") == "DeclRefExpr":
        return callee.get("referencedDecl", {}).get("name")
    return None


def scalar_size(type_text):
    t = strip_type(type_text)
    if t.endswith("*"):
        return 8
    return SCALAR_SIZES.get(t)


def scalar_size_ps1(type_text):
    t = strip_type(type_text)
    if t.endswith("*") or "(*)" in t:
        return 4
    return SCALAR_SIZES_PS1.get(t)


def instance_how(kind, value):
    if kind == "scratchpad":
        return "scratchpad"
    if value & 0xFFFFFFFF == 0x80000000:
        return "guest null"  # the port's stand-in for reading PS1 address 0
    return "fixed address"


def address_kind(value):
    value &= 0xFFFFFFFF
    if 0x1F800000 <= value < 0x1F800400:
        return "scratchpad"
    if 0x1F801000 <= value < 0x1F803000:
        return "io"
    if 0x80000000 <= value < 0x80200000:
        return "guest RAM"
    if value == 0xFFFFFFFF:
        return "-1 sentinel"
    if value == 0:
        return "null"
    return "other"


class Analyzer:
    def __init__(self, unit, records32, records64, imports):
        self.u = unit
        self.r32 = records32
        self.r64 = records64
        self.imports = imports
        self.function = None
        self.function_depth = 0

    def record_of(self, type_text_sugared, type_text_desugared=None):
        for t in (type_text_sugared, type_text_desugared):
            if not t:
                continue
            t = strip_type(t)
            if t in self.r32 and t in self.r64:
                return t
            # "struct X" vs "X"
            bare = re.sub(r"^(struct|union)\s+", "", t)
            if bare in self.r32 and bare in self.r64:
                return bare
            for prefix in ("struct ", "union "):
                if prefix + t in self.r32 and prefix + t in self.r64:
                    return prefix + t
        return None

    def record_pointee(self, node):
        sugared = pointee(type_of(node, False))
        desugared = pointee(type_of(node, True))
        if sugared is None and desugared is None:
            return None
        return self.record_of(sugared, desugared)

    def add(self, kind, node, **data):
        if kind in ("pointer_to_int", "int_to_pointer") and ACCESSOR_UINTPTR in (
                data.get("to"), data.get("from_type")):
            return
        if kind == "byte_offset_untyped" and data.get("cast_to") == ACCESSOR_UINTPTR:
            return
        data.pop("cast_to", None)
        file, line = self.u.where(node)
        data.update({"file": file, "line": line, "function": self.function, "unit": self.u.source})
        self.u.findings[kind].append(data)

    def walk(self, node, parents):
        kind = node.get("kind")
        entered = False
        if kind == "FunctionDecl" and any(k.get("kind") == "CompoundStmt" for k in kids(node)):
            self.function = node.get("name")
            file, line = self.u.where(node)
            self.u.function_locs[self.function] = (file, line)
            entered = True
        handler = getattr(self, "on_" + kind, None) if kind else None
        if handler:
            handler(node, parents)
        parents.append(node)
        for child in kids(node):
            if isinstance(child, dict):
                self.walk(child, parents)
        parents.pop()
        if entered:
            self.function = None

    # -- declarations ------------------------------------------------------
    def on_TypedefDecl(self, node, parents):
        self.u.typedefs[node.get("name")] = type_of(node, True)
        self.u.record_locs.setdefault(node.get("name"), self.u.where(node))

    def on_RecordDecl(self, node, parents):
        if node.get("name") and node.get("completeDefinition"):
            name = f"{node.get('tagUsed', 'struct')} {node['name']}"
            self.u.record_locs.setdefault(name, self.u.where(node))

    def on_VarDecl(self, node, parents):
        name = node.get("name")
        storage = node.get("storageClass")
        local = self.function is not None
        sugared, desugared = type_of(node, False), type_of(node, True)
        base_s, count = split_array(strip_type(sugared))
        base_d, _ = split_array(strip_type(desugared))
        record = self.record_of(base_s, base_d)
        import_name = name in self.imports
        if storage == "extern" and import_name:
            self.u.import_types[name] = {"type": sugared, "desugared": desugared, "record": record, "count": count}
        if record is None:
            return
        if storage == "extern":
            where = "image import" if import_name else "extern"
        elif local and storage != "static":
            where = "stack"
        elif local:
            where = "static local"
        else:
            where = "static" if storage == "static" else "global"
        if where == "extern":
            return
        file, line = self.u.where(node)
        self.u.instances[record].append(
            {"how": where, "name": name, "count": count, "file": file, "line": line,
             "function": self.function, "unit": self.u.source}
        )

    def on_MemberExpr(self, node, parents):
        """Accesses to pointer members: the psx_get/psx_set sites of a PS1 layout."""
        if not kids(node) or not POINTERISH.search(type_of(node, True)):
            return
        base = kids(node)[0]
        if node.get("isArrow"):
            record = self.record_pointee(base)
        else:
            record = self.record_of(type_of(base, False), type_of(base, True))
        if record:
            file, line = self.u.where(node)
            self.u.member_uses[record].append((node.get("name"), file, line))

    def on_DeclRefExpr(self, node, parents):
        ref = node.get("referencedDecl", {})
        if ref.get("kind") == "VarDecl" and ref.get("name") in self.imports:
            self.u.decl_refs[ref["name"]] += 1

    # -- casts -------------------------------------------------------------
    def on_CStyleCastExpr(self, node, parents):
        self.cast(node, parents, explicit=True)

    def on_ImplicitCastExpr(self, node, parents):
        self.cast(node, parents, explicit=False)

    def cast(self, node, parents, explicit):
        cast_kind = node.get("castKind")
        inner = kids(node)[0] if kids(node) else None
        if inner is None:
            return
        if cast_kind == "PointerToIntegral":
            target = strip_type(type_of(node))
            size = scalar_size(target)
            # (u_long)ptr is 8 bytes under LP64 but truncates once u_long is
            # retyped to 32 bits as recommended, so it is listed too.
            if size is not None and (size < 8 or LONG_TYPE.match(target)):
                self.add(
                    "pointer_to_int",
                    node,
                    to=type_of(node, False),
                    from_type=type_of(inner, False),
                    category="via long (truncates after the u_long retype)" if size == 8 else "truncates",
                    explicit=explicit,
                    record=self.record_pointee(inner),
                )
            return
        if cast_kind == "IntegralToPointer":
            value = const_value(inner)
            source_type = type_of(inner)
            size = scalar_size(source_type)
            if value is not None:
                category = "literal " + address_kind(value)
            elif strip_casts(inner).get("kind") == "CallExpr" and callee_name(strip_casts(inner)) and \
                    type_of(strip_casts(inner)) == "int":
                category = "int-returning call (implicit declaration?)"
            elif size is not None and size < 8:
                category = "zero-extends" if UNSIGNED.search(strip_type(source_type)) else "sign-extends"
            elif LONG_TYPE.match(strip_type(source_type)):
                category = "from long (32-bit after the u_long retype: " + (
                    "zero-extends)" if UNSIGNED.search(strip_type(source_type)) else "sign-extends)")
            else:
                category = "64-bit integer"
            record = self.record_pointee(node)
            if value is not None and record:
                kind = address_kind(value)
                file, line = self.u.where(node)
                self.u.instances[record].append(
                    {"how": instance_how(kind, value), "address": f"{value & 0xFFFFFFFF:#x}",
                     "file": file, "line": line, "function": self.function, "unit": self.u.source}
                )
            if category != "64-bit integer":
                self.add("int_to_pointer", node, to=type_of(node, False), from_type=type_of(inner, False),
                         category=category, explicit=explicit, record=record,
                         value=None if value is None else f"{value & 0xFFFFFFFF:#x}")
            return
        if cast_kind != "BitCast":
            return
        record = self.record_pointee(node)
        if record is None:
            return
        # Ignore the record-to-same-record and the implicit chains above us.
        root = strip_casts(inner)
        root_kind = root.get("kind") if root else None
        if root_kind == "CallExpr" and callee_name(root) in ALLOC_CALLS:
            call = root
            args = kids(call)[1:]
            index = ALLOC_CALLS[callee_name(call)]
            size = const_value(args[index]) if index < len(args) else None
            file, line = self.u.where(node)
            self.u.instances[record].append(
                {"how": "heap", "call": callee_name(call), "size": size, "file": file, "line": line,
                 "function": self.function, "unit": self.u.source}
            )
            if size is not None:
                self.literal_size(node, callee_name(call), record, 1, size, role="alloc")
            return
        # Only count the outermost cast of a chain as a reinterpretation.
        if parents and parents[-1].get("kind") in ("ImplicitCastExpr", "CStyleCastExpr"):
            return
        value = const_value(inner)
        if value is None and root_kind in ("IntegerLiteral", "BinaryOperator"):
            value = const_value(root)
        if value is not None:
            kind = address_kind(value)
            file, line = self.u.where(node)
            self.u.instances[record].append(
                {"how": instance_how(kind, value), "address": f"{value & 0xFFFFFFFF:#x}",
                 "file": file, "line": line, "function": self.function, "unit": self.u.source}
            )
            return
        source_record = self.record_pointee(root) if root else None
        if source_record == record:
            return
        source_type = strip_type(type_of(root, True)) if root else ""
        source_pointee = source_type[:-1].strip() if source_type.endswith("*") else None
        if source_record is not None:
            how = "cast from struct"
        elif source_pointee == "void":
            how = "cast from void*"  # generic pointer: says nothing about the storage
        elif root_kind == "BinaryOperator" and source_type.endswith("*") or source_pointee in SCALAR_SIZES:
            how = "reinterpreted buffer"
        else:
            how = "cast from " + (root_kind or "?")
        file, line = self.u.where(node)
        self.u.instances[record].append(
            {"how": how, "from": type_of(root, False) if root else None, "source_record": source_record,
             "expr": describe(root), "file": file, "line": line, "function": self.function, "unit": self.u.source}
        )

    # -- calls with sizes ----------------------------------------------------
    def on_CallExpr(self, node, parents):
        name = callee_name(node)
        if name not in SIZE_CALLS:
            return
        args = kids(node)[1:]
        dst, src, size_index = SIZE_CALLS[name]
        if size_index >= len(args):
            return
        size = const_value(args[size_index])
        if size is None:
            return
        for role, index in (("dst", dst), ("src", src)):
            if index is None or index >= len(args):
                continue
            record, count = self.record_target(args[index])
            if record:
                self.literal_size(node, name, record, count, size, role=role)

    def record_target(self, expr):
        node = expr
        while node:
            kind = node.get("kind")
            if kind == "UnaryOperator" and node.get("opcode") == "&" and kids(node):
                operand = kids(node)[0]
                base_s, count = split_array(strip_type(type_of(operand, False)))
                base_d, _ = split_array(strip_type(type_of(operand, True)))
                record = self.record_of(base_s, base_d)
                return (record, count) if record else (None, 0)
            record = self.record_pointee(node)
            if record:
                return record, 1
            if kind == "DeclRefExpr" or kind == "MemberExpr":
                base_s, count = split_array(strip_type(type_of(node, False)))
                base_d, _ = split_array(strip_type(type_of(node, True)))
                record = self.record_of(base_s, base_d)
                return (record, count) if record else (None, 0)
            if kind in ("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr") and kids(node):
                node = kids(node)[0]
                continue
            return None, 0
        return None, 0

    def literal_size(self, node, call, record, count, size, role):
        s32, s64 = self.r32[record]["size"], self.r64[record]["size"]
        if s32 == s64:
            return
        if size == s32 * max(count, 1):
            verdict = "matches PS1 sizeof: use sizeof"
        elif s32 and size % s32 == 0:
            verdict = f"{size // s32} x PS1 sizeof"
        elif size < s32:
            verdict = "partial (prefix of the struct)"
        else:
            verdict = "unrelated to sizeof"
        self.add("literal_size", node, call=call, role=role, record=record, literal=size,
                 sizeof_ps1=s32 * max(count, 1), sizeof_lp64=s64 * max(count, 1), verdict=verdict)

    # -- byte-offset arithmetic ---------------------------------------------
    def on_BinaryOperator(self, node, parents):
        if node.get("opcode") not in ("+", "-") or len(kids(node)) != 2:
            return
        left, right = kids(node)
        constant = const_value(right)
        if constant is None:
            return
        if node.get("opcode") == "-":
            constant = -constant
        self.offset_access(node, left, constant)

    def on_ArraySubscriptExpr(self, node, parents):
        if len(kids(node)) != 2:
            return
        base, index = kids(node)
        constant = const_value(index)
        if constant is None:
            return
        self.offset_access(node, base, constant)

    def offset_access(self, node, base, constant):
        base = strip_implicit(base)
        if not base or base.get("kind") != "CStyleCastExpr" or not kids(base):
            return
        inner = kids(base)[0]
        root = strip_casts(inner)
        cast_kind = base.get("castKind")
        if cast_kind == "BitCast":
            element = pointee(type_of(base, True))
            if element is None:
                return
            scale = scalar_size_ps1(element)  # the literal was written for the PS1
            if scale is None:
                return
        elif cast_kind == "PointerToIntegral":
            scale = 1
        else:
            return
        record = self.record_pointee(inner) or (self.record_pointee(root) if root else None)
        byte = constant * scale
        data = {"byte_offset": byte, "base_type": type_of(inner, False), "expr": describe(root)}
        if record is None:
            self.add("byte_offset_untyped", node, cast_to=type_of(base, False), **data)
            return
        data.update(resolve_offset(self.r32[record], self.r64[record], byte, self.r32, self.r64))
        long32 = self.r64_long32.get(record)
        if long32 is not None:
            data["status_long32"] = resolve_offset(self.r32[record], long32, byte, self.r32,
                                                   self.r64_long32, long_size=4)["status"]
        else:
            data["status_long32"] = data["status"]
        data["record"] = record
        self.add("byte_offset", node, **data)


def describe(node, depth=0):
    """A short source-like rendering of an expression, for the report."""
    if not node or depth > 4:
        return "..."
    kind = node.get("kind")
    k = kids(node)
    if kind == "DeclRefExpr":
        return node.get("referencedDecl", {}).get("name", "?")
    if kind == "IntegerLiteral":
        return hex(int(node["value"])) if int(node["value"]) > 9 else node["value"]
    if kind == "MemberExpr" and k:
        return describe(k[0], depth + 1) + ("->" if node.get("isArrow") else ".") + node.get("name", "?")
    if kind in ("ImplicitCastExpr", "ParenExpr") and k:
        return describe(k[0], depth)
    if kind == "CStyleCastExpr" and k:
        return f"({type_of(node, False)}){describe(k[0], depth + 1)}"
    if kind == "BinaryOperator" and len(k) == 2:
        return f"{describe(k[0], depth + 1)} {node.get('opcode')} {describe(k[1], depth + 1)}"
    if kind == "UnaryOperator" and k:
        return node.get("opcode", "") + describe(k[0], depth + 1)
    if kind == "ArraySubscriptExpr" and len(k) == 2:
        return f"{describe(k[0], depth + 1)}[{describe(k[1], depth + 1)}]"
    if kind == "CallExpr":
        return (callee_name(node) or "?") + "(...)"
    return kind or "?"


def field_paths(layout):
    stack, out = [], []
    for e in layout["entries"]:
        del stack[e["depth"] - 1:]
        stack.append(e["name"])
        out.append(".".join(stack))
    return out


def element_sizes(type_text, r32, r64, long_size):
    """PS1 and target sizes of one element of a field's type, or None."""
    base, count = split_array(strip_type(type_text))
    if count <= 1 and base == strip_type(type_text):
        return None
    s32 = scalar_size_ps1(base)
    if s32 is not None:
        s64 = 8 if (base.endswith("*") or "(*)" in base) else (
            long_size if LONG_TYPE.match(base) else SCALAR_SIZES.get(base))
        return (s32, s64, None) if s64 else None
    for name in (base, "struct " + base, "union " + base):
        if name in r32 and name in r64:
            return r32[name]["size"], r64[name]["size"], name
    return None


def resolve_offset(l32, l64, byte, r32=None, r64=None, long_size=8):
    """Which field a PS1 byte offset names, and that field's LP64 offset."""
    s32, s64 = l32["size"], l64["size"]
    if not s32 or byte < 0:
        return {"field": None, "lp64_offset": None, "status": "negative or empty"}
    element, rest = divmod(byte, s32)
    e32, e64 = l32["entries"], l64["entries"]
    if len(e32) != len(e64):
        return {"field": None, "lp64_offset": None, "status": "layouts differ in shape"}
    paths = field_paths(l32)
    exact = [i for i, e in enumerate(e32) if e["offset"] == rest and (e["bits"] is None or e["bits"][0] == 0)]
    if exact:
        whole = [i for i in exact if e32[i]["bits"] is None]
        i = (whole or exact)[-1]  # deepest field starting here
        lp64 = element * s64 + e64[i]["offset"]
        status = "same" if lp64 == byte else "differs"
        return {"field": paths[i], "lp64_offset": lp64, "status": status, "element": element}
    before = [i for i, e in enumerate(e32) if e["offset"] <= rest]
    if not before:
        return {"field": None, "lp64_offset": None, "status": "no field"}
    i = before[-1]
    delta = rest - e32[i]["offset"]
    lp64 = element * s64 + e64[i]["offset"] + delta
    sizes = element_sizes(e32[i]["type"], r32 or {}, r64 or {}, long_size)
    if sizes and sizes[0]:  # inside an array member: elements may grow too
        index, within = divmod(delta, sizes[0])
        inner = within
        if sizes[2] is not None and within:
            nested = resolve_offset(r32[sizes[2]], r64[sizes[2]], within, r32, r64, long_size)
            if nested.get("lp64_offset") is not None:
                inner = nested["lp64_offset"]
        lp64 = element * s64 + e64[i]["offset"] + index * sizes[1] + inner
    status = "same" if lp64 == byte else "differs"
    return {"field": f"{paths[i]}+{delta}", "lp64_offset": lp64, "status": status + " (inside field)", "element": element}


def force_layouts(data, ast):
    """Append sizeof uses so clang dumps every file-scope record's layout."""
    names = []
    for node in kids(ast):
        kind = node.get("kind")
        if kind == "TypedefDecl":
            # Follow the sugar (ElaboratedType, TypedefType) to a RecordType;
            # "typedef struct {...} Foo;" desugars to "Foo" itself.
            sugar = kids(node)[0] if kids(node) else None
            while sugar and sugar.get("kind") in ("ElaboratedType", "TypedefType", "ParenType") and kids(sugar):
                sugar = kids(sugar)[0]
            if sugar and sugar.get("kind") == "RecordType":
                names.append(node["name"])
        elif kind == "RecordDecl" and node.get("name") and node.get("completeDefinition"):
            names.append(f"{node.get('tagUsed', 'struct')} {node['name']}")
    extra = "".join(f"\ntypedef char lp64_audit_force_{i}[sizeof({n})];" for i, n in enumerate(names))
    return data + extra.encode() + b"\n"


def analyse(job):
    name, source, path64, path32, ref_target, imports = job
    data64 = pathlib.Path(path64).read_bytes()
    data32 = pathlib.Path(path32).read_bytes()
    unit = Unit(name, source, data64, data32, ref_target)
    ast_text, errors = run_clang(["-target", TARGET_LP64, *CLANG_COMMON, "-Xclang", "-ast-dump=json"], data64)
    try:
        ast = json.loads(ast_text)
    except json.JSONDecodeError:
        return {"name": name, "source": source, "error": "no AST"}
    layout64_text, _ = run_clang(["-target", TARGET_LP64, *CLANG_COMMON, "-Xclang", "-fdump-record-layouts"],
                                 force_layouts(data64, ast))
    layout32_text, _ = run_clang(["-target", ref_target, *CLANG_COMMON, "-Xclang", "-fdump-record-layouts"],
                                 force_layouts(data32, ast))
    records64 = parse_layouts(layout64_text.decode(errors="replace"))
    records32 = parse_layouts(layout32_text.decode(errors="replace"))
    # Layouts again with long/u_long at 32 bits (PS1 sources only), to tell
    # pointer-driven changes from the ones the u_long retype alone fixes.
    records64_long32 = {}
    if ref_target == TARGET_PS1:
        text, _ = run_clang(["-target", TARGET_LP64, *CLANG_COMMON, "-Xclang", "-fdump-record-layouts"],
                            force_layouts(long_as_int(data64), ast))
        records64_long32 = parse_layouts(text.decode(errors="replace"))
    analyzer = Analyzer(unit, records32, records64, imports)
    analyzer.r64_long32 = records64_long32
    sys.setrecursionlimit(100000)
    analyzer.walk(ast, [])
    return {
        "name": name,
        "source": source,
        "group": group_of(source),
        "clang_errors": errors,
        "records32": records32,
        "records64": records64,
        "records64_long32": records64_long32,
        "record_locs": unit.record_locs,
        "typedefs": unit.typedefs,
        "instances": dict(unit.instances),
        "findings": dict(unit.findings),
        "import_types": unit.import_types,
        "decl_refs": dict(unit.decl_refs),
        "member_uses": {k: sorted(set(map(tuple, v))) for k, v in unit.member_uses.items()},
    }


# --------------------------------------------------------------------------
# Inputs.

def first_marker(path):
    with open(path, "rb") as f:
        for raw in f:
            match = re.match(rb'^#\s*\d+\s+"([^"<]+)"', raw)
            if match:
                return match.group(1).decode()
    return str(path)


def runtime_units(work, sdl_include):
    """Port runtime C files the build compiles directly (not via .i)."""
    out = []
    pre = work / "pre"
    pre.mkdir(parents=True, exist_ok=True)
    flags = ["-E", "-fno-builtin", "-I" + str(sdl_include)]  # as build_boot.py compiles these files
    names = ["boot_runtime", "boot_window", "boot_controller", "boot_gamepad"] + [
        "audio_libspu", "audio_output", "cd_drive", "xa_audio", "mdec", "memory_card", "dev_rpc", "gl_present",
        "gpu_hw", "scene_capture", "shadows", "ptgi", "glows", "overlay_statics"]
    for name in names:
        source = f"pc/src/{name}.c"
        p64, p32 = pre / f"{name}.64.i", pre / f"{name}.32.i"
        ok = True
        # clang preprocesses here: gcc-preprocessed glibc headers use gcc-only types.
        for path, target, extra in ((p64, TARGET_LP64, ["-DROOD_PC_LP64"]), (p32, TARGET_I386, [])):
            result = subprocess.run(["clang", "-target", target, *flags, *extra, source, "-o", str(path)], cwd=ROOT,
                                    capture_output=True)
            ok = ok and result.returncode == 0
        if ok:
            out.append((name, source, str(p64), str(p32), TARGET_I386))
    return out


def read_imports(path):
    imports = {}
    if path.exists():
        for name, value in re.findall(r"^(\w+)\s*=\s*(0x[0-9a-fA-F]+);", path.read_text(), re.M):
            imports[name] = int(value, 16)
    return imports


def read_function_tables(path):
    tables = {}
    if path.exists():
        for name, body in re.findall(r"void\* (\w+)\[\] = \{(.*?)\};", path.read_text()):
            tables[name] = len([e for e in body.split(",") if e.strip()])
    return tables


def read_images():
    """Each disc file's load address and bytes, from the splat configs (local data/)."""
    images = {}
    for config in sorted((ROOT / "config").rglob("splat.yaml")):
        text = config.read_text()
        target = re.search(r"target_path:\s*(\S+)", text)
        segment = re.search(r"-\s*(?:name:.*\n\s*)?start:\s*(0x[0-9A-Fa-f]+|\d+)[\s\S]*?vram:\s*(0x[0-9A-Fa-f]+)", text)
        if not target or not segment:
            continue
        path = ROOT / target.group(1)
        if not path.exists():
            continue
        key = path.name.split(".")[0].lower()
        if key == "slus_010":
            key = "main"
        images[key] = {"file": str(path.relative_to(ROOT)), "offset": int(segment.group(1), 0),
                       "vram": int(segment.group(2), 16), "data": path.read_bytes()}
    return images


OVERLAY_OF_GROUP = {"BATTLE": ["battle", "initbtl"], "TITLE/ending": ["title"], "MENU": ["mainmenu"]}


def import_content(name, address, size, groups, images):
    """Where an import's bytes come from and whether they look like guest pointers."""
    key = name.split("__")[1] if "__" in name else None
    candidates = []
    if key:
        candidates = [key, {"init": "initbtl"}.get(key, key)]
    elif address < 0x80068800:
        candidates = ["main"]
    else:
        for g in groups:
            candidates += OVERLAY_OF_GROUP.get(g, [])
        candidates += [k for k in images if re.match(r"menu[0-9a-f]$", k)] if "MENU" in groups else []
    candidates = [k for k in dict.fromkeys(candidates) if k in images]
    # An image whose file holds the address wins; otherwise the bss of the
    # candidate loaded closest below it.
    holding = [k for k in candidates if images[k]["offset"] <= address - images[k]["vram"] + images[k]["offset"]
               < len(images[k]["data"])]
    if not holding:
        below = [k for k in candidates if images[k]["vram"] <= address]
        if below:
            k = max(below, key=lambda k: images[k]["vram"])
            return {"image": images[k]["file"], "content": "bss (past the end of the file)"}
        return {"image": None, "content": "unknown"}
    for k in holding[:1]:
        image = images[k]
        start = address - image["vram"] + image["offset"]
        data = image["data"][start:start + max(4, min(size or 4, 0x4000))]
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - 3, 4)]
        pointers = sum(1 for w in words if 0x80010000 <= w < 0x80200000)
        content = "zero" if not any(data) else "initialised"
        return {"image": image["file"], "content": content, "words": len(words), "guest_pointer_words": pointers}
    return {"image": None, "content": "unknown"}


def text_scan():
    hits = collections.defaultdict(list)
    files = sorted(
        [p for p in (ROOT / "src").rglob("*") if p.suffix in (".c", ".h")]
        + [p for p in (ROOT / "include").rglob("*") if p.suffix == ".h"]
        + [p for p in (ROOT / "pc/src").rglob("*") if p.suffix in (".c", ".h", ".cpp", ".inc")]
    )
    for path in files:
        rel = str(path.relative_to(ROOT))
        in_comment = False
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            code = line
            if in_comment:
                if "*/" in code:
                    code = code.split("*/", 1)[1]
                    in_comment = False
                else:
                    continue
            code = re.sub(r"/\*.*?\*/", "", code)
            if "/*" in code:
                code, in_comment = code.split("/*", 1)[0], True
            code = code.split("//", 1)[0]
            if "INCLUDE_ASM" in code or "INCLUDE_RODATA" in code:
                continue
            for label, pattern in TEXT_PATTERNS:
                if pattern.search(code):
                    hits[label].append({"file": rel, "line": number, "text": code.strip()[:160]})
    return dict(hits)


# --------------------------------------------------------------------------
# Merge and classify.

POINTERISH = re.compile(r"\*|\(\*\)")
LONGISH = re.compile(r"\b(long|u_long|ulong|size_t|ssize_t|ptrdiff_t|intptr_t|uintptr_t|time_t|off_t)\b")


def merge(results, imports, function_tables):
    records = {}
    for r in results:
        if "error" in r:
            continue
        for name, l64 in r["records64"].items():
            l32 = r["records32"].get(name)
            if l32 is None:
                continue
            signature = (l32["size"], l64["size"], tuple(e["offset"] for e in l32["entries"]),
                         tuple(e["offset"] for e in l64["entries"]))
            if name.startswith("struct __") or name.startswith("union __"):
                continue
            where = r["record_locs"].get(name) or unnamed_location(name)
            if where and (where[0].startswith(("/", "<", "pc/third_party/"))):
                continue  # host system and vendored library types are native already
            entry = records.setdefault(name, {"variants": {}, "units": set(), "loc": None, "long32": None})
            entry["units"].add(r["source"])
            if entry["long32"] is None and name in r.get("records64_long32", {}):
                entry["long32"] = r["records64_long32"][name]
            here = r["record_locs"].get(name)
            # A game definition wins over a same-named port runtime one.
            if here and (not entry["loc"] or entry["loc"][0].startswith("pc/") and not here[0].startswith("pc/")):
                entry["loc"] = here
            entry["variants"].setdefault(signature, (l32, l64))
    typedefs = {}
    for r in results:
        typedefs.update(r.get("typedefs", {}))

    def resolved(type_text):
        t = strip_type(type_text)
        base, _ = split_array(t)
        return typedefs.get(base, base)

    out = {}
    for name, entry in records.items():
        (l32, l64) = next(iter(entry["variants"].values()))
        direct = []
        for e32, e64 in zip(l32["entries"], l64["entries"]):
            if e32["depth"] != 1:
                continue
            t = e32["type"]
            reasons = []
            if POINTERISH.search(t) or POINTERISH.search(resolved(t)):
                reasons.append("pointer")
            elif LONGISH.search(t) or LONGISH.search(resolved(t)):
                reasons.append("long")
            base, _ = split_array(strip_type(t))
            nested = base if base in records else None
            direct.append({"name": e32["name"], "type": t, "ps1": e32["offset"], "lp64": e64["offset"],
                           "reason": reasons[0] if reasons else None, "record": nested})
        changed = l32["size"] != l64["size"] or any(d["ps1"] != d["lp64"] for d in direct)
        long32 = entry["long32"]
        if long32 is not None and len(long32["entries"]) == len(l32["entries"]):
            pointer_driven = long32["size"] != l32["size"] or any(
                a["offset"] != b["offset"] for a, b in zip(l32["entries"], long32["entries"]))
            size_long32 = long32["size"]
        else:  # host-only runtime records: long stays long
            pointer_driven, size_long32 = changed, l64["size"]
        has_ptr = any(d["reason"] == "pointer" for d in direct)
        has_long = any(d["reason"] == "long" for d in direct)
        if not (changed or has_ptr or has_long):
            continue
        loc = entry["loc"] or unnamed_location(name) or ("?", 0)
        out[name] = {
            "name": name,
            "file": loc[0],
            "line": loc[1],
            "group": group_of(loc[0]) if loc[0] != "?" else "main",
            "size_ps1": l32["size"],
            "size_lp64": l64["size"],
            "changed": changed,
            "pointer_driven": changed and pointer_driven,
            "size_lp64_long32": size_long32,
            "pointer_members": sum(d["reason"] == "pointer" for d in direct),
            "long_members": sum(d["reason"] == "long" for d in direct),
            "members": direct,
            "layout_variants": len(entry["variants"]),
            "units": sorted(entry["units"]),
            "instances": [],
            "embedded_in": [],
        }
    # Embedding (by value) between affected records.
    for name, rec in out.items():
        for m in rec["members"]:
            if m["record"] in out:
                out[m["record"]]["embedded_in"].append(name)
    # Instances, deduplicated across units (headers repeat).
    for r in results:
        for name, items in r.get("instances", {}).items():
            if name in out:
                out[name]["instances"].extend(items)
    uses = collections.defaultdict(set)
    for r in results:
        for name, items in r.get("member_uses", {}).items():
            uses[name].update(map(tuple, items))
    for name, rec in out.items():
        rec["pointer_member_uses"] = len(uses.get(name, ()))
        rec["pointer_member_uses_by_group"] = dict(collections.Counter(group_of(f) for _, f, _ in uses.get(name, ())))
    for rec in out.values():
        seen, unique = set(), []
        for i in rec["instances"]:
            key = (i["how"], i.get("name"), i["file"], i["line"])
            if key not in seen:
                seen.add(key)
                unique.append(i)
        rec["instances"] = unique
    classify(out)
    return out


OVERLAY_EVIDENCE = {"image import", "scratchpad", "fixed address", "reinterpreted buffer"}
RUNTIME_EVIDENCE = {"heap", "global", "static", "static local", "stack"}


def classify(records):
    for rec in records.values():
        hows = collections.Counter(i["how"] for i in rec["instances"])
        rec["evidence"] = dict(hows)
    # Embedded in a PS1-layout record means PS1 layout too: iterate to a fixpoint.
    overlay = {n for n, r in records.items() if set(r["evidence"]) & OVERLAY_EVIDENCE}
    changed = True
    while changed:
        changed = False
        for name, rec in records.items():
            if name not in overlay and any(p in overlay for p in rec["embedded_in"]):
                overlay.add(name)
                changed = True
    for name, rec in records.items():
        evidence = set(rec["evidence"])
        if not rec["changed"]:
            rec["class"] = "unaffected"
            rec["why"] = "same layout under LP64 (only 64-bit-safe members)"
        elif not rec["pointer_driven"]:
            rec["class"] = "u_long retype"
            rec["why"] = "only long/u_long members change it: fixed by 32-bit u_long under ROOD_PC"
        elif name in overlay:
            runtime = evidence & RUNTIME_EVIDENCE
            rec["class"] = "ps1-overlay"
            reasons = sorted(evidence & OVERLAY_EVIDENCE)
            if not reasons:
                reasons = ["embedded in " + ", ".join(sorted(p for p in rec["embedded_in"] if p in overlay)[:3])]
            rec["why"] = "; ".join(reasons) + ("; also native instances: " + ", ".join(sorted(runtime)) if runtime else "")
        elif rec["group"] == "pc":
            rec["class"] = "host native"
            rec["why"] = "port runtime type, not overlaid on guest data"
        elif not evidence & RUNTIME_EVIDENCE and "cast from struct" in evidence:
            sources = sorted({i["source_record"] for i in rec["instances"] if i.get("source_record")})
            rec["class"] = "alias view"
            rec["alias_of"] = sources
            rec["why"] = "a cast over another struct's memory: " + ", ".join(sources[:4]) + (
                " ..." if len(sources) > 4 else "") + "; follows that struct's layout decision"
        elif evidence & RUNTIME_EVIDENCE or "cast from struct" in evidence:
            rec["class"] = "runtime"
            rec["why"] = ", ".join(sorted(evidence))
        else:
            rec["class"] = "runtime?"
            rec["why"] = "no instantiation seen (used through pointers only)" + (
                "; casts: " + ", ".join(sorted(evidence)) if evidence else "")
        if rec["class"] in ("runtime", "runtime?") and "cast from void*" in evidence:
            rec["why"] += "; review the void* casts: a file buffer would make it ps1-overlay"


# --------------------------------------------------------------------------
# Report.

def md_table(rows, header):
    lines = ["| " + " | ".join(header) + " |", "| " + " | ".join("---" for _ in header) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(str(c).replace("|", "\\|") for c in row) + " |")
    return "\n".join(lines)


def loc(item):
    return f"`{item['file']}:{item['line']}`"


def build64_section(build64):
    files = build64.get("files", [])
    if not files:
        return ["## 64-bit build", "", "(no pc/build/boot64/report.json: run `build_boot.py --arch 64`)", ""]
    failed = [f for f in files if not f["compiled"]]
    categories = collections.Counter()
    for f in files:
        categories.update(f["categories"])
    groups = collections.defaultdict(lambda: [0, 0, 0])
    for f in files:
        g = groups[f["group"]]
        g[0] += 1
        g[1] += not f["compiled"]
        g[2] += f["errors"]
    rows = [[g, v[0], v[1], v[2]] for g, v in sorted(groups.items())]
    worst = sorted(failed, key=lambda f: -f["errors"])[:15]
    return [
        "## 64-bit build",
        "",
        "`build_boot.py --arch 64` (gcc -m64 -fno-pie, `-Werror=pointer-to-int-cast`,",
        "`-Werror=int-to-pointer-cast`; no link yet; full list in `pc/build/boot64/report.txt`):",
        f"{len(files)} translation units, {len(files) - len(failed)} compile, {len(failed)} fail with "
        f"{sum(f['errors'] for f in files)} errors (" + ", ".join(f"{c} {n}" for c, n in categories.most_common()) + ").",
        "These are the truncating casts only: layout changes compile silently, which is",
        "what the rest of this audit is for.",
        "",
        md_table(rows, ["group", "units", "failing", "errors"]),
        "",
        "Most errors: " + ", ".join(f"`{f['source']}` {f['errors']}" for f in worst) + ".",
        "",
    ]


def write_markdown(path, audit):
    records = audit["records"]
    findings = audit["findings"]
    imports = audit["imports"]
    groups = ["main", "BATTLE", "MENU", "TITLE/ending", "pc"]
    changed = {n: r for n, r in records.items() if r["changed"]}
    by_class = collections.Counter(r["class"] for r in records.values())
    lines = [
        "# LP64 audit",
        "",
        "Generated by `pc/tools/lp64_audit.py` from the preprocessed sources of",
        "`build_boot.py --arch 64` (the build's include paths and defines). Layouts",
        "come from clang: PS1 = mipsel ILP32 (i386 for host-only runtime files),",
        "LP64 = x86_64. The machine-readable list is `pc/build/lp64_audit/audit.json`",
        "(regenerate: `python3 pc/tools/build_boot.py --arch 64 && python3 pc/tools/lp64_audit.py`).",
        "",
        "## Totals",
        "",
        md_table(
            [
                ["translation units analysed", audit["totals"]["units"]],
                ["structs/unions with pointer or long members, or a changed layout", len(records)],
                ["... whose layout changes under LP64", len(changed)],
                ["... still changing with a 32-bit u_long (pointer-driven)", sum(1 for r in changed.values() if r["pointer_driven"])],
                ["... with pointer members", sum(1 for r in records.values() if r["pointer_members"])],
                ["... with long/unsigned long members", sum(1 for r in records.values() if r["long_members"])],
                ["classified ps1-overlay (keep PS1 layout, psx_ptr)", by_class["ps1-overlay"]],
                ["classified runtime (native layout)", by_class["runtime"]],
                ["classified runtime? (no instance seen)", by_class["runtime?"]],
                ["classified alias view (cast over another struct)", by_class["alias view"]],
                ["classified u_long retype (long-only change)", by_class["u_long retype"]],
                ["classified host native (pc/src types)", by_class["host native"]],
                ["unaffected (pointer-free change-free)", by_class["unaffected"]],
                ["literal sizes on changed structs", len(findings.get("literal_size", []))],
                ["... matching the PS1 sizeof (fix with sizeof)", sum(1 for f in findings.get("literal_size", []) if f["verdict"].startswith("matches"))],
                ["byte offsets into structs (constant)", len(findings.get("byte_offset", []))],
                ["... whose LP64 offset differs", sum(1 for f in findings.get("byte_offset", []) if f["status"].startswith("differs"))],
                ["... still differing with a 32-bit u_long", sum(1 for f in findings.get("byte_offset", []) if f.get("status_long32", "").startswith("differs"))],
                ["byte offsets from untyped pointers (constant)", len(findings.get("byte_offset_untyped", []))],
                ["pointer -> 32-bit integer casts", sum(1 for f in findings.get("pointer_to_int", []) if f.get("category") == "truncates")],
                ["pointer -> long casts (truncate once u_long is 32-bit)", sum(1 for f in findings.get("pointer_to_int", []) if f.get("category") != "truncates")],
                ["integer -> pointer casts (all kinds, incl. literals)", len(findings.get("int_to_pointer", []))],
                ["... sign-extending (wrong for addresses >= 0x80000000)", sum(1 for f in findings.get("int_to_pointer", []) if "sign-extends" in f["category"])],
                ["data imports (imports.ld)", len(imports)],
                ["... whose declared type holds pointers", sum(1 for i in imports.values() if i.get("holds_pointers"))],
                ["... with no declaration seen", sum(1 for i in imports.values() if not i.get("type"))],
                ["generated function tables (function_tables.c)", len(audit["function_tables"])],
                ["text hits: packed OT / 24-bit masks", len(audit["text"].get("ot_mask_24", []))],
                ["text hits: OT/prim macros", len(audit["text"].get("prim_macros", []))],
                ["text hits: KSEG0 literals / `| 0x80000000`", len(audit["text"].get("kseg0_literal", [])) + len(audit["text"].get("kseg0_or", []))],
                ["text hits: scratchpad literals", len(audit["text"].get("scratchpad_literal", []))],
                ["clang errors (units still analysed)", audit["totals"]["clang_errors"]],
            ],
            ["what", "count"],
        ),
        "",
        *build64_section(audit.get("build64", {})),
        *CLASSIFICATION.splitlines(),
        "",
    ]

    # Records by group.
    retype = sorted(n for n, r in changed.items() if r["class"] == "u_long retype")
    lines += ["## Structs fixed by a 32-bit u_long", "",
              f"{len(retype)} structs change only because `long`/`u_long` members grow (the PS1 SDK",
              "uses `u_long` for 32-bit words: OT entries, packet tags, VECTOR, DR_ENV). Making",
              "`u_long`/`ulong` 32-bit under `ROOD_PC` (in `include/psx/sys/types.h` and",
              "`include/psx/stddef.h`, plus `long` members of game structs as `s32`) keeps their",
              "layout and removes most of the byte-offset differences below. PsyCross's own",
              "`psx/types.h` needs the same change for the 64-bit build.", "",
              ", ".join(f"`{n}`" for n in retype), "",
              "## Structs whose layout changes because of pointers, by overlay group", ""]
    for group in groups:
        rows = []
        for r in sorted(changed.values(), key=lambda r: (r["class"], r["name"])):
            if r["group"] != group or r["class"] in ("u_long retype",):
                continue
            ptr = [m["name"] for m in r["members"] if m["reason"] == "pointer"]
            lng = [m["name"] for m in r["members"] if m["reason"] == "long"]
            members = ", ".join(ptr[:4]) + (f" +{len(ptr) - 4}" if len(ptr) > 4 else "")
            if lng:
                members += ("; " if members else "") + "long: " + ", ".join(lng[:3])
            evidence = ", ".join(f"{k} {v}" for k, v in sorted(r["evidence"].items()))
            rows.append([f"`{r['name']}`", r["class"], f"{r['size_ps1']:#x} -> {r['size_lp64_long32']:#x}", members or "(nested)",
                         r.get("pointer_member_uses", 0), evidence or "-", f"`{r['file']}:{r['line']}`"])
        if rows:
            lines += [f"### {group} ({len(rows)})", "",
                      md_table(rows, ["struct", "class", "size PS1 -> LP64 (u_long 32)", "pointer/long members", "ptr uses", "instances", "defined"]), ""]

    # Literal sizes.
    lits = findings.get("literal_size", [])
    lines += ["## Literal sizes on changed structs", "",
              "Clears, copies and allocations whose size is a literal while the",
              "struct's size changes under LP64.", ""]
    rows = [[loc(f), f["function"] or "-", f"{f['call']}({f['role']})", f"`{f['record']}`", f"{f['literal']:#x}",
             f"{f['sizeof_ps1']:#x}", f"{f['sizeof_lp64']:#x}", f["verdict"]] for f in sorted(lits, key=lambda f: (f["file"], f["line"]))]
    lines += [md_table(rows, ["where", "function", "call", "struct", "literal", "PS1 sizeof", "LP64 sizeof", "verdict"]) if rows else "(none)", ""]

    # Byte offsets.
    offs = findings.get("byte_offset", [])
    diff = [f for f in offs if f["status"].startswith("differs")]
    diff_ptr = [f for f in diff if f.get("status_long32", f["status"]).startswith("differs")]
    lines += ["## Byte-offset arithmetic", "",
              f"{len(offs)} constant byte offsets into struct pointers (many from GTE macros);",
              f"{len(diff)} name a field whose LP64 offset differs, {len(diff) - len(diff_ptr)} of them only",
              f"because of `long` members (fixed by the u_long retype). The {len(diff_ptr)} pointer-driven",
              "ones, by struct and function, need `PSX_OFFSET(T, field, literal)` or a member",
              "access (or disappear when the struct keeps its PS1 layout):", ""]
    by_key = collections.defaultdict(list)
    for f in diff_ptr:
        by_key[(f["record"], f["file"], f["function"] or "-")].append(f)
    rows = []
    for (record, file, function), items in sorted(by_key.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        fields = sorted({f["field"] or "?" for f in items})
        rows.append([f"`{record}`", f"`{file}`", function, len(items),
                     ", ".join(f"`{x}`" for x in fields[:5]) + (f" +{len(fields) - 5}" if len(fields) > 5 else "")])
    lines += [md_table(rows[:80], ["struct", "file", "function", "offsets", "fields"]) if rows else "(none)", ""]
    if len(rows) > 80:
        lines += [f"({len(rows) - 80} more function/struct pairs in audit.json.)", ""]
    untyped = findings.get("byte_offset_untyped", [])
    per_file = collections.Counter(f["file"] for f in untyped)
    lines += [f"{len(untyped)} more constant offsets are taken from pointers whose pointee is not a",
              "struct (void*, u8*, integers): file and packet parsing that is layout-independent",
              "unless the buffer holds a changed struct. Top files: " +
              ", ".join(f"`{f}` {n}" for f, n in per_file.most_common(12)) + ".", ""]

    # Casts.
    p2i = findings.get("pointer_to_int", [])
    i2p = findings.get("int_to_pointer", [])
    lines += ["## Pointer/integer casts", ""]
    rows = []
    for group in groups:
        g_p2i = [f for f in p2i if group_of(f["file"]) == group]
        g_i2p = [f for f in i2p if group_of(f["file"]) == group]
        cats = collections.Counter(f["category"] for f in g_i2p)
        rows.append([group, len(g_p2i), len(g_i2p), ", ".join(f"{k} {v}" for k, v in cats.most_common())])
    lines += [md_table(rows, ["group", "pointer -> 32-bit int", "int -> pointer", "int -> pointer kinds"]), "",
              "Sign-extending conversions turn a guest address such as 0x80010000 held in",
              "an `int`/`s32` into 0xFFFFFFFF80010000; they must go through `u32` (or",
              "`psx_get`). Pointer-to-int casts are fine when the pointer is a guest address",
              "(it fits in 32 bits) and the value is only compared or masked; they are the",
              "packed-OT and address-arithmetic idioms below.", ""]
    top = collections.Counter(f["file"] for f in p2i + i2p)
    lines += ["Top files: " + ", ".join(f"`{f}` {n}" for f, n in top.most_common(15)) + ".", ""]

    # Text idioms.
    lines += ["## 24/32-bit address idioms (text scan)", ""]
    rows = []
    for label, hits in sorted(audit["text"].items()):
        files = collections.Counter(h["file"] for h in hits)
        rows.append([label, len(hits), ", ".join(f"`{f}` {n}" for f, n in files.most_common(6))])
    lines += [md_table(rows, ["idiom", "lines", "top files"]), "",
              "`ot_mask_24` and `prim_macros` are the packed ordering-table links: a 24-bit",
              "next pointer in each primitive's tag word. Under LP64 the plan replaces them",
              "with indices or a PC-side 64-bit list (`ROOD_PC`), which also removes the",
              "16 MB native-code placement.", ""]

    # Imports.
    holding = {n: i for n, i in imports.items() if i.get("holds_pointers")}
    lines += ["## Data imports", "",
              f"{len(imports)} data imports are linked to their PS1 addresses. {len(holding)} are",
              "declared with a type that holds pointers (4-byte guest addresses in the image,",
              "8-byte pointers in LP64 C): each must become `psx_ptr` data or a generated",
              "native table. Imports without a seen declaration are referenced only by",
              "overlay-local renames or through asm-era code.", ""]
    content = collections.Counter(i.get("content") for i in imports.values())
    pointer_words = {n: i for n, i in imports.items() if i.get("guest_pointer_words")}
    lines += ["Contents in the local image (bytes are read, never stored): " +
              ", ".join(f"{k} {v}" for k, v in content.most_common()) +
              f". {len(pointer_words)} hold words that look like guest pointers (0x8001xxxx-0x801Fxxxx):",
              ", ".join(f"`{n}` ({i['guest_pointer_words']}/{i['words']} words, `{i['type']}`)" for n, i in sorted(pointer_words.items())) + ".",
              "Zero-initialised imports are runtime state that only lives at its PS1 address because",
              "the port links it there; they can become native globals (native layout) unless",
              "something else addresses them by number (asm-era code, guest pointers in other data,",
              "overlay loads over the region).", ""]
    rows = [[f"`{n}`", f"`{i['type']}`", i.get("record") or "-", i.get("content", "?"), ", ".join(i.get("groups", [])), i.get("refs", 0)]
            for n, i in sorted(holding.items())]
    lines += ["Pointer-typed imports:", "",
              md_table(rows, ["import", "declared type", "struct", "image content", "used by", "refs"]) if rows else "(none)", ""]
    by_group = collections.Counter(g for i in imports.values() for g in i.get("groups", []))
    lines += ["Imports referenced per group: " + ", ".join(f"{g} {n}" for g, n in by_group.most_common()) + ".", "",
              f"Generated function tables: {len(audit['function_tables'])} (" +
              ", ".join(f"`{n}`[{c}]" for n, c in sorted(audit["function_tables"].items())) +
              "). These are `void*` arrays in C already and become native 8-byte tables for",
              "free; their C declarations must agree (no `int`/`u32` element types).", ""]

    # Work split.
    lines += ["## Work split for phase 1", ""]
    rows = []
    for group in groups:
        recs = [r for r in changed.values() if r["group"] == group]
        cls = collections.Counter(r["class"] for r in recs)
        def count(kind, pred=lambda f: True):
            return sum(1 for f in findings.get(kind, []) if group_of(f["file"]) == group and pred(f))
        rows.append([
            group,
            len(recs),
            f"{cls['ps1-overlay']}/{cls['runtime']}/{cls['runtime?']}",
            count("literal_size"),
            count("byte_offset", lambda f: f.get("status_long32", f["status"]).startswith("differs")),
            count("pointer_to_int") + count("int_to_pointer", lambda f: f["category"] not in ("literal scratchpad", "literal io")),
            sum(1 for i in imports.values() if group in i.get("groups", []) and i.get("holds_pointers")),
            sum(1 for e in audit["build64"].get("files", []) if e["group"] == group and not e["compiled"]),
        ])
    lines += [md_table(rows, ["group", "changed structs", "overlay/runtime/runtime?", "literal sizes", "offsets differ",
                              "ptr/int casts (non-literal)", "pointer imports", "64-bit TUs failing"]), ""]
    hot = collections.Counter()
    for kind in ("literal_size", "pointer_to_int"):
        for f in findings.get(kind, []):
            hot[f["file"]] += 1
    for f in findings.get("int_to_pointer", []):
        if not f["category"].startswith("literal scratchpad") and f["category"] != "literal io":
            hot[f["file"]] += 1
    for f in findings.get("byte_offset", []):
        if f.get("status_long32", f["status"]).startswith("differs"):
            hot[f["file"]] += 1
    rows = [[f"`{f}`", group_of(f), n] for f, n in hot.most_common(25)]
    lines += ["Files with the most sites (literal sizes, pointer-driven offsets, non-literal",
              "pointer/int casts):", "", md_table(rows, ["file", "group", "sites"]), ""]
    lines += [audit["work_split_text"], ""]
    # Hand-written progress notes at the end of the file survive regeneration.
    previous = path.read_text() if path.exists() else ""
    progress = previous.find("\n## Progress\n")
    if progress >= 0:
        lines += [previous[progress + 1:].rstrip("\n")]
    path.write_text("\n".join(lines) + "\n")


CLASSIFICATION = """## Classification rule

- **ps1-overlay**: an instance is an image data import, sits at a fixed
  guest or scratchpad address, or is reinterpreted from a byte buffer
  (disc/file data), or the struct is embedded by value in such a struct.
  Keep the PS1 layout: pointer members become `psx_ptr(T)`
  (`pc/src/psx_types.h`), `long` members become `s32`/`u32`, and
  `PSX_ASSERT_SIZE`/`PSX_ASSERT_OFFSET` pin the layout.
- **runtime**: only built by game code (static/global, stack, heap via
  `vs_main_allocHeap`). Let it grow to the native layout; replace literal
  sizes and offsets with `PSX_SIZE`/`PSX_OFFSET`. The PS1 heap lives in
  guest RAM (`vs_main_initHeap(0x8010C000, 0xF2000)`), so structs that grow
  also grow heap use: check the heap high-water mark after converting.
- **runtime?**: no instance seen in C (passed around by pointer, filled by
  asm-era code or the runtime). Review: default to runtime unless a reader
  finds it in file data.
- **alias view**: only ever a cast over another struct's memory (the
  decompiled BATTLE code views actor and state blocks through per-function
  structs). It follows the layout decision of the struct it aliases; where
  that struct goes native the view needs the same widening, so prefer
  replacing the view with the real type.
- **u_long retype**: changes only because `long`/`u_long` members grow;
  fixed for all of them by a 32-bit `u_long` (next section).
- **host native**: port runtime types in `pc/src`; native already.
- `ptr uses` counts accesses to a struct's pointer members: the sites where
  a PS1 layout needs `psx_get`/`psx_set`. They are listed in `audit.json`
  (`member_uses`), so a clang-based rewriter can apply them mechanically;
  on the PS1 the accessors expand to the original expression.
- The evidence is syntactic. A cast from `void*` says nothing about the
  storage and is not counted as overlay evidence; a cast from `u8*`/`u_long*`
  or pointer arithmetic is ("reinterpreted buffer"), which can be a false
  positive when the buffer is native. Reclassify by hand where a reader knows
  better and note it in the conversion commit.

## Recommendation

1. **Retype `u_long`/`ulong` to 32 bits under `ROOD_PC`** (PS1 SDK headers,
   PsyCross's `psx/types.h`) and `long` struct members to `s32`. This is
   one small change that keeps every GPU packet, OT entry, `VECTOR`, `MATRIX`
   and `DR_ENV` at its PS1 layout and removes most byte-offset differences.
2. **Keep PS1 layouts for everything that lives in guest memory and is not
   clearly runtime-only** (`ps1-overlay`, plus the scratchpad and sound
   driver state): retype pointer members to `psx_ptr(T)` and rewrite the
   member uses with a codemod. No sizes, offsets or alias views change, so
   the risky part (silent layout drift) is avoided.
3. **Let `runtime` structs go native** where the literal sizes and offsets
   are few (see the tables), fixing each with `PSX_SIZE`/`PSX_OFFSET`, and
   widen their alias views in the same change. Where a struct has many alias
   views (the BATTLE actor/state blocks), keeping the PS1 layout with
   `psx_ptr` is cheaper and safer: decide per family, not per struct.
4. **Sign-extending int-to-pointer casts** and pointer-to-`int` casts are
   fixed individually (`u32`/`uintptr_t`); the 64-bit build's
   `-Werror=pointer-to-int-cast`/`-Werror=int-to-pointer-cast` keeps them out.
5. **Data imports**: zero-initialised ones can move to native definitions;
   initialised pointer-bearing ones keep PS1 layout via `psx_ptr` (or a
   build-time generated native table, as `function_tables.c` already does
   for code pointers).
"""


WORK_SPLIT = """Proposed phase 1 tasks. Each runs in its own worktree; `make commit-check`
stays at "All files match" (every src/ change is a `psx_types.h` retype, an
accessor whose PS1 expansion is the original expression, or `ROOD_PC`); the
32-bit build stays green throughout because `psx_ptr(T)` is `T*` there. The
64-bit build does not link until task F, so a header retype may leave 64-bit
errors at use sites in other groups; those are exactly the next tasks' lists.

0. **Foundation (one agent, first, small)**: 32-bit `u_long`/`ulong` under
   `ROOD_PC` (PS1 SDK headers and PsyCross `psx/types.h`), include
   `psx_types.h` from the common header, `PSX_ASSERT_SIZE` for the GPU packet
   and GTE types, `pc_psx_bad_pointer` in the runtime. Rerun the audit.
1. **Codemod (one agent, alongside 0)**: `pc/tools/psx_ptr_codemod.py`: given
   a struct and member list, rewrite member reads to `psx_get`, writes to
   `psx_set` and copies to `psx_copy` using clang's AST (the audit's
   `member_uses` are the sites); verify with commit-check. Agents 2-5 use it.
2. **main (agent A)**: the sound driver (`25AC.c`, `sound.h`: FSoundChannel,
   FSoundChannelConfig, soundData_t at fixed addresses -> PS1 layout), heap and
   CD queue (`vs_main_HeapHeader`, `vs_main_CdQueueSlot`, `vs_main_disk_t`),
   `vs_scratch_t` (scratchpad -> PS1 layout; its uses in other groups follow),
   main.c casts.
3. **BATTLE actors (agent B1)**: the actor family `D_800F4538_t`,
   `D_800F45E0_t`, `struct vs_battle_actor`, `vs_battle_actor_dat2` and their
   alias views; files `3A1A0.c`, `44F14.c`, `40564.c`, the AI files (`6FC00.c`,
   `71618.c`, `74088.c`, `7CD68.c`, `771A8.c`) and `pc/src/battle_movement.c`,
   `navigation.c`, `actor_*.c`. Decide native vs PS1 layout once for the
   family (the many alias views favour PS1 layout + psx_ptr).
4. **BATTLE world and UI (agent B2)**: rooms and zones (`146C.c`:
   `_mpdRoomSection`, `_zoneContext_t`), shapes and sequences (`30DB0.c`),
   menus and text (`5BF94.c`, `2842C.c`, `49268.c`, `38C1C.c`, `4A0A8.c`),
   INITBTL, GIM, `pc/src/room_*.c`.
5. **Effects (agent B3)**: `src/EFFECT` (`rings.h`, `particleDispatch.h`
   literal allocations, trails, meshes, PLG sources), `pc/src/effect_particles.c`
   (its static asserts already fire on LP64), `pc/src/effects`.
6. **MENU and TITLE/ending (agent C, small)**: MAINMENU (allocates `0x1900` for
   an actor struct that is `0x1ba8` under LP64), MENU0-F, TITLE, ENDING.
7. **Build, imports and OT (agent F)**: link `--arch 64` (64-bit SDL3 and
   libraries, traps, generated tables), data imports (native definitions for
   zero-initialised ones, `psx_ptr` declarations for pointer-bearing ones),
   OT links as indices or a PC-side list (the `ot_mask_24`/`prim_macros`
   sites, `pc/src/gpu_hw.c`, `boot_sdk.c`), drop the 16 MB code placement,
   host casts in `boot_runtime.c`; then runs the phase 0 suite on 64-bit.

Order: 0 and 1 first (hours), then 2-7 in parallel; 7 finishes last because
it needs the others' errors gone to link. Rerun `build_boot.py --arch 64` and
this audit after each merge to watch the counts fall."""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pre-dir", default="pc/build/boot64", help="preprocessed .i files (build_boot.py --arch 64)")
    parser.add_argument("--imports", default="pc/build/boot/imports.ld", help="32-bit build's data imports")
    parser.add_argument("--function-tables", default="pc/build/boot/function_tables.c")
    parser.add_argument("--out-json", default="pc/build/lp64_audit/audit.json")
    parser.add_argument("--out-md", default="pc/docs/LP64_Audit.md")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--only", help="regex: analyse only matching sources (debugging)")
    args = parser.parse_args()

    work = ROOT / pathlib.Path(args.out_json).parent
    work.mkdir(parents=True, exist_ok=True)
    imports = read_imports(ROOT / args.imports)
    import_names = set(imports) | {n.split("__")[0] for n in imports}
    function_tables = read_function_tables(ROOT / args.function_tables)

    jobs = []
    for path in sorted((ROOT / args.pre_dir).glob("*.i")):
        source = first_marker(path)
        jobs.append((path.stem, source, str(path), str(path), TARGET_PS1, import_names))
    sdl_include = ROOT / "pc/third_party/SDL3/include"
    for name, source, p64, p32, target in runtime_units(work, sdl_include):
        jobs.append((name, source, p64, p32, target, import_names))
    if args.only:
        jobs = [j for j in jobs if re.search(args.only, j[1])]
    if not jobs:
        sys.exit(f"no inputs in {args.pre_dir}: run build_boot.py --arch 64 first")
    print(f"lp64_audit: {len(jobs)} units, {len(imports)} imports", flush=True)
    with multiprocessing.Pool(args.jobs) as pool:
        results = []
        for i, result in enumerate(pool.imap_unordered(analyse, jobs), 1):
            results.append(result)
            if i % 25 == 0:
                print(f"  {i}/{len(jobs)}", flush=True)
    results.sort(key=lambda r: r["source"])

    records = merge(results, imports, function_tables)
    findings = collections.defaultdict(list)
    seen = {}
    for r in results:
        for kind, items in r.get("findings", {}).items():
            for item in items:
                # Headers repeat across units: one finding per site, units listed.
                key = (kind, item["file"], item["line"],
                       json.dumps({k: v for k, v in item.items() if k != "unit"}, sort_keys=True, default=str))
                if key in seen:
                    seen[key]["units"].append(item["unit"])
                    continue
                item = {**{k: v for k, v in item.items() if k != "unit"}, "units": [item["unit"]]}
                seen[key] = item
                findings[kind].append(item)
    # Imports: declared type, pointer content, users.
    import_info = {}
    images = read_images()
    declared = {}
    refs = collections.defaultdict(collections.Counter)
    for r in results:
        for name, info in r.get("import_types", {}).items():
            declared.setdefault(name, info)
        for name, n in r.get("decl_refs", {}).items():
            refs[name][r["group"]] += n
    for name, address in sorted(imports.items()):
        base = name.split("__")[0]
        info = declared.get(name) or declared.get(base) or {}
        record = info.get("record")
        type_text = info.get("type")
        holds = False
        if type_text:
            holds = bool(POINTERISH.search(type_text) or POINTERISH.search(info.get("desugared", "")))
            if record and record in records and records[record]["pointer_driven"]:
                holds = True
        users = refs.get(name) or refs.get(base) or collections.Counter()
        size = None
        if record and record in records:
            size = records[record]["size_ps1"] * max(info.get("count", 1), 1)
        elif type_text:
            element, count = split_array(strip_type(info.get("desugared", type_text)))
            element_size = scalar_size_ps1(element)
            if element_size:
                size = element_size * count if count else None
        if size is None:  # up to the next import, capped
            later = [a for a in imports.values() if a > address]
            size = min(min(later) - address if later else 4, 0x1000)
        content = import_content(name, address, size, sorted(users), images)
        import_info[name] = {
            "size_ps1": size,
            **content,
            "address": f"{address:#x}",
            "type": type_text,
            "record": record,
            "holds_pointers": holds,
            "groups": sorted(users),
            "refs": sum(users.values()),
        }
    build64 = {}
    report = ROOT / args.pre_dir / "report.json"
    if report.exists():
        build64 = json.loads(report.read_text())
        for f in build64.get("files", []):
            f.pop("diagnostics", None)
    audit = {
        "totals": {"units": len(results), "clang_errors": sum(r.get("clang_errors", 0) for r in results)},
        "units": [{"source": r["source"], "group": r.get("group"), "clang_errors": r.get("clang_errors"),
                   "error": r.get("error")} for r in results],
        "records": records,
        "findings": findings,
        "imports": import_info,
        "function_tables": function_tables,
        "text": text_scan(),
        "build64": build64,
        "work_split_text": WORK_SPLIT,
    }
    out_json = ROOT / args.out_json
    out_json.write_text(json.dumps(audit, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o)))
    write_markdown(ROOT / args.out_md, audit)
    print(f"lp64_audit: wrote {out_json} and {args.out_md}")


if __name__ == "__main__":
    main()
