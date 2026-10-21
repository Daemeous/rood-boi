#!/usr/bin/env python3
"""The prompt a model gets for one bake-off function
(pc/docs/porting/battle/BAKEOFF.md): the task, PORTING.md, the two example
ports with their C, the function's C (the routed .i GCC compiled, and the
.c when it differs), and only the generated declarations the function
touches, extracted from the 64-bit build's views_<unit>/c_<unit> modules.

    python3 pc/tools/bakeoff_context.py FUNCTION [--out FILE] [--no-guide]
    python3 pc/tools/bakeoff_context.py --all OUTDIR      # one FUNCTION.md each, and sizes

FUNCTION is the original symbol or the module name (bakeoff.toml). The
declarations are chosen from the C body's identifiers: the views `g`
accessors of the globals it names, the c module's prototypes of the
functions it calls and the constants it names (a static of the unit shows
as its @compileError), and the views types it names (its parameters,
locals, casts, the globals' types) closed over the fields it accesses by
name (a renamed field matches its `/// orig:` name). Struct layouts keep
their fields and bitfield tables, not their offset asserts. psx.zig's
public API (signatures and doc comments) is included too. Needs a 64-bit
build (python3 pc/tools/build_boot.py --arch 64).
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

BAKEOFF = ROOT / "pc/tests/zig_diff/bakeoff.toml"
GUIDE = ROOT / "pc/docs/porting/PORTING.md"
# The two worked examples: a bitfield index past an array (psx.at), wrapping
# score arithmetic; and globals, psx_ptr stores, string tables, a weak hook.
EXAMPLES = [("_updateEnemyKills", "game/battle/score.zig"), ("vs_battle_displaySceneMessage", "game/battle/scene_message.zig")]
IDENT = re.compile(r"[A-Za-z_]\w*")


def bakeoff_functions():
    return tomllib.loads(BAKEOFF.read_text())["function"]


def unit_of(table, name):
    for sub in table.subsystems:
        if any(n == name for _, n in sub.functions):
            return sub
    raise SystemExit(f"bakeoff_context: {name} is not routed in game/ownership.toml")


def matching_brace(text, open_index):
    """The index of the `}` that closes the `{` at `open_index`, skipping
    string and character literals and comments (None when unbalanced)."""
    depth, i, n = 0, open_index, len(text)
    while i < n:
        ch = text[i]
        if ch in "\"'":
            i += 1
            while i < n and text[i] != ch:
                i += 2 if text[i] == "\\" else 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                return None
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2)
            if i < 0:
                return None
            i += 1
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return None


def c_body(text, name):
    """The definition of `name` (or `name__orig`) in C source text, from its
    return type to its matching closing brace (a one-line body ends on its
    own line)."""
    m = re.search(rf"^[A-Za-z_][^;{{}}\n]*\b{re.escape(name)}(?:__orig)?\s*\([^;{{]*\)\s*\{{", text, re.M | re.S)
    if not m:
        return None
    end = matching_brace(text, m.end() - 1)
    if end is None:
        return None
    return text[m.start():end + 1].replace(name + "__orig", name)


def blocks(text):
    """Top-level declarations of a generated Zig module: (name, text, kind),
    with the doc comment lines that precede each one."""
    out, lines, i, doc = [], text.split("\n"), 0, []
    while i < len(lines):
        line = lines[i]
        m = re.match(r"pub (?:const|extern fn|inline fn|fn) (\w+)", line)
        if line.startswith("///"):
            doc.append(line)
            i += 1
            continue
        if not m:
            doc, i = [], i + 1
            continue
        start = i
        if line.rstrip().endswith("{"):
            while i < len(lines) and lines[i] not in ("};", "}"):
                i += 1
        out.append((m.group(1), "\n".join(doc + lines[start:i + 1]), line))
        doc, i = [], i + 1
    return out


def strip_asserts(text):
    """A views struct without its comptime offset/size asserts."""
    out, skip = [], 0
    for line in text.split("\n"):
        if re.match(r"\s*comptime \{\s*$", line):
            skip = 1
            continue
        if skip:
            skip += line.count("{") - line.count("}")
            if skip <= 0 or line.strip() == "}":
                skip = 0
            continue
        out.append(line)
    return "\n".join(out)


def field_names(struct_text):
    """(field, orig name, type text) of a views struct's fields."""
    fields, orig = [], None
    for line in struct_text.split("\n")[1:]:
        m = re.match(r"\s*/// orig: (\w+)", line)
        if m:
            orig = m.group(1)
            continue
        m = re.match(r"\s{4}(@\"\w+\"|\w+): (.+),\s*$", line)
        if m:
            fields.append((m.group(1).strip('@"'), orig, m.group(2)))
        orig = None
    return fields


