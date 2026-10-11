#!/usr/bin/env python3
"""Codemod: keep a struct's PS1 layout under LP64 by turning its pointer
members into psx_ptr(T) and rewriting every access through psx_types.h.

    pc/tools/memcap python3 pc/tools/psx_ptr_codemod.py --struct vs_main_HeapHeader --dry-run
    pc/tools/memcap python3 pc/tools/psx_ptr_codemod.py --struct vs_main_CdQueueSlot \\
        --struct soundData_t.sfxData --assert-size
    pc/tools/memcap python3 pc/tools/psx_ptr_codemod.py --class ps1-overlay --dry-run --report r.json

Memory: each unit's JSON AST is parsed in its own worker process (one unit
per process, --jobs 2 by default) and only the extracted edit lists come
back, so the peak is one AST per job: about 450 MB for the largest unit
(--class ps1-overlay, 118 units, ~20 s). Run it through pc/tools/memcap
anyway, like every clang-AST tool.

Inputs: pc/build/lp64_audit/audit.json (`lp64_audit.py`; names, classes and
the units that see each struct) and the preprocessed units of
`build_boot.py --arch 64` (pc/build/boot64/*.i, only to map units to sources
and to skip units that never name a field). Every candidate unit's original
source is parsed by clang with the 32-bit port's flags (i386, -DROOD_PC), so
source ranges are exact and macro expansions are visible in the AST.

Approach (why matched code keeps matching): the rewrite only ever inserts
psx_types.h macros whose PS1 and 32-bit expansion is the original expression,
parenthesised:

    declaration  T* f;          ->  psx_ptr(T) f;          (T* f)
    read         p->f           ->  psx_get(p->f)          ((p->f))
    to integer   (int)p->f      ->  (int)psx_raw(p->f)     ((unsigned long)(p->f))
    write        p->f = q       ->  psx_set(p->f, q)       ((p->f) = (q))
    copy         p->f = q->f    ->  psx_copy(p->f, q->f)   ((p->f) = (q->f))
    increment    p->f++, ++p->f ->  psx_postinc(p->f)      ((p->f)++)
                 (++p->f as a value: psx_add(p->f, 1))
    add          p->f += n      ->  psx_add(p->f, n)       ((p->f) += (n))
    integer      p->f = (T*)v   ->  psx_set_raw(p->f, T*, v)  ((p->f) = (T*)(v))

Extra parentheses and a same-width integer cast are invisible to cc1, so the
PS1 objects (`make commit-check`) and the 32-bit port build are unchanged;
only the LP64 build (ROOD_PC_LP64) sees the 4-byte guest-address type. A
declaring header that cannot see psx_types.h yet gets
`#include "pc/src/psx_types.h"` (found through -I. by every build).
psx_set and psx_copy expand to an assignment whose LP64 value is the raw
address, so they are only used where the assignment's value is discarded.

Idempotent: the parse force-includes a shim that redefines the accessors as
`__builtin_choose_expr(1, ...)`, so already converted sites show up in the AST
as ChooseExpr and are skipped; converted declarations are recognised by their
`psx_ptr(` spelling. Running twice gives no edits the second time.

Sites that cannot be rewritten safely are reported, never guessed:

    macro            the access or the assignment is spelled in a macro body
    address-of       &p->f (the address of a psx_ptr is not a T**)
    arithmetic       p->f--, p->f -= n (no accessor expands to the original)
    value-used       the value of `p->f = q` is used (LP64 value is the raw u32)
    int-to-pointer   p->f = (T*)integer whose cast is not spelled `(T*)expr`
    alias-cast       a cast between a pointer to the struct and a pointer to
                     another struct (an alias view: its accesses are not this
                     field's, convert the view too)
    initializer      a brace initializer of the struct (static data with
                     pointers needs psx_ptr-aware initialisation)
    array-whole      an array-of-pointers member used without a subscript
    declaration      a declarator that is not `T* name` (function pointers,
                     several declarators in one declaration, qualifiers)
    conflict         two units want different edits at one place (headers)
    other            any other context (the AST kind is named)

Sites in code the port does not compile (asm-only functions, #ifndef ROOD_PC
branches) are not seen; they stay as they are, which is correct on the PS1.
--dry-run prints a unified diff and changes nothing; --report writes the edit
and unsafe-site lists as JSON. Needs only clang.
"""

import argparse
import collections
import difflib
import json
import multiprocessing
import os
import pathlib
import re
import resource
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORK = ROOT / "pc/build/psx_ptr_codemod"
PSX_TYPES = "pc/src/psx_types.h"
TARGET = "i386-unknown-linux-gnu"  # the 32-bit port: psx_ptr(T) is T*

