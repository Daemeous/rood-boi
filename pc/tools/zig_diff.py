#!/usr/bin/env python3
"""Differential check of a Zig port against its original C body
(PORTING.md §19): CPU only, no game run, a few seconds per function.

    python3 pc/tools/zig_diff.py [FUNCTION...] [--self-check] [--iterations N] [--seed S]

For each function (default: every one in pc/tests/zig_diff/specs.toml):
  - the original body is the build's own: `FUNCTION__orig` from the unit's
    routed .i (the text GCC compiled for the game), kept whole while every
    other body is stripped (header `static inline`s are kept too); it is
    compiled with the game's C flags;
  - the port is the file `ownership.toml`'s `zig` path names, compiled with
    the game's Zig flags against the build's generated views_<unit>/c_<unit>;
  - every global either side reaches is defined at its C size in one
    section (`zdiff_state`); the unit's own data definitions keep their
    values (`zdiff_fixed`); the PS1 scratchpad and PS1 address 0 (low RAM, where
    the PC C's NULL guards read) are mapped;
  - every external function is a recording stub generated from the `c`
    module's prototype (the spec may give one a behaviour: an arena block
    for an allocator); a static the C calls is a trap;
  - pc/tests/zig_diff/main.zig runs both on the spec's random and edge
    inputs and compares all of that state, the external call traces (with a
    hash of the state at each call) and the return values.
--self-check also applies each of the spec's mutations (wrong variants of
the port, or with `target = "c"` of the C body, run against a stand-in port
that is the C itself) and requires the harness to catch every one, and
requires every input class the spec declares to occur.

--coverage adds the coverage lane (TRIAL.md's design note): the original
body again at -O0 with gcov (branches; conditions on GCC 14+), every
comparison's operands (-fsanitize-coverage=trace-cmp) and every load
(-fsanitize=thread, with zdiff_cov.c as its runtime), run on the spec's
inputs (the C only). Per function it reports branches hit/total with each
uncovered one and its line, the arguments' value ranges, every comparison's
outcomes (==, <, >) and every load's range (zero, negative, max, the values
when few). The gate: branch coverage at --coverage-threshold (default 100%),
and no value flag: a compare whose sides were seen both ways but never equal
(`+N eq`: a port's `<` for `<=` would pass), a nonzero test only ever seen
with the sign bit set (`+N sign`). Waivers, with reasons, in the function's
specs.toml entry: [[function.F.coverage_waiver]] id = "+12 b1", reason = "...".
Needs a 64-bit build (build_boot.py --arch 64): it reads pc/build/boot64.
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import bakeoff_context  # noqa: E402
import ownership  # noqa: E402
import zig_toolchain  # noqa: E402

SPECS = ROOT / "pc/tests/zig_diff"
HARNESS = SPECS / "main.zig"
API = SPECS / "zdiff.zig"
# build_boot.py's game C flags for the 64-bit build (the .i is already
# preprocessed), and its Zig flags for owned objects.
C_FLAGS = ["-m64", "-fpie", "-funsigned-char", "-fno-strict-aliasing", "-fwrapv", "-fno-builtin",
           "-fgnu89-inline", "-O2", "-g", "-std=gnu17", "-w",
           # one .data/.bss to rename into zdiff_fixed
           "-fno-data-sections", "-fno-zero-initialized-in-bss"]
ZIG_FLAGS = ["-target", "x86_64-linux-none", "-mcpu", "x86_64", "-O", "ReleaseSafe", "-fllvm", "-fsingle-threaded",
             "-fno-stack-check", "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections", "-fPIE"]
DATA_SECTIONS = [".data", ".bss", ".data.rel", ".data.rel.local"]
HARNESS_DEFINED = {"pc_psx_bad_pointer", "pc_zig_panic", "zdiff_unexpected"}  # main.zig
COV_RUNTIME = SPECS / "zdiff_cov.c"
# The coverage lane: gcov branches (and conditions, MC/DC, on GCC 14+), every
# comparison's operands and every load (the value probes, zdiff_cov.c).
# A compare whose sides were equal fewer times than this (and under 1% of its
# executions) is flagged: one equal input may be masked by a later test.
RARE_EQUAL = 10
COVERAGE_FLAGS = ["--coverage", "-fsanitize-coverage=trace-cmp", "-fsanitize=thread"]
# GCC's own runtime calls in the game's objects (a function with a local
# array gets the stack protector, as in the game build): the harness links libc's.
LIBC = {"__stack_chk_fail"}


class Failure(Exception):
    pass


def run(args, **kw):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, **kw)
    if result.returncode:
        raise Failure(f"{' '.join(str(a) for a in args[:3])} ...: {(result.stderr or result.stdout)[-3000:]}")
    return result.stdout


def gcc_major():
    version = subprocess.run(["gcc", "-dumpversion"], capture_output=True, text=True).stdout
    return int(re.match(r"\d+", version).group())


def gcc_permissive():
    return ["-fpermissive"] if gcc_major() >= 14 else []


def condition_coverage():
    """GCC 14+ measures conditions (MC/DC) too; older GCC only branches."""
    return gcc_major() >= 14


def find_route(table, function):
    for sub in table.subsystems:
        if any(name == function for _, name in sub.functions):
            return sub, sub.zig.get(function)
    raise Failure(f"{function}: not routed in game/ownership.toml")


def static_inline_names(text):
    tokens, found = ownership._file_scope_functions(text)
    names = []
    for name_index, _, _, start in found:
        spec = [t[2] for t in tokens[start:name_index]]
        if b"static" in spec and any(t in spec for t in (b"inline", b"__inline", b"__inline__")):
            names.append(tokens[name_index][2].decode())
    return names


def sizes_from_dwarf(obj, names):
    """sizeof each global in the unit object's DWARF (-1: incomplete)."""
    script = ("import gdb, json\nout = {}\nfor n in %r:\n    try:\n        out[n] = int(gdb.parse_and_eval('sizeof(' + n + ')'))\n"
              "    except Exception:\n        out[n] = -1\nprint('ZDIFF' + json.dumps(out))\n" % list(names))
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as f:
        f.write(script)
    try:
        out = subprocess.run(["gdb", "-batch", "-nx", "-iex", "set auto-load off", str(obj), "-x", f.name],
                             capture_output=True, text=True).stdout
    finally:
        os.unlink(f.name)
    line = next((l for l in out.splitlines() if l.startswith("ZDIFF")), None)
    if line is None:
        raise Failure(f"gdb on {obj}: no sizes")
    return json.loads(line[5:])


