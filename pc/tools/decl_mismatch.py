#!/usr/bin/env python3
"""Cross-unit declaration check for the 64-bit build.

Decompiled units often declare a function or data import locally
(`int func_800AD494(D_800F4538_t*, int, int*);`) instead of including the
header of the unit that defines it. When the local type disagrees with the
definition, C compiles each unit happily and the 32-bit build usually works
(pointers and ints are both 4 bytes), but on LP64 the caller passes, returns
or stores the wrong amount of storage: an `int` slot handed to a callee that
writes a pointer through it is an 8-byte store into 4 bytes of stack.

This tool compares every declaration a 64-bit object was compiled with
against the definition the linker resolves it to, using the objects' own
DWARF (`-g`): GCC records a declaration DIE, with parameter types, for each
external function a unit calls and each external variable it uses. So it
checks exactly what each caller was compiled with, after the build's
renames (overlay-local imports `name__key`, PLG prefixes), with real sizes
for every type (struct sizes included). Imports with no C definition (data
linked to PS1 addresses, asm-era functions) are compared declaration against
declaration.

Severity:
  HAZARD  wrong-size storage on LP64: pointer vs (<8-byte) integer as a
          parameter, return value or variable; a pointee that is a pointer on
          one side and a 4-byte (or smaller) integer or small struct on the
          other (out-parameters: the 452d2a33 bug); 8- vs 4-byte integers;
          array vs pointer for data; int vs float argument classes; struct
          by value of a different size.
  REVIEW  pointee structs of different sizes where one holds pointers or
          longs (an alias view that may not be converted), a pointer vs a
          larger struct, a different parameter count.

Known benign mismatches are listed in BENIGN below with the reason; they are
reported as "benign" and do not fail the check.

  python3 pc/tools/build_boot.py --arch 64
  pc/tools/memcap python3 pc/tools/decl_mismatch.py [--review] [--json out.json]

--windows checks the --target windows build instead (pc/build/win64: the MinGW
COFF twins, read with MinGW's objdump, which prints the same DWARF dump). There
the game units' long is long long (lp64_longs) while the host units keep
Windows' 4-byte long, so a declaration that crosses between them with `long`
in it shows up as an 8- vs 4-byte HAZARD that the Linux build cannot see.

  python3 pc/tools/build_boot.py --target windows
  python3 pc/tools/decl_mismatch.py --windows

Exit status 1 when a HAZARD that is not in BENIGN is found.
"""

import argparse
import collections
import concurrent.futures
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# The DWARF dumps: binutils' readelf for the Linux build's ELF objects; for the
# Windows build's COFF twins, MinGW's objdump (same output format). Chosen by
# --windows, passed to the worker processes through the environment.
WINDOWS = bool(__import__("os").environ.get("DECL_MISMATCH_WINDOWS"))
DUMP_INFO = (["x86_64-w64-mingw32-objdump", "--dwarf=info", "--dwarf-depth=100"] if WINDOWS
             else ["readelf", "--debug-dump=info", "--dwarf-depth=100"])
DUMP_LINE = ["x86_64-w64-mingw32-objdump", "--dwarf=rawline"] if WINDOWS else ["readelf", "--debug-dump=line"]

# Generated objects whose C types are not the game's (address-only externs,
# native import regions, fail-fast traps): never definitions or declarations.
GENERATED = {"function_tables", "plg_entries", "native_imports", "traps", "overlay_statics_table"}