CLANG_FLAGS = [
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

# Accessors as markers the AST keeps: already converted sites are skipped.
SHIM = f"""#include "{PSX_TYPES}"
#undef psx_get
#undef psx_set
#undef psx_copy
#undef psx_raw
#define psx_get(f) __builtin_choose_expr(1, (f), 0)
#define psx_set(f, p) __builtin_choose_expr(1, ((f) = (p)), 0)
#define psx_copy(d, s) __builtin_choose_expr(1, ((d) = (s)), 0)
#define psx_raw(f) __builtin_choose_expr(1, ((unsigned long)(f)), 0)
#undef psx_postinc
#undef psx_add
#undef psx_set_raw
#define psx_postinc(f) __builtin_choose_expr(1, ((f)++), 0)
#define psx_add(f, n) __builtin_choose_expr(1, ((f) += (n)), 0)
#define psx_set_raw(f, T, v) __builtin_choose_expr(1, ((f) = (T)(v)), 0)
"""

ACCESSOR = re.compile(rb"psx_(get|set|copy|raw|postinc|add|set_raw)\s*\(")


def build_flags():
    """The C preprocessor flags build_boot.py uses for src/ and pc/src units."""
    menubg = ROOT / "build/assets/TITLE/TITLE.PRG/_titleScreenBg.rleRgb555.img.bin"
    return [
        "-nostdinc",
        "-Ipc/include",
        "-Iinclude/psx",
        "-Isrc/include",
        "-I.",
        "-D_LANGUAGE_C",
        "-DOBJDIFF",
        "-DROOD_PC",
        "-DROOD_PC_BOOT_DIAGNOSTIC",
        *[f"-D{n}=pc_psx_{n}" for n in ["open", "read", "write", "close", "rename"]],
        f"-DMENUBGSZ={menubg.stat().st_size if menubg.exists() else 0}",
    ]


# --------------------------------------------------------------------------
# Clang JSON: locations omit "file" when it repeats the previous one printed,
# so resolve every location in document order.

def resolve_files(ast):
    current = None
    stack = [ast]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            if "offset" in node:
                if "file" in node:
                    current = os.path.normpath(node["file"])
                node["_file"] = current
            # Document order: push values reversed.
            stack.extend(reversed([v for k, v in node.items() if not k.startswith("_")]))
        elif isinstance(node, list):
            stack.extend(reversed(node))


def link_parents(ast):
    stack = [(ast, None, 0)]
    while stack:
        node, parent, index = stack.pop()
        node["_parent"] = parent
        node["_index"] = index
        for i, child in enumerate(node.get("inner", ())):
            if isinstance(child, dict):
                stack.append((child, node, i))


def kids(node):
    return [k for k in node.get("inner", ()) if isinstance(k, dict)]


def parent(node, skip=("ParenExpr",)):
    """Nearest ancestor whose kind is not in skip, and the child under it."""
    child, up = node, node.get("_parent")
    while up is not None and up.get("kind") in skip:
        child, up = up, up.get("_parent")
    return up, child


def strip(node, kinds=("ParenExpr",), casts=()):
    while node is not None:
        kind = node.get("kind")
        if kind in kinds or (kind == "ImplicitCastExpr" and node.get("castKind") in casts):
            inner = kids(node)
            if not inner:
                return node
            node = inner[0]
            continue
        return node
    return node


class Source:
    """Text of the files a unit touches, for spelling checks."""

    def __init__(self):
        self.cache = {}

    def text(self, path):
        if path not in self.cache:
            try:
                self.cache[path] = (ROOT / path).read_bytes()
            except OSError:
                self.cache[path] = b""
        return self.cache[path]


def editable(path):
    if not path or os.path.isabs(path) or path.startswith(("..", "pc/build/", "build/", "<")):
        return False
    return path.startswith(("src/", "pc/src/", "include/"))


def file_pos(loc, src, end=False):
    """(file, byte offset) of a range bound spelled in a file; None in a macro.

    A location inside the argument of one of our accessors (an already
    converted site) is spelled at the call site, so it stays editable."""
    if not loc:
        return None
    if "spellingLoc" in loc:
        exp, spell = loc.get("expansionLoc", {}), loc["spellingLoc"]
        if not exp.get("isMacroArgExpansion"):
            # NULL at a range bound (`f = c ? p : NULL`): its name. Other
            # macros may expand to several tokens of which the node is a part.
            text, at = src.text(exp.get("_file")), exp.get("offset", 0)
            if exp.get("tokLen") == 4 and text[at:at + 4] == b"NULL":
                return exp.get("_file"), at + (4 if end else 0)
            return None
        if not ACCESSOR.match(src.text(exp.get("_file")), exp.get("offset", 0)):
            return None
        loc = spell
    path = loc.get("_file")
    if path is None or "offset" not in loc:
        return None
    return path, loc["offset"] + (loc.get("tokLen", 0) if end else 0)


def matching_paren(text, at):
    """Offset of the ')' closing the first '(' at or after `at`."""
    start = text.find(b"(", at)
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(text)):
        c = text[i:i + 1]
        if c == b"(":
            depth += 1
        elif c == b")":
            depth -= 1
            if depth == 0:
                return i
    return None


def node_range(node, src):
    r = node.get("range", {})
    b, e = file_pos(r.get("begin"), src), file_pos(r.get("end"), src, end=True)
    if not b or not e or b[0] != e[0] or b[1] > e[1]:
        return None
    return b[0], b[1], e[1]


