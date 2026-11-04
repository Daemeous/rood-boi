#!/usr/bin/env python3
"""Owned game code (pc/docs/Ownership.md): the routing table game/ownership.toml
and the build steps that route a function between the original (src/) and
owned code (game/):

  - rename_definitions: in a unit's preprocessed text, the routed functions'
    definitions become name__orig; calls, prototypes and address-of uses keep
    the name, which the trampoline defines (a static function of a host
    .inc the unit includes is made global the same way);
  - strip_bodies: the unit's declarations only (the input of zig translate-c);
  - routing_source: the trampolines (asm per ABI) and the subsystem tables;
  - ownership_zig: the table as a comptime Zig module.
"""
import dataclasses
import pathlib
import re
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OWNERS = ("original", "owned")
MAX_SUBSYSTEMS = 128  # pc_owner[] in ownership.c


@dataclasses.dataclass
class Subsystem:
    name: str
    index: int
    overlay: str
    unit: str
    default: str
    roots: list
    functions: list  # [(address, name)]
    views: dict
    # Original name -> the Zig function that ports it, as a path from the
    # overlay root ("score.updateEnemyKills"); absent: the original name.
    zig: dict = dataclasses.field(default_factory=dict)

    def zig_name(self, name):
        return self.zig.get(name, name)


@dataclasses.dataclass
class Table:
    subsystems: list

    def routed_names(self, unit):
        return {name for s in self.subsystems if s.unit == unit for _, name in s.functions}

    def for_build(self, compiled_units, overlays):
        """The subsystems this build can route: the unit is compiled here AND
        the subsystem's overlay (its game/<overlay> Zig object and the
        overlay's function addresses) is part of the build. A unit that every
        configuration compiles (pc/src/boot_sdk.c is in the plain boot build
        as well) must not drag a battle subsystem into a build that has no
        BATTLE.PRG: the table check would find no function at the battle
        addresses and the empty battle Zig object would be a new link input."""
        return Table([s for s in self.subsystems if s.unit in compiled_units and s.overlay in overlays])

    def routes(self):
        """[(name, subsystem index)] in table order."""
        return [(name, s.index) for s in self.subsystems for _, name in s.functions]

    def zig_routes(self):
        """[(original name, Zig function path, subsystem index)] in table order."""
        return [(name, s.zig_name(name), s.index) for s in self.subsystems for _, name in s.functions]

    def problems(self, function_addresses, compiled_units):
        """Inconsistencies with the reference ELFs and the units this build
        compiles; [] when the table can be used. function_addresses is
        name -> address, or (name, address) pairs: a static name two units
        define (BATTLE's _nop1 and SLUS's) needs pairs, a dict keeps one."""
        # ENDING loads over BATTLE (0x80068800): two names can share an address
        # (146C's vs_battle_copyInventoryBladeStats and ENDING's _addRenderer, 0x8006AB44).
        pairs = function_addresses.items() if hasattr(function_addresses, "items") else function_addresses
        by_address = {}
        for n, a in pairs:
            if a is not None and n not in by_address.setdefault(a, []):
                by_address[a].append(n)
        out = []
        for s in self.subsystems:
            if s.unit not in compiled_units:
                out.append(f"{s.name}: its unit {s.unit} is not compiled in this build")
            for address, name in s.functions:
                if address is None:  # host function: nothing in the reference ELFs to check
                    continue
                names = by_address.get(address, [])
                if name not in names:
                    actual = " / ".join(sorted(names))
                    out.append(f"{s.name}: {address:#x} is {actual or 'no function'}, the table says {name}"
                               + (f" (renamed upstream? use {actual})" if actual else ""))
        return out


def orig_comment_problems(table, game_root=ROOT / "game"):
    """Each route with a Zig name must be a `pub fn` under game/<overlay>/
    whose doc comment has the line `/// orig: <name> (<unit>)` (PORTING.md
    §16): the table and the source say the same pairing. [] when they do."""
    out = []
    for s in table.subsystems:
        for _, name in s.functions:
            if name not in s.zig:
                continue
            fn = s.zig[name].rsplit(".", 1)[-1]
            pattern = re.compile(r"^[ \t]*/// orig: " + re.escape(name) + r" \(" + re.escape(s.unit) + r"\)[ \t]*\n"
                                 r"(?:[ \t]*///[^\n]*\n)*[ \t]*pub fn " + re.escape(fn) + r"\(", re.M)
            hits = [p for p in sorted((pathlib.Path(game_root) / s.overlay).rglob("*.zig"))
                    if pattern.search(p.read_text())]
            if len(hits) != 1:
                out.append(f"{s.name}: {name} -> {s.zig[name]}: expected one `/// orig: {name} ({s.unit})` "
                           f"doc line on `pub fn {fn}` under game/{s.overlay}/, found {len(hits)}")
    return out