_OBJECT_SIZES = {}


def sizes_from_objects(build, names):
    """An array declared `extern T x[];` here but defined in another unit:
    its symbol size in the build's object that defines it (nm -S)."""
    if not names:
        return {}
    key = str(build)
    if key not in _OBJECT_SIZES:
        sizes = {}
        objects = sorted(str(o) for o in pathlib.Path(build).glob("*.o"))
        if objects:
            out = subprocess.run(["nm", "-S", "--defined-only", *objects], capture_output=True, text=True).stdout
            for line in out.splitlines():
                parts = line.split()
                if len(parts) == 4 and parts[2] in "DdBbRrGgSs":
                    sizes[parts[3]] = max(sizes.get(parts[3], 0), int(parts[1], 16))
        _OBJECT_SIZES[key] = sizes
    return {n: _OBJECT_SIZES[key][n] for n in names if _OBJECT_SIZES[key].get(n, 0) > 0}


def sizes_from_imports(imports_ld, names):
    """An unsized array's room: the gap to the next PS1 symbol (imports.ld)."""
    addresses = {}
    for m in re.finditer(r"^(\w+) = (0x[0-9a-fA-F]+);", imports_ld.read_text(), re.M):
        addresses[m.group(1)] = int(m.group(2), 16)
    ordered = sorted(set(addresses.values()))
    out = {}
    for n in names:
        if n in addresses:
            i = ordered.index(addresses[n])
            if i + 1 < len(ordered) and ordered[i + 1] - addresses[n] <= 0x10000:
                out[n] = ordered[i + 1] - addresses[n]
    return out


def sizes_from_native_imports(native_c, names):
    """An unsized pointer array the 64-bit build moved to a native region
    (native_imports.c): from its offset to the region's end."""
    text = native_c.read_text() if native_c.exists() else ""
    regions = {m.group(1): int(m.group(2)) * 8
               for m in re.finditer(r"unsigned long long (rood_lp64_\w+)\[(\d+)\]", text)}
    out = {}
    for m in re.finditer(r"\.set (\w+), (rood_lp64_\w+) \+ (\d+)", text):
        if m.group(1) in names and m.group(2) in regions:
            out[m.group(1)] = regions[m.group(2)] - int(m.group(3))
    return out