def object_macro(node, src):
    """Range of a node that is exactly one object-like macro (NULL): its name."""
    r = node.get("range", {})
    b, e = r.get("begin", {}), r.get("end", {})
    bx, ex = b.get("expansionLoc"), e.get("expansionLoc")
    if not bx or not ex or bx.get("isMacroArgExpansion") or bx.get("offset") != ex.get("offset"):
        return None
    path, start = bx.get("_file"), bx.get("offset", 0)
    end = start + bx.get("tokLen", 0)
    if not path or not re.fullmatch(rb"[A-Za-z_]\w*", src.text(path)[start:end]):
        return None
    if ACCESSOR.match(src.text(path), start):
        # An already converted accessor call (`q = psx_get(x)` as a right-hand
        # side) is reported at the accessor's name: take the whole call, or
        # `psx_set(f, psx_get)(x)` results.
        close = matching_paren(src.text(path), end)
        return None if close is None else (path, start, close + 1)
    if re.match(rb"\s*\(", src.text(path)[end:end + 16]):
        return None  # another function-like macro: not one token
    return path, start, end


def line_of(src, path, offset):
    return src.text(path).count(b"\n", 0, offset) + 1


def loc_of(node, src):
    """file:line for reports, also for macro locations (expansion site)."""
    r = node.get("range", {}).get("begin") or node.get("loc") or {}
    if "expansionLoc" in r:
        r = r["expansionLoc"]
    path = r.get("_file")
    if not path:
        return "?", 0
    return path, line_of(src, path, r.get("offset", 0))


# --------------------------------------------------------------------------
# One unit.

POINTER = re.compile(r"\*\s*(const\s*|volatile\s*)*$")
POINTER_ARRAY = re.compile(r"\*\s*(\[\d*\])+$")
INTEGER_TYPE = re.compile(
    r"^(const )?(volatile )?(unsigned |signed )?(char|short|int|long|long long|u_char|u_short|u_int|u_long|"
    r"s8|u8|s16|u16|s32|u32|ulong|uint|size_t|intptr_t|uintptr_t)( int)?$")


def field_kind(field):
    t = field.get("type", {})
    texts = [t.get("qualType", ""), t.get("desugaredQualType", "")]
    if any("(*" in x for x in texts):
        return "function"
    if any(POINTER_ARRAY.search(x) for x in texts):
        return "array"
    if any(POINTER.search(x) for x in texts):
        return "pointer"
    return None


