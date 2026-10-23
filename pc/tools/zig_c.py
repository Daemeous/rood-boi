#!/usr/bin/env python3
"""The `c` module for owned Zig code (pc/docs/porting/PORTING.md §2): every
declaration an original unit sees (its typedefs, prototypes, enum constants
and externs, including the ones the .c file declares itself, and the
block-scope `extern` declarations of its ROOD_PC hooks), from the exact text
GCC compiled (the unit's routed .i), as `zig translate-c` makes of it.

    translate(zig, unit_i, out, target)

Post-processing:
- the unit's data definitions become extern declarations (the C object
  defines them);
- the unit's file-scope `static`s (variables, tables, functions, header
  `static inline`s) become `@compileError` declarations: translate-c would
  otherwise give the Zig object its own copies (a second `charge_skip_held`,
  a `.rodata` copy of a table at another address), which owned code must
  never reach (PORTING.md §6.5). Using one fails the build, naming it;
- any other mutable `pub var` left fails the generation;
- the translated _Static_asserts go (some assert sizes of types translate-c
  demotes to opaque: the views assert layouts instead);
- warning comments lose the scratch path;
- a routed function the unit has no prototype of (only its renamed
  definition, FUNCTION__orig) is declared under its own name too: the
  routed symbol owned code calls;
- an opaque type (a union or struct with bitfields) a prototype passes by
  value becomes the unsigned integer of its size when that is 1, 2, 4 or 8
  bytes (Zig allows no opaque parameter; the calling conventions pass such
  an all-integer aggregate as that integer), and so does one a struct
  embeds by value when its alignment is its size (Zig embeds no opaque);
- `T f();` (no prototype) before the unit's own declaration or definition
  of f with its parameters: the unprototyped one is dropped from the header
  translate-c reads (it keeps the first, `f(...)`, which no port can
  define), and `f(void)` is declared `f()`.
The Windows twin's module is translated from the twin's own .i (MinGW
preprocessed, the game's longs already respelled `long long` there,
build_boot.py lp64_longs) with a Linux target for GCC's bitfield layout:
its remaining `long`s are MinGW's 4-byte ones, which `c_long` already is
when the module is compiled for Windows, so nothing is respelled here.
Extern variables are left in but are never used (globals go through the
views' psx.global: no .refptr on Windows); types come from the views,
functions and constants from here.
"""
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ownership  # noqa: E402

_ASSERT = re.compile(r"comptime \{\n\s*if \(.*?\) @compileError\(\"static assertion failed.*?\n\}\n?", re.S)