# (symbol, reason). Triaged by hand; see pc/docs/LP64_Audit.md.
BENIGN = {
    # Port code declares the 0x800F58xx AI words and other guest words as
    # 32-bit integers and converts with psx_to_ptr/psx_to_int (task 3/4).
    # build_boot.py places pointer-typed imports in native 8-byte slots
    # holding guest addresses (zero-extended, < 4 GB), so a 4-byte access
    # reads/writes the low half of the slot, which is the whole value.
    **{name: "port code views a guest pointer word as u32 (low half of the native slot; values are guest addresses)"
       for name in ["D_800EB9B8", "D_800F51B8", "D_800F58B8", "D_800F58BC", "D_800F58C0", "D_800F58C8",
                    "D_800F58CC", "D_800F58D0", "D_800F58D8", "D_800F58F8", "D_800F5904", "D_800F5908",
                    "D_800F5910", "D_800F5920"]},
    "strlen": "the SDK's unprototyped int strlen() reads the low 32 bits of glibc's size_t (lengths are small)",
    # Extra trailing arguments: the callee never reads them (x86-64 and
    # MIPS pass the first arguments in registers), as on the PS1.
    **{name: "caller passes extra trailing arguments the definition ignores"
       for name in ["func_800AD62C", "func_800E1908", "func_800E19FC", "func_800E2CCC", "func_800E4C28"]},
    "func_800E5154": "146C.c's ROOD_PC call casts it to int (*)(int) and passes the actor id; the (void) "
                     "prototype is the PS1's (it relied on $a0)",
    "CompMatrixLV": "the game's two-argument in-place entry (boot_sdk.c); 49268.c calls it through a "
                    "two-argument cast, the SDK header keeps PsyQ's three",
}

HEADER = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((\w+)\))?")
ATTR = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s?(.*)$")


def attr_value(text):
    text = text.strip()
    if text.startswith("(indirect"):
        text = text.split("): ", 1)[1] if "): " in text else ""
    return text


def ref(text):
    m = re.match(r"<0x([0-9a-f]+)>", text.strip())
    return int(m.group(1), 16) if m else None


def number(text):
    m = re.match(r"(-?(?:0x[0-9a-f]+|\d+))", text.strip())
    return int(m.group(1), 0) if m else None