_IMPORT = re.compile(r'^pub const (\w+) = @import\("([^"]+\.zig)"\);', re.M)


def zig_source(overlay, zig_path, game_root=ROOT / "game", candidate=False):
    """(file, function) a route's `zig` path names: each module on the path
    is a `pub const NAME = @import("file.zig");` of the file before it,
    starting at game/<overlay>/root.zig ("score.updateEnemyKills" ->
    game/battle/score.zig; D2's "pc.func_800D826C.func_800D826C" ->
    game/battle/pc/func_800D826C.zig). The import path is relative to the
    importing file, so a subdirectory's root.zig can nest further. With
    `candidate` (zig_diff: a port being written and not imported yet, or one a
    stand-in or reference run never compiles), a
    last module the file does not import is `<module>.zig` beside it."""
    *modules, function = zig_path.split(".")
    file = pathlib.Path(game_root) / overlay / "root.zig"
    for i, module in enumerate(modules):
        imports = dict(_IMPORT.findall(file.read_text()))
        if module not in imports:
            if candidate and i == len(modules) - 1:
                return file.parent / f"{module}.zig", function
            raise ValueError(f"{zig_path}: {file.relative_to(game_root)} has no `pub const {module} = @import(\"...\")`")
        file = file.parent / imports[module]
    return file, function


def load(path=ROOT / "game/ownership.toml"):
    data = tomllib.loads(pathlib.Path(path).read_text())
    subsystems, seen = [], {}
    for index, (name, entry) in enumerate(data.get("subsystem", {}).items()):
        default = entry.get("default", "original")
        if default not in OWNERS:
            raise ValueError(f"{name}: default {default!r} is not one of {', '.join(OWNERS)}")
        # A host function (pc/src/*.inc, generated wrappers) has no reference-ELF
        # address: `{ name = "pc_cb_80092540", host = true }`; problems() skips it.
        functions = [(int(f["addr"]) if "addr" in f else None, f["name"]) for f in entry.get("functions", [])]
        for f in entry.get("functions", []):
            if ("addr" in f) == bool(f.get("host")):
                raise ValueError(f"{name}: {f['name']}: give either addr or host = true (a host function has no address)")
        zig = {f["name"]: f["zig"] for f in entry.get("functions", []) if "zig" in f}
        for original, path in zig.items():
            if not re.fullmatch(r"[A-Za-z_]\w*(\.[A-Za-z_]\w*)*", path):
                raise ValueError(f"{name}: {original}: zig = {path!r} is not a Zig declaration path")
        for _, fn in functions:
            if fn in seen:
                raise ValueError(f"{fn} is routed by both {seen[fn]} and {name}")
            seen[fn] = name
        subsystems.append(Subsystem(name, index, entry["overlay"], entry["unit"], default,
                                    list(entry.get("roots", [])), functions, dict(entry.get("views", {})), zig))
    if len(subsystems) > MAX_SUBSYSTEMS:
        raise ValueError(f"{len(subsystems)} subsystems: pc_owner[] holds {MAX_SUBSYSTEMS}")
    return Table(subsystems)


_TOKEN = re.compile(rb'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|^#[^\n]*|[A-Za-z_]\w*|\S', re.M)


def _matching(tokens, i, open_=b"(", close=b")"):
    depth = 0
    for j in range(i, len(tokens)):
        if tokens[j][2] == open_:
            depth += 1
        elif tokens[j][2] == close:
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced " + open_.decode())


def _skip_attributes(tokens, k):
    while k < len(tokens) and tokens[k][2] in (b"__attribute__", b"__asm__", b"asm"):
        k = _matching(tokens, k + 1) + 1
    return k


def _body_start(tokens, k):
    """Body after parameters and attributes, including K&R declarations."""
    if k >= len(tokens):
        return None
    if tokens[k][2] == b"{":
        return k
    if not re.fullmatch(rb"[A-Za-z_]\w*", tokens[k][2]):
        return None
    depth = 0
    for j in range(k, len(tokens)):
        t = tokens[j][2]
        if t == b"(":
            depth += 1
        elif t == b")":
            depth -= 1
        elif depth == 0 and t == b";":
            continue
        elif depth == 0 and t == b"{":
            return j
        elif depth == 0 and t in (b"=", b","):
            return None
    return None