class Unit:
    def __init__(self, source, targets):
        self.source = source
        self.targets = targets  # {record name: set(fields) or None (all pointers)}
        self.src = Source()
        self.edits = []
        self.unsafe = []
        self.skipped = collections.Counter()
        self.fields = {}  # FieldDecl id -> (record, field, kind)
        self.records = {}  # RecordDecl id -> record name (as targeted)
        self.handled = set()

    def report(self, category, node, record, field, detail=""):
        path, line = loc_of(node, self.src)
        text = self.src.text(path).split(b"\n")[line - 1].decode(errors="replace").strip() if line else ""
        self.unsafe.append({"category": category, "file": path, "line": line, "struct": record,
                            "field": field, "detail": detail, "text": text[:160]})

    # -- declarations ------------------------------------------------------

    def find_records(self, ast):
        names = collections.defaultdict(set)  # RecordDecl id -> names
        decls = {}

        def walk(node):
            kind = node.get("kind")
            if kind == "RecordDecl" and node.get("completeDefinition"):
                decls[node["id"]] = node
                if node.get("name"):
                    tag = node["name"]
                    names[node["id"]].update({tag, f"{node.get('tagUsed', 'struct')} {tag}"})
            elif kind == "TypedefDecl":
                for owned in iter_dicts(node):
                    target = owned.get("ownedTagDecl") or owned.get("decl")
                    if isinstance(target, dict) and target.get("kind") == "RecordDecl":
                        names[target["id"]].add(node["name"])
                        decls.setdefault(target["id"] + "#typedef", node)
                        break
            for k in kids(node):
                if k.get("kind") in ("RecordDecl", "TypedefDecl", "FieldDecl", "LinkageSpecDecl"):
                    walk(k)

        for top in kids(ast):
            walk(top)
        self.all_record_names = set().union(*names.values()) if names else set()
        for rid, node in decls.items():
            if rid.endswith("#typedef"):
                continue
            chosen = next((n for n in sorted(names[rid]) if n in self.targets), None)
            if not chosen:
                continue
            self.records[rid] = chosen
            wanted = self.targets[chosen]
            siblings = collections.Counter(
                f.get("range", {}).get("begin", {}).get("offset") for f in kids(node) if f.get("kind") == "FieldDecl")
            for f in kids(node):
                if f.get("kind") != "FieldDecl":
                    continue
                kind = field_kind(f)
                if wanted is not None and f.get("name") not in wanted:
                    continue
                if kind is None:
                    if wanted is not None:
                        self.report("declaration", f, chosen, f.get("name"), "not a pointer member")
                    continue
                self.fields[f["id"]] = (chosen, f["name"], kind)
                self.declaration(f, chosen, kind, siblings)
            if wanted is not None:
                missing = wanted - {f.get("name") for f in kids(node) if f.get("kind") == "FieldDecl"}
                for name in sorted(missing):
                    self.report("declaration", node, chosen, name, "no such member")
            self.decl_sites.append((chosen, node, decls.get(rid + "#typedef")))

    def declaration(self, f, record, kind, siblings):
        r = f.get("range", {})
        begin = r.get("begin", {})
        if "spellingLoc" in begin:
            exp = begin.get("expansionLoc", {})
            if self.src.text(exp.get("_file")).startswith(b"psx_ptr(", exp.get("offset", 0)):
                return  # converted already
            self.report("macro", f, record, f["name"], "declaration spelled in a macro")
            return
        start = file_pos(begin, self.src)
        name = file_pos(f.get("loc"), self.src)
        if not start or not name or start[0] != name[0]:
            self.report("declaration", f, record, f["name"], "no file range")
            return
        if siblings[begin.get("offset")] > 1:
            self.report("declaration", f, record, f["name"], "several declarators share one declaration")
            return
        if kind == "function":
            self.report("declaration", f, record, f["name"], "function pointer: give it a typedef first")
            return
        prefix = self.src.text(start[0])[start[1]:name[1]].decode()
        stripped = prefix.rstrip()
        if not stripped.endswith("*") or "(" in stripped or re.search(r"\b(const|volatile)\s*$", stripped):
            self.report("declaration", f, record, f["name"], f"declarator `{prefix.strip()}`")
            return
        pointee = stripped[:-1].rstrip()
        self.edits.append(("decl", start[0], start[1], name[1], f"psx_ptr({pointee}) "))

    # -- uses --------------------------------------------------------------

    def target_lvalue(self, node):
        """(record, field, member node) if node is an lvalue of a target field."""
        node = strip(node)
        if node is None:
            return None
        if node.get("kind") == "MemberExpr":
            info = self.fields.get(node.get("referencedMemberDecl"))
            if info and info[2] == "pointer":
                return info[0], info[1], node
        if node.get("kind") == "ArraySubscriptExpr":
            base = strip(kids(node)[0], casts=("ArrayToPointerDecay",)) if kids(node) else None
            if base is not None and base.get("kind") == "MemberExpr":
                info = self.fields.get(base.get("referencedMemberDecl"))
                if info and info[2] == "array":
                    return info[0], info[1], base
        return None

    def in_accessor(self, node, allowed=("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr")):
        up = node.get("_parent")
        while up is not None and up.get("kind") in allowed:
            up = up.get("_parent")
        return up is not None and up.get("kind") == "ChooseExpr"

    def statement_context(self, node):
        """True when the value of expression node is discarded."""
        up, child = parent(node)
        if up is None:
            return False
        kind, index = up.get("kind"), child.get("_index")
        if kind in ("CompoundStmt", "LabelStmt", "CaseStmt", "DefaultStmt"):
            return kind != "CaseStmt" or index == len(up.get("inner", ())) - 1
        if kind == "BinaryOperator" and up.get("opcode") == ",":
            return index == 0 or self.statement_context(up)
        cond = {"IfStmt": int(bool(up.get("hasInit"))) + int(bool(up.get("hasVar"))),
                "WhileStmt": int(bool(up.get("hasVar"))), "DoStmt": 1, "ForStmt": 2,
                "SwitchStmt": None}.get(kind, -2)
        if cond == -2 or cond is None:
            return False
        return index != cond

    def assignments(self, node):
        if node.get("kind") == "BinaryOperator" and node.get("opcode") == "=":
            inner = kids(node)
            lhs = self.target_lvalue(inner[0]) if inner else None
            if lhs:
                self.assignment(node, inner[0], inner[1], lhs)
        elif node.get("kind") in ("CompoundAssignOperator", "UnaryOperator"):
            op = node.get("opcode")
            inner = kids(node)
            if inner and (node.get("kind") == "CompoundAssignOperator" or op in ("++", "--")):
                lhs = self.target_lvalue(inner[0])
                if lhs:
                    self.handled.add(id(strip(inner[0])))
                    if not self.in_accessor(node):
                        self.arithmetic(node, inner, op, lhs)

    def arithmetic(self, node, inner, op, lhs):
        """f++ / ++f -> psx_postinc(f) or psx_add(f, 1); f += n -> psx_add(f, n).

        Both expand to the original operator on the PS1 and return the
        operator's value, so the context does not matter. Decrements and
        -= stay reported: psx_add(f, -(n)) is not the original expression."""
        record, field, _ = lhs
        whole = node_range(node, self.src)
        lr = node_range(inner[0], self.src)
        if not whole or not lr or whole[0] != lr[0]:
            self.report("macro", node, record, field, f"`{op}` spelled in a macro")
            return
        text = self.src.text(lr[0])
        if op == "++":
            if node.get("isPostfix"):
                if not re.fullmatch(rb"\s*\+\+", text[lr[2]:whole[2]]):
                    self.report("other", node, record, field, "unexpected `++` spelling")
                    return
                self.edits.append(("postinc", lr[0], lr[1], lr[2], whole[2]))
            else:
                if not re.fullmatch(rb"\+\+\s*", text[whole[1]:lr[1]]):
                    self.report("other", node, record, field, "unexpected `++` spelling")
                    return
                # psx_postinc keeps the old value: ++f with its value used
                # needs the new one (psx_add(f, 1)).
                kind = "preinc_stmt" if self.statement_context(node) else "preinc"
                self.edits.append((kind, lr[0], whole[1], lr[1], lr[2]))
            return
        if op == "+=" and len(inner) > 1:
            rr = node_range(inner[1], self.src) or object_macro(inner[1], self.src)
            if not rr or rr[0] != lr[0]:
                self.report("macro", node, record, field, "operand spelled in a macro")
                return
            if not re.fullmatch(rb"\s*\+=\s*", text[lr[2]:rr[1]]):
                self.report("other", node, record, field, "unexpected `+=` spelling")
                return
            self.edits.append(("add", lr[0], lr[1], lr[2], rr[1], rr[2]))
            return
        self.report("arithmetic", node, record, field, f"`{op}` on the field")

    def assignment(self, node, lhs_node, rhs_node, lhs):
        record, field, _ = lhs
        lvalue = strip(lhs_node)
        self.handled.add(id(lvalue))
        core = strip(rhs_node, casts=("LValueToRValue", "NoOp"))
        rhs = self.target_lvalue(core)
        same = rhs and core.get("type", {}).get("qualType") == lvalue.get("type", {}).get("qualType")
        if same:
            self.handled.add(id(strip(core)))
        if self.in_accessor(node, allowed=("ParenExpr",)):
            return  # psx_set/psx_copy already
        bare = strip(rhs_node, casts=("NoOp",))
        if bare.get("kind") == "CStyleCastExpr" and bare.get("castKind") == "IntegralToPointer":
            self.set_raw(node, lhs_node, bare, record, field)
            return
        if not self.statement_context(node):
            self.report("value-used", node, record, field, "the assignment's value is used")
            return
        lr, rr = node_range(lhs_node, self.src), node_range(rhs_node, self.src) or object_macro(rhs_node, self.src)
        if not lr or not rr or lr[0] != rr[0]:
            self.report("macro", node, record, field, "assignment spelled in a macro")
            return
        gap = self.src.text(lr[0])[lr[2]:rr[1]]
        if not re.fullmatch(rb"\s*=\s*", gap):
            self.report("other", node, record, field, f"unexpected text between operands: {gap[:40]!r}")
            return
        self.edits.append(("copy" if same else "set", lr[0], lr[1], lr[2], rr[1], rr[2]))

    def set_raw(self, node, lhs_node, cast, record, field):
        """f = (T*)integer -> psx_set_raw(f, T*, integer): on the PS1
        ((f) = (T*)(integer)); its value is the stored pointer, so the
        context does not matter."""
        inner = kids(cast)
        lr = node_range(lhs_node, self.src)
        cr = node_range(cast, self.src)
        sr = node_range(inner[0], self.src) if inner else None
        if not sr and inner:
            sr = object_macro(inner[0], self.src)
        if not lr or not cr or not sr or not (lr[0] == cr[0] == sr[0]):
            self.report("macro", node, record, field, "integer store spelled in a macro")
            return
        text = self.src.text(lr[0])
        if not re.fullmatch(rb"\s*=\s*", text[lr[2]:cr[1]]):
            self.report("other", node, record, field, "unexpected text before the cast")
            return
        m = re.fullmatch(rb"\(\s*([^()]*?)\s*\)\s*", text[cr[1]:sr[1]])
        if not m or sr[2] != cr[2]:
            self.report("int-to-pointer", node, record, field, "cast not spelled `(T*)expr`")
            return
        self.edits.append(("set_raw", lr[0], lr[1], lr[2], cr[1], sr[1], sr[2], m.group(1).decode()))

    def member(self, node):
        if node.get("kind") != "MemberExpr":
            return
        info = self.fields.get(node.get("referencedMemberDecl"))
        if not info:
            return
        record, field, kind = info
        lvalue = node
        if kind == "array":
            up, _ = parent(node)
            sub = None
            if up is not None and up.get("kind") == "ImplicitCastExpr" and up.get("castKind") == "ArrayToPointerDecay":
                sub, child = parent(up)
                if sub is None or sub.get("kind") != "ArraySubscriptExpr" or child.get("_index") != 0:
                    sub = None
            if sub is None:
                if not self.unevaluated(node):
                    self.report("array-whole", node, record, field, "array of pointers used without a subscript")
                return
            lvalue = sub
        if id(lvalue) in self.handled:
            return
        up, _ = parent(lvalue)
        kind_up = up.get("kind") if up else None
        if kind_up == "ChooseExpr":
            return
        if kind_up == "ImplicitCastExpr" and up.get("castKind") == "LValueToRValue":
            if self.in_accessor(lvalue):
                return
            outer, _ = parent(up)
            raw = (outer is not None and outer.get("kind") == "CStyleCastExpr"
                   and outer.get("castKind") == "PointerToIntegral")
            r = node_range(lvalue, self.src)
            if not r:
                self.report("macro", lvalue, record, field, "read spelled in a macro")
                return
            self.edits.append(("raw" if raw else "get", r[0], r[1], r[2]))
            return
        if kind_up == "UnaryOperator" and up.get("opcode") == "&":
            if not self.in_accessor(up):
                self.report("address-of", up, record, field, "address of a pointer member")
            return
        if self.unevaluated(lvalue):
            self.skipped["unevaluated"] += 1
            return
        self.report("other", lvalue, record, field, f"used under {kind_up}")

    def unevaluated(self, node):
        up = node.get("_parent")
        while up is not None:
            if up.get("kind") == "UnaryExprOrTypeTraitExpr":
                return True
            if up.get("kind") in ("CompoundStmt", "FunctionDecl"):
                return False
            up = up.get("_parent")
        return False

    def casts(self, node):
        if node.get("kind") != "CStyleCastExpr":
            return
        inner = kids(node)
        if not inner:
            return
        to = self.pointee_record(node)
        frm = self.pointee_record(strip(inner[0], casts=("LValueToRValue", "NoOp", "ArrayToPointerDecay")))
        if to and frm and to != frm and (to in self.target_names or frm in self.target_names):
            target = to if to in self.target_names else frm
            self.report("alias-cast", node, target, None, f"cast {frm} * -> {to} *")

    def pointee_record(self, node):
        """The record a pointer-typed expression points to, by any of its names."""
        t = node.get("type", {})
        for text in (t.get("qualType", ""), t.get("desugaredQualType", "")):
            m = re.fullmatch(r"(?:const |volatile )*((?:struct |union )?[A-Za-z_]\w*) \*", text)
            if m and m.group(1) in self.target_names:
                return m.group(1)
        for text in (t.get("qualType", ""), t.get("desugaredQualType", "")):
            m = re.fullmatch(r"(?:const |volatile )*((?:struct |union )?[A-Za-z_]\w*) \*", text)
            if m and m.group(1) in self.all_record_names:
                return m.group(1)
        return None

    def initializers(self, node):
        if node.get("kind") != "InitListExpr":
            return
        t = node.get("type", {})
        for text in (t.get("qualType", ""), t.get("desugaredQualType", "")):
            base = re.sub(r"\s*\[\d*\]$", "", text)
            if base in self.target_names and not (node.get("_parent") or {}).get("kind") == "InitListExpr":
                self.report("initializer", node, base, None, "brace initializer")
                return

    def analyse(self, ast):
        resolve_files(ast)
        link_parents(ast)
        self.decl_sites = []
        self.find_records(ast)
        # A typedef target is also known by the record's tag ("struct X").
        self.target_names = set(self.targets) | set(self.records.values())
        if not self.fields and not self.decl_sites:
            return
        nodes = []
        stack = [ast]
        while stack:
            node = stack.pop()
            nodes.append(node)
            stack.extend(reversed(kids(node)))
        for node in nodes:
            self.assignments(node)
        for node in nodes:
            self.member(node)
            self.casts(node)
            self.initializers(node)