class Build:
    def __init__(self, function, entry, build_dir, work, table, port_source=None, route=None, views=None, cmod=None):
        """port_source: a candidate file to compile in place of the port
        (pc/tools/bakeoff_score.py); the checkout's file is never changed.
        route, views, cmod (pc/tools/port_pipeline.py, before the table and
        the build know the function): (subsystem, zig path), and the
        generated views_<unit>/c_<unit> modules to use instead of the build's."""
        self.function, self.entry, self.build, self.work = function, entry, build_dir, work
        self.sub, zig_path = route or find_route(table, function)
        if not zig_path:
            raise Failure(f"{function}: no `zig` path in ownership.toml")
        *mods, self.zig_fn = zig_path.split(".")
        if len(mods) != 1:
            raise Failure(f"{function}: zig path {zig_path}: expected file.function")
        self.port_rel = pathlib.Path("game") / self.sub.overlay / (mods[0] + ".zig")
        self.port_source = pathlib.Path(port_source) if port_source else ROOT / self.port_rel
        self.stem = pathlib.PurePosixPath(self.sub.unit).stem
        units = list(build_dir.glob(f"*_{self.stem}.i"))
        if len(units) != 1:
            raise Failure(f"{build_dir}: expected one *_{self.stem}.i (build_boot.py --arch 64), found {len(units)}")
        self.unit_i = units[0]
        self.unit_o = self.unit_i.with_suffix(".o")
        self.views = pathlib.Path(views) if views else build_dir / "zig" / f"views_{self.stem}.zig"
        self.c = pathlib.Path(cmod) if cmod else build_dir / "zig" / f"c_{self.stem}.zig"
        for path in (self.unit_o, self.views, self.c, build_dir / "imports.ld"):
            if not path.exists():
                raise Failure(f"{path}: missing (build_boot.py --arch 64)")
        self.zig = str(zig_toolchain.ensure())

    def sources(self, mutation):
        """game/common and the port, copied, with the mutation applied."""
        src = self.work / "game"
        if src.exists():
            shutil.rmtree(src)
        shutil.copytree(ROOT / "game/common", src / "common")
        port = src / self.sub.overlay / self.port_rel.name
        port.parent.mkdir(parents=True)
        shutil.copy(self.port_source, port)
        if mutation:
            target = self.work / mutation.get("file", str(self.port_rel))
            if not target.exists():
                raise Failure(f"mutation {mutation['name']}: {mutation.get('file')} is not the port or game/common")
            text = target.read_text()
            if text.count(mutation["from"]) != 1:
                raise Failure(f"mutation {mutation['name']}: its `from` text occurs {text.count(mutation['from'])} "
                              f"times in {target.relative_to(self.work)} (stale: update specs.toml)")
            target.write_text(text.replace(mutation["from"], mutation["to"]))
        return src, port

    def reference(self, standin=False, c_mutation=None, coverage=False):
        """The original body as GCC compiles it for the game (`FUNCTION__orig`;
        a function the build does not route yet is renamed so). standin: a
        second copy, `FUNCTION__standin`, that the stand-in port calls (with
        c_mutation applied to it: a wrong variant of the C). coverage: -O0
        with gcov and the value probes (zdiff_cov.c)."""
        text = self.unit_i.read_bytes()
        orig = self.function + "__orig"
        if (orig + "(").encode() not in text:
            text, counts = ownership.rename_definitions(text, [self.function])
            if counts[self.function] != 1:
                raise Failure(f"{self.unit_i.name}: no definition of {self.function}")
        keep = [orig] + static_inline_names(text)
        ref_c = self.work / ("cov" if coverage else "") / "ref.c"
        ref_c.parent.mkdir(parents=True, exist_ok=True)
        source = ownership.strip_bodies(text, keep=keep)
        if (orig + "(").encode() not in source:
            raise Failure(f"{self.unit_i.name}: no {orig} definition")
        if standin:
            source += self.standin_source(source.decode(errors="replace"), c_mutation).encode()
        ref_c.write_bytes(source)
        ref_o = ref_c.with_suffix(".o")
        flags = C_FLAGS
        if coverage:
            flags = [f for f in C_FLAGS if f not in ("-O2", "-fno-data-sections")] + ["-O0", "-fdata-sections", *COVERAGE_FLAGS]
            if condition_coverage():
                flags.append("-fcondition-coverage")
        run(["gcc", *flags, *gcc_permissive(), "-c", ref_c, "-o", ref_o], cwd=ref_c.parent)
        present = run(["objdump", "-h", ref_o])
        renames = []
        sections = re.findall(r"^\s*\d+\s+(\S+)", present, re.M)
        for section in sections:
            if section in DATA_SECTIONS or (coverage and re.match(r"\.(data|bss)(\.rel(\.local)?)?\.", section)
                                            and not re.search(r"gcov|LPBX|\.rel\.ro", section)):
                renames += ["--rename-section", f"{section}=zdiff_fixed"]
        if coverage:
            # gcov's counters stay out of zdiff_fixed (restored every iteration).
            renames = [r for r in renames if not re.search(r"gcov|LPBX", r)]
        if renames:
            run(["objcopy", *renames, ref_o])
        return ref_o

    def standin_source(self, ref_text, c_mutation=None):
        """The body again as `FUNCTION__standin` (no line markers: its lines
        are its own file's), the C mutation applied to it."""
        body = bakeoff_context.c_body(ref_text, self.function)
        if body is None:
            raise Failure(f"stand-in: no body of {self.function}")
        inner = body[body.index("{"):]
        if re.search(r"\bstatic\b", inner):
            raise Failure(f"stand-in: {self.function} has a block-scope static (a second copy would have its own)")
        if c_mutation:
            if inner.count(c_mutation["from"]) != 1:
                raise Failure(f"mutation {c_mutation['name']}: its `from` text occurs {inner.count(c_mutation['from'])} "
                              f"times in the C body of {self.function} (copy it exactly from the C)")
            inner = inner.replace(c_mutation["from"], c_mutation["to"])
        head = re.sub(rf"\b{re.escape(self.function)}(\s*\()", self.function + r"__standin\1", body[:body.index("{")], count=1)
        return f'\n#line 1 "zdiff_standin.c"\n{head}{inner}\n'

    def symbols(self, ref_o):
        undefined = set(run(["nm", "-u", ref_o]).split()) - {"U", "w", "_GLOBAL_OFFSET_TABLE_"}
        defined = {l.split()[-1] for l in run(["nm", "--defined-only", ref_o]).splitlines() if l.strip()}
        ctext = self.c.read_text()
        c_functions = set(re.findall(r"^pub extern fn (\w+)\(", ctext, re.M))
        statics = set(re.findall(r"^pub const (\w+) = @compileError", ctext, re.M))
        undefined -= HARNESS_DEFINED | LIBC
        undefined = {u for u in undefined if not u.startswith(("__gcov", "__sanitizer_cov", "__tsan_"))}
        stubs = sorted(u for u in undefined if u in c_functions)
        traps = sorted(u for u in undefined if u in statics)
        data = {u for u in undefined if u not in c_functions and u not in statics}
        data |= set(self.sub.views.get("globals", [])) - defined
        return stubs, traps, sorted(data)

    def state_source(self, traps, data):
        sizes = sizes_from_dwarf(self.unit_o, data) if data else {}
        missing = [n for n in data if sizes.get(n, -1) <= 0]
        sizes.update(sizes_from_imports(self.build / "imports.ld", missing))
        missing = [n for n in data if sizes.get(n, -1) <= 0]
        sizes.update(sizes_from_native_imports(self.build / "native_imports.c", missing))
        missing = [n for n in data if sizes.get(n, -1) <= 0]
        sizes.update(sizes_from_objects(self.build, missing))
        missing = [n for n in data if sizes.get(n, -1) <= 0]
        if missing:
            raise Failure(f"no size for {', '.join(missing)} (neither DWARF, imports.ld, native_imports.c nor "
                          f"the defining object)")
        lines = ["/* Generated by pc/tools/zig_diff.py. */",
                 "__attribute__((noreturn)) void zdiff_unexpected(const char*);",
                 'unsigned char zdiff_fixed_anchor[16] __attribute__((section("zdiff_fixed"), aligned(16))) = { 1 };']
        for n in data:
            lines.append(f'unsigned char {n}[{sizes[n]}] __attribute__((section("zdiff_state"), aligned(16)));')
        for n in traps:
            lines.append(f'void {n}(void) {{ zdiff_unexpected("{n}"); }}')
        path = self.work / "state.c"
        path.write_text("\n".join(lines) + "\n")
        return path, sizes

    def standin_port(self, port):
        """A port that calls `FUNCTION__standin` (a copy of the C), with the
        real port's (or the signature file's) type: zig_diff checks a spec
        and its C mutations before, and without, any Zig port."""
        path = self.work / "standin.zig"
        path.write_text("//! Generated by pc/tools/zig_diff.py: the original C as the port.\n"
                        'const std = @import("std");\n'
                        f'const real = @import("port_real").{self.zig_fn};\n'
                        "const G = @TypeOf(real);\n"
                        "const R = @typeInfo(G).@\"fn\".return_type.?;\n"
                        "fn body(args: std.meta.ArgsTuple(G)) R {\n"
                        f'    return @call(.auto, @extern(*const G, .{{ .name = "{self.function}__standin" }}), args);\n'
                        "}\n"
                        f'pub const {self.zig_fn} = @import("common").route.withSignature(G, body);\n')
        return path

    def compile(self, mutation=None, coverage=False, standin=False):
        """The harness executable. mutation: a wrong variant of the port, or
        of the C (`target = "c"`: run against the stand-in port, the C
        itself). standin: the port is the C (no Zig port needed: the port
        file only gives the signature). coverage: only the C runs, built for
        the coverage lane."""
        c_mutation = mutation if mutation and mutation.get("target") == "c" else None
        standin = standin or bool(c_mutation)
        src, port = self.sources(None if c_mutation else mutation)
        ref_o = self.reference(standin=standin, c_mutation=c_mutation, coverage=coverage)
        stubs, traps, data = self.symbols(ref_o)
        state_c, sizes = self.state_source(traps, data)
        spec = self.work / "spec.zig"
        shutil.copy(SPECS / self.entry["spec"], spec)
        extra_objects = []
        if coverage:
            cov_o = self.work / "cov" / "zdiff_cov.o"
            run(["gcc", "-m64", "-O1", "-g", "-fno-pie", "-c", COV_RUNTIME, "-o", cov_o])
            extra_objects.append(cov_o)
        for attempt in range(2):
            gen = self.work / "gen.zig"
            gen.write_text("//! Generated by pc/tools/zig_diff.py.\n"
                           f'pub const c = @import("c_{self.stem}");\n'
                           f'pub const port_fn = @import("port").{self.zig_fn};\n'
                           f'pub const orig_name = "{self.function}__orig";\n'
                           f'pub const function = "{self.function}";\n'
                           f"pub const coverage = {'true' if coverage else 'false'};\n"
                           "pub const stubs = [_][]const u8{ " + ", ".join(f'"{s}"' for s in stubs) + " };\n")
            obj = self.work / "harness.o"
            views, cmod = f"views_{self.stem}", f"c_{self.stem}"
            modules = [
                ("root", HARNESS, ["zdiff", "gen", "spec", "common"]),
                ("zdiff", API, []),
                ("gen", gen, ["port", cmod]),
                ("spec", spec, ["zdiff", "common", views, cmod, "port"]),
            ]
            if standin:
                modules += [("port", self.standin_port(port), ["common", "port_real"]),
                            ("port_real", port, ["common", views, cmod])]
            else:
                modules.append(("port", port, ["common", views, cmod]))
            modules += [
                (views, self.views, ["common"]),
                (cmod, self.c, []),
                ("common", src / "common/common.zig", []),
            ]
            args = [self.zig, "build-obj", "-fno-compiler-rt", f"-femit-bin={obj}",
                    "--cache-dir", self.work.parent / "zig-cache", "--global-cache-dir", zig_toolchain.global_cache()]
            for name, path, deps in modules:
                args += ZIG_FLAGS
                for dep in deps:
                    args += ["--dep", dep]
                args.append(f"-M{name}={path}")
            run(args)
            exe = self.work / ("cov/zdiff" if coverage else "zdiff")
            link = subprocess.run(["gcc", "-m64", "-no-pie", "-o", str(exe), str(obj), str(ref_o), str(state_c),
                                   *map(str, extra_objects), *(["--coverage"] if coverage else [])],
                                  capture_output=True, text=True)
            if not link.returncode:
                return exe, sizes
            # A function only the port calls: stub it too, once.
            extra = sorted(set(re.findall(r"undefined reference to `(\w+)'", link.stderr))
                           & set(re.findall(r"^pub extern fn (\w+)\(", self.c.read_text(), re.M)) - set(stubs))
            if attempt or not extra:
                raise Failure(f"link: {link.stderr[-3000:]}")
            stubs = sorted(set(stubs) | set(extra))

    def name_address(self, exe, text):
        """Name the addresses in the harness output (symbol+offset): memory
        that differs, a crash's fault address and the arguments it ran on."""
        return name_addresses(exe, text)