def _file_scope_functions(text):
    """Tokens and (name, parameters, body, declaration start) indices."""
    tokens = [(m.start(), m.end(), m.group()) for m in _TOKEN.finditer(text)]
    found, depth, start, i = [], 0, 0, 0
    while i < len(tokens):
        t = tokens[i][2]
        if t.startswith(b"#"):
            start = i + 1 if depth == 0 and start == i else start
            i += 1
            continue
        if t == b"{":
            depth += 1
        elif t == b"}":
            depth -= 1
            if depth == 0:
                start = i + 1
        elif t == b";" and depth == 0:
            start = i + 1
        elif depth == 0 and re.fullmatch(rb"[A-Za-z_]\w*", t) and i + 1 < len(tokens) and tokens[i + 1][2] == b"(":
            close = _matching(tokens, i + 1)
            # A leading attribute is not a function declarator.
            if t in (b"__attribute__", b"__asm__", b"asm"):
                i = close + 1
                continue
            body = _body_start(tokens, _skip_attributes(tokens, close + 1))
            if body is not None:
                found.append((i, i + 1, body, start))
                end = _matching(tokens, body, b"{", b"}")
                i, start = end + 1, end + 1
                continue
            i = close + 1
            continue
        i += 1
    return tokens, found


# Files whose `static` functions may be routed (see rename_definitions): the host
# C that a game unit #includes (pc/src/*.inc, the generated *_callbacks.inc).
HOST_INCLUDE_SUFFIXES = (".inc",)

_LINEMARKER = re.compile(rb'^#(?:line)?[ \t]+\d+[ \t]+"((?:\\.|[^"\\\n])*)"', re.M)


def _after_blanks(text, i):
    while text[i:i + 1] in (b" ", b"\t"):
        i += 1
    return i


def _file_at(markers, offset):
    """The source file the preprocessor says `offset` is in (linemarkers:
    `# 12 "pc/src/x.inc" 1`), b"" before the first one."""
    import bisect
    i = bisect.bisect_right(markers, (offset, b"\xff")) - 1
    return markers[i][1] if i >= 0 else b""


def rename_definitions(text, names, host_suffixes=HOST_INCLUDE_SUFFIXES):
    """Rename file-scope definitions only; return bytes and per-name counts.
    Each renamed definition is preceded by a declaration of the original
    name with the definition's own header (`T* f(int);`), so the unit's later
    calls still see the prototype when the definition was the unit's only
    one: without it they would be implicit `int f()` calls, which truncate a
    returned pointer on LP64 (2842C's func_8009195C, called by
    func_800919D8 after it). A K&R definition gets `T f();`.

    A `static` definition is refused, except one that the linemarkers place in
    a host include (`host_suffixes`: the .inc files game units #include, such
    as pc/src/room_callbacks.inc or the generated effect_callbacks.inc): there
    `static` is dropped from the definition (it becomes the global F__orig),
    from the prelude declaration and from every earlier file-scope
    declaration of F, so F is the external symbol the trampoline defines and
    the unit's calls and address-of uses reach it. Host names are unique
    (pc_cb_<addr>, charge_skip_*), so the global names cannot collide; a
    `static inline` is refused. Unrouted statics keep their linkage."""
    tokens, found = _file_scope_functions(text)
    markers = sorted((m.start(), m.group(1)) for m in _LINEMARKER.finditer(text))
    wanted = {n.encode(): n for n in names}
    counts = {n: 0 for n in names}
    edits, unstatic = [], set()
    for name_index, params, body, start in found:
        token = tokens[name_index][2]
        if token not in wanted:
            continue
        statics = [i for i in range(start, name_index) if tokens[i][2] == b"static"]
        if statics:
            where = _file_at(markers, tokens[name_index][0]).decode(errors="replace")
            if not where.endswith(tuple(host_suffixes)):
                raise ValueError(f"{wanted[token]}: a static function cannot be routed "
                                 f"(only static host code in {'/'.join(host_suffixes)} files can)")
            if any(tokens[i][2] == b"inline" for i in range(start, name_index)):
                raise ValueError(f"{wanted[token]}: a static inline function cannot be routed")
            unstatic.add(token)
            edits += [(tokens[i][0], _after_blanks(text, tokens[i][1]), b"") for i in statics]
        counts[wanted[token]] += 1
        close = _matching(tokens, params)
        if _skip_attributes(tokens, close + 1) != body:  # K&R parameter declarations follow
            end, tail = tokens[params][0], b"()"
        else:
            end, tail = tokens[close][1], b""
        head, at = b"", tokens[start][0]
        for i in range(start, name_index):  # the header without its `static`
            if i in statics:
                head += text[at:tokens[i][0]]
                at = _after_blanks(text, tokens[i][1])
        head += text[at:end] + tail
        edits.append((tokens[start][0], tokens[start][0], head + b";\n"))
        edits.append((tokens[name_index][0], tokens[name_index][1], token + b"__orig"))
    if unstatic:
        edits += _static_declarations(text, tokens, unstatic)
    for begin, end, new in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        text = text[:begin] + new + text[end:]
    return text, counts


