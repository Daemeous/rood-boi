#!/usr/bin/env python3
"""Globalise the battle statics that STATE.tsv lists (PORTING.md §6.5).

A C `static` (file scope or block scope) is a local symbol: an owned Zig
object cannot link to it, and translate-c would give it a copy of its own. So
the build makes each listed static global, under a prefixed name, in its own
object and section (the overlay restart still covers it), and the Zig port
reaches that one storage through the views (`g.<name>()`, psx.global):

    file static `name` of unit object battle_5BF94          -> battle_5BF94__name
    block-scope static `name` of function F in battle_44F14  -> battle_44F14__F__name

The list is STATE.tsv's: rows whose storage says "file static (local
symbol)" (the object is `defined_in`) or "function-local static in F (object
symbol name.N in OBJECT.o ...)" (folded ones, with no symbol, are ported as
Zig `const` values). The views name them as STATE.tsv does: `name`, or
`F::name` (accessor `F__name`).

The symbol is found by pattern, not by GCC's spelling: a file static is the
local data symbol `name`, a block-scope one `name.N` (GCC's suffix changes
with the compiler and the source), exactly one of them; when the object holds
several (`reported.1`, `reported.2` in 44F14), the DWARF scope decides
(gdb: the block-scope statics of F, at their addresses). Anything else fails
the build: a listed static that is gone, or that cannot be told apart.

build_boot.py runs it on each battle unit's object (and its COFF twin), and
zig_diff.py on the reference object it compiles from the unit's .i (where
only the statics the kept bodies use exist: `required=False`).

    python3 pc/tools/battle_statics.py OBJECT...   print what would be renamed
    python3 pc/tools/battle_statics.py --unit OBJECT...   a pc/src unit's file statics

The pc/src units (the asm ports, PORTING.md §20 "D2") are globalised from their
objects, not only from STATE.tsv (see file_statics / globalise_unit).
"""
import json
import pathlib
import re
import subprocess
import sys

try:
    import gdb  # noqa: F401  (the gdb half: block_statics)
    IN_GDB = True
except ImportError:
    IN_GDB = False

ROOT = pathlib.Path(__file__).resolve().parents[2]
STATE = ROOT / "pc/docs/porting/battle/STATE.tsv"


def listed(path=STATE):
    """{object stem: [(scope, name)]}: scope is the function of a block-scope
    static, None for a file static."""
    out = {}
    lines = pathlib.Path(path).read_text().splitlines()
    header = lines[0].split("\t")
    col = {name: header.index(name) for name in ("symbol", "storage", "defined_in")}
    for line in lines[1:]:
        parts = line.split("\t")
        symbol, storage, defined = parts[col["symbol"]], parts[col["storage"]], parts[col["defined_in"]]
        if storage.startswith("function-local static in "):
            where = re.search(r"\(object symbol .*? in (\w+)\.o\b", storage)
            if not where:
                continue  # folded into code: no storage to share
            scope, name = symbol.split("::", 1)
            out.setdefault(where.group(1), []).append((scope, name))
        elif "file static (local symbol)" in storage and re.fullmatch(r"\w+", defined):
            out.setdefault(defined, []).append((None, symbol))
    return out


def link_name(object_stem, scope, name):
    """The global name a listed static gets."""
    return f"{object_stem}__{scope}__{name}" if scope else f"{object_stem}__{name}"


def view_name(scope, name):
    """How STATE.tsv and a views manifest name it."""
    return f"{scope}::{name}" if scope else name


def accessor_name(view):
    """The views accessor of a static: `F::name` -> `F__name`."""
    return view.replace("::", "__")


def split_view(view):
    scope, _, name = view.rpartition("::")
    return scope or None, name


