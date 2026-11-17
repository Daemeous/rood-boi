#!/usr/bin/env python3
"""Replace Zig's raw offset accesses by named fields (memory lane S1, pc/docs/plans/2026-11-16-memory-lane.md).

    python3 pc/tools/zig_offsets.py scan                 count the sites by kind
    python3 pc/tools/zig_offsets.py named [--max N] [--files A,B]
        psx.fieldAt(T, &X.a.b, off).*  ->  X.<the scalar field of that struct off bytes on, as wide as T>
    python3 pc/tools/zig_offsets.py fix                  build, put back the lines the compiler rejects, until it builds
    python3 pc/tools/zig_offsets.py check                compare battle_zig.o with the build before (informational)

`named` finds the struct from the views' DWARF dumps (the chain's first field must be one the structs agree on),
walks nested structs and arrays to the field at the target byte, and replaces the access only when that field is
exactly T's size (a wider T reads several fields: left for a hand edit). The compiler's errors name the sites where
the resolution picked the wrong struct (the line is put back). Object code is not expected to be identical (typed
access drops the null checks of the pointer arithmetic): the proof is the behaviour gates.
"""
import argparse
import collections
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SAVED = ROOT / "pc/build/zig_offsets_saved.tsv"  # file, line, original text (to put a line back)


def zig_files():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "game"], capture_output=True, text=True, check=True).stdout
    return [f for f in out.splitlines() if f.endswith(".zig")]


def split_args(text, start):
    """text[start] is just after '('. -> (args [(begin, end)], index of the closing ')') or None."""
    depth, args, begin, i = 0, [], start, start
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            if c == "\\":
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append((begin, i))
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append((begin, i))
            begin = i + 1
        i += 1
    return None


CALL = re.compile(r"psx\.(fieldAt|byteAt)\(")


def sites(text):
    """Every psx.fieldAt/byteAt call: (start, end, kind, [arg strings])."""
    out = []
    for m in CALL.finditer(text):
        got = split_args(text, m.end())
        if got:
            args, close = got
            out.append((m.start(), close + 1, m.group(1), [text[a:b].strip() for a, b in args]))
    return out


def scan():
    kinds = collections.Counter()
    for f in zig_files():
        text = (ROOT / f).read_text()
        for start, end, kind, args in sites(text):
            if kind == "fieldAt" and len(args) == 3:
                zero = args[2] == "0"
                bits = "._bits_" in args[1] or args[1].startswith("&_bits_")
                kinds[("fieldAt", "zero" if zero else "offset", "bits" if bits else "field")] += 1
            else:
                kinds[(kind, "?", "")] += 1
    for k, n in sorted(kinds.items()):
        print(n, *k)


ZIG_SIZES = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4, "c_int": 4, "c_uint": 4, "u64": 8, "i64": 8}