class Dwarf:
    """The DIE tree of one object (readelf text), plus its file table."""

    def __init__(self, path):
        self.dies = {}  # offset -> [tag, attrs, children, parent]
        self.files = {}
        text = subprocess.run([*DUMP_INFO, str(path)], capture_output=True, text=True, errors="replace").stdout
        stack = []
        current = None
        for line in text.splitlines():
            m = HEADER.match(line)
            if m:
                depth, off, abbrev, tag = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), m.group(4)
                if abbrev == 0:
                    current = None
                    continue
                del stack[depth:]
                parent = stack[-1] if stack else None
                current = [tag, {}, [], parent]
                self.dies[off] = current
                if parent is not None:
                    self.dies[parent][2].append(off)
                stack.append(off)
                continue
            m = ATTR.match(line)
            if m and current is not None:
                current[1][m.group(1)] = m.group(2)
        self.read_files(path)

    def read_files(self, path):
        text = subprocess.run([*DUMP_LINE, str(path)], capture_output=True, text=True, errors="replace").stdout
        dirs, section = {}, None
        for line in text.splitlines():
            if "The Directory Table" in line:
                section = "dirs"
                continue
            if "The File Name Table" in line:
                section = "files"
                continue
            if section and not line.strip():
                if section == "files":
                    break
                continue
            parts = line.split(None, 2 if section == "files" else 1)
            if not parts or not parts[0].isdigit():
                continue
            if section == "dirs":
                dirs[int(parts[0])] = attr_value(parts[1])
            elif section == "files" and len(parts) == 3:
                directory = dirs.get(int(parts[1]), "")
                name = attr_value(parts[2])
                full = pathlib.PurePosixPath(directory) / name if not name.startswith("/") else pathlib.PurePosixPath(name)
                try:
                    full = pathlib.Path(full).resolve().relative_to(ROOT)
                except ValueError:
                    pass
                self.files[int(parts[0])] = str(full)

    def where(self, off):
        attrs = self.dies[off][1]
        f, line = number(attrs.get("DW_AT_decl_file", "")), number(attrs.get("DW_AT_decl_line", ""))
        return f"{self.files.get(f, '?')}:{line}" if line is not None else "?"

    # Types: canonical tuples (no typedefs/qualifiers) and display strings.
    def canon(self, off, memo=None):
        memo = {} if memo is None else memo
        if off is None:
            return ("void",)
        if off in memo:
            return memo[off]
        memo[off] = ("cycle",)
        tag, attrs, children, _ = self.dies[off]
        inner = ref(attrs.get("DW_AT_type", ""))
        size = number(attrs.get("DW_AT_byte_size", ""))
        if tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type",
                   "DW_TAG_atomic_type"):
            result = self.canon(inner, memo)
        elif tag == "DW_TAG_base_type":
            name = attr_value(attrs.get("DW_AT_name", ""))
            result = ("float" if any(w in name for w in ("float", "double")) else "int", size, name)
        elif tag == "DW_TAG_enumeration_type":
            result = ("int", size, "enum")
        elif tag == "DW_TAG_pointer_type":
            result = ("ptr", self.canon(inner, memo))
        elif tag == "DW_TAG_array_type":
            count = None
            for c in children:
                ca = self.dies[c][1]
                if "DW_AT_count" in ca:
                    count = number(ca["DW_AT_count"])
                elif "DW_AT_upper_bound" in ca:
                    upper = number(ca["DW_AT_upper_bound"])
                    count = None if upper is None else upper + 1
                if count is not None:
                    break  # outer dimension; inner ones fold into the element size
            element = self.canon(inner, memo)
            dims = [self.dies[c][1] for c in children if self.dies[c][0] == "DW_TAG_subrange_type"]
            for extra in reversed(dims[1:]):
                n = number(extra.get("DW_AT_upper_bound", ""))
                n = number(extra["DW_AT_count"]) if "DW_AT_count" in extra else (None if n is None else n + 1)
                element = ("arr", n, element)
            result = ("arr", count, element)
        elif tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_class_type"):
            name = attr_value(attrs.get("DW_AT_name", "")) or f"<anon {self.where(off)}>"
            wide = self.wide(off)
            result = ("rec", name, None if "DW_AT_declaration" in attrs else size, wide)
        elif tag == "DW_TAG_subroutine_type":
            result = self.signature(off, memo)
        else:
            result = ("other", tag)
        memo[off] = result
        return result

    def wide(self, off):
        """Where a type (by value) holds a pointer or an 8-byte integer: a
        member path ("" for the type itself), or None. Does not follow
        pointers, so it never loops."""
        if off is None:
            return None
        tag, attrs, children, _ = self.dies[off]
        if tag == "DW_TAG_pointer_type":
            return ""
        if tag in ("DW_TAG_base_type", "DW_TAG_enumeration_type"):
            return "" if number(attrs.get("DW_AT_byte_size", "")) == 8 else None
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_class_type"):
            for c in children:
                if self.dies[c][0] == "DW_TAG_member":
                    inner = self.wide(ref(self.dies[c][1].get("DW_AT_type", "")))
                    if inner is not None:
                        name = attr_value(self.dies[c][1].get("DW_AT_name", "")) or "<anon>"
                        return name + ("." + inner if inner else "")
            return None
        if tag == "DW_TAG_array_type":
            # psx_ptr(T) is {u32 raw; T* psx_type_[0];}: a zero-length
            # array is a type tag, not storage.
            subranges = [self.dies[c][1] for c in children if self.dies[c][0] == "DW_TAG_subrange_type"]
            first = subranges[0] if subranges else {}
            upper = number(first.get("DW_AT_upper_bound", ""))
            count = number(first["DW_AT_count"]) if "DW_AT_count" in first else None if upper is None else upper + 1
            if not count:
                return None
        if tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type",
                   "DW_TAG_atomic_type", "DW_TAG_array_type"):
            return self.wide(ref(attrs.get("DW_AT_type", "")))
        return None

    def signature(self, off, memo=None):
        tag, attrs, children, _ = self.dies[off]
        params, variadic = [], False
        for c in children:
            ctag, cattrs = self.dies[c][0], self.dies[c][1]
            if ctag == "DW_TAG_formal_parameter":
                params.append(self.canon(ref(cattrs.get("DW_AT_type", "")), memo))
            elif ctag == "DW_TAG_unspecified_parameters":
                variadic = True
        prototyped = "DW_AT_prototyped" in attrs
        return ("fn", self.canon(ref(attrs.get("DW_AT_type", "")), memo), tuple(params) if prototyped else None,
                variadic)

    def show(self, off, depth=0):
        if off is None:
            return "void"
        if depth > 8:
            return "..."
        tag, attrs, children, _ = self.dies[off]
        inner = ref(attrs.get("DW_AT_type", ""))
        name = attr_value(attrs.get("DW_AT_name", ""))
        if tag in ("DW_TAG_typedef", "DW_TAG_base_type", "DW_TAG_structure_type", "DW_TAG_union_type",
                   "DW_TAG_enumeration_type") and name:
            prefix = {"DW_TAG_structure_type": "struct ", "DW_TAG_union_type": "union ",
                      "DW_TAG_enumeration_type": "enum "}.get(tag, "")
            return prefix + name
        if tag == "DW_TAG_pointer_type":
            target = self.dies.get(inner)
            if target and target[0] == "DW_TAG_subroutine_type":
                return f"{self.show(ref(target[1].get('DW_AT_type', '')), depth + 1)} (*)({self.show_params(inner, depth + 1)})"
            return self.show(inner, depth + 1) + "*"
        if tag in ("DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type"):
            return self.show(inner, depth + 1)
        if tag == "DW_TAG_array_type":
            dims = []
            for c in children:
                ca = self.dies[c][1]
                n = number(ca["DW_AT_count"]) if "DW_AT_count" in ca else number(ca.get("DW_AT_upper_bound", ""))
                n = n if "DW_AT_count" in ca or n is None else n + 1
                dims.append("" if n is None else str(n))
            return self.show(inner, depth + 1) + "".join(f"[{d}]" for d in dims)
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            return f"struct <anon {self.where(off)}>"
        if tag == "DW_TAG_subroutine_type":
            return f"{self.show(inner, depth + 1)} ({self.show_params(off, depth + 1)})"
        return name or tag

    def show_params(self, off, depth=0):
        parts = []
        for c in self.dies[off][2]:
            ctag, cattrs = self.dies[c][0], self.dies[c][1]
            if ctag == "DW_TAG_formal_parameter":
                parts.append(self.show(ref(cattrs.get("DW_AT_type", "")), depth))
            elif ctag == "DW_TAG_unspecified_parameters":
                parts.append("...")
        if not parts and "DW_AT_prototyped" in self.dies[off][1]:
            return "void"
        return ", ".join(parts)