def iter_dicts(node):
    stack = [node]
    while stack:
        n = stack.pop()
        if isinstance(n, dict):
            yield n
            # Skip our "_parent" links: following them would walk the whole
            # tree again (and again: an unbounded loop).
            stack.extend(v for k, v in n.items() if not k.startswith("_"))
        elif isinstance(n, list):
            stack.extend(n)


def analyse_unit(job):
    source, targets, flags, shim = job
    result = subprocess.run(
        ["clang", "-target", TARGET, *CLANG_FLAGS, *flags, "-include", shim, "-Xclang", "-ast-dump=json", source],
        cwd=ROOT, capture_output=True)
    errors = result.stderr.decode(errors="replace").count(" error: ")
    try:
        ast = json.loads(result.stdout)
    except json.JSONDecodeError:
        return {"source": source, "error": "no AST", "clang_errors": errors}
    del result
    unit = Unit(source, {k: (set(v) if v is not None else None) for k, v in targets.items()})
    unit.analyse(ast)
    decls = []
    for record, node, typedef in unit.decl_sites:
        anchor = typedef or node
        end = file_pos(anchor.get("range", {}).get("end"), unit.src, end=True)
        decls.append({"record": record, "file": end[0] if end else None, "end": end[1] if end else None})
    del ast  # only the small extracted lists leave the worker
    return {"source": source, "clang_errors": errors, "edits": unit.edits, "unsafe": unit.unsafe,
            "skipped": dict(unit.skipped), "decls": decls,
            "fields": sorted({(r, f, k) for r, f, k in unit.fields.values()})}