def name_addresses(exe, text):
    symbols = _symbols(exe)

    def name(hex_text, fault=False):
        address = int(hex_text, 16)
        if fault and address < 0x10000:
            return f"{hex_text} [NULL+{address:#x}: a field of a null pointer]"
        where = _addr_name(symbols, address)
        if where != f"{address:#x}":
            return f"{hex_text} [{where}]"
        if fault:
            return f"{hex_text} [memory the harness does not own: a pointer from random bytes?]"
        return hex_text
    text = re.sub(r"memory at (0x[0-9a-f]+)", lambda m: "memory at " + name(m.group(1)), text)
    text = re.sub(r"fault address (0x[0-9a-f]+)", lambda m: "fault address " + name(m.group(1), True), text)

    def args(m):
        return m.group(1) + re.sub(r"0x[0-9a-f]{6,}", lambda a: name(a.group(0)), m.group(2))
    return re.sub(r"(crashed on arguments:)([^\n]*)", args, text)


# ---------------------------------------------------------------- coverage lane
def _symbols(exe):
    out = []
    for line in run(["nm", "-n", "-S", exe]).splitlines():
        parts = line.split()
        if len(parts) == 4:
            out.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
    return out


def _addr_name(symbols, address):
    if 0x1F800000 <= address < 0x1F801000:
        return f"scratchpad+{address - 0x1F800000:#x}"
    if 0x80000000 <= address < 0x80008000:
        return f"low_ram+{address - 0x80000000:#x}"
    for start, size, name in symbols:
        if start <= address < start + size:
            return "arena" if name == "zdiff_arena" else f"{name}+{address - start:#x}"
    return f"{address:#x}"