# ------------------------------------------------------------------ gdb half
def block_statics():
    """Inside gdb: every block-scope static of the loaded object, as
    [(function, name, address, gdb.Symbol)] (function: the innermost enclosing
    function, an inlined one included, without a routed `__orig` suffix)."""
    import re as _re
    sections = []
    for line in gdb.execute("maint info sections", to_string=True).splitlines():
        m = _re.match(r"\s*\[\d+\]\s+(0x[0-9a-f]+)->(0x[0-9a-f]+) at 0x[0-9a-f]+: (\S+) (.*)", line)
        if m and "CODE" in m.group(4):
            sections.append((int(m.group(1), 16), int(m.group(2), 16)))
    found, seen = {}, set()
    for lo, hi in sections:
        # Every address: a nested block starts anywhere inside its parent.
        for pc in range(lo, hi):
            block = gdb.block_for_pc(pc)
            if block is None or (block.start, block.end) in seen:
                continue
            seen.add((block.start, block.end))
            b = block
            while b is not None and not b.is_static and not b.is_global:
                for symbol in b:
                    if symbol.addr_class != gdb.SYMBOL_LOC_STATIC:
                        continue
                    f = b
                    while f is not None and f.function is None:
                        f = f.superblock
                    if f is None:
                        continue
                    function = f.function.name.removesuffix("__orig")
                    address = int(symbol.value().address)
                    found.setdefault((function, symbol.name, address), symbol)
                b = b.superblock
    return [(f, n, a, s) for (f, n, a), s in sorted(found.items(), key=lambda kv: kv[0])]


def _dies(obj):
    """The object's DWARF DIEs from readelf: [(depth, offset, tag, {attribute: text})]."""
    text = subprocess.run(["readelf", "--debug-dump=info", str(obj)], capture_output=True, text=True,
                          check=True).stdout
    dies = []
    for line in text.splitlines():
        head = re.match(r"\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)", line)
        if head:
            dies.append((int(head.group(1)), int(head.group(2), 16), head.group(3), {}))
            continue
        attribute = re.match(r"\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)", line)
        if attribute and dies:
            value = attribute.group(2).strip()
            indirect = re.match(r"\(indirect (?:line )?string, offset: (?:0x)?[0-9a-f]+\): (.*)", value)
            dies[-1][3][attribute.group(1)] = indirect.group(1) if indirect else value
    return dies


def dwarf_static_type(scope, name):
    """Inside gdb: the type of the block-scope static `name` of `scope` from the
    object's DWARF, for one gdb's blocks do not hold (an inline function's
    static whose calls left no inlined block: only the abstract instance
    declares it). None when the DWARF has not exactly one."""
    dies = _dies(gdb.objfiles()[0].filename)
    by_offset = {d[1]: d for d in dies}
    children = {}
    stack, found = [], []
    for die in dies:
        depth, offset, tag, attrs = die
        while stack and stack[-1][0] >= depth:
            stack.pop()
        if stack:
            children.setdefault(stack[-1][1], []).append(die)
        stack.append(die)
        if tag == "DW_TAG_variable" and attrs.get("DW_AT_name") == name and "DW_OP_addr" in attrs.get("DW_AT_location", ""):
            function = next((d for d in reversed(stack[:-1]) if d[2] == "DW_TAG_subprogram"), None)
            while function is not None and "DW_AT_name" not in function[3] and "DW_AT_abstract_origin" in function[3]:
                function = by_offset.get(int(function[3]["DW_AT_abstract_origin"].strip("<>"), 16))
            if function is not None and function[3].get("DW_AT_name", "").removesuffix("__orig") == scope:
                found.append(die)
    if len(found) != 1:
        return None

    def build(offset):
        _, _, tag, attrs = by_offset[offset]
        target = attrs.get("DW_AT_type")
        inner = (lambda: build(int(target.strip("<>"), 16))) if target else (lambda: gdb.lookup_type("void"))
        if tag in ("DW_TAG_base_type", "DW_TAG_typedef"):
            return gdb.lookup_type(attrs["DW_AT_name"])
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_enumeration_type"):
            keyword = {"DW_TAG_structure_type": "struct", "DW_TAG_union_type": "union",
                       "DW_TAG_enumeration_type": "enum"}[tag]
            return gdb.lookup_type(f"{keyword} {attrs['DW_AT_name']}")
        if tag == "DW_TAG_pointer_type":
            return inner().pointer()
        if tag == "DW_TAG_const_type":
            return inner().const()
        if tag == "DW_TAG_volatile_type":
            return inner().volatile()
        if tag == "DW_TAG_array_type":
            result = inner()
            for sub in reversed(children.get(offset, [])):
                bound = sub[3].get("DW_AT_upper_bound") or (sub[3].get("DW_AT_count") and
                                                            str(int(sub[3]["DW_AT_count"], 0) - 1))
                result = result.array(int(bound, 0) if bound else -1)
            return result
        raise RuntimeError(f"{scope}::{name}: DWARF type {tag} is not supported (battle_statics.py)")
    return build(int(found[0][3]["DW_AT_type"].strip("<>"), 16))


