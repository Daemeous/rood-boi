#!/usr/bin/env python3
"""The battle port pipeline: a cheap model drafts each function's zig_diff
spec and its port, gated by the differential checker; results land in the
tree, with a record per function and a queue for a stronger reviewer.

    python3 pc/tools/port_pipeline.py FUNCTION...              # these functions
    python3 pc/tools/port_pipeline.py --unit 146C [--difficulty S] [--limit N]
    python3 pc/tools/port_pipeline.py ... --dry-run            # prompts only, no calls, nothing written
    bash -ic 'python3 pc/tools/port_pipeline.py func_8006B214 --budget 0.10'   # the key from your profile

Per function (all CPU, no game run; needs a 64-bit build, build_boot.py --arch 64):
 0. plan, deterministically: the Zig name (MEANINGS.tsv's zig_name, else the
    decompilation's name without its vs_battle_/vs_main_/_ prefix, else the
    original symbol kept), the module (game/battle/<snake_case>.zig), the
    spec file, the subsystem (battle_<unit>_port, one per unit), the
    signature (the unit's translate-c prototype, struct pointers as views
    types), and the views the function needs (the types and globals its body
    names, closed over the fields it reaches); the unit's views and c modules
    are generated for it in the work directory (zig_views.py, zig_c.py), as
    build_boot.py will once the table lists it;
 1. the model drafts the spec and 3-6 mutations of the C (spec_bakeoff.py's
    prompt, with the C as the reference); gate: zig_diff's --self-check
    against a stand-in port that is the C itself (0 mismatches, every class
    occurs, every mutation caught) and --coverage (every branch covered and
    no value flag, or waivers with reasons); one retry fed the checker's
    errors;
 2. the model ports the function (bakeoff_context.py's prompt with the
    planned name and signature); gate: zig_diff with the spec, 0 mismatches
    on 3 seeds, and the `/// orig:` line; one retry fed the first errors;
 3. writes the spec, its specs.toml entry, the port, the ownership.toml route
    and views, and root.zig's import and export into this checkout (after
    checking that the unit's existing specs still pass with the widened
    views), a JSON record per function and a unit summary;
 4. regenerates REVIEW.md: what a stronger reviewer must look at.

Everything sent is passed through pc/tools/prompt_privacy.py and refused if
anything local is left. The model is called through OpenRouter
(OPENROUTER_API_KEY from the environment, never printed or logged),
temperature 0, max_tokens 64000; --budget (US$, default 5) is checked before
every call. Resumable: a function whose record says `ported` is skipped, and
a saved reply (in --work) is reused instead of calling again when it answered
the same conversation (the model, the prompt and any feedback: a key in its
.json; a reply placed by hand, without one, is always used); one that
answered another is kept aside as .staleN.
"""
import argparse
import dataclasses
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import bakeoff_context as bc  # noqa: E402
import ownership  # noqa: E402
import prompt_privacy  # noqa: E402
import spec_bakeoff as sb  # noqa: E402
import zig_c  # noqa: E402
import zig_diff  # noqa: E402
import zig_toolchain  # noqa: E402
import zig_views  # noqa: E402

BATTLE = ROOT / "pc/docs/porting/battle"
FUNCTIONS_TSV = BATTLE / "FUNCTIONS.tsv"
MEANINGS_TSV = BATTLE / "MEANINGS.tsv"
RECORDS = BATTLE / "pipeline"
SPECS_TOML = zig_diff.SPECS / "specs.toml"
GAME = ROOT / "game"
OWNERSHIP = GAME / "ownership.toml"
OVERLAY = "battle"
ROOT_ZIG = GAME / OVERLAY / "root.zig"
SPEC_DIR = zig_diff.SPECS
DEFAULT_MODEL = "anthropic/claude-haiku-5.5"
MIN_MUTATIONS = 3
ITERATIONS = (2000, 40000)


class Stop(Exception):
    """The budget is spent (or a call failed): stop before the next call."""


# ---------------------------------------------------------------- plan
@dataclasses.dataclass
class Plan:
    name: str
    unit: str
    stem: str
    address: int
    difficulty: str
    zig_name: str = ""
    name_source: str = ""
    module: str = ""
    spec_file: str = ""
    subsystem: str = ""
    signature: str = ""
    params: list = dataclasses.field(default_factory=list)
    ret: str = "void"
    types: list = dataclasses.field(default_factory=list)
    globals: list = dataclasses.field(default_factory=list)
    hooks: list = dataclasses.field(default_factory=list)
    statics: list = dataclasses.field(default_factory=list)
    skip: str = ""


def functions_tsv():
    lines = FUNCTIONS_TSV.read_text().splitlines()
    header = lines[0].split("\t")
    return {row[0]: dict(zip(header, row)) for row in (l.split("\t") for l in lines[1:])}


def meanings():
    lines = MEANINGS_TSV.read_text().splitlines()
    header = lines[0].split("\t")
    col = header.index("zig_name") if "zig_name" in header else None
    out = {}
    for line in lines[1:]:
        parts = line.split("\t")
        if col is not None and len(parts) > col and parts[col] and "." not in parts[0]:
            out[parts[0]] = parts[col]
    return out


def readable_name(name, names):
    """(zig name, why): PORTING.md §16's readable name where one exists."""
    if names.get(name):
        return names[name], "MEANINGS.tsv zig_name"
    if re.fullmatch(r"func_[0-9A-Fa-f]{8}", name):
        return name, "no readable name: the original symbol is kept"
    base = re.sub(r"^(vs_battle_|vs_main_|vs_)", "", name).lstrip("_")
    if not base or not re.fullmatch(r"[A-Za-z]\w*", base):
        return name, "no readable name: the original symbol is kept"
    return base[0].lower() + base[1:], "the decompilation's name (prefix dropped)"


def snake(name):
    if re.fullmatch(r"func_[0-9A-Fa-f]{8}", name):
        return name
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()


def eligible(row):
    if not row["file"].startswith("src/BATTLE/") or not row["file"].endswith(".c"):
        return f"not a battle C unit ({row['file']})"
    if not row["kind"].startswith("c"):
        return f"not portable C ({row['kind']})"
    if row["static"]:
        return "a static function cannot be routed"
    if not row["address"]:
        return "no address"
    return ""


def c_decls(c_text):
    """The c module's struct-like type names and its globals."""
    types = set()
    for m in re.finditer(r"^pub const (\w+) = (?:extern struct|extern union|opaque|struct_\1;|union_\1;)", c_text, re.M):
        if not m.group(1).startswith(("struct_", "union_")):
            types.add(m.group(1))
    # translate-c spells a global either way: `pub extern var X: T;` (most)
    # or `pub const X: T = @extern(T, ...)` (an array of unknown size).
    globals_ = set(re.findall(r"^pub const (\w+): [^=]+ = @extern\(", c_text, re.M))
    globals_ |= set(re.findall(r"^pub extern (?:var|const) (\w+):", c_text, re.M))
    return types, globals_


# Zig's own type names: an alias that ends at one of these is spelled as it.
PRIMITIVE = re.compile(r"(?:[iu]\d+|c_u?(?:char|short|int|long|longlong)|c_longdouble|f(?:16|32|64|80|128)|bool|void|"
                       r"anyopaque|usize|isize|anyerror|noreturn)")