def _line_of(exe, pcs):
    """ref.c line of each return address (the call's own instruction)."""
    if not pcs:
        return {}
    out = run(["addr2line", "-e", exe, *[f"{pc - 1:#x}" for pc in pcs]]).splitlines()
    lines = {}
    for pc, where in zip(pcs, out):
        m = re.search(r"(ref\.c|zdiff_standin\.c):(\d+)", where)
        lines[pc] = int(m.group(2)) if m and m.group(1) == "ref.c" else None
    return lines


def _parse_values(path):
    cmps, reads, args = [], [], []
    for line in path.read_text().splitlines() if path.exists() else []:
        f = line.split()
        if f[0] == "cmp":
            cmps.append({"pc": int(f[1], 16), "size": int(f[2]), "const": int(f[3]), "count": int(f[4]),
                         "eq": int(f[5]), "slt": int(f[6]), "sgt": int(f[7]), "ult": int(f[8]), "ugt": int(f[9]),
                         "amin": int(f[10]), "amax": int(f[11]), "bmin": int(f[12]), "bmax": int(f[13]),
                         "zero": int(f[14]), "neg": int(f[15]), "nzpos": int(f[16]), "nzneg": int(f[17]),
                         "gtpos": int(f[18]), "gtneg": int(f[19]), "gtmin": int(f[20]), "distinct": int(f[21]),
                         "values": [int(x) for x in f[22:]]})
        elif f[0] in ("read", "arg"):
            if f[0] == "read":
                head = {"pc": int(f[1], 16), "size": int(f[2]), "count": int(f[3]), "addr": int(f[4], 16),
                        "same": f[5] == "1"}
                rest = f[6:]
            else:
                head = {"index": int(f[1]), "pointer": f[2] == "1", "bits": int(f[3]), "count": int(f[4])}
                rest = f[5:]
            head.update(smin=int(rest[0]), smax=int(rest[1]), umin=int(rest[2]), umax=int(rest[3]),
                        zero=int(rest[4]), neg=int(rest[5]), smaxhit=int(rest[6]), umaxhit=int(rest[7]),
                        distinct=int(rest[8]), values=[int(x) for x in rest[9:]])
            (reads if f[0] == "read" else args).append(head)
    return cmps, reads, args


def _range_text(r, bits):
    parts = [f"min {r['smin']}", f"max {r['smax']}"]
    if r["umax"] != r["smax"] or r["umin"] != r["smin"]:
        parts.append(f"unsigned {r['umin']}..{r['umax']}")
    seen = [name for name, key in (("zero", "zero"), ("negative", "neg"), ("signed max", "smaxhit"),
                                   ("all ones", "umaxhit")) if r[key]]
    never = [name for name, key in (("zero", "zero"), ("negative", "neg")) if not r[key]]
    if seen:
        parts.append("seen " + "/".join(seen))
    if never:
        parts.append("never " + "/".join(never))
    if r["distinct"] <= 8:
        parts.append(f"{r['distinct']} value(s): {', '.join(str(v) for v in r['values'])}")
    return ", ".join(parts)