def _static_declarations(text, tokens, names):
    """Edits removing `static` from the file-scope prototypes of `names` (the
    ones whose static definition was made global): a later non-static
    declaration would otherwise keep the earlier static linkage."""
    edits, depth, start = [], 0, 0
    for i, (_, _, t) in enumerate(tokens):
        if t.startswith(b"#"):
            start = i + 1 if depth == 0 and start == i else start
        elif t == b"{":
            depth += 1
        elif t == b"}":
            depth -= 1
            if depth == 0:
                start = i + 1
        elif t == b";" and depth == 0:
            start = i + 1
        elif depth == 0 and t in names and i + 1 < len(tokens) and tokens[i + 1][2] == b"(" \
                and _body_start(tokens, _skip_attributes(tokens, _matching(tokens, i + 1) + 1)) is None:
            edits += [(tokens[j][0], _after_blanks(text, tokens[j][1]), b"")
                      for j in range(start, i) if tokens[j][2] == b"static"]
    return edits


def _block_externs(text, tokens, body, end):
    """The block-scope `extern` declarations in a body (the ROOD_PC hooks
    declare their host functions there), as file-scope declarations."""
    out, i = [], body + 1
    while i < end:
        if tokens[i][2] == b"extern" and tokens[i - 1][2] in (b"{", b"}", b";"):
            j = i
            while tokens[j][2] != b";":
                j = _matching(tokens, j) if tokens[j][2] == b"(" else j
                j += 1
            out.append(text[tokens[i][0]:tokens[j][1]] + b"\n")
            i = j
        i += 1
    return out


def strip_bodies(text, keep=()):
    """Declarations only: replace function bodies with ';', drop linemarkers.
    Block-scope `extern` declarations are kept, after their function. The
    definitions named in `keep` stay whole (pc/tools/zig_diff.py)."""
    tokens, found = _file_scope_functions(text)
    keep = {k.encode() if isinstance(k, str) else k for k in keep}
    pieces, last = [], 0
    for name_index, _, body, _ in found:
        if tokens[name_index][2] in keep:
            continue
        end = _matching(tokens, body, b"{", b"}")
        pieces += [text[last:tokens[body][0]].rstrip(), b";\n"]
        pieces += _block_externs(text, tokens, body, end)
        last = tokens[end][1]
    pieces.append(text[last:])
    return re.sub(rb"(?m)^#[^\n]*\n?", b"", b"".join(pieces))