def size_of(t):
    kind = t[0]
    if kind == "ptr":
        return 8
    if kind in ("int", "float"):
        return t[1]
    if kind == "arr":
        inner = size_of(t[2])
        return None if t[1] is None or inner is None else t[1] * inner
    if kind == "rec":
        return t[2]
    return None


def short(t):
    kind = t[0]
    if kind == "ptr":
        return short(t[1]) + "*"
    if kind in ("int", "float"):
        return t[2]
    if kind == "arr":
        return f"{short(t[2])}[{'' if t[1] is None else t[1]}]"
    if kind == "rec":
        return f"{t[1]} ({t[2]} bytes" + (f", 8-byte member {t[3]})" if t[3] else ")")
    if kind == "fn":
        return "function"
    return kind


def compare_value(a, b, what, out, data=False):
    """Values (parameters, returns, variables) passed or stored as a and b."""
    if a == b:
        return
    ka, kb = a[0], b[0]
    if "cycle" in (ka, kb) or "void" in (ka, kb) or "other" in (ka, kb):
        return
    if ka == "arr" and kb == "arr":
        compare_value(a[2], b[2], what + " element", out, data)
        return
    if data and (ka == "arr") != (kb == "arr"):
        arr, other = (a, b) if ka == "arr" else (b, a)
        if other[0] == "ptr":
            out.append(("HAZARD", f"{what}: array {short(arr)} vs pointer {short(other)}"))
            return
        compare_value(arr[2], other, what + " element", out, data)
        return
    if ka == "ptr" and kb == "ptr":
        compare_pointee(a[1], b[1], what, out, 1)
        return
    if ka == "ptr" or kb == "ptr":
        other = b if ka == "ptr" else a
        if other[0] in ("int", "float") and size_of(other) != 8:
            out.append(("HAZARD", f"{what}: pointer vs {short(other)}"))
        elif other[0] == "rec":
            out.append(("HAZARD", f"{what}: pointer vs struct by value {short(other)}"))
        return
    if ka in ("int", "float") and kb in ("int", "float"):
        if ka != kb:
            out.append(("HAZARD", f"{what}: {short(a)} vs {short(b)} (integer vs floating register class)"))
        elif a[1] != b[1] and 8 in (a[1], b[1]):
            out.append(("HAZARD", f"{what}: {short(a)} vs {short(b)}"))
        return
    # By-value records: a size or shape difference matters for LP64 only
    # when a record holds pointers/longs (otherwise the PS1 has it too: data
    # imports are often declared as byte arrays or partial views).
    if ka == "rec" and kb == "rec":
        if a[2] is not None and b[2] is not None and a[2] != b[2] and (a[3] or b[3]):
            out.append(("HAZARD", f"{what}: struct by value {short(a)} vs {short(b)}"))
        return
    if "rec" in (ka, kb):
        rec = a if ka == "rec" else b
        if rec[3]:
            out.append(("HAZARD", f"{what}: {short(a)} vs {short(b)} by value (the struct holds pointers)"))