def c_aliases(c_text):
    """The c module's plain aliases (`pub const u_short = c_ushort;`)."""
    return dict(re.findall(r"^pub const (\w+) = (\w+);$", c_text, re.M))


def c_names(c_text):
    return set(re.findall(r"^pub (?:const|extern var|extern const|extern fn|inline fn|fn) (\w+)", c_text, re.M))


def split_params(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def views_globals(views_text):
    """The globals a views module has accessors for (in `g`)."""
    return set(re.findall(r'pub inline fn \w+\(\) [^{]*\{ return psx\.global(?:Many)?\([^\n]*?, "(\w+)"\); \}',
                          views_text))


def zig_type(t, views_types, c_types, aliases=None, names=None):
    """A translate-c type as the port's parameter type: struct pointers as
    views pointers, the c module's typedef aliases resolved to Zig's own
    types (`u_short` is `c_ushort`), anything else the unit declares through
    the c module (the signature is compiled where `c` and `v` are the only
    names the unit's declarations go by)."""
    t = t.strip()
    aliases, names = aliases or {}, names or set()

    def resolve(word):
        seen = set()
        while word in aliases and word not in seen and word not in views_types and word not in c_types:
            seen.add(word)
            word = aliases[word]
        return word
    m = re.fullmatch(r"(\[\*c\]|\?\*)(const )?(\w+)", t)
    if m and resolve(m.group(3)) in views_types:
        return f"?*{m.group(2) or ''}v.{resolve(m.group(3))}"

    def qualify(match):
        word = resolve(match.group(0))
        if word in views_types:
            return "v." + word
        if word in c_types or (word in names and not PRIMITIVE.fullmatch(word)):
            return "c." + word
        return word
    return re.sub(r"(?<![.\w@])[A-Za-z_]\w*", qualify, t)


def c_param_names(header):
    """Parameter names from a C definition's header (`int f(int a, T* b)`)."""
    inside = header[header.index("(") + 1:header.rindex(")")]
    names = []
    for p in split_params(inside):
        if p in ("void", ""):
            continue
        m = re.search(r"(\w+)\s*(\[[^\]]*\])*\s*$", p)
        names.append(m.group(1) if m else f"arg{len(names)}")
    return names


def signature_of(plan, c_text, views_types, c_types, header):
    # A function the build already routes is declared as NAME__orig (and as
    # NAME only where the unit has a prototype of its own).
    m = re.search(rf"^pub extern fn {re.escape(plan.name)}(?:__orig)?\((.*)\) (.+);$", c_text, re.M)
    if not m:
        raise SystemExit(f"port_pipeline: {plan.name}: no prototype in c_{plan.stem}")
    params = split_params(m.group(1))
    names = c_param_names(header)
    aliases, names_ = c_aliases(c_text), c_names(c_text)
    out = []
    for i, p in enumerate(params):
        ptype = p.split(":", 1)[1] if ":" in p and not p.startswith(("[", "?")) else p
        name = names[i] if i < len(names) else f"arg{i}"
        if name in zig_views.KEYWORDS:
            name = name + "_"
        out.append((name, zig_type(ptype, views_types, c_types, aliases, names_)))
    ret = zig_type(m.group(2), views_types, c_types, aliases, names_)
    sig = f"pub fn {plan.zig_name}(" + ", ".join(f"{n}: {t}" for n, t in out) + f") callconv(.c) {ret}"
    return sig, out, ret


def gdb_types(obj, names):
    """The names gdb finds as types in the unit object's DWARF."""
    script = ("import gdb, json\nout = []\nfor n in %r:\n    try:\n        gdb.lookup_type(n)\n        out.append(n)\n"
              "    except Exception:\n        pass\nprint('PIPE' + json.dumps(out))\n" % sorted(names))
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as f:
        f.write(script)
    try:
        out = subprocess.run(["gdb", "-batch", "-nx", "-iex", "set auto-load off", str(obj), "-x", f.name],
                             capture_output=True, text=True).stdout
    finally:
        os.unlink(f.name)
    line = next((l for l in out.splitlines() if l.startswith("PIPE")), "PIPE[]")
    return set(json.loads(line[4:]))


def opaque_targets(ref, out):
    """Named pointer targets the views left opaque (?*anyopaque) in a dump ref."""
    if not isinstance(ref, dict):
        return
    if ref.get("kind") in ("ptr", "psx_ptr") and ref.get("target", {}).get("kind") == "opaque":
        out.add(re.sub(r"^(struct|union) ", "", ref["target"].get("name", "")).strip())
    for key in ("target", "elem"):
        if key in ref:
            opaque_targets(ref[key], out)


class Unit:
    """A unit's generated views/c modules for the pipeline (work/gen/<stem>)."""

    def __init__(self, stem, unit, build, work, table):
        self.stem, self.unit, self.build = stem, unit, build
        self.dir = work / "gen" / stem
        self.dir.mkdir(parents=True, exist_ok=True)
        units = list(build.glob(f"*_{stem}.i"))
        if len(units) != 1:
            raise SystemExit(f"port_pipeline: {build}: no *_{stem}.i (build_boot.py --arch 64)")
        self.unit_i = units[0]
        self.unit_o = self.unit_i.with_suffix(".o")
        built_c = build / "zig" / f"c_{stem}.zig"
        if built_c.exists():
            self.c_path = built_c
        else:
            self.c_path = zig_c.translate(zig_toolchain.ensure(), self.unit_i, self.dir / f"c_{stem}.zig",
                                          "x86_64-linux-none")
        self.c_text = self.c_path.read_text()
        self.c_types, self.c_globals = c_decls(self.c_text)
        self.base_types, self.base_globals = set(), set()
        for s in table.subsystems:
            if s.unit == unit and s.views:
                self.base_types |= set(s.views.get("types", []))
                self.base_globals |= set(s.views.get("globals", []))
        self.dwarf_types = gdb_types(self.unit_o, self.c_types)

    def views(self, extra_types=(), extra_globals=(), tag="all"):
        """Generate views_<stem> for the unit's manifest plus extras; (path, dump)."""
        types = sorted(self.base_types | set(extra_types))
        globals_ = sorted(self.base_globals | set(extra_globals))
        out = self.dir / tag
        out.mkdir(parents=True, exist_ok=True)
        dump = zig_views.dump_with_gdb(self.unit_o, types, globals_, out / f"views_{self.stem}.json")
        path = out / f"views_{self.stem}.zig"
        text = zig_views.emit(dump, zig_views.load_names())
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
        return path, dump


def plan_function(name, row, table, names, units, build, work):
    unit = row["file"]
    stem = pathlib.PurePosixPath(unit).stem
    plan = Plan(name=name, unit=unit, stem=stem, address=int(row["address"], 16), difficulty=row["difficulty"])
    plan.skip = eligible(row)
    for s in table.subsystems:
        for _, fn in s.functions:
            if fn == name:
                plan.skip = plan.skip or (f"already ported ({s.name}: {s.zig[name]})" if name in s.zig else
                                          f"routed by {s.name} without a Zig port")
    if plan.skip:
        return plan, None
    if stem not in units:
        units[stem] = Unit(stem, unit, build, work, table)
    u = units[stem]
    plan.zig_name, plan.name_source = readable_name(name, names)
    plan.module = snake(plan.zig_name)
    if (GAME / OVERLAY / f"{plan.module}.zig").exists():
        plan.module = f"{plan.module}_{stem.lower()}"
    plan.spec_file = f"{plan.zig_name}.zig"
    if (zig_diff.SPECS / plan.spec_file).exists():
        plan.spec_file = f"{plan.zig_name}_{stem}.zig"
    plan.subsystem = f"battle_{stem}_port"
    text = u.unit_i.read_text(errors="replace")
    body = bc.c_body(text, name)
    if body is None:
        plan.skip = f"no definition in {u.unit_i.name}"
        return plan, None
    source_body = bc.c_body((ROOT / unit).read_text(errors="replace"), name) or ""
    tokens = set(bc.IDENT.findall(body))
    fields = set(re.findall(r"(?:->|\.)\s*(\w+)", body))
    plan.globals = sorted((tokens & u.c_globals) - {name})
    types = (tokens & u.dwarf_types)
    for _ in range(4):
        _, dump = u.views(types, plan.globals, tag=f"plan-{name}")
        more = set()
        for tname, t in dump["types"].items():
            for f in t["fields"]:
                if f["name"] in fields:
                    opaque_targets(f["type"], more)
        for gname, g in dump["globals"].items():
            if gname in plan.globals:
                opaque_targets(g, more)
        more = (more & u.dwarf_types) - types
        if not more:
            break
        types |= more
    # Every global the body names gets a views accessor (the prompts show
    # only what exists, so a missing one would leave the model guessing).
    views_path, _ = u.views(types, plan.globals, tag=f"plan-{name}")
    missing = sorted(set(plan.globals) - views_globals(views_path.read_text()))
    if missing:
        plan.skip = f"no views accessor generated for {', '.join(missing)}"
        return plan, None
    plan.types = sorted(types - u.base_types)
    plan.globals = sorted(set(plan.globals) - u.base_globals)
    views_types = set(dump["types"])
    header = body[:body.index("{")]
    plan.signature, plan.params, plan.ret = signature_of(plan, u.c_text, views_types, u.c_types, header)
    statics = set(re.findall(r"^pub const (\w+) = @compileError", u.c_text, re.M))
    # The unit's statics (psx.h's static inline accessors are the build's own, not state).
    plan.statics = sorted(t for t in tokens & statics if not t.startswith("psx_")) + (["a block-scope static"] if re.search(r"\bstatic\b", body[body.index("{"):]) else [])
    plan.hooks = sorted({t for t in tokens if t.startswith("pc_")}) + (["ROOD_PC block in the source"] if "ROOD_PC" in source_body else [])
    return plan, u


def route_of(plan, views):
    sub = ownership.Subsystem(plan.subsystem, 0, OVERLAY, plan.unit, "original", [], [(plan.address, plan.name)],
                              {"types": plan.types, "globals": plan.globals}, {plan.name: f"{plan.module}.{plan.zig_name}"})
    return sub, f"{plan.module}.{plan.zig_name}"


def signature_file(plan, path):
    """The port's type only (the stand-in calls the C): what the spec's Args is built from."""
    params = ", ".join(f"_: {t}" for _, t in plan.params)
    path.write_text(f"//! The planned signature of {plan.name} (pc/tools/port_pipeline.py).\n"
                    f'const v = @import("views_{plan.stem}");\nconst c = @import("c_{plan.stem}");\n'
                    f"pub fn {plan.zig_name}({params}) callconv(.c) {plan.ret} {{\n"
                    f'    @panic("signature only");\n}}\n')
    return path


# ---------------------------------------------------------------- prompts
def spec_prompt(plan, unit, views_path, table, build):
    specs = tomllib.loads(SPECS_TOML.read_text())["function"]
    text = unit.unit_i.read_text(errors="replace")
    i_body = bc.c_body(text, plan.name)
    c_source = bc.c_body((ROOT / plan.unit).read_text(errors="replace"), plan.name)
    tokens = set(bc.IDENT.findall(i_body)) | set(bc.IDENT.findall(c_source or ""))
    doc = sb.HARNESS_DOC.replace("{views}", f"views_{plan.stem}").replace("{cmod}", f"c_{plan.stem}") \
        .replace("{fn}", plan.zig_name)
    doc = doc.replace('and "port" (the accepted port\'s file: `@import("port").' + plan.zig_name + '`)',
                      'and "port" (`@import("port").' + plan.zig_name + '`: only its type matters to you)')
    doc = doc.replace("""A mutation is a wrong variant of the port: `from` must occur exactly once
in the port file below (copy it character for character, indentation and
line breaks included), `to` replaces it, and the result must still compile
(an unused local or parameter is a compile error in Zig).""",
                      """A mutation is a wrong variant of the ORIGINAL C (there is no Zig port yet:
the checker runs the C against a copy of itself, the copy mutated): give
every mutation `target = "c"`; `from` must occur exactly once in the C body
below (copy it character for character, spaces and line breaks included),
`to` replaces it, and the result must still compile as C.""")
    parts = [f"""# Write the zig_diff spec of `{plan.name}` ({plan.unit})

A differential harness checks a Zig port against the original C body: both
run on the inputs a spec builds and every touched byte, external call and
return value must agree. The port does not exist yet; it will have exactly
this signature (in `game/{OVERLAY}/{plan.module}.zig`, imported as "port"):

    {plan.signature}

(`v` is `views_{plan.stem}`, `c` is `c_{plan.stem}`.) Write the spec: the file
`pc/tests/zig_diff/{plan.spec_file}` and its `specs.toml` entry. Your spec is
checked now against the C itself (a stand-in port that calls a copy of the
C), with your mutations applied to that copy.

The spec must:
- build the inputs through the zdiff API (`zdiff.zig`, below): the arguments
  and every object and global the C reads, so that the C never faults.
  Globals start as random bytes: an entry of a global pointer table that
  the C reads must be set by prepare (through its `v.g` accessor, below:
  its type gives the table's length and its element type), and an object
  the C indexes past a `[0]` array needs room for every element it reads;
- reach every branch of the C: the checker builds the C with gcov and fails
  on any branch your inputs never take;
- hit every value edge the C has: an ordered comparison (`<`, `>=`, ...) must
  see its two sides EQUAL at least 10 times (and in at least 1% of its runs);
  a test of a value against zero (`if (x & MASK)`, `if (x)`) must see nonzero
  values both with and without the sign bit; also overflow and wrapping,
  negative values, signed vs unsigned reads, null where the C tests for it,
  out-of-range indices the C allows (as far as they stay inside memory the
  harness owns);
- declare input classes (`classes` + `classify`) naming those cases; every
  class must occur;
- come with {MIN_MUTATIONS}-6 mutations of the C (`target = "c"`) that your
  inputs catch, each a mistake a porter could make: a wrong bound (`<` for
  `<=`), a signed/unsigned slip, a wrong constant, a dropped call or store,
  a wrong field.

A branch or value flag that no input can reach (a weak symbol that is
always linked, a test the harness cannot make differ) may be waived in the
entry: `[[function.{plan.name}.coverage_waiver]]` with `id` (as the checker
prints it, e.g. "+12 b1") and a `reason`; only when it truly cannot be reached.

Reply with exactly two fenced blocks: first ```zig (the whole spec file,
starting with a `//! zig_diff spec: {plan.name} -> {plan.module}.{plan.zig_name} ({pathlib.PurePosixPath(plan.unit).name}).`
line and a short `//!` paragraph saying what the inputs cover), then ```toml
(the `[function.{plan.name}]` table with `spec = "{plan.spec_file}"`, `iterations`
(20000 to 40000), and its `[[function.{plan.name}.mutation]]` entries). Nothing else.""",
             doc,
             "## The porting guide's reviewer checklist (pc/docs/porting/PORTING.md §19)\n\n" + sb.section_19(),
             "## The API (pc/tests/zig_diff/zdiff.zig)\n\n```zig\n" + zig_diff.API.read_text().strip() + "\n```"]
    for example in sb.EXAMPLES:
        esub, eport, efn = sb.route(table, example)
        parts.append(f"## Example: `{example}` ({esub.unit}) -> `{eport.stem}.{efn}`\n\nIts port "
                     f"(`game/{esub.overlay}/{eport.name}`; its mutations edit the port, yours edit the C):\n\n"
                     f"```zig\n{eport.read_text().strip()}\n```\n\n"
                     f"Its spec (`pc/tests/zig_diff/{specs[example]['spec']}`):\n\n```zig\n"
                     + (zig_diff.SPECS / specs[example]["spec"]).read_text().strip()
                     + "\n```\n\nIts specs.toml entry:\n\n```toml\n" + sb.toml_entry(example) + "\n```")
    parts.append(f"## The C (as GCC compiled it: the routed `.i`; mutations copy their `from` from here)\n\n```c\n{i_body}\n```")
    if c_source and re.sub(r"\s+", " ", c_source) != re.sub(r"\s+", " ", i_body):
        parts.append(f"The source (`{plan.unit}`, for names and comments):\n\n```c\n{c_source}\n```")
    parts.append(f"## `views_{plan.stem}` (the declarations it touches)\n\n```zig\n"
                 + bc.views_subset(views_path.read_text(), tokens) + "\n```")
    parts.append(f"## `c_{plan.stem}` (the declarations it touches)\n\n```zig\n"
                 + bc.c_subset(unit.c_text, tokens, plan.name) + "\n```")
    parts.append("## `common.psx` (game/common/psx.zig, public API)\n\n```zig\n" + bc.psx_api() + "\n```")
    return "\n\n".join(parts) + "\n"


def port_prompt(plan, unit, views_path, table, build):
    sub, _ = route_of(plan, views_path)
    entry = {"name": plan.name, "module": plan.module, "signature": plan.signature}
    text = bc.context(entry, table, build, guide=True, sub=sub, views_path=views_path, c_path=unit.c_path)
    note = (f"\nThe Zig name `{plan.zig_name}` is fixed ({plan.name_source}); keep it, and keep the "
            f"signature exactly (the checker builds its inputs from this type).\n")
    return text.replace("- Reply with the file only.", "- Reply with the file only." + note, 1)


# ---------------------------------------------------------------- the gates
CRASH_ADVICE = """FAIL: {what} crashed on your inputs (the C runs first, so this is the spec's
bug). The harness fills every global and the arena with random bytes before
prepare, so every pointer the C follows must be one prepare set: each
argument, each pointer field of an object you build, and each ENTRY of a
global pointer table the C indexes (only the entry it reads: e.g.
`v.g.TABLE()[i] = obj`), pointing at an object from `ctx.alloc`/`ctx.allocBytes`
(sized for every element the C indexes past a `[0]` array), at a global, or
null where the C tests for null. An index into a global table must stay
inside its length (the views accessor's type gives it). Below: the crash, its
fault address (named when it is inside an object the harness owns) and the
arguments of that iteration:
"""


def first_lines(text, n=12):
    keep = [l for l in text.splitlines() if l.startswith(("FAIL", "zdiff:", "coverage", "  UNCOVERED", "  FLAG"))
            or "error:" in l]
    return "\n".join(keep[:n]) if keep else text.strip()[-1500:]


def run_exe(exe, iterations, seed, timeout=300):
    try:
        r = subprocess.run(["nice", "-n", "10", str(exe), str(iterations), str(seed)], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, f"no result after {timeout} s (an endless loop?)"
    return r.returncode, r.stdout + r.stderr


def spec_gate(plan, entry, spec_path, views_path, unit, table, build, work):
    """zig_diff --self-check --coverage with the C as the port; (ok, errors, details)."""
    errors, details = [], {"classes": {}, "mutations": [], "coverage": None}
    work.mkdir(parents=True, exist_ok=True)
    iterations = max(ITERATIONS[0], min(ITERATIONS[1], int(entry.get("iterations", 20000))))
    mutations = entry.get("mutation", [])
    for m in mutations:
        m["target"] = "c"
        m.pop("file", None)
    if len(mutations) < MIN_MUTATIONS:
        errors.append(f"FAIL: {len(mutations)} mutation(s); give {MIN_MUTATIONS}-6 (`target = \"c\"`)")
    waivers = entry.get("coverage_waiver", [])
    for w in waivers:
        if not isinstance(w, dict) or not str(w.get("reason", "")).strip() or not w.get("id"):
            errors.append(f"FAIL: coverage_waiver {w!r}: needs an `id` and a `reason`")
    signature = signature_file(plan, work / "signature.zig")
    route = route_of(plan, views_path)
    try:
        build_ = zig_diff.Build(plan.name, {"spec": str(spec_path)}, build, work / "build", table,
                                port_source=signature, route=route, views=views_path, cmod=unit.c_path)
        exe, _ = build_.compile(standin=True)
    except zig_diff.Failure as error:
        errors.append("FAIL: the harness does not build:\n" + str(error)[-4000:])
        return False, errors, details
    code, text = run_exe(exe, iterations, 1)
    text = zig_diff.name_addresses(exe, text or "")
    classes = re.search(r"classes:(.*)", text)
    details["classes"] = dict(c.split("=") for c in classes.group(1).split()) if classes else {}
    if code == 4 and "CRASH" in text:
        # Nothing else can be judged on inputs that crash the C itself: the
        # mutations and the coverage run would only crash the same way.
        errors.append(CRASH_ADVICE.format(what="the C itself") + first_lines(text))
        if mutations:
            errors.append("(the mutations and the coverage gate are checked once the C runs crash-free)")
        return False, errors, details
    if code in (3, 4) and re.search(r"(PANIC|CRASH)[^\n]* in the (prepare|classify) run", text):
        # The spec's own code failed: nothing else can be judged either.
        errors.append("FAIL: your spec's own code (prepare or classify) failed before the C ran: a Zig safety "
                      "check (an index past an array's length, an integer that does not fit, a null unwrap) "
                      "or a fault in it:\n" + first_lines(text))
        if mutations:
            errors.append("(the mutations and the coverage gate are checked once the spec runs)")
        return False, errors, details
    if code != 0:
        errors.append(f"FAIL: the C differs from a copy of itself under this spec (exit {code}: the spec is not "
                      f"deterministic: everything prepare does must come from `ctx`):\n" + first_lines(text))
    empty = [c for c, n in details["classes"].items() if n == "0"]
    if empty:
        errors.append("FAIL: input classes never generated: " + ", ".join(empty))
    if not details["classes"] and code is not None:
        errors.append("FAIL: no input classes declared (`classes` and `classify`)")
    for m in mutations:
        record = {"name": m.get("name"), "caught": False}
        try:
            if not isinstance(m.get("from"), str) or not isinstance(m.get("to"), str):
                raise zig_diff.Failure("needs string `from` and `to`")
            mexe, _ = build_.compile(m, standin=True)
            mcode, mtext = run_exe(mexe, iterations, 1)
            mtext = zig_diff.name_addresses(mexe, mtext or "")
            expect = m.get("expect", "mismatch")
            record["caught"] = mcode is not None and sb.caught(mcode, mtext, expect)
            if not record["caught"]:
                hint = ""
                if mcode == 4 and expect != "crash" and "in the Zig run" in mtext:
                    hint = (" (the mutated copy crashes where the C does not: if that is what this mistake does on "
                            "your inputs, say `expect = \"crash\"`; otherwise choose a mutation your inputs show as a "
                            "difference)")
                elif mcode == 0:
                    hint = " (no input made the mutated copy behave differently: add inputs that do)"
                errors.append(f"FAIL: mutation {m.get('name')}: not caught as {expect} (exit {mcode}){hint}: "
                              + first_lines(mtext, 4))
        except zig_diff.Failure as error:
            errors.append(f"FAIL: mutation {m.get('name')}: {str(error)[-1200:]}")
        details["mutations"].append(record)
    try:
        cexe, _ = build_.compile(coverage=True, standin=True)
        report = zig_diff.coverage_run(build_, cexe, iterations, 1)
        passed, lines = zig_diff.coverage_verdict(report, 100.0, waivers)
        details["coverage"] = {k: report[k] for k in ("branches", "branches_hit", "conditions", "conditions_hit",
                                                      "lines", "lines_hit", "uncovered", "flags", "args")}
        details["coverage"]["compares"] = report["compares"]
        details["coverage"]["passed"] = passed
        if not passed:
            errors.append("FAIL: the coverage gate (zig_diff.py --coverage):\n" + "\n".join(
                l for l in lines if not l.startswith(("  read", "  compare", "  arg")))
                + "\nWhat to add:\n" + "\n".join(zig_diff.coverage_hints(report, waivers)))
    except zig_diff.Failure as error:
        errors.append(f"FAIL: the coverage run: {str(error)[-1500:]}")
    build_.compile(standin=True)
    return not errors, errors, details


def port_gate(plan, entry, spec_path, port_path, views_path, unit, table, build, work, seeds):
    errors, runs = [], []
    work.mkdir(parents=True, exist_ok=True)
    text = port_path.read_text()
    if not re.search(rf"^[ \t]*/// orig: {re.escape(plan.name)} \({re.escape(plan.unit)}\)[ \t]*\n(?:[ \t]*///[^\n]*\n)*"
                     rf"[ \t]*pub fn {re.escape(plan.zig_name)}\(", text, re.M):
        errors.append(f"FAIL: the doc comment of `pub fn {plan.zig_name}` must start with "
                      f"`/// orig: {plan.name} ({plan.unit})`")
    iterations = max(ITERATIONS[0], min(ITERATIONS[1], int(entry.get("iterations", 20000))))
    try:
        build_ = zig_diff.Build(plan.name, {"spec": str(spec_path)}, build, work / "build", table,
                                port_source=port_path, route=route_of(plan, views_path), views=views_path,
                                cmod=unit.c_path)
        exe, _ = build_.compile()
    except zig_diff.Failure as error:
        message = re.sub(r"\S*/(game/[\w/]+\.zig)", r"\1", str(error))
        errors.append("FAIL: does not compile:\n" + message[-3000:])
        return False, errors, runs
    for seed in seeds:
        code, out = run_exe(exe, iterations, seed)
        summary = re.search(r"(\d+) iterations, seed \d+, (\d+) mismatches", out or "")
        runs.append({"seed": seed, "exit": code, "mismatches": int(summary.group(2)) if summary else None,
                     "first": None if code == 0 else sb.first_line(out or "")})
        if code != 0:
            out = zig_diff.name_addresses(exe, out or "")
            errors.append(f"FAIL: seed {seed}: the port differs from the C (exit {code}):\n" + first_lines(out, 6))
            break
    return not errors, errors, runs


# ---------------------------------------------------------------- the model
def feedback(errors, what):
    return prompt_privacy.sanitise(
        f"The checker's output for your {what} (fix it; reply as before, the whole answer again):\n\n"
        + "\n\n".join(errors))


def conversation_key(messages, model):
    """What a saved reply answered: the model and every message sent."""
    return hashlib.sha256(json.dumps({"model": model, "messages": messages}, sort_keys=True).encode()).hexdigest()


def ask(state, messages, path, label):
    """A saved reply to this very conversation, or a call (budget checked
    first); (text, call record). A saved reply to anything else (the prompt or
    the checker's feedback changed since, or no key: an older run) is moved
    aside as `<name>.stale<N>.md` and asked again; a reply without a .json
    (placed by hand: the tests' canned replies) is used as it is."""
    meta = path.with_suffix(".json")
    key = conversation_key(messages, state["model"])
    if path.exists():
        if not meta.exists():  # placed by hand (the tests' canned replies): used as it is
            return path.read_text(), None
        saved = json.loads(meta.read_text())
        if saved.get("conversation") == key:
            return path.read_text(), saved
        n = 1
        while path.with_name(f"{path.stem}.stale{n}.md").exists():
            n += 1
        path.rename(path.with_name(f"{path.stem}.stale{n}.md"))
        if meta.exists():
            meta.rename(path.with_name(f"{path.stem}.stale{n}.json"))
        print(f"port_pipeline: {label}: the saved reply answered another prompt: asking again", file=sys.stderr)
    if state["dry_run"]:
        raise Stop("dry run")
    if state["spent"] + state["reserve"] > state["budget"]:
        raise Stop(f"budget: ${state['spent']:.4f} spent of ${state['budget']:.2f}")
    call = sb.call_model(messages, state["model"], state["max_tokens"])
    state["spent"] += call["cost"] or 0.0
    path.write_text(call["text"])
    record = {k: v for k, v in call.items() if k != "text"}
    record["conversation"] = key
    meta.write_text(json.dumps(record, indent=1))
    print(f"port_pipeline: {label}: {call['prompt_tokens']} in, {call['completion_tokens']} out, ${call['cost']:.4f}, "
          f"{call['seconds']} s (run total ${state['spent']:.4f})", file=sys.stderr)
    return call["text"], record


def zig_block(text):
    m = re.search(r"```zig\s*\n(.*?)\n```", text, re.S)
    if m:
        return m.group(1) + "\n"
    stripped = text.strip()
    return stripped + "\n" if stripped.startswith(("//", "const ", "pub ")) else None


# ---------------------------------------------------------------- writing
def toml_string(s):
    return json.dumps(s, ensure_ascii=True)


def toml_entry(plan, entry, model):
    lines = [f"[function.{plan.name}]", f'spec = "{plan.spec_file}"',
             f"iterations = {max(ITERATIONS[0], min(ITERATIONS[1], int(entry.get('iterations', 20000))))}",
             f"# Drafted by pc/tools/port_pipeline.py ({model}); review queue: pc/docs/porting/battle/pipeline/REVIEW.md"]
    for m in entry.get("mutation", []):
        lines += ["", f"[[function.{plan.name}.mutation]]", f"name = {toml_string(m['name'])}", 'target = "c"',
                  f"from = {toml_string(m['from'])}", f"to = {toml_string(m['to'])}"]
        if m.get("expect", "mismatch") != "mismatch":
            lines.append(f"expect = {toml_string(m['expect'])}")
    for w in entry.get("coverage_waiver", []):
        lines += ["", f"[[function.{plan.name}.coverage_waiver]]", f"id = {toml_string(w['id'])}",
                  f"reason = {toml_string(w['reason'])}"]
    return "\n".join(lines) + "\n"


def render_subsystem(name, sub):
    funcs = "\n".join(f'    {{ addr = {f["addr"]:#010X}, name = "{f["name"]}", zig = "{f["zig"]}" }},'.replace("0X", "0x")
                      for f in sub["functions"])

    def lst(items):
        return "[" + ", ".join(f'"{i}"' for i in items) + "]"
    return (f"[subsystem.{name}]\noverlay = \"{sub['overlay']}\"\nunit = \"{sub['unit']}\"\ndefault = \"original\"\n"
            f"roots = {lst(sub.get('roots', []))}\nfunctions = [\n{funcs}\n]\n\n[subsystem.{name}.views]\n"
            f"types = {lst(sub['views'].get('types', []))}\nglobals = {lst(sub['views'].get('globals', []))}\n")


PIPELINE_HEADER = ("\n# The port pipeline (pc/tools/port_pipeline.py): one subsystem per unit,\n"
                   "# battle_<unit>_port, written by the pipeline (default original).\n")


def write_ownership(plan):
    text = OWNERSHIP.read_text()
    data = tomllib.loads(text)
    name = plan.subsystem
    sub = data.get("subsystem", {}).get(name)
    if sub is None:
        sub = {"overlay": OVERLAY, "unit": plan.unit, "roots": [], "functions": [], "views": {"types": [], "globals": []}}
    sub = json.loads(json.dumps(sub))
    sub.setdefault("views", {}).setdefault("types", [])
    sub["views"].setdefault("globals", [])
    sub["functions"] = [f for f in sub["functions"] if f["name"] != plan.name] + [
        {"addr": plan.address, "name": plan.name, "zig": f"{plan.module}.{plan.zig_name}"}]
    sub["functions"].sort(key=lambda f: f["addr"])
    sub["views"]["types"] = sorted(set(sub["views"]["types"]) | set(plan.types))
    sub["views"]["globals"] = sorted(set(sub["views"]["globals"]) | set(plan.globals))
    block = render_subsystem(name, sub)
    start = text.find(f"[subsystem.{name}]\n")
    if start < 0:
        if PIPELINE_HEADER.strip() not in text:
            text = text.rstrip("\n") + "\n" + PIPELINE_HEADER
        text = text.rstrip("\n") + "\n\n" + block
    else:
        nxt = re.search(r"^\[subsystem\.(?!" + re.escape(name) + r"\.views\])", text[start + 1:], re.M)
        end = start + 1 + nxt.start() if nxt else len(text)
        text = text[:start] + block + ("\n" if nxt else "") + text[end:]
    OWNERSHIP.write_text(text)
    ownership.load(OWNERSHIP)  # still a valid table


def write_root(plan):
    text = ROOT_ZIG.read_text()
    marker = "// The port pipeline (pc/tools/port_pipeline.py): one module per function."
    imp = f'pub const {plan.module} = @import("{plan.module}.zig");'
    if imp not in text:
        if marker not in text:
            anchor = text.index("\ncomptime {", text.index("pub const actor_animation"))
            text = text[:anchor] + "\n" + marker + "\n" + text[anchor:]
        i = text.index(marker) + len(marker)
        while text.startswith("\npub const ", i):
            i = text.index("\n", i + 1)
        text = text[:i] + "\n" + imp + text[i:]
    export = (f"    common.route.exportAll(@backingInt(ownership.Subsystem.{plan.subsystem}), "
              f"ownership.routesOf(.{plan.subsystem}), @This());")
    if export not in text:
        last = text.rindex("ownership.routesOf(.")
        close = text.index("\n}", last)
        text = text[:close] + "\n" + export + text[close:]
    ROOT_ZIG.write_text(text)


def regression(plan, unit, views_path, table, build, work):
    """The unit's existing specs with the widened views: [] when they pass."""
    specs = tomllib.loads(SPECS_TOML.read_text())["function"]
    problems = []
    for s in table.subsystems:
        if s.unit != plan.unit:
            continue
        for _, fn in s.functions:
            if fn not in specs or fn not in s.zig:
                continue
            try:
                b = zig_diff.Build(fn, specs[fn], build, work / "regression" / fn, table, views=views_path,
                                   cmod=unit.c_path)
                (work / "regression" / fn).mkdir(parents=True, exist_ok=True)
                exe, _ = b.compile()
                code, out = run_exe(exe, 2000, 1)
                if code != 0:
                    problems.append(f"{fn}: {sb.first_line(out)}")
            except zig_diff.Failure as error:
                problems.append(f"{fn}: {str(error)[-400:]}")
    return problems


# ---------------------------------------------------------------- records
def review_reasons(record):
    out = []
    plan = record["plan"]
    spec = record.get("spec", {})
    if len(spec.get("attempts", [])) > 1:
        out.append("the spec needed a retry")
    if len(record.get("port", {}).get("attempts", [])) > 1:
        out.append("the port needed a retry")
    if plan.get("hooks"):
        out.append("ROOD_PC hooks: " + ", ".join(plan["hooks"]))
    if plan.get("statics"):
        out.append("statics: " + ", ".join(plan["statics"]))
    if spec.get("waivers"):
        out.append("coverage waivers: " + "; ".join(f"{w['id']} ({w['reason']})" for w in spec["waivers"]))
    if record["status"] != "ported":
        out.append(f"not ported: {record['status']}")
    return out


def write_review(records_dir):
    records = []
    for p in sorted(records_dir.glob("*/*.json")):
        if p.name == "summary.json":
            continue
        records.append(json.loads(p.read_text()))
    lines = ["# Port pipeline: the review queue",
             "",
             "Generated by `pc/tools/port_pipeline.py` from the records beside this file;",
             "do not edit. A stronger reviewer checks, per function: every spec's input",
             "classes and value edges (are the C's boundaries, signs and nulls all",
             "there?), and above all the functions flagged below (a retry, a ROOD_PC",
             "hook, statics, waivers). Mark a function reviewed by setting its record's",
             "`reviewed` to a one-line verdict, with the details (spec fixes, mutations",
             "added, port fixes, a rename, corpus reach) in `review`; the queue then",
             "lists it under Reviewed.", ""]
    pending = [r for r in records if not r.get("reviewed")]
    flagged = [r for r in pending if review_reasons(r)]
    lines.append(f"{len(pending)} to review ({len(flagged)} flagged), {len(records) - len(pending)} reviewed.")
    for r in sorted(pending, key=lambda r: (not review_reasons(r), r["unit"], r["function"])):
        plan, spec = r["plan"], r.get("spec", {})
        lines += ["", f"## `{r['function']}` ({r['unit']}) -> `{plan['module']}.{plan['zig_name']}`: {r['status']}", ""]
        reasons = review_reasons(r)
        lines.append("- **Flags:** " + ("; ".join(reasons) if reasons else "none"))
        lines.append(f"- Name: {plan['name_source']}; signature `{plan['signature']}`")
        if spec.get("classes"):
            lines.append("- Input classes: " + ", ".join(f"{k}={v}" for k, v in spec["classes"].items()))
        if spec.get("mutations"):
            lines.append("- Mutations (of the C): " + "; ".join(f"{m['name']} ({'caught' if m['caught'] else 'MISSED'})"
                                                                for m in spec["mutations"]))
        cov = spec.get("coverage") or {}
        if cov:
            lines.append(f"- Coverage: branches {cov.get('branches_hit')}/{cov.get('branches')}, lines "
                         f"{cov.get('lines_hit')}/{cov.get('lines')}")
            for a in cov.get("args", []):
                lines.append(f"  - arg {a['arg']}: {a['text']}")
            for c in cov.get("compares", []):
                lines.append(f"  - compare +{c['line']} ({c['size'] * 8}-bit, {c['operand']}): {c['outcomes']}: `{c['text']}`")
            for u in cov.get("uncovered", []) + cov.get("flags", []):
                lines.append(f"  - {u['id']}: {u['what']}")
    done = [r for r in records if r.get("reviewed")]
    if done:
        lines += ["", "## Reviewed", ""]
        for r in sorted(done, key=lambda r: (r["unit"], r["function"])):
            review = r.get("review") or {}
            renamed = f" -> `{review['renamed']}`" if review.get("renamed") else ""
            verdict = r["reviewed"] if isinstance(r["reviewed"], str) else "reviewed"
            lines.append(f"- `{r['function']}` ({r['unit']}){renamed}: {verdict}")
            for key, label in (("port_fixes", "port fix"), ("spec_fixes", "spec fix"),
                               ("mutations_added", "mutations added"), ("corpus", "corpus")):
                value = review.get(key)
                if value:
                    lines.append(f"  - {label}: " + ("; ".join(value) if isinstance(value, list) else value))
    (records_dir / "REVIEW.md").write_text(prompt_privacy.sanitise("\n".join(lines) + "\n"))


def write_summary(records_dir, unit_stem):
    rows = []
    for p in sorted((records_dir / unit_stem).glob("*.json")):
        if p.name != "summary.json":
            rows.append(json.loads(p.read_text()))
    totals = {"functions": len(rows), "ported": sum(r["status"] == "ported" for r in rows),
              "cost": round(sum(r["totals"]["cost"] for r in rows), 4),
              "prompt_tokens": sum(r["totals"]["prompt_tokens"] for r in rows),
              "completion_tokens": sum(r["totals"]["completion_tokens"] for r in rows),
              "model_seconds": round(sum(r["totals"]["model_seconds"] for r in rows), 1),
              "check_seconds": round(sum(r["totals"]["check_seconds"] for r in rows), 1),
              "statuses": {s: sum(r["status"] == s for r in rows) for s in sorted({r["status"] for r in rows})}}
    (records_dir / unit_stem / "summary.json").write_text(json.dumps(
        {"unit": unit_stem, "totals": totals, "functions": [
            {"function": r["function"], "status": r["status"], "spec_attempts": len(r.get("spec", {}).get("attempts", [])),
             "port_attempts": len(r.get("port", {}).get("attempts", [])), "cost": r["totals"]["cost"]} for r in rows]},
        indent=1) + "\n")
    return totals


# ---------------------------------------------------------------- one function
def totals_of(record):
    calls = [a.get("call") or {} for k in ("spec", "port") for a in record.get(k, {}).get("attempts", [])]
    return {"cost": round(sum(c.get("cost") or 0.0 for c in calls), 5),
            "prompt_tokens": sum(c.get("prompt_tokens") or 0 for c in calls),
            "completion_tokens": sum(c.get("completion_tokens") or 0 for c in calls),
            "model_seconds": round(sum(c.get("seconds") or 0.0 for c in calls), 1),
            "check_seconds": round(record.get("check_seconds", 0.0), 1)}


def run_function(plan, unit, table, args, state):
    record = {}
    try:
        return run_steps(plan, unit, table, args, state, record)
    except Stop as stop:
        record["status"] = "stopped"
        record["stopped"] = str(stop)
        return record


def run_steps(plan, unit, table, args, state, record):
    fdir = args.work / plan.name
    fdir.mkdir(parents=True, exist_ok=True)
    record.update({"function": plan.name, "unit": plan.unit, "difficulty": plan.difficulty, "model": args.model,
              "status": "started", "reviewed": False, "plan": dataclasses.asdict(plan), "spec": {"attempts": []},
              "port": {"attempts": []}, "written": [], "check_seconds": 0.0})
    record["plan"]["address"] = f"{plan.address:#010x}"
    views_path, _ = unit.views(plan.types, plan.globals, tag=f"fn-{plan.name}")
    spec_text = prompt_privacy.sanitise(spec_prompt(plan, unit, views_path, table, args.build))
    port_text = prompt_privacy.sanitise(port_prompt(plan, unit, views_path, table, args.build))
    (fdir / "spec_prompt.md").write_text(spec_text)
    (fdir / "port_prompt.md").write_text(port_text)
    if args.leak_probe:
        probe = feedback([f"FAIL: {fdir}/build/ref.c:12: error in {ROOT}/game/battle/x.zig "
                          f"({zig_toolchain.global_cache()}/o/abc) {pathlib.Path.home()}/.cache/x"], "probe")
        (fdir / "feedback_probe.md").write_text(probe)
    if args.dry_run:
        record["status"] = "dry-run"
        print(f"port_pipeline: {plan.name}: dry run: spec prompt {len(spec_text)} chars, port prompt "
              f"{len(port_text)} chars -> {plan.module}.{plan.zig_name} ({plan.name_source})", file=sys.stderr)
        return record
    started = time.monotonic()
    # 1. The spec.
    messages = [{"role": "user", "content": spec_text}]
    entry = spec_path = None
    for attempt in (1, 2):
        reply, call = ask(state, messages, fdir / f"spec_reply{attempt}.md", f"{plan.name} spec {attempt}")
        zig_text, toml_text = sb.split_reply(reply)
        entry, toml_error = sb.parse_toml(plan.name, toml_text)
        errors, details = [], {}
        if zig_text is None:
            errors.append("FAIL: no ```zig block in the reply")
        if toml_error:
            errors.append(toml_error)
        ok = False
        if not errors:
            spec_path = fdir / f"spec{attempt}.zig"
            spec_path.write_text(zig_text)
            ok, errors, details = spec_gate(plan, entry, spec_path, views_path, unit, table, args.build,
                                            fdir / f"spec{attempt}")
        record["spec"]["attempts"].append({"call": call, "passed": ok, "errors": [prompt_privacy.sanitise(e)[:3000]
                                                                                for e in errors]})
        record["spec"].update(details)
        if ok:
            record["spec"]["waivers"] = entry.get("coverage_waiver", [])
            break
        messages += [{"role": "assistant", "content": reply}, {"role": "user", "content": feedback(errors, "spec")}]
    record["check_seconds"] += time.monotonic() - started
    if not record["spec"]["attempts"][-1]["passed"]:
        record["status"] = "spec-failed"
        return record
    # 2. The port.
    started = time.monotonic()
    messages = [{"role": "user", "content": port_text}]
    port_path = None
    for attempt in (1, 2):
        reply, call = ask(state, messages, fdir / f"port_reply{attempt}.md", f"{plan.name} port {attempt}")
        code = zig_block(reply)
        errors, runs, ok = [], [], False
        if code is None:
            errors.append("FAIL: no Zig file in the reply")
        else:
            port_path = fdir / f"port{attempt}" / f"{plan.module}.zig"
            port_path.parent.mkdir(parents=True, exist_ok=True)
            port_path.write_text(code)
            ok, errors, runs = port_gate(plan, entry, spec_path, port_path, views_path, unit, table, args.build,
                                         fdir / f"port{attempt}", args.seeds)
        record["port"]["attempts"].append({"call": call, "passed": ok, "runs": runs,
                                           "errors": [prompt_privacy.sanitise(e)[:3000] for e in errors]})
        if ok:
            break
        messages += [{"role": "assistant", "content": reply}, {"role": "user", "content": feedback(errors, "port")}]
    record["check_seconds"] += time.monotonic() - started
    if not record["port"]["attempts"][-1]["passed"]:
        record["status"] = "port-failed"
        return record
    # 3. Into the tree (unless the unit's existing ports break with the widened views).
    problems = regression(plan, unit, views_path, table, args.build, fdir)
    if problems:
        record["status"] = "views-conflict"
        record["conflicts"] = [prompt_privacy.sanitise(p) for p in problems]
        return record
    if args.no_write:
        record["status"] = "passed (not written)"
        return record
    spec_dest = SPEC_DIR / plan.spec_file
    spec_dest.write_text(spec_path.read_text())
    SPECS_TOML.write_text(SPECS_TOML.read_text().rstrip("\n") + "\n\n" + toml_entry(plan, entry, args.model))
    port_dest = GAME / OVERLAY / f"{plan.module}.zig"
    port_dest.write_text(port_path.read_text())
    write_ownership(plan)
    write_root(plan)
    unit.base_types |= set(plan.types)
    unit.base_globals |= set(plan.globals)
    record["written"] = [str(p.relative_to(ROOT)) for p in (spec_dest, SPECS_TOML, port_dest, OWNERSHIP, ROOT_ZIG)]
    record["status"] = "ported"
    return record


def select(args, rows):
    if args.functions:
        missing = [f for f in args.functions if f not in rows]
        if missing:
            raise SystemExit(f"port_pipeline: not in FUNCTIONS.tsv: {', '.join(missing)}")
        return list(args.functions)
    unit = args.unit if args.unit.endswith(".c") else None
    out = [n for n, r in rows.items() if (r["file"] == unit if unit else pathlib.PurePosixPath(r["file"]).stem == args.unit)]
    if args.difficulty:
        out = [n for n in out if rows[n]["difficulty"] in args.difficulty.split(",")]
    out = [n for n in out if not eligible(rows[n])]
    return out[:args.limit] if args.limit else out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--unit", help="every portable function of a unit (146C or src/BATTLE/BATTLE.PRG/146C.c)")
    parser.add_argument("--difficulty", help="with --unit: only these difficulties (S,M,L,XL)")
    parser.add_argument("--limit", type=int, help="with --unit: at most this many functions")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--budget", type=float, default=5.0, help="US$ for this run, checked before every call")
    parser.add_argument("--reserve", type=float, default=0.02, help="US$ kept free for the next call (default 0.02)")
    parser.add_argument("--max-tokens", type=int, default=64000)
    parser.add_argument("--seeds", default="1,2,3", help="the port gate's harness seeds")
    parser.add_argument("--dry-run", action="store_true", help="plan and write the prompts; no calls, nothing written")
    parser.add_argument("--no-write", action="store_true", help="run the gates but leave the tree alone")
    parser.add_argument("--force", action="store_true", help="run functions whose record says ported again")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "pc/build/port-pipeline",
                        help="prompts, replies (reused on resume), scratch builds")
    parser.add_argument("--records", type=pathlib.Path, default=RECORDS,
                        help="per-function records, unit summaries and REVIEW.md (committed)")
    parser.add_argument("--out", type=pathlib.Path, help="(--dry-run) the work directory, records under it")
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    parser.add_argument("--leak-probe", action="store_true", help=argparse.SUPPRESS)
    # The tests: plan against a copy of the table (with --no-write).
    parser.add_argument("--table", type=pathlib.Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if bool(args.functions) == bool(args.unit):
        parser.error("give FUNCTION... or --unit UNIT")
    if args.out:
        args.work, args.records = args.out, args.out / "records"
    args.work = args.work.resolve()
    args.seeds = [int(s) for s in args.seeds.split(",") if s]
    if not (args.build / "imports.ld").exists():
        print(f"port_pipeline: {args.build} has no 64-bit build (python3 pc/tools/build_boot.py --arch 64)",
              file=sys.stderr)
        return 77
    if not args.dry_run and not os.environ.get("OPENROUTER_API_KEY"):
        print("port_pipeline: OPENROUTER_API_KEY is not set (run through `bash -ic` so your profile sets it)",
              file=sys.stderr)
        return 2
    rows = functions_tsv()
    names = meanings()
    if args.table and not args.no_write and not args.dry_run:
        parser.error("--table needs --no-write")
    table = ownership.load(args.table or OWNERSHIP)
    state = {"spent": 0.0, "budget": args.budget, "reserve": args.reserve, "model": args.model,
             "max_tokens": args.max_tokens, "dry_run": args.dry_run}
    units, done_units, results = {}, set(), []
    for name in select(args, rows):
        stem = pathlib.PurePosixPath(rows[name]["file"]).stem
        record_path = args.records / stem / f"{name}.json"
        if record_path.exists() and not args.force and json.loads(record_path.read_text()).get("status") == "ported":
            print(f"port_pipeline: {name}: already ported (record): skipped", file=sys.stderr)
            continue
        plan, unit = plan_function(name, rows[name], table, names, units, args.build, args.work)
        if plan.skip:
            print(f"port_pipeline: {name}: skipped: {plan.skip}", file=sys.stderr)
            continue
        record = run_function(plan, unit, table, args, state)
        record["totals"] = totals_of(record)
        results.append(record)
        print(f"port_pipeline: {name}: {record['status']} (${record['totals']['cost']:.4f})", file=sys.stderr)
        if args.dry_run:
            continue
        record_path.parent.mkdir(parents=True, exist_ok=True)
        record_path.write_text(prompt_privacy.sanitise(json.dumps(record, indent=1)) + "\n")
        done_units.add(stem)
        if record["status"] == "ported":
            table = ownership.load(OWNERSHIP)
        if record["status"] == "stopped":
            print(f"port_pipeline: stopped in {name}: {record['stopped']}", file=sys.stderr)
            break
    for stem in sorted(done_units):
        totals = write_summary(args.records, stem)
        print(f"port_pipeline: unit {stem}: {totals}", file=sys.stderr)
    if done_units:
        write_review(args.records)
    print(f"port_pipeline: {len(results)} function(s), ${state['spent']:.4f} spent this run", file=sys.stderr)
    return 0 if all(r["status"] in ("ported", "dry-run", "passed (not written)") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