def views_subset(views_text, tokens):
    decls = blocks(views_text)
    types = {n: t for n, t, first in decls if re.search(r"= (extern struct|extern union|opaque|struct)\b", first) and n != "g"}
    g_text = next((t for n, t, _ in decls if n == "g"), "")
    accessors = []
    wanted = set()
    for m in re.finditer(r"((?:\s*/// .*\n)*)\s*pub inline fn (\w+)\(\) (.*?) \{ return psx\.global(?:Many)?\((.*?), \"(\w+)\"\); \}",
                         g_text):
        if m.group(5) in tokens:
            accessors.append((m.group(1) + "    pub inline fn " + m.group(2) + "() " + m.group(3)
                              + " { return psx.global" + ("Many" if m.group(3).startswith("[*]") else "") + "(" + m.group(4)
                              + ', "' + m.group(5) + '"); }').strip("\n"))
            wanted |= {t for t in IDENT.findall(m.group(3)) if t in types}
    wanted |= {t for t in tokens if t in types}
    # Close over the fields the body accesses by name.
    queue = list(wanted)
    while queue:
        name = queue.pop()
        for field, orig, ftype in field_names(types[name]):
            if field in tokens or (orig and orig in tokens):
                for t in IDENT.findall(ftype):
                    if t in types and t not in wanted:
                        wanted.add(t)
                        queue.append(t)
    header = [l for l in views_text.split("\n")[:4] if l.startswith(("//!", "const "))]
    body = [strip_asserts(types[n]) for n, _, _ in decls if n in wanted]
    g = "pub const g = struct {\n" + "\n".join(accessors) + "\n};" if accessors else ""
    return "\n".join(header) + "\n" + "\n".join(body) + ("\n" + g if g else "")


def c_subset(c_text, tokens, own):
    decls = blocks(c_text)
    by_name = {n: (t, first) for n, t, first in decls}
    out, types = [], set()

    def show(n):
        t, first = by_name[n]
        if len(t.split("\n")) == 1:
            out.append(t)
            target = re.match(r"pub const \w+ = (\w+);", first)
            if target and target.group(1) in by_name:
                show(target.group(1))
        elif re.search(r"= opaque \{", first):
            out.append(first.split("=")[0] + "= opaque { ... }; // incomplete here (translate-c could not lay it out): "
                       "view pointers go to it through @ptrCast")
        elif re.search(r"= (extern struct|extern union|struct|union) \{", first):
            out.append(first + " ... }; // the views type of this name has the layout")
        else:
            out.append(t)

    for n, t, first in decls:
        if n in tokens and n != own and first.startswith("pub extern fn"):
            out.append(t)
            types |= set(IDENT.findall(first[len("pub extern fn " + n):]))
    for n in sorted((tokens | types) - {own}):
        if n in by_name and not by_name[n][1].startswith("pub extern fn") and not n.startswith("__"):
            show(n)
    return "\n".join(dict.fromkeys(out))


def psx_api():
    text = (ROOT / "game/common/psx.zig").read_text()
    out, doc = [], []
    for line in text.split("\n"):
        if line.startswith("test "):
            break
        if line.startswith("///"):
            doc.append(line)
            continue
        if line.startswith("pub "):
            out += doc + [line.rstrip(" {") + (" { ... }" if line.rstrip().endswith("{") else "")]
        doc = []
    return "\n".join(out)


def disassembly(obj, name):
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "-r", f"--disassemble={name}", str(obj)],
                          capture_output=True, text=True).stdout
    start = text.find(f"<{name}>:")
    if start < 0:
        return None
    end = text.find("\nDisassembly of section", start)
    return text[start:end if end > 0 else None].strip()