# --------------------------------------------------------------------------
# Applying edits.

def primitive_ops(edit):
    """(pos, rank, sub, delete_to, text): closes, then replacements, then opens."""
    kind = edit[0]
    if kind == "decl":
        _, _, start, end, text = edit
        return [(start, 1, 0, end, text)]
    if kind == "insert":
        _, _, pos, text = edit
        return [(pos, 1, 0, pos, text)]
    if kind in ("get", "raw"):
        _, _, b, e = edit
        return [(b, 2, -e, b, f"psx_{kind}("), (e, 0, -b, e, ")")]
    if kind == "postinc":  # f++ -> psx_postinc(f)
        _, _, lb, le, oe = edit
        return [(lb, 2, -oe, lb, "psx_postinc("), (le, 0, -lb, oe, ")")]
    if kind in ("preinc_stmt", "preinc"):  # ++f -> psx_postinc(f) / psx_add(f, 1)
        _, _, ob, lb, le = edit
        name, close = ("psx_postinc(", ")") if kind == "preinc_stmt" else ("psx_add(", ", 1)")
        return [(ob, 2, -le, lb, name), (le, 0, -ob, le, close)]
    if kind == "set_raw":  # f = (T)v -> psx_set_raw(f, T, v)
        _, _, lb, le, cb, sb, se, cast = edit
        return [(lb, 2, -se, lb, "psx_set_raw("), (le, 1, 0, sb, f", {cast}, "), (se, 0, -lb, se, ")")]
    _, _, lb, le, rb, re_ = edit
    return [(lb, 2, -re_, lb, f"psx_{kind}("), (le, 1, 0, rb, ", "), (re_, 0, -lb, re_, ")")]