def coverage_run(build, exe, iterations, seed):
    """Run the coverage build; per function: branches (and conditions on GCC
    14+) with the uncovered ones and their lines, the arguments' ranges, the
    comparisons' and loads' value ranges, and the flags branch coverage
    cannot see (a boundary never equal, a nonzero test only ever seen with
    the sign bit set). Ids (`+LINE bK`, `+LINE cK`, `+LINE eq`, `+LINE sign`)
    are relative to the function's first line, so waivers survive edits to
    the rest of the unit."""
    covdir = exe.parent
    for stale in covdir.glob("*.gcda"):
        stale.unlink()
    values = covdir / "values.txt"
    values.unlink(missing_ok=True)
    result = subprocess.run([str(exe), str(iterations), str(seed)], capture_output=True, text=True, timeout=600,
                            env=dict(os.environ, ZDIFF_COV_OUT=str(values)))
    if result.returncode not in (0, 1):
        raise Failure(f"coverage run: exit {result.returncode}: {(result.stdout + result.stderr)[-800:]}")
    gcov = subprocess.run(["gcov", "-j", "-t", "-b", "-o", str(covdir), "ref.c"], cwd=covdir, capture_output=True, text=True)
    if gcov.returncode:
        raise Failure(f"gcov: {gcov.stderr[-800:]}")
    data = json.loads(gcov.stdout)
    orig = build.function + "__orig"
    ref_lines = (covdir / "ref.c").read_text(errors="replace").split("\n")
    fn = start = end = None
    lines = []
    for f in data["files"]:
        if not f["file"].endswith("ref.c"):
            continue
        for x in f["functions"]:
            if x["name"] == orig:
                fn, start, end = x, x["start_line"], x["end_line"]
        lines = f["lines"]
    if fn is None:
        raise Failure(f"gcov: no {orig}")
    report = {"function": build.function, "branches": 0, "branches_hit": 0, "conditions": None,
              "conditions_hit": None, "lines": 0, "lines_hit": 0, "uncovered": [], "flags": [], "args": [],
              "compares": [], "reads": [], "conditions_measured": condition_coverage()}

    def text_of(n):
        return ref_lines[n - 1].strip() if 0 < n <= len(ref_lines) else ""

    for line in lines:
        n = line["line_number"]
        if not (start <= n <= end) or line.get("function_name", orig) != orig:
            continue
        report["lines"] += 1
        report["lines_hit"] += line["count"] > 0
        for k, b in enumerate(line.get("branches", [])):
            report["branches"] += 1
            if b["count"]:
                report["branches_hit"] += 1
            else:
                report["uncovered"].append({"id": f"+{n - start} b{k}", "line": n - start, "text": text_of(n),
                                            "what": "branch never taken" + (" (not executed)" if not line["count"] else "")})
        for k, c in enumerate(line.get("conditions", [])):
            report["conditions"] = (report["conditions"] or 0) + c["count"]
            report["conditions_hit"] = (report["conditions_hit"] or 0) + c["covered"]
            for term in c.get("not_covered_true", []):
                report["uncovered"].append({"id": f"+{n - start} c{k}.{term}t", "line": n - start, "text": text_of(n),
                                            "what": f"condition {term} never true"})
            for term in c.get("not_covered_false", []):
                report["uncovered"].append({"id": f"+{n - start} c{k}.{term}f", "line": n - start, "text": text_of(n),
                                            "what": f"condition {term} never false"})
    symbols = _symbols(exe)
    low = next((a for a, size, name in symbols if name == orig), None)
    high = low + next(size for a, size, name in symbols if name == orig) if low is not None else None
    cmps, reads, args = _parse_values(values)
    cmps = [c for c in cmps if low is not None and low <= c["pc"] < high]
    reads = [r for r in reads if low is not None and low <= r["pc"] < high]
    where = _line_of(exe, sorted({x["pc"] for x in cmps + reads}))
    for a in sorted(args, key=lambda a: a["index"]):
        entry = {"arg": a["index"], "count": a["count"]}
        if a["pointer"]:
            entry["text"] = f"pointer: {'null seen' if a['zero'] else 'never null'}"
        else:
            entry["text"] = f"{a['bits']}-bit: " + _range_text(a, a["bits"])
        report["args"].append(entry)
    for c in sorted(cmps, key=lambda c: (where.get(c["pc"]) or 0, c["pc"])):
        n = where.get(c["pc"])
        rel = None if n is None else n - start
        text = text_of(n) if n else ""
        kind = {0: "a == b / a < b", 1: "against a constant", 2: "switch"}[c["const"]]
        outcomes = f"== {c['eq']}, < {c['slt']}, > {c['sgt']} (signed); < {c['ult']}, > {c['ugt']} (unsigned)"
        if c["const"] == 1:
            operand = f"constant {c['bmin']}"
        elif c["const"] == 0:
            operand = f"b {c['bmin']}..{c['bmax']}"
        else:
            operand = "cases"
        entry = {"line": rel, "text": text, "size": c["size"], "count": c["count"], "kind": kind,
                 "outcomes": outcomes, "operand": operand, "a": f"{c['amin']}..{c['amax']}"}
        report["compares"].append(entry)
        if rel is None or c["const"] == 2:
            continue
        ordered = (c["slt"] and c["sgt"]) or (c["ult"] and c["ugt"])
        rare = c["eq"] < RARE_EQUAL and c["eq"] * 100 < c["count"]
        if ordered and rare and not any(u["line"] == rel for u in report["uncovered"]):
            seen = "never occurred" if not c["eq"] else f"occurred only {c['eq']} time(s) in {c['count']}"
            report["flags"].append({"id": f"+{rel} eq", "line": rel, "text": text, "kind": "eq", "bits": c["size"] * 8,
                                    "equal": c["eq"], "count": c["count"],
                                    "what": f"boundary equality {seen} ({c['size'] * 8}-bit compare, {operand}: both "
                                            f"sides seen; a port's `<` for `<=` could pass)"})
        all_ones, sign_bit = (1 << (8 * c["size"])) - 1, 1 << (8 * c["size"] - 1)
        # Only -1 (a 1-bit signed field) or only the sign bit (a mask that
        # isolates it) above K: the test IS a sign test, not a gap.
        above = [x for x in c["values"] if x > c["bmin"]]
        sign_test = c["distinct"] <= 8 and above and all(x in (all_ones, sign_bit) for x in above)
        if c["const"] == 1 and c["bmin"] >= 0 and c["gtneg"] and not c["gtpos"] and not sign_test:
            report["flags"].append({"id": f"+{rel} sign", "line": rel, "text": text, "kind": "sign",
                                    "bits": c["size"] * 8, "constant": c["bmin"],
                                    "what": f"every value above {c['bmin']} had the sign bit set (a port testing the "
                                            f"sign, `< 0`, for this test would pass)"})
    for r in sorted(reads, key=lambda r: (where.get(r["pc"]) or 0, r["pc"])):
        n = where.get(r["pc"])
        name = _addr_name(symbols, r["addr"]) if r["same"] else f"{_addr_name(symbols, r['addr']).split('+')[0]}..."
        if name.startswith("__gcov"):
            continue
        report["reads"].append({"line": None if n is None else n - start, "text": text_of(n) if n else "",
                                "size": r["size"], "count": r["count"], "where": name,
                                "range": _range_text(r, r["size"] * 8), "distinct": r["distinct"]})
    return report


