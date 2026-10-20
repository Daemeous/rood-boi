#!/usr/bin/env python3
"""Typed views of game state for owned Zig code (pc/docs/porting/PORTING.md
§2, §7), from the DWARF of the original unit's object of the same build: the
layouts GCC used on that target (ILP32; LP64 with psx_ptr; the native moves),
so the views are right on every build by construction.

Two halves:
  - inside gdb (dump): the requested types and globals as JSON (fields with
    bit positions and sizes, signedness, arrays, pointers, psx_ptr);
  - emit(dump, names): the Zig module: extern structs/unions with explicit
    padding, align(1) where C is misaligned, psx.Ptr(T) for psx_ptr fields,
    a `bitfields` table per struct (psx.getBits/setBits), comptime asserts of
    every offset and size, and `g.<global>()` accessors (psx.global).
Readable names (PORTING.md §16) come from battle/MEANINGS.tsv's zig_name
column: a global's accessor, a field (`Type.field` rows) or a bitfield is
emitted under its readable name with an `/// orig:` comment; the symbol and
the offsets stay the original's. Pointer targets outside the requested set
are ?*anyopaque; a pointer to a struct aligned above 4 is ?*align(4) T (a
guest address is only 4-aligned).

    python3 pc/tools/zig_views.py OBJ OUT.zig --types T... --globals G... [--names MEANINGS.tsv]
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

try:
    import gdb  # noqa: F401  (inside gdb)
    IN_GDB = True
except ImportError:
    IN_GDB = False

ROOT = pathlib.Path(__file__).resolve().parents[2]
MEANINGS = ROOT / "pc/docs/porting/battle/MEANINGS.tsv"


# ---------------------------------------------------------------- gdb half
def _dump(type_names, global_names):
    wanted, order, types = set(type_names), [], {}

    def is_signed(t):
        t = t.strip_typedefs()
        if t.code == gdb.TYPE_CODE_ENUM:
            # GCC: unsigned int unless an enumerator is negative.
            return any(f.enumval < 0 for f in t.fields())
        if t.code == gdb.TYPE_CODE_BOOL:
            return False
        try:
            return bool(t.is_signed)
        except AttributeError:
            return not re.match(r"(unsigned|u_|_Bool|bool)", str(t))

    def is_psx_ptr(t):
        return [f.name for f in t.fields()] == ["raw", "psx_type_"]

    def psx_target(t):
        # psx_type_ is T* [0]: the array's element is T*, its target T.
        return t.fields()[1].type.strip_typedefs().target().target()

    def ref(t, context):
        if t.code == gdb.TYPE_CODE_TYPEDEF:
            target = t.strip_typedefs()
            if target.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
                if is_psx_ptr(target):
                    return {"kind": "psx_ptr", "target": pointee(psx_target(target), context)}
                return named(str(t), target)
            return ref(target, context)
        code = t.code
        if code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_CHAR, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_ENUM):
            return {"kind": "int", "size": t.sizeof, "signed": is_signed(t)}
        if code == gdb.TYPE_CODE_FLT:
            return {"kind": "float", "size": t.sizeof}
        if code == gdb.TYPE_CODE_PTR:
            return {"kind": "ptr", "target": pointee(t.target(), context)}
        if code == gdb.TYPE_CODE_ARRAY:
            low, high = t.range()
            # An unsized array (`T x[]`, `T x[0]`) has no storage of its own.
            count = 0 if t.sizeof == 0 else max(0, high - low + 1)
            return {"kind": "array", "count": count, "elem": ref(t.target(), context)}
        if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            if is_psx_ptr(t):
                return {"kind": "psx_ptr", "target": pointee(psx_target(t), context)}
            return named(t.tag or context, t)
        if code == gdb.TYPE_CODE_VOID:
            return {"kind": "opaque", "name": "void"}
        raise RuntimeError(f"{context}: unsupported type {t} (code {code})")

    def pointee(target, context):
        stripped = target.strip_typedefs()
        if stripped.code == gdb.TYPE_CODE_FUNC:
            return {"kind": "fn"}
        if target.code == gdb.TYPE_CODE_TYPEDEF and str(target) in wanted:
            return ref(target, context)
        if stripped.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION) and stripped.tag in wanted:
            return named(stripped.tag, stripped)
        if stripped.code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_CHAR, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL):
            return ref(stripped, context)
        return {"kind": "opaque", "name": str(target)}

    def named(name, struct):
        if name not in types:
            types[name] = None  # reserve (recursive types)
            fields = []
            for f in struct.fields():
                fields.append({"name": f.name, "bitpos": f.bitpos, "bitsize": f.bitsize,
                               "type": ref(f.type, f"{name}_{f.name}")})
            types[name] = {"size": struct.sizeof, "union": struct.code == gdb.TYPE_CODE_UNION, "fields": fields}
            order.append(name)
        return {"kind": "named", "name": name}

    for name in type_names:
        ref(gdb.lookup_type(name), name)
    globals_ = {}
    for name in global_names:
        symbol = gdb.lookup_global_symbol(name) or gdb.lookup_static_symbol(name) or gdb.lookup_symbol(name)[0]
        if symbol is None:
            raise RuntimeError(f"{name}: no such global in this unit's DWARF")
        globals_[name] = ref(symbol.type, name)
    pointer_size = gdb.lookup_type("void").pointer().sizeof
    return {"pointer_size": pointer_size, "order": order, "types": types, "globals": globals_}


def dump_with_gdb(obj, type_names, global_names, out):
    """Run the dump in gdb on `obj`; returns the JSON (also written to `out`)."""
    request = pathlib.Path(str(out) + ".request")
    request.write_text(json.dumps({"types": list(type_names), "globals": list(global_names), "out": str(out)}))
    result = subprocess.run(["gdb", "-batch", "-nx", "-iex", "set auto-load off", str(obj), "-ex",
                             f"python import sys; sys.argv = ['zig_views', {str(request)!r}]",
                             "-x", str(pathlib.Path(__file__).resolve())], capture_output=True, text=True)
    if result.returncode or not pathlib.Path(out).exists():
        raise SystemExit(f"zig_views: gdb on {obj}: {result.stderr[-2000:]}")
    return json.loads(pathlib.Path(out).read_text())


# ---------------------------------------------------------------- names
def load_names(path=MEANINGS):
    """{"globals": {symbol: zig}, "fields": {(type, field): zig}} from the
    zig_name column of MEANINGS.tsv (PORTING.md §16)."""
    names = {"globals": {}, "fields": {}}
    path = pathlib.Path(path)
    if not path.exists():
        return names
    lines = path.read_text().splitlines()
    header = lines[0].split("\t")
    if "zig_name" not in header:
        return names
    column = header.index("zig_name")
    for line in lines[1:]:
        parts = line.split("\t")
        if len(parts) <= column or not parts[column]:
            continue
        symbol, zig = parts[0], parts[column]
        if not re.fullmatch(r"[A-Za-z_]\w*", zig):
            raise SystemExit(f"{path}: {symbol}: zig_name {zig!r} is not an identifier")
        if "." in symbol:
            owner, field = symbol.split(".", 1)
            names["fields"][(owner, field)] = zig
        else:
            names["globals"][symbol] = zig
    return names


# ---------------------------------------------------------------- emitter
KEYWORDS = {"addrspace", "align", "allowzero", "and", "anyframe", "anytype", "asm", "break", "callconv", "catch",
            "comptime", "const", "continue", "defer", "else", "enum", "errdefer", "error", "export", "extern", "fn",
            "for", "if", "inline", "linksection", "noalias", "noinline", "nosuspend", "opaque", "or", "orelse",
            "packed", "pub", "resume", "return", "struct", "suspend", "switch", "test", "threadlocal", "try",
            "union", "unreachable", "var", "volatile", "while", "type", "void", "bool", "anyopaque", "null",
            "undefined", "true", "false", "g", "std", "psx"}


def zid(name):
    return f'@"{name}"' if name in KEYWORDS or not re.fullmatch(r"[A-Za-z_]\w*", name) else name


class Emitter:
    def __init__(self, dump, names):
        self.d = dump
        self.names = names
        self.ptr = dump["pointer_size"]
        self.max_align = 4 if self.ptr == 4 else 16  # i386 caps scalar alignment in structs at 4
        self._layout = {}

    # ---- types
    def ztype(self, r):
        k = r["kind"]
        if k == "int":
            return ("i" if r["signed"] else "u") + str(8 * r["size"])
        if k == "float":
            return "f32" if r["size"] == 4 else "f64"
        if k == "ptr":
            t = r["target"]
            if t["kind"] == "fn":
                return "?*const anyopaque"
            if t["kind"] == "opaque":
                return "?*anyopaque"
            # A guest address is only 4-aligned (PORTING.md §20 item 1).
            over = t["kind"] == "named" and self.align(t) > 4
            return ("?*align(4) " if over else "?*") + self.ztype(t)
        if k == "psx_ptr":
            t = r["target"]
            return f"psx.Ptr({'anyopaque' if t['kind'] in ('opaque', 'fn') else self.ztype(t)})"
        if k == "array":
            return f"[{r['count']}]{self.ztype(r['elem'])}"
        if k == "named":
            return zid(r["name"])
        return "anyopaque"

    def size(self, r):
        k = r["kind"]
        if k in ("int", "float"):
            return r["size"]
        if k == "ptr":
            return self.ptr
        if k == "psx_ptr":
            return 4
        if k == "array":
            return r["count"] * self.size(r["elem"])
        return self.d["types"][r["name"]]["size"]

    def align(self, r):
        k = r["kind"]
        if k in ("int", "float"):
            return min(r["size"], self.max_align)
        if k == "ptr":
            return self.ptr
        if k == "psx_ptr":
            return 4
        if k == "array":
            return self.align(r["elem"])
        return self.layout(r["name"])["align"]

    def layout(self, name):
        """Zig alignment of the emitted type and whether it must be all align(1)."""
        if name in self._layout:
            return self._layout[name]
        info = self.d["types"][name]
        self._layout[name] = {"align": 1, "packed": False}  # recursion guard
        members = [f for f in info["fields"] if not f["bitsize"]]
        packed = False
        natural = 1
        for f in members:
            a = self.align(f["type"])
            if not info["union"] and (f["bitpos"] // 8) % a:
                continue  # emitted align(1): contributes 1
            natural = max(natural, a)
        if info["size"] % natural:
            packed = True
        if packed:
            natural = 1
        self._layout[name] = {"align": natural, "packed": packed}
        return self._layout[name]

    def field_name(self, owner, field):
        return self.names["fields"].get((owner, field), field)

    def struct(self, name):
        info = self.d["types"][name]
        lines, asserts, bits = [], [], []
        packed = self.layout(name)["packed"]

        def orig(owner, field, zig, indent="    "):
            return [f"{indent}/// orig: {field}"] if zig != field else []

        if info["union"]:
            # A bitfield member of a union would be emitted at its declared
            # type's full width and clobber its neighbours' bits on a store:
            # no union in src/ has one today, so refuse rather than guess.
            bad = [f["name"] for f in info["fields"] if f["bitsize"]]
            if bad:
                raise SystemExit(f"zig_views: union {name} has bitfield member(s) {', '.join(bad)}: "
                                 "not supported (wrap them in a struct, or extend zig_views.py)")
            for f in info["fields"]:
                zig = self.field_name(name, f["name"])
                lines += orig(name, f["name"], zig)
                lines.append(f"    {zid(zig)}: {self.ztype(f['type'])}{' align(1)' if packed else ''},")
            lines.append(f"    _bytes: [{info['size']}]u8,")
            asserts.append(f"std.debug.assert(@sizeOf(@This()) == {info['size']});")
            return self.wrap(name, "extern union", lines, asserts, bits)
        cursor = 0
        normal = sorted((f for f in info["fields"] if not f["bitsize"]), key=lambda f: f["bitpos"])
        bitfields = [f for f in info["fields"] if f["bitsize"]]
        bit_bytes = sorted({b for f in bitfields for b in range(f["bitpos"] // 8, (f["bitpos"] + f["bitsize"] + 7) // 8)})
        runs = []  # contiguous byte ranges of bitfield storage
        for b in bit_bytes:
            if runs and runs[-1][1] == b:
                runs[-1][1] = b + 1
            else:
                runs.append([b, b + 1])
        items = sorted([(f["bitpos"] // 8, 1, f) for f in normal] + [(s, 0, e) for s, e in runs],
                       key=lambda x: (x[0], x[1]))
        for offset, kind, item in items:
            if kind == 0:  # bitfield bytes
                start = max(offset, cursor)
                if item > start:
                    if start > cursor:
                        lines.append(f"    _pad_{cursor}: [{start - cursor}]u8,")
                    lines.append(f"    _bits_{start}: [{item - start}]u8,")
                    cursor = item
                continue
            f = item
            if offset < cursor:
                raise SystemExit(f"zig_views: {name}.{f['name']} at {offset} overlaps the previous field")
            if offset > cursor:
                lines.append(f"    _pad_{cursor}: [{offset - cursor}]u8,")
            aligned = not packed and offset % self.align(f["type"]) == 0
            zig = self.field_name(name, f["name"])
            lines += orig(name, f["name"], zig)
            lines.append(f"    {zid(zig)}: {self.ztype(f['type'])}{'' if aligned else ' align(1)'},")
            asserts.append(f'std.debug.assert(@offsetOf(@This(), "{zig}") == {offset});')
            cursor = offset + self.size(f["type"])
        if info["size"] > cursor:
            lines.append(f"    _pad_{cursor}: [{info['size'] - cursor}]u8,")
        asserts.append(f"std.debug.assert(@sizeOf(@This()) == {info['size']});")
        for f in bitfields:
            signed = f["type"].get("signed", False)
            zig = self.field_name(name, f["name"])
            if zig != f["name"]:
                bits.append(f"        // orig: {f['name']}")
            bits.append(f"        .{zid(zig)} = psx.Bit{{ .byte = {f['bitpos'] // 8}, "
                        f".bit = {f['bitpos'] % 8}, .width = {f['bitsize']}, .signed = {str(signed).lower()} }},")
        return self.wrap(name, "extern struct", lines, asserts, bits)

    def wrap(self, name, kind, lines, asserts, bits):
        out = [f"pub const {zid(name)} = {kind} {{"] + lines
        if bits:
            out += ["    pub const bitfields = .{"] + bits + ["    };"]
        out += ["    comptime {"] + [f"        {a}" for a in asserts] + ["    }", "};"]
        return "\n".join(out)

    def globals_(self):
        out = ["/// Globals by readable name (MEANINGS.tsv zig_name) or their own; each links to the original symbol.",
               "pub const g = struct {"]
        for name, r in sorted(self.d["globals"].items()):
            zig = self.names["globals"].get(name, name)
            if zig != name:
                out.append(f"    /// orig: {name}")
            if r["kind"] == "array" and r["count"] == 0:
                out.append(f"    pub inline fn {zid(zig)}() [*]{self.ztype(r['elem'])} "
                           f'{{ return psx.globalMany({self.ztype(r["elem"])}, "{name}"); }}')
            else:
                out.append(f"    pub inline fn {zid(zig)}() *{self.ztype(r)} "
                           f'{{ return psx.global({self.ztype(r)}, "{name}"); }}')
        out.append("};")
        return "\n".join(out)


def emit(dump, names=None, psx_import="common"):
    names = names if names is not None else {"globals": {}, "fields": {}}
    e = Emitter(dump, names)
    head = ["//! Generated by build_boot.py (pc/tools/zig_views.py) from the original object's DWARF: do not edit.",
            "const std = @import(\"std\");",
            f"const psx = @import(\"{psx_import}\")" + (".psx;" if psx_import == "common" else ";")]
    body = [e.struct(name) for name in dump["order"]]
    # Every layout is checked whenever the module is imported, used or not.
    check = ["comptime {"] + [f"    _ = @sizeOf({zid(name)});" for name in dump["order"]] + ["}"]
    return "\n".join(head + body + [e.globals_()] + check) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("obj")
    parser.add_argument("out")
    parser.add_argument("--types", nargs="*", default=[])
    parser.add_argument("--globals", nargs="*", default=[])
    parser.add_argument("--names", default=str(MEANINGS))
    args = parser.parse_args(argv)
    dump = dump_with_gdb(args.obj, args.types, args.globals, args.out + ".json")
    pathlib.Path(args.out).write_text(emit(dump, load_names(args.names)))


if IN_GDB:
    request = json.loads(pathlib.Path(sys.argv[1]).read_text())
    result = _dump(request["types"], request["globals"])
    pathlib.Path(request["out"]).write_text(json.dumps(result, indent=1))
elif __name__ == "__main__":
    main()