def _gdb_main(request_path):
    request = json.loads(pathlib.Path(request_path).read_text())
    msymbols = {}
    for line in gdb.execute("maint print msymbols", to_string=True).splitlines():
        m = re.match(r"\[\s*\d+\]\s+\S\s+(0x[0-9a-f]+)\s+(\S+)", line)
        if m:
            msymbols.setdefault(int(m.group(1), 16), []).append(m.group(2))
    out = []
    for function, name, address, _ in block_statics():
        out.append({"function": function, "name": name, "symbols": msymbols.get(address, [])})
    pathlib.Path(request["out"]).write_text(json.dumps(out))


# ------------------------------------------------------------------ build half
def _local_data(obj, nm):
    """{name: type letter} of the object's local data symbols, and the set of
    its global data symbols."""
    local, defined = {}, set()
    text = subprocess.run([*nm, "--defined-only", str(obj)], capture_output=True, text=True, check=True).stdout
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "bdrsg":
            local[parts[2]] = parts[1]
        elif len(parts) == 3 and parts[1] in "BDRSGC":
            defined.add(parts[2])
    return local, defined


def _scoped(obj, work):
    """The block-scope statics of obj with their object symbols, from gdb."""
    request = pathlib.Path(str(work) + ".statics.request")
    out = pathlib.Path(str(work) + ".statics.json")
    request.write_text(json.dumps({"out": str(out)}))
    result = subprocess.run(["gdb", "-batch", "-nx", "-iex", "set auto-load off", str(obj), "-ex",
                             f"python import sys; sys.argv = ['battle_statics', {str(request)!r}]",
                             "-x", str(pathlib.Path(__file__).resolve())], capture_output=True, text=True)
    if result.returncode or not out.exists():
        raise SystemExit(f"battle_statics: gdb on {obj}: {result.stderr[-2000:]}")
    return json.loads(out.read_text())


def declared(source, name):
    """Whether the preprocessed unit declares a static `name` (a static of one
    configuration only, 146C's `sp20` under ROOD_PC_LP64, is absent from the
    other's object). A later declarator of a list with initializers counts:
    `static short near_depth = 8, far_depth = 0x7ff;` declares `far_depth`."""
    return re.search(rb"\bstatic\b[^;{}()]*[\s*,]" + re.escape(name.encode()) + rb"\s*[\[;=,]", source) is not None


def configured(views, entries, source):
    """The views names of one configuration: `views` without the listed statics
    the preprocessed unit `source` (bytes) does not declare. 146C's `sp20` is a
    static only under ROOD_PC_LP64, so the 32-bit views have no accessor for it
    and its port tests the configuration (PORTING.md §6.5)."""
    absent = {view_name(scope, name) for scope, name in entries if not declared(source, name)}
    return [view for view in views if view not in absent]


def resolve(obj, object_stem, entries, nm=("nm",), required=True, work=None, source=None):
    """{object symbol: link name} for the listed statics of obj. A static
    already globalised (the link name defined) is skipped, and so is one the
    preprocessed unit `source` (bytes) does not declare."""
    local, defined = _local_data(obj, nm)
    renames, scoped = {}, None
    for scope, name in entries:
        target = link_name(object_stem, scope, name)
        if target in defined or (source is not None and not declared(source, name)):
            continue
        if scope is None:
            found = [name] if name in local else []
        else:
            found = sorted(n for n in local if re.fullmatch(re.escape(name) + r"\.\d+", n))
            # Several statics of that name, or several listed (zig_diff's
            # reference object keeps one function's): the DWARF scope decides.
            if len(found) > 1 or sum(1 for s, n in entries if s is not None and n == name) > 1:
                if scoped is None:
                    scoped = _scoped(obj, work or obj)
                mine = {s for r in scoped if r["function"] == scope and r["name"] == name for s in r["symbols"]}
                found = [n for n in found if n in mine]
        if not found and not required:
            continue
        if len(found) != 1:
            what = view_name(scope, name) + ("" if scope is None else f" ({name}.N)")
            raise SystemExit(f"battle_statics: {obj.name}: {what}, listed in STATE.tsv, matches "
                             f"{len(found)} local data symbols ({', '.join(found) or 'none'}): PORTING.md §6.5")
        if found[0] in renames:
            raise SystemExit(f"battle_statics: {obj.name}: {found[0]} claimed by {renames[found[0]]} and {target}")
        renames[found[0]] = target
    return renames