def coverage_hints(report, waivers=()):
    """What each open coverage gap asks of the spec, in plain words (for a
    spec writer's retry: the gate's lines name the gap, these say what to add)."""
    waived = {w["id"] for w in waivers}
    out = []
    for u in report["uncovered"]:
        if u["id"] in waived:
            continue
        if "(not executed)" in u["what"]:
            out.append(f"- `{u['text']}` (+{u['line']}) never ran: no input reaches this line; add inputs that get "
                       f"there (the tests before it must come out the other way).")
        else:
            out.append(f"- `{u['text']}` (+{u['line']}): one way out of this test ({u['id']}) was never taken; add "
                       f"inputs that take it.")
    for f in report["flags"]:
        if f["id"] in waived:
            continue
        if f.get("kind") == "sign":
            k, bits = f["constant"], f["bits"]
            examples = ", ".join(str(x) for x in dict.fromkeys([k + 1, max(k * 2 + 1, 0x100), (1 << (bits - 1)) - 1]))
            out.append(f"- `{f['text']}` (+{f['line']}): this compares a value with {k}. Every input where the value "
                       f"was above {k} also had its sign bit set (negative as a signed {bits}-bit number), so a port "
                       f"that tests the sign instead (`< 0`, or a signed read where the C reads unsigned) would pass "
                       f"your spec. Add inputs where the value is above {k} AND positive (e.g. {examples}).")
        elif f.get("kind") == "eq":
            out.append(f"- `{f['text']}` (+{f['line']}): the two sides of this ordered compare were equal "
                       f"{f['equal']} time(s) in {f['count']}. A port's `<` for `<=` would pass; add inputs that make "
                       f"them exactly equal at least {RARE_EQUAL} times and in at least 1% of the runs.")
    if out:
        out.append(f"Only a gap no input can reach (say why) may be waived instead: "
                   f"[[function.{report['function']}.coverage_waiver]] with its `id` and a `reason`.")
    return out


def coverage_verdict(report, threshold, waivers, values_gate=True):
    """(ok, lines): the gate. Waived ids count as covered; value flags gate
    too unless values_gate is False."""
    waived = {w["id"]: w.get("reason", "") for w in waivers}
    missing = [w for w in waived if not w.strip()]
    unwaived = [u for u in report["uncovered"] if u["id"] not in waived]
    flags = [f for f in report["flags"] if f["id"] not in waived]
    branches, hit = report["branches"], report["branches_hit"] + sum(
        1 for u in report["uncovered"] if u["id"] in waived and " b" in u["id"])
    percent = 100.0 if not branches else 100.0 * hit / branches
    lines = [f"coverage {report['function']}: branches {report['branches_hit']}/{branches}"
             + (f" ({percent:.1f}% with waivers)" if hit != report["branches_hit"] else f" ({percent:.1f}%)")
             + (f", conditions {report['conditions_hit']}/{report['conditions']}" if report["conditions"] is not None
                else ", conditions not measured (GCC < 14: branch coverage only)" if not report["conditions_measured"] else "")
             + f", lines {report['lines_hit']}/{report['lines']}"]
    for u in report["uncovered"]:
        tag = "waived" if u["id"] in waived else "UNCOVERED"
        lines.append(f"  {tag} {u['id']}: {u['what']}: `{u['text']}`"
                     + (f" ({waived[u['id']]})" if u["id"] in waived else ""))
    for f in report["flags"]:
        tag = "waived" if f["id"] in waived else "FLAG"
        lines.append(f"  {tag} {f['id']}: {f['what']}: `{f['text']}`" + (f" ({waived[f['id']]})" if f["id"] in waived else ""))
    for a in report["args"]:
        lines.append(f"  arg {a['arg']}: {a['text']}")
    for c in report["compares"]:
        lines.append(f"  compare +{c['line']} ({c['size'] * 8}-bit, {c['operand']}): {c['outcomes']}; a {c['a']}: `{c['text']}`")
    for r in report["reads"]:
        lines.append(f"  read +{r['line']} {r['size']} B at {r['where']}: {r['range']}: `{r['text']}`")
    stale = sorted(set(waived) - {u["id"] for u in report["uncovered"]} - {f["id"] for f in report["flags"]})
    ok = percent >= threshold and not [u for u in unwaived if " b" not in u["id"]] and not missing \
        and (not values_gate or not flags)
    if threshold >= 100:
        ok = ok and not unwaived
    if stale:
        lines.append(f"  note: waivers no longer needed: {', '.join(stale)}")
    if missing:
        lines.append(f"  FAIL: waivers without a reason: {', '.join(missing)}")
    lines.append(f"coverage {report['function']}: " + ("passed" if ok else
                 f"FAILED (threshold {threshold:g}%: {len(unwaived)} uncovered, {len(flags)} value flag(s) not waived)"))
    return ok, lines


