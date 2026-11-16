#!/usr/bin/env python3
"""The game's Zig (game/): the table game/ownership.toml and the build steps that use it:

  - strip_bodies: a unit's declarations only (the input of zig translate-c);
  - routing_source: the development library's slot trampolines;
  - ownership_zig: the table as a comptime Zig module.

Each listed function is exported from its Zig function under the original's name (game/common/route.zig).
"""
import dataclasses
import pathlib
import re
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]


@dataclasses.dataclass
class Subsystem:
    name: str
    index: int
    overlay: str
    unit: str
    functions: list  # [(address, name)]
    views: dict
    # Original name -> the Zig function that ports it, as a path from the
    # overlay root ("score.updateEnemyKills"); absent: the original name.
    zig: dict = dataclasses.field(default_factory=dict)
    # Stays in the host in the development build: it sits at the vsync, where
    # snapshots, rewind and the reload cut the stack (pc/src/game_host.inc).
    resident: bool = False

    def zig_name(self, name):
        return self.zig.get(name, name)


@dataclasses.dataclass
class Table:
    subsystems: list

    def for_build(self, compiled_units, overlays):
        """The subsystems this build can use: the unit is compiled here AND
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

    def reloadable_routes(self):
        """The routes of the development library: all but the resident ones."""
        return [(name, s.index) for s in self.subsystems if not s.resident for _, name in s.functions]

    def zig_routes(self):
        """[(original name, Zig function path, subsystem index, resident)] in table order."""
        return [(name, s.zig_name(name), s.index, s.resident) for s in self.subsystems for _, name in s.functions]

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


def zig_source(overlay, zig_path, game_root=ROOT / "game"):
    """(file, function) a route's `zig` path names: each module on the path
    is a `pub const NAME = @import("file.zig");` of the file before it,
    starting at game/<overlay>/root.zig ("score.updateEnemyKills" ->
    game/battle/score.zig; D2's "pc.vs_battle_aiIsEngagementReady.vs_battle_aiIsEngagementReady" ->
    game/battle/pc/vs_battle_aiIsEngagementReady.zig). The import path is relative to the
    importing file, so a subdirectory's root.zig can nest further."""
    *modules, function = zig_path.split(".")
    file = pathlib.Path(game_root) / overlay / "root.zig"
    for i, module in enumerate(modules):
        imports = dict(_IMPORT.findall(file.read_text()))
        if module not in imports:
            raise ValueError(f"{zig_path}: {file.relative_to(game_root)} has no `pub const {module} = @import(\"...\")`")
        file = file.parent / imports[module]
    return file, function


def load(path=ROOT / "game/ownership.toml"):
    data = tomllib.loads(pathlib.Path(path).read_text())
    subsystems, seen = [], {}
    for index, (name, entry) in enumerate(data.get("subsystem", {}).items()):
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
        subsystems.append(Subsystem(name, index, entry["overlay"], entry["unit"], functions,
                                    dict(entry.get("views", {})), zig, bool(entry.get("resident", False))))
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


def strip_bodies(text):
    """Declarations only: replace function bodies with ';', drop linemarkers.
    Block-scope `extern` declarations are kept, after their function."""
    tokens, found = _file_scope_functions(text)
    pieces, last = [], 0
    for name_index, _, body, _ in found:
        end = _matching(tokens, body, b"{", b"}")
        pieces += [text[last:tokens[body][0]].rstrip(), b";\n"]
        pieces += _block_externs(text, tokens, body, end)
        last = tokens[end][1]
    pieces.append(text[last:])
    return re.sub(rb"(?m)^#[^\n]*\n?", b"", b"".join(pieces))


def routing_source(table):
    """The development library's trampolines (sysv64): each reloadable route's name jumps through its slot of
    pc_owned_table without clobbering argument registers. The library publishes a whole new table at once;
    the loader (pc/src/game_host.inc) binds every slot before the Zig can run."""
    import hashlib
    routes = table.reloadable_routes()
    key = hashlib.sha256(repr(routes).encode()).hexdigest()
    out = ["/* Generated by pc/tools/ownership.py from game/ownership.toml. */",
           f'const char pc_owned_abi[] = "{key}";',
           f"void* pc_owned_slots[{max(1, len(routes))}];",
           "void** pc_owned_table = pc_owned_slots;"]
    for name, _ in routes:
        out.append(f"extern const char {name}__owned_abi[];")
    out.append("const char* const pc_route_signatures[] = { " +
               ", ".join(name + "__owned_abi" for name, _ in routes) + " };")
    for slot, (name, _) in enumerate(routes):
        asm = "\\n".join([
            ".text", f".globl {name}", f".type {name}, @function", ".p2align 4",
            f"{name}:", "    movq pc_owned_table(%rip), %r11", f"    jmp *{slot * 8}(%r11)",
            f".size {name}, .-{name}",
        ])
        out.append(f'__asm__("{asm}\\n");')
    names = ", ".join(f'"{n}"' for n, _ in routes) or "0"
    out += [f"const unsigned pc_route_count = {len(routes)};",
            f"const char* const pc_route_names[] = {{ {names} }};"]
    return "\n".join(out) + "\n"


def ownership_zig(table):
    """The table for owned code (comptime): subsystem slots and routes."""
    lines = ["//! Generated by pc/tools/ownership.py from game/ownership.toml.",
             "/// name: the original symbol (exported, and the reload pairs by it);",
             "/// zig: the Zig function that ports it, as a path from the overlay root;",
             "/// resident: stays in the host in the development build.",
             "pub const Route = struct { name: []const u8, zig: []const u8, subsystem: u8, resident: bool };",
             "pub const Subsystem = enum(u8) {"]
    lines += [f"    {s.name} = {s.index}," for s in table.subsystems]
    lines += ["};", "pub const routes = [_]Route{"]
    lines += [f'    .{{ .name = "{n}", .zig = "{z}", .subsystem = {i}, .resident = {"true" if r else "false"} }},'
              for n, z, i, r in table.zig_routes()]
    lines += ["};",
              "/// The routes of one subsystem (original name and Zig function).",
              "pub fn routesOf(comptime s: Subsystem) []const Route {",
              "    @setEvalBranchQuota(100 * routes.len + 1000); // grows with the routes",
              "    comptime var out: []const Route = &.{};",
              "    inline for (routes) |r| {",
              "        if (r.subsystem == @intFromEnum(s)) out = out ++ [_]Route{r};",
              "    }", "    return out;", "}"]
    return "\n".join(lines) + "\n"