def globalise(obj, object_stem, entries, objcopy, nm=("nm",), required=True, work=None, source=None):
    """Rename and globalise obj's listed statics in place; objcopy(args, obj)
    runs the edit. Returns {object symbol: link name}."""
    renames = resolve(obj, object_stem, entries, nm=nm, required=required, work=work, source=source)
    if renames:
        objcopy([f"--redefine-sym={old}={new}" for old, new in sorted(renames.items())], obj)
        objcopy([f"--globalize-symbol={new}" for new in sorted(renames.values())], obj)
    return renames


# ---- pc/src asm-port units (PORTING.md §6.5, §20 "D2")
# STATE.tsv is built from what BATTLE's C functions reference, so it never lists
# the file-scope statics only a pc/src unit's own functions use (map_view.c's
# emitted-span table) and lists the others only when a BATTLE function
# touches them. For these units the object is the authority: every file-scope
# static data symbol it holds is globalised under `<unit object>__<name>`,
# STATE.tsv's rows or not. Function-local statics (`name.N`), string literals
# and other compiler symbols have a dot in the name and are left alone.
def file_statics(obj, object_stem, nm=("nm",)):
    """The names of obj's file-scope static data: its local data symbols
    without a dot (not yet globalised), and the globals this tool already made
    from them (`<object_stem>__name`, so zig_diff finds them in the built
    unit)."""
    local, defined = _local_data(obj, nm)
    names = {n for n, t in local.items() if t in "bdr" and re.fullmatch(r"\w+", n)}
    prefix = object_stem + "__"
    names |= {n[len(prefix):] for n in defined if n.startswith(prefix) and re.fullmatch(r"\w+", n[len(prefix):])}
    return sorted(names)


def globalise_unit(obj, object_stem, entries, objcopy, nm=("nm",), work=None, source=None):
    """globalise() for a pc/src unit: the STATE.tsv `entries` as before (the
    ones the preprocessed `source` does not declare skipped), then every other
    file-scope static the object holds. Returns the others, [(None, name)]:
    the statics STATE.tsv does not list, which the unit's views may still name."""
    globalise(obj, object_stem, entries, objcopy, nm=nm, work=work, source=source)
    listed_links = {(scope + "__" if scope else "") + name for scope, name in entries}
    local, _ = _local_data(obj, nm)
    rest = [name for name in file_statics(obj, object_stem, nm) if name not in listed_links]
    renames = {n: link_name(object_stem, None, n) for n in rest if n in local}
    if renames:
        objcopy([f"--redefine-sym={old}={new}" for old, new in sorted(renames.items())], obj)
        objcopy([f"--globalize-symbol={new}" for new in sorted(renames.values())], obj)
    return [(None, name) for name in rest]


def main(argv):
    if argv[:1] == ["--unit"]:
        # What a pc/src unit's object holds, against STATE.tsv: the check to run
        # on a build's objects (build/<name>.o) after a unit changes.
        table = listed()
        for path in map(pathlib.Path, argv[1:]):
            known = {name for scope, name in table.get(path.stem, []) if scope is None}
            for name in file_statics(path, path.stem):
                print(f"{path.name}: {name} -> {link_name(path.stem, None, name)}"
                      + ("" if name in known else "  (not in STATE.tsv)"))
        return 0
    table = listed()
    for path in map(pathlib.Path, argv):
        entries = table.get(path.stem, [])
        renames = resolve(path, path.stem, entries) if entries else {}
        for old, new in sorted(renames.items()):
            print(f"{path.name}: {old} -> {new}")
        if not entries:
            print(f"{path.name}: no listed statics")
    return 0


if IN_GDB and sys.argv[:1] == ["battle_statics"]:
    _gdb_main(sys.argv[1])
elif __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