def compare_pointee(a, b, what, out, depth):
    """What two pointers point at: the storage a callee reads or writes."""
    if a == b:
        return
    ka, kb = a[0], b[0]
    label = what + " pointee" + ("" if depth == 1 else f" (level {depth})")
    if "cycle" in (ka, kb) or "void" in (ka, kb) or "other" in (ka, kb):
        return
    if ka == "fn" and kb == "fn":
        compare_signature(a, b, label + " function", out, review=True)
        return
    if "fn" in (ka, kb):
        return
    if ka == "arr" and kb == "arr":
        compare_pointee(a[2], b[2], what, out, depth)
        return
    if ka == "arr" or kb == "arr":
        arr, other = (a, b) if ka == "arr" else (b, a)
        compare_pointee(arr[2], other, what, out, depth)
        return
    if ka == "ptr" and kb == "ptr":
        compare_pointee(a[1], b[1], what, out, depth + 1)
        return
    if ka == "ptr" or kb == "ptr":
        other = b if ka == "ptr" else a
        size = size_of(other)
        if other[0] in ("int", "float") and size != 8:
            out.append(("HAZARD", f"{label}: pointer (8 bytes) vs {short(other)} ({size} bytes)"))
        elif other[0] == "rec" and size is not None and size < 8:
            out.append(("HAZARD", f"{label}: pointer (8 bytes) vs {short(other)}"))
        elif other[0] == "rec":
            out.append(("REVIEW", f"{label}: pointer vs {short(other)}"))
        return
    if ka in ("int", "float") and kb in ("int", "float"):
        if a[1] != b[1] and 8 in (a[1], b[1]):
            out.append(("HAZARD", f"{label}: {short(a)} vs {short(b)}"))
        return
    if ka == "rec" and kb == "rec":
        if a[2] is not None and b[2] is not None and a[2] != b[2] and (a[3] or b[3]):
            out.append(("REVIEW", f"{label}: {short(a)} vs {short(b)}"))
        return
    rec, other = (a, b) if ka == "rec" else (b, a)
    if rec[3] and other[0] == "int" and rec[2] is not None and rec[2] > (other[1] or 0) and other[1] in (4, 8):
        out.append(("REVIEW", f"{label}: {short(rec)} (holds pointers) vs {short(other)}"))