def apply_edits(text, edits):
    ops = sorted(op for e in edits for op in primitive_ops(e))
    out, cursor = [], 0
    for pos, _, _, delete_to, insert in ops:
        if pos < cursor:
            raise ValueError(f"overlapping edits at byte {pos}")
        out.append(text[cursor:pos])
        out.append(insert.encode())
        cursor = delete_to
    out.append(text[cursor:])
    return b"".join(out)


def edit_key(edit):
    """Edits at one lvalue (or declaration) must agree across units."""
    kind, path = edit[0], edit[1]
    if kind in ("decl", "insert"):
        return (path, "decl", edit[2])
    return (path, "site", edit[2], edit[3])


def include_edit(decl_file, decl_offset):
    """#include "pc/src/psx_types.h" after the last #include above the struct."""
    text = (ROOT / decl_file).read_bytes()
    pos = 0
    for m in re.finditer(rb'^[ \t]*#[ \t]*(include[^\n]*|pragma once[^\n]*)\n', text[:decl_offset], re.M):
        pos = m.end()
    return ("insert", decl_file, pos, f'#include "{PSX_TYPES}"\n')


# --------------------------------------------------------------------------

def unit_sources(pre_dir):
    out = {}
    for path in sorted((ROOT / pre_dir).glob("*.i")):
        with open(path, "rb") as f:
            for raw in f:
                m = re.match(rb'^#\s*\d+\s+"([^"<]+)"', raw)
                if m:
                    out[m.group(1).decode()] = path
                    break
    return out