class Layouts:
    """The views' DWARF dumps (the last build's): struct fields with byte offsets, sizes and nested types."""

    def __init__(self):
        import json
        self.types = {}
        for path in sorted((ROOT / "pc/build/boot64/zig").glob("views_*.json")):
            for name, t in json.load(open(path))["types"].items():
                self.types.setdefault(name, t)
        sys.path.insert(0, str(ROOT / "pc/tools"))
        import zig_views
        self.zig = zig_views.load_names()["fields"]  # (struct, DWARF field) -> the name the Zig view gives it
        self.owners = collections.defaultdict(list)  # field name -> [(struct, field)]
        for s, t in self.types.items():
            for f in t["fields"]:
                self.owners[f["name"]].append((s, f))

    def size(self, ty):
        k = ty["kind"]
        if k == "int":
            return ty["size"]
        if k == "named":
            return self.types.get(ty["name"], {}).get("size")
        if k == "array":
            e = self.size(ty["elem"])
            return None if e is None else e * ty["count"]
        if k == "ptr":
            return 8
        return None

    def path(self, struct, offset, size):
        """The scalar field of `struct` at byte `offset` of exactly `size` bytes: its path ('a.b[2].c'), or None."""
        t = self.types.get(struct)
        if not t:
            return None
        for f in t["fields"]:
            if f["bitsize"] or f["bitpos"] % 8:
                continue
            start = f["bitpos"] // 8
            fs = self.size(f["type"])
            if fs is None or not (start <= offset < start + max(fs, 1)):
                continue
            return self._into(self.zig.get((struct, f["name"]), f["name"]), f["type"], offset - start, size)
        return None

    def _into(self, name, ty, rel, size):
        k = ty["kind"]
        if k == "int":
            return name if rel == 0 and ty["size"] == size else None
        if k == "named":
            sub = self.path(ty["name"], rel, size)
            return None if sub is None else name + "." + sub
        if k == "array":
            es = self.size(ty["elem"])
            if not es:
                return None
            sub = self._into(name + "[%d]" % (rel // es), ty["elem"], rel % es, size) if ty["elem"]["kind"] != "named" \
                else self._into_named(name + "[%d]" % (rel // es), ty["elem"], rel % es, size)
            return sub
        return None

    def _into_named(self, name, ty, rel, size):
        sub = self.path(ty["name"], rel, size)
        return None if sub is None else name + "." + sub

    def resolve(self, comps, off, size):
        """comps: the chain's field names after the root variable (with [i] stripped); -> the replacement path or None.
        Every struct that has the first field must agree."""
        first = re.sub(r"\[.*", "", comps[0])
        results = set()
        for s, f in self.owners.get(first, []):
            base, ty, ok = f["bitpos"] // 8, f["type"], not f["bitsize"]
            for c in comps[1:]:
                nm = re.sub(r"\[.*", "", c)
                while ty["kind"] == "array":
                    es = self.size(ty["elem"])
                    ty = ty["elem"]
                    m = re.search(r"\[(\d+)\]", c)
                    base += (int(m.group(1)) if m else 0) * (es or 0)
                    break
                if ty["kind"] != "named":
                    ok = False
                    break
                nxt = [g for g in self.types.get(ty["name"], {}).get("fields", []) if g["name"] == nm]
                if len(nxt) != 1 or nxt[0]["bitsize"]:
                    ok = False
                    break
                base += nxt[0]["bitpos"] // 8
                ty = nxt[0]["type"]
            if not ok:
                results.add(None)  # an owner the chain does not fit: the first field's name is ambiguous
                continue
            if comps and re.search(r"\[(\d+)\]", comps[-1]) and ty["kind"] == "array":
                es = self.size(ty["elem"])
                base += int(re.search(r"\[(\d+)\]", comps[-1]).group(1)) * (es or 0)
            results.add(self.path(s, base + off, size))
        return results.pop() if len(results) == 1 and None not in results else None


def named(limit, only):
    """psx.fieldAt(T, &X.a.b, off): the scalar field of X's struct that off bytes after a.b, as wide as T, replaces it
    (the chain's struct is found by the first field's name; every struct that has it must agree)."""
    lay = Layouts()
    saved, done = [], 0
    for f in zig_files():
        if only and pathlib.Path(f).stem not in only:
            continue
        path = ROOT / f
        text = path.read_text()
        edits = []
        for start, end, kind, args in sites(text):
            if kind != "fieldAt" or len(args) != 3 or not args[1].startswith("&") or args[0] not in ZIG_SIZES:
                continue
            if "_bits_" in args[1] or not re.fullmatch(r"-?\d+", args[2]):
                continue
            m = re.fullmatch(r"&(\w+)((?:\.\w+(?:\[\d+\])?)+)", args[1])
            if not m:
                continue
            root, comps = m.group(1), m.group(2)[1:].split(".")
            new = lay.resolve(comps, int(args[2]), ZIG_SIZES[args[0]])
            if not new:
                continue
            new = root + "." + new
            tail = text[end:end + 2]
            edits.append((start, end + 2, new) if tail == ".*" else (start, end, "&" + new))
        if not edits:
            continue
        if limit and done + len(edits) > limit:
            edits = edits[:max(0, limit - done)]
            if not edits:
                break
        lines = text.split("\n")
        for start, end, _ in edits:
            line = text.count("\n", 0, start)
            saved.append((f, line + 1, lines[line]))
        for start, end, new in sorted(edits, reverse=True):
            text = text[:start] + new + text[end:]
        path.write_text(text)
        done += len(edits)
        if limit and done >= limit:
            break
    SAVED.parent.mkdir(parents=True, exist_ok=True)
    SAVED.write_text("".join(f"{f}\t{l}\t{t}\n" for f, l, t in saved))
    print(f"rewrote {done} sites")


ZIG_ERR = re.compile(r"^(game/\S+?\.zig):(\d+):(\d+): (?:error:|note: called inline here)", re.M)


def fix(rounds=200):
    original = collections.defaultdict(dict)
    for l in SAVED.read_text().split("\n"):
        if l:
            f, n, t = l.split("\t", 2)
            original[f][int(n)] = t
    left = []
    for n in range(rounds):
        proc = subprocess.run(["python3", "pc/tools/play_mode.py", "--", "python3", "pc/tools/build_boot.py", "--arch", "64"],
                              cwd=ROOT, capture_output=True, text=True)
        if proc.returncode == 0:
            print(f"round {n}: builds, {len(left)} lines put back", flush=True)
            break
        hits = sorted({(m.group(1), int(m.group(2))) for m in ZIG_ERR.finditer(proc.stdout + proc.stderr)})
        put = 0
        for f, line in hits:
            if f in original and line not in original[f] and original[f]:
                # the error is downstream of an edit (a type changed): put the whole file back
                p = ROOT / f
                lines = p.read_text().split("\n")
                for l2, txt in list(original[f].items()):
                    lines[l2 - 1] = txt
                    left.append((f, l2))
                    put += 1
                original[f].clear()
                p.write_text("\n".join(lines))
            elif line in original.get(f, {}):
                p = ROOT / f
                lines = p.read_text().split("\n")
                lines[line - 1] = original[f].pop(line)
                p.write_text("\n".join(lines))
                left.append((f, line))
                put += 1
        print(f"round {n}: {len(hits)} errors, {put} lines put back", flush=True)
        if not put:
            print((proc.stdout + proc.stderr)[-2000:])
            return 1
    pathlib.Path(ROOT / "pc/names/zig_offsets_left.tsv").write_text(
        "# lines the zero-offset pass put back (the field's type is not T): file\tline\n"
        + "".join(f"{f}\t{l}\n" for f, l in sorted(left)))
    return 0


def check(base_dir):
    """Compare battle_zig.o with the build before, function by function (anonymous constants' numbers left out)."""
    sys.path.insert(0, str(ROOT / "pc/tools"))
    import object_identity as oi
    norm = lambda lines: sorted(re.sub(r"__anon_\d+", "__anon", x) for x in lines)
    a = oi.instructions(pathlib.Path(base_dir) / "battle_zig.o", {})
    b = oi.instructions(ROOT / "pc/build/boot64/battle_zig.o", {})
    bad = {k for k in set(a) | set(b) if norm(a.get(k, [])) != norm(b.get(k, []))}
    changed = subprocess.run(["git", "-C", str(ROOT), "diff", "--name-only", "--", "game"], capture_output=True,
                             text=True, check=True).stdout.split()
    back = []
    for f in changed:
        stem = pathlib.Path(f).stem
        defined = set(re.findall(r"\bfn\s+(\w+)\s*\(", (ROOT / f).read_text())) | {stem}
        if any(k.split(".")[0] in defined for k in bad):
            back.append(f)
    print(f"{len(bad)} functions differ in code (not an error: typed access drops pointer checks); "
          f"{len(back)} of the {len(changed)} edited files define one of them")
    unexplained = [k for k in bad if not any(k.split(".")[0] in set(re.findall(r"\bfn\s+(\w+)\s*\(", (ROOT / f).read_text())) | {pathlib.Path(f).stem} for f in changed if (ROOT / f).exists())]
    if unexplained:
        print("differ without an edited file of their name:", unexplained[:10])
    return 0


# ------------------------------------------------------------------ the type oracle: the compiler says what X is
PROBES = ROOT / "pc/build/zig_offsets_probes.tsv"  # index, file, kind, T, root expression, offset, original text
LOG = re.compile(r'@as\(\*const \[\d+:0\]u8, "PROBE"\), @as\(comptime_int, (\d+)\), @as\(\*const \[\d+:0\]u8, "([^"]*)"\)')


def bits(limit=0):
    """psx.fieldAt(T, &X._bits_G, off) -> psx.bitsAt(T, &X._bits_G, off) (off a literal): the same access, bounds-checked
    against the group's bytes by the compiler. fix puts back the lines that read past the group."""
    saved, n = [], 0
    for f in zig_files():
        path = ROOT / f
        text = path.read_text()
        edits = []
        for start, end, kind, args in sites(text):
            if kind == "fieldAt" and len(args) == 3 and re.fullmatch(r"&[\w.()]*\._bits_\w+", args[1]) and re.fullmatch(r"\d+", args[2]):
                edits.append((start, start + len("psx.fieldAt")))
        if not edits:
            continue
        lines = text.split("\n")
        for s, e in edits:
            ln = text.count("\n", 0, s)
            saved.append((f, ln + 1, lines[ln]))
        for s, e in sorted(edits, reverse=True):
            text = text[:s] + "psx.bitsAt" + text[e:]
        # the offset argument is dropped into the 3-argument form
        text = re.sub(r"psx\.bitsAt\(([^,]+), (&[\w.()]*\._bits_\w+), (\d+)\)", r"psx.bitsAt(\1, \2, \3)", text)
        path.write_text(text)
        n += len(edits)
    SAVED.parent.mkdir(parents=True, exist_ok=True)
    SAVED.write_text("".join(f"{f}\t{l}\t{t}\n" for f, l, t in saved))
    print(f"rewrote {n} sites")


PAD = [
    # const x: *T = @ptrFromInt(pad.addr(0xN));  ->  pad.at(T, 0xN)
    (re.compile(r"(:\s*\*(?:const\s+)?)([\w.\[\]]+)(\s*=\s*)@ptrFromInt\(pad\.addr\((0x[0-9A-Fa-f]+)\)\)"),
     lambda m: m.group(1) + m.group(2) + m.group(3) + "pad.at(" + m.group(2) + ", " + m.group(4) + ")"),
    # x: [*]T = ...  ->  pad.many(T, 0xN)
    (re.compile(r"(:\s*\[\*\](?:const\s+)?(?:align\(\d+\)\s+)?)([\w.]+)(\s*=\s*)@ptrFromInt\(pad\.addr\((0x[0-9A-Fa-f]+)\)\)"),
     lambda m: m.group(1) + m.group(2) + m.group(3) + "pad.many(" + m.group(2) + ", " + m.group(4) + ")"),
    # @as([*]const T, @ptrFromInt(pad.addr(0xN)))  ->  pad.many(T, 0xN)
    (re.compile(r"@as\(\[\*\](?:const\s+)?(?:align\(\d+\)\s+)?([\w.]+), @ptrFromInt\(pad\.addr\((0x[0-9A-Fa-f]+)\)\)\)"),
     lambda m: "pad.many(" + m.group(1) + ", " + m.group(2) + ")"),
    # @as(*[3]i32, @ptrFromInt(...))  ->  pad.at([3]i32, 0xN)
    (re.compile(r"@as\(\*(?:const\s+)?(\[\d+\][\w.]+), @ptrFromInt\(pad\.addr\((0x[0-9A-Fa-f]+)\)\)\)"),
     lambda m: "pad.at(" + m.group(1) + ", " + m.group(2) + ")"),
    # @as(*const T, @ptrFromInt(pad.addr(0xN)))  ->  pad.at(T, 0xN)
    (re.compile(r"@as\(\*(?:const\s+)?([\w.]+), @ptrFromInt\(pad\.addr\((0x[0-9A-Fa-f]+)\)\)\)"),
     lambda m: "pad.at(" + m.group(1) + ", " + m.group(2) + ")"),
]


def pad_mode():
    saved, n = [], 0
    for f in zig_files():
        if f.endswith("game/common/pad.zig"):
            continue
        path = ROOT / f
        text = path.read_text()
        orig = text.split("\n")
        new = text
        for rx, fn in PAD:
            new = rx.sub(fn, new)
        if new != text:
            for i, (a, b) in enumerate(zip(orig, new.split("\n"))):
                if a != b:
                    saved.append((f, i + 1, a))
                    n += 1
            path.write_text(new)
    SAVED.parent.mkdir(parents=True, exist_ok=True)
    SAVED.write_text("".join(f"{f}\t{l}\t{t}\n" for f, l, t in saved))
    print(f"rewrote {n} lines")


def probe_edit(only):
    """Rewrite each fieldAt/byteAt call as a block that @compileLog's the type of its base expression and then
    does the call. The build then fails, once, listing the types; `probe-read` turns them into named fields."""
    rows, idx = [], 0
    for f in zig_files():
        if only and pathlib.Path(f).stem not in only:
            continue
        path = ROOT / f
        text = path.read_text()
        edits = []
        for start, end, kind, args in sites(text):
            if len(args) != 3 or args[0] not in ZIG_SIZES or not re.fullmatch(r"-?\d+|0x[0-9A-Fa-f]+", args[2]):
                continue
            if kind == "fieldAt":
                if not args[1].startswith("&") or "_bits_" in args[1]:
                    continue
                target = args[1][1:]
                m = re.fullmatch(r"(.+)\.(\w+)", target)
                base = m.group(1) if m else target  # the struct holding the field, or the variable itself
                member = m.group(2) if m else None
            else:
                base, member = args[1], None
            call = text[start:end]
            probe = f'(blk{idx}: {{ @compileLog("PROBE", {idx}, @typeName(@TypeOf({base}))); break :blk{idx} {call}; }})'
            rows.append((idx, f, kind, args[0], base, member or "", args[2]))
            edits.append((start, end, probe))
            idx += 1
        for start, end, new in sorted(edits, reverse=True):
            text = text[:start] + new + text[end:]
        if edits:
            path.write_text(text)
    PROBES.parent.mkdir(parents=True, exist_ok=True)
    PROBES.write_text("".join("\t".join(map(str, r)) + "\n" for r in rows))
    print(f"{len(rows)} probes in the tree: build, then probe-read")


def probe_apply(logfile):
    """Take the types the probe build printed and rewrite each site whose base type is a struct of the views."""
    subprocess.run(["git", "-C", str(ROOT), "checkout", "--", "game"], check=True)  # the probes out
    lay = Layouts()
    types = {int(m.group(1)): m.group(2) for m in LOG.finditer(pathlib.Path(logfile).read_text())}
    rows = [l.split("\t") for l in PROBES.read_text().split("\n") if l]
    byfile = collections.defaultdict(dict)
    for idx, f, kind, T, base, member, off in rows:
        byfile[f][int(idx)] = (kind, T, base, member, int(off, 0))
    done, saved = 0, []
    idx = 0
    for f in zig_files():
        if f not in byfile:
            continue
        path = ROOT / f
        text = path.read_text()
        edits = []
        for start, end, kind, args in sites(text):
            if len(args) != 3 or args[0] not in ZIG_SIZES or not re.fullmatch(r"-?\d+|0x[0-9A-Fa-f]+", args[2]):
                continue
            if kind == "fieldAt" and (not args[1].startswith("&") or "_bits_" in args[1]):
                continue
            mine = [i for i in byfile[f] if i == idx]
            cur = idx
            idx += 1
            if not mine:
                continue
            kind_, T, base, member, off = byfile[f][cur]
            ty = types.get(cur)
            if not ty or ty.startswith("?") or "[" in ty:
                continue
            name = ty.lstrip("*").replace("const ", "").split(".")[-1]
            if name not in lay.types:
                continue
            size = ZIG_SIZES[T]
            if kind_ == "fieldAt" and member:
                fields = [g for g in lay.types[name]["fields"] if lay.zig.get((name, g["name"]), g["name"]) == member
                          or g["name"] == member]
                if len(fields) != 1 or fields[0]["bitsize"]:
                    continue
                target = lay.path(name, fields[0]["bitpos"] // 8 + off, size)
            else:
                target = lay.path(name, off, size)
            if not target:
                continue
            new = base + "." + target
            tail = text[end:end + 2]
            edits.append((start, end + 2, new) if tail == ".*" else (start, end, "&" + new))
        lines = text.split("\n")
        for s, e, _ in edits:
            ln = text.count("\n", 0, s)
            saved.append((f, ln + 1, lines[ln]))
        for s, e, new in sorted(edits, reverse=True):
            text = text[:s] + new + text[e:]
        if edits:
            path.write_text(text)
        done += len(edits)
    SAVED.write_text("".join(f"{f}\t{l}\t{t}\n" for f, l, t in saved))
    print(f"rewrote {done} sites of {len(rows)} probes")
    return 0

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["scan", "named", "fix", "check", "probe", "probe-apply", "bits", "pad"])
    ap.add_argument("--max", type=int, default=0)
    ap.add_argument("--files")
    ap.add_argument("--log", help="probe-apply: the failed build's output")
    ap.add_argument("--base", default="/mnt/tomb-3/rood-boi/wip/t7-base/objs", help="the build before's objects (check)")
    a = ap.parse_args()
    if a.mode == "scan":
        scan()
    elif a.mode == "named":
        named(a.max, set(a.files.split(",")) if a.files else None)
    elif a.mode == "pad":
        pad_mode()
    elif a.mode == "bits":
        bits()
    elif a.mode == "probe":
        probe_edit(set(a.files.split(",")) if a.files else None)
    elif a.mode == "probe-apply":
        return probe_apply(a.log)
    elif a.mode == "check":
        return check(a.base)
    else:
        return fix()
    return 0


if __name__ == "__main__":
    sys.exit(main())