def routing_source(table, abi, *, reloadable=False):
    """Tail jumps via pc_owner[] without clobbering argument registers.

    Reloadable builds keep __owned addresses in the host: shadow wrappers
    and address-taken calls must see the same slots as ordinary owner calls.
    The loader must bind all slots before owned code can run.
    """
    if abi not in ("i386", "sysv64", "win64"):
        raise ValueError(f"unknown routing ABI: {abi}")
    if reloadable and abi != "sysv64":
        raise ValueError("reloadable ownership requires sysv64")
    out = ["/* Generated by pc/tools/ownership.py from game/ownership.toml. */"]
    if reloadable:
        import hashlib
        key = hashlib.sha256(repr(table.routes()).encode()).hexdigest()
        out.append(f'const char pc_owned_abi[] = "{key}";')
        out.append(f"void* pc_owned_slots[{max(1, len(table.routes()))}];")
        out.append("void** pc_owned_table = pc_owned_slots;")
        for name, _ in table.routes():
            out.append(f"extern const char {name}__owned_abi[];")
        out.append("const char* const pc_route_signatures[] = { " +
                   ", ".join(name + "__owned_abi" for name, _ in table.routes()) + " };")
        for slot, (name, _) in enumerate(table.routes()):
            asm = "\\n".join([
                ".text", f".globl {name}__owned",
                f".type {name}__owned, @function", ".p2align 4",
                f"{name}__owned:", "    movq pc_owned_table(%rip), %r11", f"    jmp *{slot * 8}(%r11)",
                f".size {name}__owned, .-{name}__owned",
            ])
            out.append(f'__asm__("{asm}\\n");')
    rip = "" if abi == "i386" else "(%rip)"
    for route, (name, index) in enumerate(table.routes()):
        reg = "%eax" if abi == "i386" else "%r11d"
        address = f"pc_owner+{index}{rip}"
        # Owner 3, alternate (the oracle's per-call alternation, state_diff.c):
        # each call flips this route's bit, odd calls owned, even ones
        # original, and counts both (pc_owner_alternate_report). Only the
        # flags and the scratch register change before the tail jump.
        body = [f"movzbl {address}, {reg}", f"cmpl $1, {reg}", f"je {name}__owned",
                f"cmpl $2, {reg}", f"je {name}__shadow", f"cmpl $3, {reg}", "je 1f", f"jmp {name}__orig",
                f"1: xorb $1, pc_route_alternate+{route}{rip}", "jz 2f",
                f"incl pc_route_alternate_calls+{8 * route}{rip}", f"jmp {name}__owned",
                f"2: incl pc_route_alternate_calls+{8 * route + 4}{rip}", f"jmp {name}__orig"]
        head = [".text", f".globl {name}"] + ([] if abi == "win64" else [f".type {name}, @function"]) \
            + [".p2align 4", f"{name}:"]
        tail = [] if abi == "win64" else [f".size {name}, .-{name}"]
        asm = "\\n".join(head + ["    " + b for b in body] + tail)
        out.append(f'__asm__("{asm}\\n");')
    names = ", ".join(f'"{s.name}"' for s in table.subsystems) or "0"
    defaults = ", ".join(str(OWNERS.index(s.default)) for s in table.subsystems) or "0"
    routes = ", ".join(f'"{n}"' for n, _ in table.routes()) or "0"
    route_subsystems = ", ".join(str(i) for _, i in table.routes()) or "0"
    out += [f"const unsigned pc_subsystem_count = {len(table.subsystems)};",
            f"const char* const pc_subsystem_names[] = {{ {names} }};",
            f"const unsigned char pc_subsystem_defaults[] = {{ {defaults} }};",
            f"const unsigned pc_route_count = {len(table.routes())};",
            f"const char* const pc_route_names[] = {{ {routes} }};",
            f"const unsigned char pc_route_subsystems[] = {{ {route_subsystems} }};",
            f"unsigned char pc_route_alternate[{max(1, len(table.routes()))}];",
            f"unsigned pc_route_alternate_calls[{2 * max(1, len(table.routes()))}];  /* owned, original */"]
    return "\n".join(out) + "\n"


def ownership_zig(table):
    """The table for owned code (comptime): subsystem slots and routes."""
    lines = ["//! Generated by pc/tools/ownership.py from game/ownership.toml.",
             "/// name: the original symbol (routing, shadow reports and hot swap pair by it);",
             "/// zig: the Zig function that ports it, as a path from the overlay root.",
             "pub const Route = struct { name: []const u8, zig: []const u8, subsystem: u8 };",
             "pub const Subsystem = enum(u8) {"]
    lines += [f"    {s.name} = {s.index}," for s in table.subsystems]
    lines += ["};", "pub const routes = [_]Route{"]
    lines += [f'    .{{ .name = "{n}", .zig = "{z}", .subsystem = {i} }},' for n, z, i in table.zig_routes()]
    lines += ["};",
              "/// The routed function names of one subsystem.",
              "pub fn namesOf(comptime s: Subsystem) []const []const u8 {",
              "    @setEvalBranchQuota(100 * routes.len + 1000); // grows with the routes",
              "    comptime var out: []const []const u8 = &.{};",
              "    inline for (routes) |r| {",
              "        if (r.subsystem == @intFromEnum(s)) out = out ++ [_][]const u8{r.name};",
              "    }", "    return out;", "}",
              "/// The routes of one subsystem (original name and Zig function).",
              "pub fn routesOf(comptime s: Subsystem) []const Route {",
              "    @setEvalBranchQuota(100 * routes.len + 1000); // grows with the routes",
              "    comptime var out: []const Route = &.{};",
              "    inline for (routes) |r| {",
              "        if (r.subsystem == @intFromEnum(s)) out = out ++ [_]Route{r};",
              "    }", "    return out;", "}"]
    return "\n".join(lines) + "\n"