def _declarations_only(text):
    """The unit's data definitions (tables with initialisers: strip_bodies
    keeps them) as extern declarations: translate-c makes them `pub export
    var/const`, which would define them a second time in the Zig object."""
    out, lines, i = [], text.split("\n"), 0
    while i < len(lines):
        m = re.match(r"pub export (var|const) (\w+): (.*?) = ", lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        depth, j = 0, i
        while True:
            depth += lines[j].count("{") - lines[j].count("}")
            if depth <= 0 and lines[j].rstrip().endswith(";"):
                break
            j += 1
        out.append(f"pub extern {m.group(1)} {m.group(2)}: {m.group(3)};")
        i = j + 1
    return "\n".join(out)


_KEYWORDS = {b"void", b"char", b"short", b"int", b"long", b"unsigned", b"signed", b"float", b"double",
             b"const", b"volatile", b"struct", b"union", b"enum", b"static", b"extern", b"inline",
             b"__inline", b"__inline__", b"_Bool", b"register", b"restrict", b"__restrict"}


def static_names(text):
    """The names a unit's file-scope `static` declarations declare (objects,
    functions, header `static inline`s): every identifier of a depth-0
    statement that starts `static` (after attributes), outside parameter
    lists, attributes, array bounds and initialisers. Over-approximate on
    purpose: a name is only acted on when translate-c also emitted it as a
    value or function."""
    decls = ownership.strip_bodies(text)
    tokens = [m.group() for m in ownership._TOKEN.finditer(decls)]
    names, statement, depth = set(), [], 0
    for t in tokens:
        if t.startswith(b"#"):
            continue
        statement.append(t)
        if t == b"{":
            depth += 1
        elif t == b"}":
            depth -= 1
        elif t == b";" and depth == 0:
            if b"static" in statement and b"typedef" not in statement:
                names |= _declarators(statement)
            statement = []
    return {n.decode() for n in names}


def _declarators(statement):
    out, i, depth, previous = set(), 0, 0, None
    skipping_init = False
    while i < len(statement):
        t = statement[i]
        if t in (b"__attribute__", b"__asm__", b"asm") and i + 1 < len(statement) and statement[i + 1] == b"(":
            i = _skip(statement, i + 1, b"(", b")")
            continue
        if t == b"(" and previous is not None and (previous == b")" or (
                re.fullmatch(rb"[A-Za-z_]\w*", previous) and previous not in _KEYWORDS)):
            i = _skip(statement, i, b"(", b")")  # a parameter list
            previous = b")"
            continue
        if t == b"[":
            i = _skip(statement, i, b"[", b"]")
            previous = b"]"
            continue
        if t == b"=" and depth == 0:
            skipping_init = True
        elif t == b"," and depth == 0:
            skipping_init = False
        elif t in (b"(", b"{"):
            depth += 1
        elif t in (b")", b"}"):
            depth -= 1
        elif not skipping_init and re.fullmatch(rb"[A-Za-z_]\w*", t) and t not in _KEYWORDS:
            out.add(t)
        previous = t
        i += 1
    return out


def _skip(tokens, i, open_, close):
    depth = 0
    for j in range(i, len(tokens)):
        if tokens[j] == open_:
            depth += 1
        elif tokens[j] == close:
            depth -= 1
            if depth == 0:
                return j + 1
    return len(tokens)


# A translated value or function (a type alias has no `: T`).
_VALUE = re.compile(r"pub (?:extern )?(?:var|const) (\w+):|pub extern fn (\w+)\(")


def _poison_statics(text, statics, module):
    """Replace translate-c's copies of the unit's statics with compile errors."""
    out, lines, i, poisoned = [], text.split("\n"), 0, []
    while i < len(lines):
        m = _VALUE.match(lines[i])
        name = m and (m.group(1) or m.group(2))
        if not name or name not in statics:
            out.append(lines[i])
            i += 1
            continue
        depth, j = 0, i
        while True:
            depth += lines[j].count("{") - lines[j].count("}")
            if depth <= 0 and lines[j].rstrip().endswith(";"):
                break
            j += 1
        out.append(f'pub const {name} = @compileError("{module}.{name} is a file-scope static of the unit: '
                   f'owned code cannot share it through c (PORTING.md §6.5)");')
        poisoned.append(name)
        i = j + 1
    return "\n".join(out), poisoned


def _routed_names(text):
    """A routed function's own name where the unit has no prototype of it:
    the build renames the definition FUNCTION__orig, and the routed symbol
    FUNCTION (the router's) is what owned code calls, a sibling or itself."""
    have = set(re.findall(r"^pub extern fn (\w+)\(", text, re.M))
    extra = [f"pub extern fn {m.group(1)}({m.group(2)}) {m.group(3)};"
             for m in re.finditer(r"^pub extern fn (\w+)__orig\((.*)\) (.+);$", text, re.M) if m.group(1) not in have]
    return text.rstrip("\n") + "\n" + "".join(e + "\n" for e in extra) if extra else text


_OPAQUE = re.compile(r"^pub const (\w+) = opaque \{\};$", re.M)


def by_value_opaques(text, fields=False):
    """The opaque types (translate-c's demotion of a union or struct with
    bitfields) a prototype passes or returns by value: Zig rejects such a
    parameter, so neither a port nor zig_diff's stub could name the function.
    fields: those a struct embeds by value instead (`position:
    vs_battle_movementPosition`, 7EE98's movementRecoveryState): Zig cannot
    embed an opaque, so no port could name a function taking that struct."""
    opaque = set(_OPAQUE.findall(text))
    used = set()
    if fields:
        for m in re.finditer(r"^\s+\w+: (?:\[\d+\])*(\w+)(?: = .*)?,$", text, re.M):
            if m.group(1) in opaque:
                used.add(m.group(1))
        return sorted(used)
    for m in re.finditer(r"^pub extern fn \w+\((.*)\) (.+);$", text, re.M):
        for part in [p.strip() for p in m.group(1).split(",")] + [m.group(2).strip()]:
            name = part.split(":")[-1].strip()
            if name in opaque:
                used.add(name)
    return sorted(used)


def _sized_opaques(text, zig, decls, target):
    """A by-value opaque type of 1, 2, 4 or 8 bytes becomes the unsigned
    integer of its size (clang's sizeof, asked through translate-c): the
    calling convention passes such an all-integer union or struct as that
    integer on every target the game builds (i386 cdecl, x86_64 SysV,
    Windows x64). A port passes its bytes (`psx.byteAt(u32, &tile, 0).*`)."""
    params = by_value_opaques(text)
    embedded = [n for n in by_value_opaques(text, fields=True) if n not in params]
    names = params + embedded
    if not names:
        return text
    def c_name(n):
        for tag in ("struct", "union"):
            if n.startswith(tag + "_"):
                return f"{tag} {n[len(tag) + 1:]}"
        return n
    probe = decls.with_suffix(".sizes.h")
    probe.write_bytes(decls.read_bytes() + ("\nenum {" + ",".join(
        f"zig_c_size_{n} = sizeof({c_name(n)}), zig_c_align_{n} = _Alignof({c_name(n)})" for n in names) + "};\n").encode())
    result = subprocess.run([str(zig), "translate-c", "-target", target, str(probe)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"zig_c: sizing {', '.join(names)}: {result.stderr[-2000:]}")
    sizes = {k: int(v) for k, v in re.findall(r"pub const zig_c_size_(\w+): c_int = (\d+);", result.stdout)}
    aligns = {k: int(v) for k, v in re.findall(r"pub const zig_c_align_(\w+): c_int = (\d+);", result.stdout)}
    for n in names:
        if sizes.get(n) not in (1, 2, 4, 8):
            continue
        if n in embedded:
            # As a field, the integer must also sit where the C puts the aggregate.
            if aligns.get(n) != sizes[n]:
                continue
            note = "embedded by value (zig_c.py: an opaque field)"
        else:
            note = "passed by value (zig_c.py: an opaque by value)"
        text = text.replace(f"pub const {n} = opaque {{}};", f"pub const {n} = u{sizes[n] * 8}; // {note}")
    return text


def _prototyped_void(text, decls):
    """`void f();` (a header's unprototyped declaration) before the unit's own
    `void f(void)`: translate-c keeps the first, `f(...)`, which no Zig port
    can define; the `(void)` the unit also declares is the prototype."""
    def fix(m):
        if re.search(rb"\b" + m.group(1).encode() + rb"\s*\(\s*void\s*\)", decls):
            return f"pub extern fn {m.group(1)}() {m.group(2)};"
        return m.group(0)
    return re.sub(r"^pub extern fn (\w+)\(\.\.\.\) (.+);$", fix, text, flags=re.M)


def _drop_unprototyped(decls):
    """A function declared `T f();` (no prototype) and declared again with its
    parameters (the unit's definition, stripped to a declaration): translate-c
    keeps the first, `f(...)`, which no port can define or call typed (44F14's
    func_800B101C). The unprototyped declaration goes; a body-less header
    needs no order."""
    names = set(re.findall(rb"^[\w\s\*]*?\b(\w+)\s*\(\s*\)\s*;", decls, re.M))
    for name in names:
        if re.search(rb"\b" + re.escape(name) + rb"\s*\((?!\s*\))[^;]*\)\s*;", decls):
            decls = re.sub(rb"^[\w\s\*]*?\b" + re.escape(name) + rb"\s*\(\s*\)\s*;[ \t]*$", b"", decls, flags=re.M)
    return decls


def translate(zig, unit_i, out, target):
    out = pathlib.Path(out)
    decls = out.with_suffix(".decls.h")
    source = pathlib.Path(unit_i).read_bytes()
    decls.write_bytes(_drop_unprototyped(ownership.strip_bodies(source)))
    result = subprocess.run([str(zig), "translate-c", "-target", target, str(decls)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"zig translate-c {unit_i}: {result.stderr[-2000:]}")
    module = out.name.split(".")[0]
    text, _ = _poison_statics(result.stdout, static_names(source), module)
    text = _declarations_only(_ASSERT.sub("", text))
    left = re.findall(r"^pub var (\w+)", text, re.M)
    if left:
        raise SystemExit(f"zig_c {unit_i}: translate-c left mutable copies {', '.join(left)}: "
                         "neither an extern nor a static of the unit (zig_c.py)")
    text = _sized_opaques(text, zig, decls, target)
    text = _prototyped_void(text, decls.read_bytes())
    text = text.replace(str(decls), pathlib.Path(unit_i).name)
    text = _routed_names(text)
    text = "//! Generated by build_boot.py (pc/tools/zig_c.py): do not edit.\n" + text
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    return out