def run_harness(exe, iterations, seed, build):
    result = subprocess.run([str(exe), str(iterations), str(seed)], capture_output=True, text=True, timeout=600)
    return result.returncode, build.name_address(exe, result.stdout + result.stderr)


def check_function(function, entry, args, table, out):
    work = out / function
    work.mkdir(parents=True, exist_ok=True)
    if getattr(args, "spec", None):
        entry = dict(entry, spec=str(pathlib.Path(args.spec).resolve()))
    build = Build(function, entry, args.build, work, table)
    iterations = args.iterations or entry.get("iterations", 20000)
    standin = getattr(args, "standin", False)
    ok = True
    exe, _ = build.compile(standin=standin)
    code, text = run_harness(exe, iterations, args.seed, build)
    print(text.rstrip())
    if code:
        print(f"FAIL: {function}: the {'C copy' if standin else 'port'} differs from the C (exit {code})")
        return False
    classes = re.search(r"classes:(.*)", text)
    if classes and args.self_check:
        empty = [c.split("=")[0] for c in classes.group(1).split() if c.endswith("=0")]
        if empty:
            print(f"FAIL: {function}: input classes never generated: {', '.join(empty)}")
            ok = False
    if getattr(args, "coverage", False):
        cov_exe, _ = build.compile(coverage=True, standin=standin)
        report = coverage_run(build, cov_exe, iterations, args.seed)
        passed, lines = coverage_verdict(report, args.coverage_threshold, entry.get("coverage_waiver", []),
                                         values_gate=args.coverage_values == "gate")
        print("\n".join(lines))
        if args.coverage_json is not None:
            args.coverage_json[function] = dict(report, passed=passed)
        ok = ok and passed
    if not args.self_check:
        return ok
    for mutation in entry.get("mutation", []):
        if standin and mutation.get("target") != "c":
            continue
        expect = mutation.get("expect", "mismatch")
        try:
            exe, _ = build.compile(mutation, standin=standin)
        except Failure as error:
            print(f"FAIL: {function}: mutation {mutation['name']}: {error}")
            ok = False
            continue
        code, text = run_harness(exe, iterations, args.seed, build)
        first = next((l for l in text.splitlines() if "MISMATCH" in l or "PANIC" in l or "CRASH" in l), "")
        caught = {"mismatch": code == 1 and "MISMATCH" in text, "panic": code == 3 and "PANIC" in text,
                  "crash": code == 4 and "CRASH" in text}[expect]
        print(("ok:   " if caught else "FAIL: ") + f"{function}: mutation {mutation['name']}: "
              + (f"caught: {first.removeprefix('zdiff: ')}" if caught else f"not caught as {expect} (exit {code}): {text[-400:]}"))
        ok = ok and caught
    build.compile(standin=standin)  # leave the clean build in place
    return ok


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--self-check", action="store_true", help="also require every mutation to be caught")
    parser.add_argument("--iterations", type=int)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "pc/build/zig-diff")
    parser.add_argument("--coverage", action="store_true",
                        help="also gate on the coverage lane: the original C at -O0 with gcov and value probes, on "
                             "the spec's inputs; every branch covered (or waived) and no value flag (or waived)")
    parser.add_argument("--coverage-threshold", type=float, default=100.0, metavar="PERCENT",
                        help="branch coverage the gate needs, waivers counted as covered (default 100)")
    parser.add_argument("--coverage-values", choices=("gate", "report"), default="gate",
                        help="value flags (boundary never equal, sign-only nonzero tests) fail the gate (default) "
                             "or are only reported")
    parser.add_argument("--coverage-json", type=pathlib.Path, metavar="FILE", help="write the coverage reports here")
    parser.add_argument("--spec", type=pathlib.Path, help="use this spec file (one FUNCTION) instead of specs.toml's")
    parser.add_argument("--standin", action="store_true",
                        help="the C itself as the port (checks a spec and its `target = \"c\"` mutations without a port)")
    args = parser.parse_args(argv)
    json_path, args.coverage_json = args.coverage_json, ({} if args.coverage_json else None)
    specs = tomllib.loads((SPECS / "specs.toml").read_text())["function"]
    functions = args.functions or sorted(specs)
    unknown = [f for f in functions if f not in specs]
    if unknown:
        print(f"zig_diff: no spec for {', '.join(unknown)} (pc/tests/zig_diff/specs.toml)")
        return 2
    if not (args.build / "imports.ld").exists():
        print(f"zig_diff: {args.build} has no 64-bit build (python3 pc/tools/build_boot.py --arch 64): skipped")
        return 77
    table = ownership.load()
    args.work.mkdir(parents=True, exist_ok=True)
    failed = []
    for function in functions:
        try:
            if not check_function(function, specs[function], args, table, args.work):
                failed.append(function)
        except Failure as error:
            print(f"FAIL: {function}: {error}")
            failed.append(function)
    if json_path:
        json_path.write_text(json.dumps(args.coverage_json, indent=1) + "\n")
    print("zig_diff: " + (f"FAILED: {', '.join(failed)}" if failed else f"{len(functions)} function(s) passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