def parse_targets(args, records):
    targets = {}
    for spec in args.struct or ():
        name, _, field = spec.partition(".")
        if name not in records and f"struct {name}" in records:
            name = f"struct {name}"
        if name not in records:
            sys.exit(f"psx_ptr_codemod: {name} is not in the audit (pointer-free or unknown)")
        if field:
            current = targets.get(name, set())
            if current is not None:
                current.add(field)
            targets[name] = current
        else:
            targets[name] = None
    for cls in args.cls or ():
        for name, rec in records.items():
            if rec.get("class") == cls and rec.get("pointer_members"):
                targets.setdefault(name, None)
    # Lookup aliases: "struct X" is also matched by its tag, typedefs by name.
    out = {}
    for name, fields in targets.items():
        out[name] = fields
        if name.startswith(("struct ", "union ")):
            out.setdefault(name.split(" ", 1)[1], fields)
    return {k: (sorted(v) if v is not None else None) for k, v in out.items()}, targets


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--struct", action="append", metavar="NAME[.field]",
                        help="struct (audit name, typedef or tag) and optionally one member; repeatable")
    parser.add_argument("--class", dest="cls", action="append", metavar="CLASS",
                        help="every pointer-bearing struct of an audit class, e.g. ps1-overlay")
    parser.add_argument("--dry-run", action="store_true", help="print a unified diff, change nothing")
    parser.add_argument("--assert-size", action="store_true",
                        help="add PSX_ASSERT_SIZE(T, PS1 size) after structs whose pointers are all converted")
    parser.add_argument("--files", help="regex: only edit files whose path matches")
    parser.add_argument("--report", help="write edits and unsafe sites as JSON")
    parser.add_argument("--audit", default="pc/build/lp64_audit/audit.json")
    parser.add_argument("--pre-dir", default="pc/build/boot64", help="build_boot.py --arch 64 units")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if not args.struct and not args.cls:
        parser.error("give --struct or --class")

    audit = json.loads((ROOT / args.audit).read_text())
    records = audit["records"]
    lookup, targets = parse_targets(args, records)
    sources = unit_sources(args.pre_dir)

    # Candidate units: the audit saw the struct there and the unit names a field.
    fields = set()
    for name, wanted in targets.items():
        members = [m["name"] for m in records[name]["members"] if m["reason"] == "pointer"]
        fields.update(wanted if wanted is not None else members)
    token = re.compile(rb"(\.|->)\s*(" + b"|".join(re.escape(f.encode()) for f in sorted(fields)) + rb")\b")
    units = set()
    for name in targets:
        units.update(records[name]["units"])
    jobs = []
    WORK.mkdir(parents=True, exist_ok=True)
    shim = WORK / "shim.h"
    shim.write_text(SHIM)
    flags = build_flags()
    for source in sorted(units):
        pre = sources.get(source)
        if pre is None:
            continue  # host runtime units: native types, not PS1 overlays
        if not token.search(pre.read_bytes()):
            continue
        jobs.append((source, lookup, flags, str(shim)))
    print(f"psx_ptr_codemod: {len(targets)} structs, {len(jobs)} units to parse", file=sys.stderr)

    # One unit per worker process: each AST (tens of MB of JSON, a few GB as
    # Python objects for the largest BATTLE units) is freed when its worker
    # exits. Run under pc/tools/memcap.
    with multiprocessing.Pool(max(1, args.jobs), maxtasksperchild=1) as pool:
        results = list(pool.imap_unordered(analyse_unit, jobs))
    results.sort(key=lambda r: r["source"])

    # Merge: one edit per place; units that disagree are conflicts.
    by_key, conflicts = {}, set()
    unsafe, seen_unsafe = [], set()
    converted = collections.defaultdict(set)
    decl_ends = {}
    skipped = collections.Counter()
    for r in results:
        if "error" in r:
            print(f"psx_ptr_codemod: {r['source']}: {r['error']}", file=sys.stderr)
            continue
        skipped.update(r["skipped"])
        for record, field, kind in r["fields"]:
            converted[record].add(field)
        for d in r["decls"]:
            if d["file"]:
                decl_ends.setdefault(d["record"], (d["file"], d["end"]))
        for e in r["edits"]:
            e = tuple(e)
            if not editable(e[1]):
                continue
            key = edit_key(e)
            if key in by_key and by_key[key] != e:
                conflicts.add(key)
            by_key.setdefault(key, e)
        for u in r["unsafe"]:
            k = (u["category"], u["file"], u["line"], u["struct"], u["field"], u["detail"])
            if k not in seen_unsafe:
                seen_unsafe.add(k)
                unsafe.append(u)
    for key in conflicts:
        e = by_key.pop(key)
        unsafe.append({"category": "conflict", "file": e[1], "line": 0, "struct": None, "field": None,
                       "detail": f"units disagree on {e}", "text": ""})
    edits = list(by_key.values())

    # psx_types.h must be visible where a converted declaration lives: the
    # declaring file includes it itself (guarded, so a second include is free).
    decl_files = {}
    for e in edits:
        if e[0] == "decl":
            decl_files[e[1]] = min(e[2], decl_files.get(e[1], e[2]))
    for path, offset in sorted(decl_files.items()):
        if PSX_TYPES.encode() not in (ROOT / path).read_bytes():
            edits.append(include_edit(path, offset))

    # Layout asserts for structs with every pointer member converted.
    if args.assert_size:
        for name in targets:
            rec = records[name]
            members = {m["name"] for m in rec["members"] if m["reason"] == "pointer"}
            longs = [m["name"] for m in rec["members"] if m["reason"] == "long"]
            where = decl_ends.get(name) or decl_ends.get(name.split(" ", 1)[-1])
            if not where or longs or members - converted.get(name, set()) - converted.get(name.split(" ", 1)[-1], set()):
                print(f"psx_ptr_codemod: no size assert for {name} (members left: "
                      f"{sorted(members - converted.get(name, set())) + longs})", file=sys.stderr)
                continue
            path, end = where
            text = (ROOT / path).read_bytes()
            semi = text.index(b";", end) + 1
            line = f"PSX_ASSERT_SIZE({name}, {rec['size_ps1']:#x});"
            if line.encode() not in text:
                edits.append(("insert", path, semi, "\n" + line))

    files = collections.defaultdict(list)
    for e in edits:
        if args.files and not re.search(args.files, e[1]):
            continue
        files[e[1]].append(e)
    changed = 0
    for path in sorted(files):
        before = (ROOT / path).read_bytes()
        after = apply_edits(before, files[path])
        if after == before:
            continue
        changed += 1
        if args.dry_run:
            sys.stdout.writelines(difflib.unified_diff(
                before.decode(errors="replace").splitlines(True), after.decode(errors="replace").splitlines(True),
                f"a/{path}", f"b/{path}"))
        else:
            (ROOT / path).write_bytes(after)

    counts = collections.Counter(e[0] for e in edits if not args.files or re.search(args.files, e[1]))
    print(f"psx_ptr_codemod: {changed} files {'would change' if args.dry_run else 'changed'}; edits "
          + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())) + "; "
          + f"{len(unsafe)} unsafe sites" + (f" ({', '.join(f'{k} {v}' for k, v in sorted(collections.Counter(u['category'] for u in unsafe).items()))})" if unsafe else "")
          + (f"; skipped {dict(skipped)}" if skipped else ""), file=sys.stderr)
    for u in sorted(unsafe, key=lambda u: (u["file"], u["line"])):
        print(f"  {u['category']:<14} {u['file']}:{u['line']}  {u['struct']}.{u['field']}  {u['detail']}"
              f"\n                 {u['text']}", file=sys.stderr)
    for r in results:
        if r.get("clang_errors"):
            print(f"psx_ptr_codemod: note: {r['source']}: {r['clang_errors']} clang errors (still analysed)",
                  file=sys.stderr)
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss // 1024
    print(f"psx_ptr_codemod: peak worker memory {peak} MB", file=sys.stderr)
    if args.report:
        pathlib.Path(args.report).write_text(json.dumps(
            {"targets": {k: v for k, v in lookup.items()}, "edits": edits, "unsafe": unsafe,
             "skipped": dict(skipped)}, indent=1, default=list))


if __name__ == "__main__":
    main()
