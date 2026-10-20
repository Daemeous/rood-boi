#!/usr/bin/env python3
"""Owned game code (pc/docs/Ownership.md): the routing table game/ownership.toml
and the build steps that route a function between the original (src/) and
owned code (game/):

  - rename_definitions: in a unit's preprocessed text, the routed functions'
    definitions become name__orig; calls, prototypes and address-of uses keep
    the name, which the trampoline defines;
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
MAX_SUBSYSTEMS = 64  # pc_owner[] in ownership.c


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


@dataclasses.dataclass
class Table:
    subsystems: list

    def routed_names(self, unit):
        return {name for s in self.subsystems if s.unit == unit for _, name in s.functions}

    def routes(self):
        """[(name, subsystem index)] in table order."""
        return [(name, s.index) for s in self.subsystems for _, name in s.functions]

    def problems(self, function_addresses, compiled_units):
        """Inconsistencies with the reference ELFs (name -> address) and the
        units this build compiles; [] when the table can be used."""
        by_address = {a: n for n, a in function_addresses.items() if a is not None}
        out = []
        for s in self.subsystems:
            if s.unit not in compiled_units:
                out.append(f"{s.name}: its unit {s.unit} is not compiled in this build")
            for address, name in s.functions:
                actual = by_address.get(address)
                if actual != name:
                    out.append(f"{s.name}: {address:#x} is {actual or 'no function'}, the table says {name}"
                               + (f" (renamed upstream? use {actual})" if actual else ""))
        return out


def load(path=ROOT / "game/ownership.toml"):
    data = tomllib.loads(pathlib.Path(path).read_text())
    subsystems, seen = [], {}
    for index, (name, entry) in enumerate(data.get("subsystem", {}).items()):
        default = entry.get("default", "original")
        if default not in OWNERS:
            raise ValueError(f"{name}: default {default!r} is not one of {', '.join(OWNERS)}")
        functions = [(int(f["addr"]), f["name"]) for f in entry.get("functions", [])]
        for _, fn in functions:
            if fn in seen:
                raise ValueError(f"{fn} is routed by both {seen[fn]} and {name}")
            seen[fn] = name
        subsystems.append(Subsystem(name, index, entry["overlay"], entry["unit"], default,
                                    list(entry.get("roots", [])), functions, dict(entry.get("views", {}))))
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


def rename_definitions(text, names):
    """Rename file-scope definitions only; return bytes and per-name counts."""
    tokens, found = _file_scope_functions(text)
    wanted = {n.encode(): n for n in names}
    counts = {n: 0 for n in names}
    edits = []
    for name_index, _, _, start in found:
        token = tokens[name_index][2]
        if token not in wanted:
            continue
        if b"static" in [t[2] for t in tokens[start:name_index]]:
            raise ValueError(f"{wanted[token]}: a static function cannot be routed")
        counts[wanted[token]] += 1
        edits.append((tokens[name_index][0], tokens[name_index][1], token + b"__orig"))
    for begin, end, new in reversed(edits):
        text = text[:begin] + new + text[end:]
    return text, counts


def strip_bodies(text):
    """Declarations only: replace function bodies with ';', drop linemarkers."""
    tokens, found = _file_scope_functions(text)
    pieces, last = [], 0
    for _, _, body, _ in found:
        end = _matching(tokens, body, b"{", b"}")
        pieces += [text[last:tokens[body][0]].rstrip(), b";\n"]
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
    for name, index in table.routes():
        reg = "%eax" if abi == "i386" else "%r11d"
        address = f"pc_owner+{index}" + ("" if abi == "i386" else "(%rip)")
        body = [f"movzbl {address}, {reg}", f"cmpl $1, {reg}", f"je {name}__owned",
                f"cmpl $2, {reg}", f"je {name}__shadow", f"jmp {name}__orig"]
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
            f"const unsigned char pc_route_subsystems[] = {{ {route_subsystems} }};"]
    return "\n".join(out) + "\n"


def ownership_zig(table):
    """The table for owned code (comptime): subsystem slots and routes."""
    lines = ["//! Generated by pc/tools/ownership.py from game/ownership.toml.",
             "pub const Route = struct { name: []const u8, subsystem: u8 };",
             "pub const Subsystem = enum(u8) {"]
    lines += [f"    {s.name} = {s.index}," for s in table.subsystems]
    lines += ["};", "pub const routes = [_]Route{"]
    lines += [f'    .{{ .name = "{n}", .subsystem = {i} }},' for n, i in table.routes()]
    lines += ["};",
              "/// The routed function names of one subsystem.",
              "pub fn namesOf(comptime s: Subsystem) []const []const u8 {",
              "    comptime var out: []const []const u8 = &.{};",
              "    inline for (routes) |r| {",
              "        if (r.subsystem == @intFromEnum(s)) out = out ++ [_][]const u8{r.name};",
              "    }", "    return out;", "}"]
    return "\n".join(lines) + "\n"