def compare_signature(a, b, what, out, review=False):
    found = []
    compare_value(a[1], b[1], f"{what} return".strip(), found)
    if a[2] is not None and b[2] is not None:
        for i, (pa, pb) in enumerate(zip(a[2], b[2])):
            compare_value(pa, pb, f"{what} parameter {i + 1}".strip(), found)
        if len(a[2]) != len(b[2]) and not (a[3] or b[3]):
            found.append(("REVIEW", f"{what} parameter count: {len(a[2])} vs {len(b[2])}".strip()))
    if review:
        found = [("REVIEW", m) for _, m in found]
    out.extend(found)


def object_symbols(path):
    """Global definitions and every referenced name of one object (after the
    build's objcopy renames)."""
    defined, names = set(), set()
    for line in subprocess.run(["nm", str(path)], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in "Uw":
            names.add(parts[1])
        elif len(parts) == 3:
            names.add(parts[2])
            if parts[1] in "TDBRGSVWC":
                defined.add(parts[2])
    return defined, names


def scan(path):
    """Declarations and definitions with external linkage in one object:
    [(c_name, linker_name, kind, is_definition, canon, display, where)]."""
    defined, names = object_symbols(path)
    alias = {}
    for s in names:
        m = re.match(r"(plg\d+)_(.+)$", s)
        if m:
            alias.setdefault(m.group(2), s)
        base, sep, key = s.rpartition("__")
        if sep and base and re.fullmatch(r"\w+", key):
            alias.setdefault(base, s)
    dwarf = Dwarf(path)
    found = []
    for off, (tag, attrs, children, parent) in dwarf.dies.items():
        if tag not in ("DW_TAG_subprogram", "DW_TAG_variable") or "DW_AT_name" not in attrs:
            continue
        declaration = "DW_AT_declaration" in attrs
        if not declaration and "DW_AT_external" not in attrs:
            # A static definition: only the ones build_boot.py globalizes
            # (main.c and MAINMENU statics other code calls).
            name = attr_value(attrs["DW_AT_name"])
            if name not in defined:
                continue
        elif declaration and "DW_AT_external" not in attrs:
            continue
        if tag == "DW_TAG_variable" and not declaration and parent is not None \
                and dwarf.dies[parent][0] != "DW_TAG_compile_unit":
            continue  # a function-scope static
        name = attr_value(attrs.get("DW_AT_linkage_name", "") or attrs["DW_AT_name"])
        linker = name if name in names else alias.get(name)
        if linker is None:
            continue  # declared but never referenced
        if declaration and linker in defined:
            continue  # this object also defines it (the compiler checked)
        if tag == "DW_TAG_subprogram":
            canon = dwarf.signature(off)
            display = f"{dwarf.show(ref(attrs.get('DW_AT_type', '')))} {name}({dwarf.show_params(off)})"
            kind = "function"
        else:
            type_off = ref(attrs.get("DW_AT_type", ""))
            canon = dwarf.canon(type_off)
            display = f"{dwarf.show(type_off)} {name}"
            kind = "data"
        found.append((name, linker, kind, not declaration, canon, display, dwarf.where(off)))
    return path.stem, found


def objects_of(build, windows=False):
    listed = build / "objects.txt"
    if windows:
        # The twins of the linked objects (build_boot.py writes the list for both targets)
        paths = ([pathlib.Path(p).with_suffix(".obj") for p in listed.read_text().split()] if listed.exists()
                 else sorted(build.glob("*.obj")))
    elif listed.exists():
        paths = [pathlib.Path(p) for p in listed.read_text().split()]
    else:
        paths = sorted(build.glob("*.o"))
    return [p if p.is_absolute() else ROOT / p for p in paths if p.stem not in GENERATED]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", help="objects of build_boot.py --arch 64 (default pc/build/boot64), "
                        "or of --target windows with --windows (default pc/build/win64)")
    parser.add_argument("--windows", action="store_true",
                        help="check the Windows build's COFF twins (host long is 4 bytes there)")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--review", action="store_true", help="also print REVIEW findings")
    parser.add_argument("--benign", action="store_true", help="also print known benign mismatches")
    parser.add_argument("--json", help="write every finding here")
    args = parser.parse_args()
    if args.windows and not WINDOWS:
        # The workers read DUMP_INFO at import: re-run with the choice in the environment
        import os
        os.execve(sys.executable, [sys.executable, *sys.argv],
                  dict(os.environ, DECL_MISMATCH_WINDOWS="1"))
    build = args.build or ("pc/build/win64" if args.windows else "pc/build/boot64")

    objects = objects_of(ROOT / build, args.windows)
    if not objects:
        sys.exit(f"no objects in {build}: run python3 pc/tools/build_boot.py "
                 f"{'--target windows' if args.windows else '--arch 64'} first")
    entries = collections.defaultdict(list)  # linker name -> [(object, entry)]
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        for stem, found in pool.map(scan, objects):
            for entry in found:
                entries[entry[1]].append((stem, entry))

    findings = []
    for linker, items in sorted(entries.items()):
        defs = [(o, e) for o, e in items if e[3]]
        decls = [(o, e) for o, e in items if not e[3]]
        if not decls:
            continue
        if defs:
            reference = defs[0]
            others = decls
            basis = "definition"
        else:
            # No C definition: compare against the most common declaration.
            counts = collections.Counter(e[4] for _, e in decls)
            common = counts.most_common(1)[0][0]
            reference = next((o, e) for o, e in decls if e[4] == common)
            others = [(o, e) for o, e in decls if e[4] != common]
            basis = "declaration"
        for obj, entry in others:
            out = []
            ref_entry = reference[1]
            if entry[2] == "function" and ref_entry[2] == "function":
                compare_signature(entry[4], ref_entry[4], "", out)
            elif entry[2] == "data" and ref_entry[2] == "data":
                compare_value(entry[4], ref_entry[4], "type", out, data=True)
            else:
                out.append(("HAZARD", f"declared as {entry[2]}, {basis} is {ref_entry[2]}"))
            for severity, message in out:
                benign = BENIGN.get(entry[0]) or BENIGN.get(linker)
                findings.append({
                    "symbol": linker,
                    "severity": "benign" if benign else severity,
                    "benign_reason": benign,
                    "message": message,
                    "declaration": {"object": obj, "where": entry[6], "type": entry[5]},
                    "against": {"object": reference[0], "where": ref_entry[6], "type": ref_entry[5], "is": basis},
                })

    shown = {"HAZARD"} | ({"REVIEW"} if args.review else set()) | ({"benign"} if args.benign else set())
    counts = collections.Counter(f["severity"] for f in findings)
    for f in findings:
        if f["severity"] not in shown:
            continue
        print(f"{f['severity']} {f['symbol']}: {f['message']}")
        print(f"    declared    {f['declaration']['where']} ({f['declaration']['object']}): {f['declaration']['type']}")
        print(f"    {f['against']['is']:<11} {f['against']['where']} ({f['against']['object']}): {f['against']['type']}")
        if f["benign_reason"]:
            print(f"    benign: {f['benign_reason']}")
    print(f"{len(objects)} objects, {len(entries)} external symbols; "
          f"HAZARD {counts['HAZARD']}, REVIEW {counts['REVIEW']}, benign {counts['benign']}")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(findings, indent=1))
    sys.exit(1 if counts["HAZARD"] else 0)


if __name__ == "__main__":
    main()
