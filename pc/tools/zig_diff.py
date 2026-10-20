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
    values (`zdiff_fixed`); the PS1 scratchpad is mapped;
  - every external function is a recording stub generated from the `c`
    module's prototype (the spec may give one a behaviour: an arena block
    for an allocator); a static the C calls is a trap;
  - pc/tests/zig_diff/main.zig runs both on the spec's random and edge
    inputs and compares all of that state, the external call traces (with a
    hash of the state at each call) and the return values.
--self-check also applies each of the spec's mutations (wrong variants of
the port) and requires the harness to catch every one, and requires every
input class the spec declares to occur. Needs a 64-bit build
(build_boot.py --arch 64): it reads pc/build/boot64.
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


def gcc_permissive():
    version = subprocess.run(["gcc", "-dumpversion"], capture_output=True, text=True).stdout
    return ["-fpermissive"] if int(re.match(r"\d+", version).group()) >= 14 else []


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
    def __init__(self, function, entry, build_dir, work, table, port_source=None):
        """port_source: a candidate file to compile in place of the port
        (pc/tools/bakeoff_score.py); the checkout's file is never changed."""
        self.function, self.entry, self.build, self.work = function, entry, build_dir, work
        self.sub, zig_path = find_route(table, function)
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
        self.views = build_dir / "zig" / f"views_{self.stem}.zig"
        self.c = build_dir / "zig" / f"c_{self.stem}.zig"
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

    def reference(self):
        text = self.unit_i.read_bytes()
        keep = [self.function + "__orig"] + static_inline_names(text)
        ref_c = self.work / "ref.c"
        ref_c.write_bytes(ownership.strip_bodies(text, keep=keep))
        if (self.function + "__orig(").encode() not in ref_c.read_bytes():
            raise Failure(f"{self.unit_i.name}: no {self.function}__orig definition")
        ref_o = self.work / "ref.o"
        run(["gcc", *C_FLAGS, *gcc_permissive(), "-c", ref_c, "-o", ref_o])
        present = run(["objdump", "-h", ref_o])
        renames = []
        for section in DATA_SECTIONS:
            if re.search(rf"\s{re.escape(section)}\s", present):
                renames += ["--rename-section", f"{section}=zdiff_fixed"]
        if renames:
            run(["objcopy", *renames, ref_o])
        return ref_o

    def symbols(self, ref_o):
        undefined = set(run(["nm", "-u", ref_o]).split()) - {"U", "w", "_GLOBAL_OFFSET_TABLE_"}
        defined = {l.split()[-1] for l in run(["nm", "--defined-only", ref_o]).splitlines() if l.strip()}
        ctext = self.c.read_text()
        c_functions = set(re.findall(r"^pub extern fn (\w+)\(", ctext, re.M))
        statics = set(re.findall(r"^pub const (\w+) = @compileError", ctext, re.M))
        undefined -= HARNESS_DEFINED | LIBC
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
        if missing:
            raise Failure(f"no size for {', '.join(missing)} (neither DWARF, imports.ld nor native_imports.c)")
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

    def compile(self, mutation=None):
        src, port = self.sources(mutation)
        ref_o = self.reference()
        stubs, traps, data = self.symbols(ref_o)
        state_c, sizes = self.state_source(traps, data)
        spec = self.work / "spec.zig"
        shutil.copy(SPECS / self.entry["spec"], spec)
        for attempt in range(2):
            gen = self.work / "gen.zig"
            gen.write_text("//! Generated by pc/tools/zig_diff.py.\n"
                           f'pub const c = @import("c_{self.stem}");\n'
                           f'pub const port_fn = @import("port").{self.zig_fn};\n'
                           f'pub const orig_name = "{self.function}__orig";\n'
                           f'pub const function = "{self.function}";\n'
                           "pub const stubs = [_][]const u8{ " + ", ".join(f'"{s}"' for s in stubs) + " };\n")
            obj = self.work / "harness.o"
            views, cmod = f"views_{self.stem}", f"c_{self.stem}"
            modules = [
                ("root", HARNESS, ["zdiff", "gen", "spec", "common"]),
                ("zdiff", API, []),
                ("gen", gen, ["port", cmod]),
                ("spec", spec, ["zdiff", "common", views, cmod, "port"]),
                ("port", port, ["common", views, cmod]),
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
            exe = self.work / "zdiff"
            link = subprocess.run(["gcc", "-m64", "-no-pie", "-o", str(exe), str(obj), str(ref_o), str(state_c)],
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
        """Name the addresses in the harness output (symbol+offset)."""
        symbols = []
        for line in run(["nm", "-n", "-S", exe]).splitlines():
            parts = line.split()
            if len(parts) == 4:
                symbols.append((int(parts[0], 16), int(parts[1], 16), parts[3]))

        def name(m):
            address = int(m.group(1), 16)
            for start, size, symbol in symbols:
                if start <= address < start + size:
                    return f"{m.group(1)} [{symbol}+{address - start:#x}]"
            return m.group(1)
        return re.sub(r"memory at (0x[0-9a-f]+)", lambda m: "memory at " + name(m), text)


def run_harness(exe, iterations, seed, build):
    result = subprocess.run([str(exe), str(iterations), str(seed)], capture_output=True, text=True, timeout=600)
    return result.returncode, build.name_address(exe, result.stdout + result.stderr)


def check_function(function, entry, args, table, out):
    work = out / function
    work.mkdir(parents=True, exist_ok=True)
    build = Build(function, entry, args.build, work, table)
    iterations = args.iterations or entry.get("iterations", 20000)
    ok = True
    exe, _ = build.compile()
    code, text = run_harness(exe, iterations, args.seed, build)
    print(text.rstrip())
    if code:
        print(f"FAIL: {function}: the port differs from the C (exit {code})")
        return False
    classes = re.search(r"classes:(.*)", text)
    if classes and args.self_check:
        empty = [c.split("=")[0] for c in classes.group(1).split() if c.endswith("=0")]
        if empty:
            print(f"FAIL: {function}: input classes never generated: {', '.join(empty)}")
            ok = False
    if not args.self_check:
        return ok
    for mutation in entry.get("mutation", []):
        expect = mutation.get("expect", "mismatch")
        try:
            exe, _ = build.compile(mutation)
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
    build.compile()  # leave the clean build in place
    return ok


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--self-check", action="store_true", help="also require every mutation to be caught")
    parser.add_argument("--iterations", type=int)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    parser.add_argument("--work", type=pathlib.Path, default=ROOT / "pc/build/zig-diff")
    args = parser.parse_args(argv)
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
    print("zig_diff: " + (f"FAILED: {', '.join(failed)}" if failed else f"{len(functions)} function(s) passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