def context(entry, table, build, guide=True, sub=None, views_path=None, c_path=None):
    """sub, views_path, c_path: pc/tools/port_pipeline.py's, for a function
    the table and the build do not route yet."""
    name, module = entry["name"], entry["module"]
    sub = sub or unit_of(table, name)
    stem = pathlib.PurePosixPath(sub.unit).stem
    unit_i = next(build.glob(f"*_{stem}.i"), None)
    views_path = pathlib.Path(views_path) if views_path else build / "zig" / f"views_{stem}.zig"
    c_path = pathlib.Path(c_path) if c_path else build / "zig" / f"c_{stem}.zig"
    for p in (unit_i, views_path, c_path):
        if p is None or not p.exists():
            raise SystemExit(f"bakeoff_context: {p or stem}: missing (python3 pc/tools/build_boot.py --arch 64)")
    i_body = c_body(unit_i.read_text(errors="replace"), name)
    c_source = c_body((ROOT / sub.unit).read_text(errors="replace"), name)
    if i_body is None:
        raise SystemExit(f"bakeoff_context: no {name} in {unit_i}")
    tokens = set(IDENT.findall(i_body)) | set(IDENT.findall(c_source or ""))
    parts = [f"""# Port `{name}` ({sub.unit}) to Zig

Write the complete file `game/battle/{module}.zig`: a faithful port of the C
below, per the porting guide (PORTING.md, included). It is compiled with
Zig 0.17 (ReleaseSafe, x86_64-linux) against the generated modules shown
here and compared byte for byte with the original C on random and edge
inputs (all memory it touches, every external call with its arguments and
the state at that call, the return value).

Rules for this file:
- It declares exactly this function (parameter names are free; an unused
  parameter must be `_`):

      {entry['signature']}

- Its doc comment starts with `/// orig: {name} ({sub.unit})`, then one
  line saying what it does; the file starts with a `//!` line naming the unit.
- It may import only `std`, `common` (`const psx = @import("common").psx;`),
  `views_{stem}` (`const v = @import("views_{stem}");`) and `c_{stem}`
  (`const c = @import("c_{stem}");`).
- Reply with the file only."""]
    if guide:
        parts.append("## The porting guide (pc/docs/porting/PORTING.md)\n\n" + GUIDE.read_text())
    for example, path in EXAMPLES:
        esub = unit_of(table, example)
        estem = pathlib.PurePosixPath(esub.unit).stem
        ei = next(build.glob(f"*_{estem}.i"))
        parts.append(f"## Example: `{example}` ({esub.unit})\n\nThe C (as GCC compiled it, from the .i):\n\n```c\n"
                     + c_body(ei.read_text(errors="replace"), example) + "\n```\n\nIts port, `" + path + "`:\n\n```zig\n"
                     + (ROOT / path).read_text().strip() + "\n```")
    parts.append(f"## The C to port\n\nAs GCC compiled it (the routed `.i`: `ROOD_PC` branches selected, macros "
                 f"expanded, `psx_get`/`psx_set` spelled out):\n\n```c\n{i_body}\n```")
    if c_source and re.sub(r"\s+", " ", c_source) != re.sub(r"\s+", " ", i_body):
        parts.append(f"The source (`{sub.unit}`, for names and comments):\n\n```c\n{c_source}\n```")
    if entry.get("asm"):
        asm = disassembly(unit_i.with_suffix(".o"), name)
        if asm:
            parts.append("The game's 64-bit object for it (`objdump -dr`), for what the C leaves undefined "
                         f"(PORTING.md §1, §11.4):\n\n```\n{asm}\n```")
    parts.append(f"## `views_{stem}` (the declarations this function touches)\n\nGenerated from the unit object's DWARF; "
                 f"`/// orig:` marks a readable name for the original field or global. Types of fields this "
                 f"function does not access are left out.\n\n```zig\n"
                 + views_subset(views_path.read_text(), tokens) + "\n```")
    parts.append(f"## `c_{stem}` (the declarations this function touches)\n\ntranslate-c of the unit's declarations "
                 f"(call C through these; pass view pointers with `psx.cptr(c.T, p)` or `@ptrCast`):\n\n```zig\n"
                 + c_subset(c_path.read_text(), tokens, name) + "\n```")
    parts.append("## `common.psx` (game/common/psx.zig, public API)\n\n```zig\n" + psx_api() + "\n```")
    return "\n\n".join(parts) + "\n"


def find(entries, key):
    for e in entries:
        if key in (e["name"], e["module"]):
            return e
    raise SystemExit(f"bakeoff_context: {key}: not a bake-off function ({BAKEOFF.relative_to(ROOT)})")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("function", nargs="?")
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument("--all", type=pathlib.Path, metavar="OUTDIR")
    parser.add_argument("--no-guide", action="store_true", help="leave PORTING.md out (it is the same for every function)")
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    args = parser.parse_args(argv)
    if bool(args.function) == bool(args.all):
        parser.error("give FUNCTION or --all OUTDIR")
    entries, table = bakeoff_functions(), ownership.load()
    if args.all:
        args.all.mkdir(parents=True, exist_ok=True)
        for e in entries:
            text = context(e, table, args.build, guide=not args.no_guide)
            (args.all / f"{e['name']}.md").write_text(text)
            print(f"{e['name']:28} {len(text.splitlines()):6} lines {len(text):8} chars", file=sys.stderr)
        return 0
    text = context(find(entries, args.function), table, args.build, guide=not args.no_guide)
    if args.out:
        args.out.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
