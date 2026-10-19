#!/usr/bin/env python3
"""The battle inventory for the bulk port (pc/docs/porting/battle/):
FUNCTIONS.tsv and STATE.tsv, regenerated from an existing build tree.

Static analysis only: nothing is built or run. It reads, from a 64-bit build
tree made by `build_boot.py --arch 64` (BATTLE linked):
  - each unit's preprocessed text (battle_<unit>.i: ROOD_PC selected, macros
    and .inc files expanded), through clang's JSON AST: functions and their
    source ranges, calls, global accesses (read, written, address taken,
    through the pointer), bitfields, goto/switch, divisions, guest-address
    literals, local statics;
  - `nm` of every object (callers outside battle, who defines a callee, which
    pc/src file implements a PS1 assembly function), the reference ELFs
    (PS1 addresses and sizes), imports.ld (data linked to guest addresses),
    lp64_imports.json (imports moved to native regions on 64-bit), and
    function_tables.c;
  - the objects' DWARF through gdb (types and sizes; with --tree32, PS1 sizes
    from the 32-bit objects too).
Hand-written meanings come from pc/docs/porting/battle/MEANINGS.tsv.

    python3 pc/tools/porting_tables.py --tree pc/build/boot64 [--tree32 pc/build/boot] [-j 4]

Needs clang-18, gdb, binutils. Intermediate results go to <tree>/porting/
(cached per unit by the .i's modification time). See
pc/docs/porting/battle/README.md for the columns.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BATTLE_DIR = ROOT / "src/BATTLE/BATTLE.PRG"
OUT = ROOT / "pc/docs/porting/battle"

# BATTLE.PRG in guest RAM (config/BATTLE/BATTLE.PRG/splat.yaml): loaded at
# 0x80068800, data up to 0x89058, bss to 0x8D124, both inside the file.
BATTLE_LO, BATTLE_DATA_END, BATTLE_HI = 0x80068800, 0x80068800 + 0x89058, 0x80068800 + 0x8D124

# pc/src objects that implement BATTLE's PS1 assembly functions.
PC_IMPL = ["battle_gte", "battle_helpers", "battle_motion", "battle_movement", "actor_blend", "actor_cleanup",
           "actor_packets", "actor_prepare", "actor_shadow", "actor_update", "actor_vertices", "room_geometry",
           "room_render", "room_lighting", "navigation", "weapon_packets", "map_view", "effect_particles", "sdk"]
OTHER_UNITS = ["main", "init_12AC", "init_18", "init_FB0"]
PC_C_FILES = {u + ".c" for u in PC_IMPL} | {"boot_sdk.c"}

TRIAL = {"_updateEnemyKills": "trial: struct-heavy",
         "func_800B07DC": "trial: GTE + scratchpad + fixed point",
         "vs_battle_displaySceneMessage": "trial: ROOD_PC hook (charging skip)"}

CLANG_FLAGS = ["-target", "x86_64-linux-gnu", "-std=gnu17", "-funsigned-char", "-fsyntax-only", "-w",
               "-Wno-error=implicit-function-declaration", "-Wno-error=return-type", "-Wno-error=int-conversion",
               "-Wno-error=incompatible-pointer-types", "-Wno-error=implicit-int", "-Wno-error=int-to-pointer-cast",
               "-Wno-error=incompatible-function-pointer-types", "-Xclang", "-ast-dump=json"]

GTE_PRIMS = {"MTC2", "MTC2_S", "CTC2", "CTC2_S", "MFC2", "MFC2_S", "CFC2", "CFC2_S", "doCOP2"}
LIBGTE = {"RotTrans", "RotTransPers", "RotTransPers3", "RotTransPers4", "RotTransSV", "SetRotMatrix", "SetTransMatrix",
          "ApplyMatrix", "ApplyMatrixSV", "ApplyMatrixLV", "MulMatrix", "MulMatrix0", "MulMatrix2", "RotMatrix",
          "RotMatrixX", "RotMatrixY", "RotMatrixZ", "RotMatrixYXZ", "RotMatrixZYX", "RotMatrix_gte", "TransMatrix",
          "ScaleMatrix", "ScaleMatrixL", "CompMatrix", "CompMatrixLV", "SetGeomOffset", "SetGeomScreen", "rsin",
          "rcos", "ratan2", "csin", "ccos", "SquareRoot0", "SquareRoot12", "VectorNormal", "VectorNormalS",
          "VectorNormalSS", "OuterProduct0", "OuterProduct12", "Square0", "SetBackColor", "SetFarColor",
          "SetColorMatrix", "SetLightMatrix", "NormalColorCol", "RotAverage4", "RotAverageNclip4", "PushMatrix",
          "PopMatrix", "InitGeom", "ReadRotMatrix", "ReadGeomOffset", "ReadGeomScreen", "SetMulMatrix",
          "MatrixNormal", "RotNclip3", "RotAverage3", "RotNclip4", "NormalClip", "DpqColor", "LocalLight",
          "SetFogNear", "SetFogNearFar", "ApplyRotMatrix", "ApplyRotMatrixLV", "ApplyTransposeMatrixLV",
          "TransposeMatrix", "SetDQA", "SetDQB", "Lzc", "RotMatrixYXZ_gte"}

# ------------------------------------------------------------------ gdb half
# Run inside gdb: argv = [request.json, out.json]; types and sizes of names.
GDB_SCRIPT = r'''
import json, sys
import gdb
req = json.load(open(sys.argv[1]))
out = {}
def walk(t, test, depth=0):
    t = t.strip_typedefs()
    if depth > 6:
        return False
    if t.code == gdb.TYPE_CODE_ARRAY:
        return walk(t.target(), test, depth + 1)
    if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        if [f.name for f in t.fields()] == ["raw", "psx_type_"]:
            return test == "psx"
        return any((test == "bits" and f.bitsize) or walk(f.type, test, depth + 1) for f in t.fields() if f.type is not None)
    return test == "ptr" and t.code == gdb.TYPE_CODE_PTR
for name in req:
    try:
        sym = gdb.lookup_global_symbol(name) or gdb.lookup_static_symbol(name) or gdb.lookup_symbol(name)[0]
        if sym is None:
            continue
        t = sym.type
        info = {"type": str(t), "size": t.sizeof, "native_ptr": walk(t, "ptr"), "psx_ptr": walk(t, "psx"),
                "bitfields": walk(t, "bits")}
        out[name] = info
    except Exception as e:
        out[name] = {"error": str(e)}
json.dump(out, open(sys.argv[2], "w"))
'''


# ------------------------------------------------------------ the AST pass
def _strip(e):
    while e and e.get("kind") in ("ImplicitCastExpr", "ParenExpr", "CStyleCastExpr") and e.get("inner"):
        e = e["inner"][0]
    return e


def _loc(node, state):
    """clang's JSON gives file/line only when they change: track them."""
    def upd(loc):
        if not loc:
            return
        if "spellingLoc" in loc:
            upd(loc["spellingLoc"])
            return
        if "presumedFile" in loc:
            state["file"] = loc["presumedFile"]
        elif "file" in loc:
            state["file"] = loc["file"]
        if "presumedLine" in loc:
            state["line"] = loc["presumedLine"]
        elif "line" in loc:
            state["line"] = loc["line"]
    upd(node.get("loc"))
    rng = node.get("range", {})
    upd(rng.get("begin"))
    begin = (state.get("file"), state.get("line"))
    upd(rng.get("end"))
    return begin, (state.get("file"), state.get("line"))


def _track(node, state):
    for ch in node.get("inner", []) or []:
        if ch:
            _loc(ch, state)
            _track(ch, state)


class _Unit:
    def __init__(self):
        self.vars, self.fields, self.decls = {}, {}, {}

    def collect(self, node, scope, state):
        k = node.get("kind")
        if k == "FieldDecl":
            self.fields[node["id"]] = (node.get("name"), bool(node.get("isBitfield")))
        if k == "VarDecl":
            sc = node.get("storageClass")
            if scope is None or sc in ("extern", "static"):
                begin, _ = _loc(node, state)
                info = {"name": node.get("name"), "type": node.get("type", {}).get("qualType"),
                        "storage": sc or "", "scope": scope, "file": begin[0], "line": begin[1]}
                self.vars[node["id"]] = info
                self.decls.setdefault(info["name"], []).append(info)
        has_body = k == "FunctionDecl" and any(c.get("kind") == "CompoundStmt" for c in node.get("inner", []) or [])
        for ch in node.get("inner", []) or []:
            if ch is not None:
                self.collect(ch, node.get("name") if has_body else scope, state)

    def name(self, e):
        e = _strip(e)
        if e and e.get("kind") == "DeclRefExpr":
            rd = e.get("referencedDecl", {})
            if rd.get("kind") == "VarDecl" and rd.get("id") in self.vars:
                return self.vars[rd["id"]]["name"]
        return None

    def root(self, e, depth=0):
        """The global at the root of an lvalue or pointer expression."""
        e = _strip(e)
        if not e or depth > 12:
            return None
        k = e.get("kind")
        if k == "DeclRefExpr":
            return self.name(e)
        if k in ("MemberExpr", "ArraySubscriptExpr") or (k == "UnaryOperator" and e.get("opcode") == "*"):
            return self.root(e["inner"][0], depth + 1)
        if k == "BinaryOperator" and e.get("opcode") in ("+", "-"):
            return self.root(e["inner"][0], depth + 1)
        return None


def _add(f, name, mode, member=None):
    f["g"].setdefault(name, set()).add(mode)
    if member:
        f["members"].setdefault(name, set()).add(member)


def _lvalue(u, f, e, mode, member=None):
    while e and e.get("kind") == "ParenExpr":
        e = e["inner"][0]
    k = e.get("kind") if e else None
    if k == "DeclRefExpr":
        n = u.name(e)
        if n:
            _add(f, n, mode, member)
        return
    if k == "MemberExpr":
        base = e["inner"][0]
        if e.get("isArrow"):
            n = u.name(base) or u.root(base)
            _expr(u, f, base)
            if n:
                _add(f, n, mode + "*", e.get("name"))
            return
        _lvalue(u, f, base, mode, e.get("name"))
        return
    if k == "ArraySubscriptExpr":
        base, idx = e["inner"][0], e["inner"][1]
        _expr(u, f, idx)
        if base.get("kind") == "ImplicitCastExpr" and base.get("castKind") == "ArrayToPointerDecay":
            _lvalue(u, f, base["inner"][0], mode, member)
            return
        n = u.name(base) or u.root(base)
        _expr(u, f, base)
        if n:
            _add(f, n, mode + "*", member)
        return
    if k == "UnaryOperator" and e.get("opcode") == "*":
        inner = e["inner"][0]
        n = u.name(inner) or u.root(inner)
        _expr(u, f, inner)
        if n:
            _add(f, n, mode + "*", member)
        return
    if k in ("ImplicitCastExpr", "CStyleCastExpr") and e.get("inner"):
        _lvalue(u, f, e["inner"][0], mode, member)
        return
    if e:
        _expr(u, f, e)


def _expr(u, f, e):
    if e is None:
        return
    k = e.get("kind")
    if k == "DeclRefExpr":
        rd = e.get("referencedDecl", {})
        if rd.get("kind") == "VarDecl" and rd.get("id") in u.vars:
            _add(f, u.vars[rd["id"]]["name"], "r")
        elif rd.get("kind") == "FunctionDecl":
            f["fnaddr"].add(rd.get("name"))
        return
    if k == "ImplicitCastExpr" and e.get("castKind") == "ArrayToPointerDecay":
        n = u.name(e["inner"][0])
        if n:
            _add(f, n, "a")
            return
        inner = _strip(e["inner"][0])
        if inner and inner.get("kind") == "MemberExpr":
            _lvalue(u, f, inner, "a")
            return
    if k == "ImplicitCastExpr" and e.get("castKind") == "LValueToRValue":
        lv = e["inner"][0]
        while lv.get("kind") == "ParenExpr":
            lv = lv["inner"][0]
        if lv.get("kind") == "MemberExpr":
            fid = lv.get("referencedMemberDecl")
            if fid in u.fields and u.fields[fid][1]:
                f["bitfield"] += 1
            base = lv["inner"][0]
            if lv.get("isArrow"):
                n = u.name(base) or u.root(base)
                if n:
                    _expr(u, f, base)
                    _add(f, n, "r*", lv.get("name"))
                    return
            else:
                n = u.name(base) if _strip(base).get("kind") == "DeclRefExpr" else None
                if n:
                    _add(f, n, "r", lv.get("name"))
                    return
        if lv.get("kind") == "ArraySubscriptExpr":
            base, idx = lv["inner"][0], lv["inner"][1]
            if not (base.get("kind") == "ImplicitCastExpr" and base.get("castKind") == "ArrayToPointerDecay"):
                n = u.name(base)
                if n:
                    _add(f, n, "r")
                    _add(f, n, "r*")
                    _expr(u, f, idx)
                    return
        if lv.get("kind") == "UnaryOperator" and lv.get("opcode") == "*":
            n = u.name(lv["inner"][0])
            if n:
                _add(f, n, "r")
                _add(f, n, "r*")
                return
    if k == "ArraySubscriptExpr":
        base, idx = e["inner"][0], e["inner"][1]
        if base.get("kind") == "ImplicitCastExpr" and base.get("castKind") == "ArrayToPointerDecay":
            n = u.name(base["inner"][0])
            if n:
                _add(f, n, "r")
                _expr(u, f, idx)
                return
            inner = _strip(base["inner"][0])
            if inner and inner.get("kind") in ("MemberExpr", "ArraySubscriptExpr") and u.root(inner):
                _expr(u, f, inner)
                _expr(u, f, idx)
                return
        else:
            n = u.name(base) or u.root(base)
            if n:
                _add(f, n, "r*")
    if k == "MemberExpr" and not e.get("isArrow"):
        n = u.name(e["inner"][0])
        if n:
            _add(f, n, "r", e.get("name"))
            return
    if k == "CallExpr":
        callee = _strip(e["inner"][0])
        if callee and callee.get("kind") == "DeclRefExpr" and callee.get("referencedDecl", {}).get("kind") == "FunctionDecl":
            name = callee["referencedDecl"]["name"]
            f["calls"][name] = f["calls"].get(name, 0) + 1
            if name in GTE_PRIMS:
                f["gte"].add(name)
            if name in LIBGTE:
                f["libgte"].add(name)
        else:
            f["icalls"] += 1
            _expr(u, f, e["inner"][0])
        for a in e["inner"][1:]:
            _expr(u, f, a)
        return
    if k == "BinaryOperator":
        op = e.get("opcode")
        if op == "=":
            _lvalue(u, f, e["inner"][0], "w")
            _expr(u, f, e["inner"][1])
            return
        f["div"] += op in ("/", "%")
    if k == "CompoundAssignOperator":
        f["div"] += e.get("opcode") in ("/=", "%=")
        _lvalue(u, f, e["inner"][0], "rw")
        _expr(u, f, e["inner"][1])
        return
    if k == "UnaryOperator":
        if e.get("opcode") in ("++", "--"):
            _lvalue(u, f, e["inner"][0], "rw")
            return
        if e.get("opcode") == "&":
            _lvalue(u, f, e["inner"][0], "a")
            return
    if k in ("GCCAsmStmt", "MSAsmStmt"):
        f["asm"] += 1
    if k == "GotoStmt":
        f["goto"] += 1
    if k == "SwitchStmt":
        f["switch"] += 1
    if k == "FloatingLiteral":
        f["float"] += 1
    if k == "IntegerLiteral":
        v = int(e.get("value", "0") or 0) & 0xFFFFFFFF
        if 0x1F800000 <= v < 0x1F800400:
            f["guestlit"].add("scratchpad")
    if k == "CStyleCastExpr" and e.get("type", {}).get("qualType", "").endswith("*"):
        inner = _strip(e["inner"][0]) if e.get("inner") else None
        if inner and inner.get("kind") == "IntegerLiteral":
            v = int(inner.get("value", "0") or 0) & 0xFFFFFFFF
            if v == 0x80000000:
                f["guestlit"].add("lowram0x80000000")
            elif 0x1F800000 <= v < 0x1F800400:
                f["guestlit"].add("scratchpad")
            elif 0x80000000 < v < 0x80200000:
                f["guestlit"].add("guestaddr")
    if k == "VarDecl" and e.get("storageClass") == "static":
        f["lstatic"].append(e.get("name"))
    for ch in e.get("inner", []) or []:
        if ch is not None:
            _expr(u, f, ch)


def analyze_unit(i_path):
    r = subprocess.run(["clang-18", *CLANG_FLAGS, str(i_path)], capture_output=True)
    tu = json.loads(r.stdout)
    u = _Unit()
    u.collect(tu, None, {})
    funcs, state = {}, {}
    for top in tu.get("inner", []):
        begin, end = _loc(top, state)
        if top.get("kind") == "FunctionDecl" and any(c.get("kind") == "CompoundStmt" for c in top.get("inner", [])):
            f = {"file": begin[0], "line": begin[1], "end": end[1], "static": top.get("storageClass") == "static",
                 "variadic": top.get("variadic", False), "type": top.get("type", {}).get("qualType"),
                 "calls": {}, "icalls": 0, "fnaddr": set(), "g": {}, "members": {}, "asm": 0, "gte": set(),
                 "libgte": set(), "goto": 0, "switch": 0, "bitfield": 0, "guestlit": set(), "div": 0,
                 "lstatic": [], "float": 0}
            for ch in top.get("inner", []):
                if ch.get("kind") == "CompoundStmt":
                    _expr(u, f, ch)
            funcs[top["name"]] = f
        _track(top, state)
    return json.dumps({"functions": funcs, "vars": u.decls}, default=lambda o: sorted(o))


def _ast_job(paths):
    i_path, out = paths
    if not out.exists() or out.stat().st_mtime < i_path.stat().st_mtime:
        out.write_text(analyze_unit(i_path))


# ----------------------------------------------------------------- inputs
def query(args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def rood_pc_blocks():
    """file -> [(first line, last line, directive, first lines of the body)]"""
    out = collections.defaultdict(list)
    for f in sorted(BATTLE_DIR.glob("*.c")):
        lines, stack = f.read_text(errors="replace").splitlines(), []
        for i, l in enumerate(lines):
            s = l.strip()
            if re.match(r"#\s*(if|ifdef|ifndef)\b", s):
                stack.append((i, s, "ROOD_PC" in s))
            elif re.match(r"#\s*endif", s) and stack:
                start, directive, pc = stack.pop()
                if pc:
                    body = " | ".join(x.strip() for x in lines[start + 1:i][:4])
                    out[f.name].append((start + 1, i + 1, directive, body))
    return out


def is_const_object(ty):
    """The object itself is const (not merely a pointer to const)."""
    ty = ty or ""
    return "*const" in ty or (ty.startswith("const ") and "*" not in ty)


def short(names, n=6):
    names = sorted(names)
    if len(names) <= n:
        return ", ".join(names)
    return ", ".join(names[:n]) + f" (+{len(names) - n})"


def counted(names, n=6):
    return f"{len(names)}: {short(names, n)}" if names else "0"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", default="pc/build/boot64", help="64-bit build tree with BATTLE (default pc/build/boot64)")
    ap.add_argument("--tree32", default=None, help="optional 32-bit build tree for PS1 sizes from DWARF")
    ap.add_argument("--work", default=None, help="intermediate results (default <tree>/porting)")
    ap.add_argument("-j", type=int, default=4)
    a = ap.parse_args()
    tree = (ROOT / a.tree).resolve()
    tree32 = (ROOT / a.tree32).resolve() if a.tree32 else None
    work = pathlib.Path(a.work).resolve() if a.work else tree / "porting"
    (work / "units").mkdir(parents=True, exist_ok=True)
    (work / "types").mkdir(exist_ok=True)

    battle_units = ["battle_" + p.stem for p in sorted(BATTLE_DIR.glob("*.c"))]
    all_units = battle_units + OTHER_UNITS + PC_IMPL

    with concurrent.futures.ProcessPoolExecutor(a.j) as pool:
        list(pool.map(_ast_job, [(tree / (u + ".i"), work / "units" / (u + ".json")) for u in all_units]))
    units = {u: json.loads((work / "units" / (u + ".json")).read_text()) for u in all_units}

    objs = {}
    for o in sorted(tree.glob("*.o")):
        d, und = {}, []
        for line in query(["nm", str(o)]).splitlines():
            p = line.split()
            if len(p) == 2 and p[0] == "U":
                und.append(p[1])
            elif len(p) == 3:
                d[p[2]] = p[1]
        objs[o.stem] = {"def": d, "und": und}
    ref = {}
    for elf in ["battle-symbols.elf", "main-symbols.elf", "init-symbols.elf"]:
        for line in query(["readelf", "-sW", str(tree / elf)]).splitlines():
            p = line.split()
            if len(p) >= 8 and p[3] in ("OBJECT", "FUNC", "NOTYPE") and p[6] != "UND":
                ref.setdefault(p[7], {"addr": int(p[1], 16), "size": int(p[2], 0), "kind": p[3]})
    imports = {m.group(1): int(m.group(2), 16)
               for m in re.finditer(r"^(\w+) = (0x[0-9a-f]+);", (tree / "imports.ld").read_text(), re.M)}
    lp64 = json.loads((tree / "lp64_imports.json").read_text())
    table_refs = set(re.findall(r"\(void\*\)(\w+)", (tree / "function_tables.c").read_text()))
    pcblocks = rood_pc_blocks()

    gdb_script = work / "gdbtypes.py"
    gdb_script.write_text(GDB_SCRIPT)

    def types(job):
        u, obj, bits = job
        names = set(units[u]["vars"])
        for f in units[u]["functions"].values():
            names |= set(f["g"])
        req, out = work / "types" / f"{u}.req.json", work / "types" / f"{u}.{bits}.json"
        req.write_text(json.dumps(sorted(names)))
        if not out.exists() or out.stat().st_mtime < obj.stat().st_mtime:
            subprocess.run(["gdb", "-batch", "-nx", str(obj), "-ex",
                            f"python import sys; sys.argv = ['x', {str(req)!r}, {str(out)!r}]", "-x", str(gdb_script)],
                           capture_output=True)
        return json.loads(out.read_text()) if out.exists() else {}
    jobs = [(u, tree / (u + ".o"), "64") for u in units]
    if tree32:
        jobs += [(u, tree32 / (u + ".o"), "32") for u in units if (tree32 / (u + ".o")).exists()]
    types64, types32 = {}, {}
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        for (u, _, bits), result in zip(jobs, pool.map(types, jobs)):
            for k, v in result.items():
                (types64 if bits == "64" else types32).setdefault(k, v)

    # ---- in-scope functions
    funcs = {}
    for u in battle_units:
        for name, f in units[u]["functions"].items():
            fl = (f["file"] or "").lstrip("./")
            if not (fl.startswith("src/BATTLE/BATTLE.PRG/") or fl.endswith(".inc")):
                continue
            if "pc/build/" in fl:
                fl = "generated " + fl.split("/")[-1] + " (build_boot.py)"
            f = dict(f, unit=u, src=fl)
            key = name
            # a static copy of another unit's function ("inlined copy") keeps its own row
            if name in funcs or (f["static"] and any(name in units[o]["functions"]
                                                     and units[o]["functions"][name]["file"] != f["file"]
                                                     for o in battle_units if o != u)):
                key = name + "@" + fl.split("/")[-1]
            funcs.setdefault(key, f)

    defined_by = collections.defaultdict(list)
    for o, d in objs.items():
        for n, t in d["def"].items():
            if t in ("T", "t", "W"):
                defined_by[n].append(o)
    asm_funcs = {}
    for s in sorted(BATTLE_DIR.glob("*.c")):
        text = s.read_text(errors="replace")
        for m in re.finditer(r"glabel (\w+);", text):
            if m.group(1) not in funcs:
                body = text[m.end(): text.find("endlabel", m.end())]
                asm_funcs[m.group(1)] = {"file": f"src/BATTLE/BATTLE.PRG/{s.name} (global __asm__ under #ifndef ROOD_PC)",
                                         "insns": body.count(";")}
    for s in sorted(BATTLE_DIR.glob("*.s")):
        for b in re.split(r"^glabel ", s.read_text(errors="replace"), flags=re.M)[1:]:
            ins = [l for l in b.splitlines()[1:] if l.strip() and not l.strip().startswith(("alabel", "endlabel", ".", "glabel"))
                   and not re.match(r"\s*\w+:$", l)]
            asm_funcs[b.split()[0]] = {"file": f"src/BATTLE/BATTLE.PRG/{s.name}", "insns": len(ins)}
    battle_obj = set(battle_units)
    pc_impl = {}
    for name in asm_funcs:
        for o in defined_by.get(name, []):
            if o in units and name in units[o]["functions"] and name not in funcs:
                pc_impl[name] = dict(units[o]["functions"][name], unit=o,
                                     src=(units[o]["functions"][name]["file"] or "").lstrip("./"))
                break

    groups = [("main", "SLUS main.c"), ("sound_driver", "SLUS 25AC.c (sound driver)"), ("sdk", "SDK layer (pc/src/boot_sdk.c)"),
              ("gpu_P", "libgpu"), ("libgte", "libgte (PsyCross)"), ("gte", "GTE (PsyCross)"), ("inline", "GTE (PsyCross)"),
              ("mainmenu_", "MAINMENU overlay"), ("menu", "MENU overlays"), ("init_", "INITBTL overlay"),
              ("effect_dispatch", "EFFPURGE"), ("screff2_", "SCREFF2"), ("plg", "PLG effects"), ("ending_", "ENDING"),
              ("title", "TITLE"), ("battle_", "pc asm port"), ("actor_", "pc asm port"), ("weapon_packets", "pc asm port"),
              ("fixed_font", "pc asm port"), ("navigation", "pc asm port"), ("room_", "pc asm port/host"),
              ("traps", "trap"), ("ui_", "pc ui bridge"), ("runtime", "pc runtime")]

    def group_of(o):
        return next((g for p, g in groups if o == p or o.startswith(p)), "pc host (" + o + ")")

    battle_defined = set(funcs) | set(asm_funcs)

    def where(n):
        if n == "psx_addr":
            return "psx_types.h inline (psx.addr)"
        if n in battle_defined:
            return "battle"
        return group_of(defined_by[n][0]) if defined_by.get(n) else "unresolved (trap)"

    outside_refs = collections.defaultdict(set)
    for o, d in objs.items():
        if o not in battle_obj:
            for n in d["und"]:
                outside_refs[n].add(o)
    callers, fnaddr_by = collections.defaultdict(set), collections.defaultdict(set)
    for name, f in list(funcs.items()) + list(pc_impl.items()):
        for c in f["calls"]:
            callers[c].add(name)
        for x in f["fnaddr"]:
            fnaddr_by[x].add(name)
    other_callers = collections.defaultdict(set)
    for u in OTHER_UNITS:
        for name, f in units[u]["functions"].items():
            for c in f["calls"]:
                other_callers[c].add(f"{u}:{name}")
    overlay_groups = {"MAINMENU overlay", "MENU overlays", "EFFPURGE", "SCREFF2", "PLG effects", "INITBTL overlay"}

    def features(f):
        out = []
        if f["gte"]:
            out.append("GTE regs (" + ",".join(f["gte"]) + ")")
        if f["libgte"]:
            out.append("libgte (" + ",".join(f["libgte"][:6]) + ")")
        if f["asm"]:
            out.append(f"inline asm x{f['asm']}")
        if f["icalls"]:
            out.append(f"fn-pointer calls x{f['icalls']}")
        if f["fnaddr"]:
            out.append("takes fn address (" + short(f["fnaddr"], 4) + ")")
        if f["variadic"]:
            out.append("variadic")
        if f["guestlit"]:
            out.append("+".join(f["guestlit"]))
        for key, label in (("bitfield", "bitfields"), ("goto", "goto"), ("switch", "switch"), ("div", "div/mod"),
                           ("float", "float literals")):
            if f[key]:
                out.append(f"{label} x{f[key]}")
        if f["lstatic"]:
            out.append("local static (" + ",".join(f["lstatic"]) + ")")
        ov = sorted({where(c) for c in f["calls"]} & overlay_groups)
        if ov or "vs_overlay_slots" in f["g"]:
            out.append("overlays (" + ",".join(ov or ["slots"]) + ")")
        if set(f["calls"]) & {"vs_main_cdEnqueue", "vs_main_cdEnqueueUrgent", "_loadfile", "vs_main_allocHeap",
                              "vs_main_allocHeapR", "vs_main_freeHeap", "vs_main_freeHeapR"}:
            out.append("heap/CD")
        if set(f["calls"]) & {"AddPrim", "ClearOTag", "ClearOTagR", "DrawOTag", "SetDrawMode"} or "vs_scratch" in f["g"]:
            out.append("GPU packets/OT")
        return out

    def rood_pc(f):
        fname, out = f["src"].split("/")[-1], set()
        for first, last, directive, _ in pcblocks.get(fname, []):
            if f["line"] and first >= f["line"] and last <= f["end"]:
                out.add(f"{fname}:{first} {directive.split()[0][1:]}")
        if f["src"].endswith(".inc") or f["src"].startswith("generated"):
            out.add("in " + f["src"])
        out |= {"calls " + c for c in f["calls"] if c.startswith("pc_")}
        return sorted(out)

    def difficulty(f, lines, pcb):
        score = lines / 35 + 3 * bool(f["gte"]) + 1.5 * bool(f["libgte"]) + 4 * bool(f["asm"]) + min(f["icalls"], 4)
        score += 0.5 * bool(f["bitfield"]) + min(f["goto"], 3) + 0.7 * len(pcb) + 2 * bool(f["guestlit"])
        score += 1.5 * bool(f["lstatic"]) + bool(f["variadic"])
        return "S" if score < 1.5 else "M" if score < 4 else "L" if score < 9 else "XL"

    def callees(f, name):
        inside = sorted(c for c in f["calls"] if c in battle_defined and c != name)
        out = collections.defaultdict(list)
        for c in f["calls"]:
            if c not in battle_defined:
                out[where(c)].append(c)
        return counted(inside), "; ".join(f"{g}: {short(v)}" for g, v in sorted(out.items())) or "-"

    def addr(n):
        x = ref.get(n, {}).get("addr")
        return f"0x{x:08X}" if x else ""

    def esc(x):
        return str(x).replace("\t", " ").replace("\n", " ")

    rows = []
    for name, f in sorted(funcs.items(), key=lambda kv: (kv[1]["src"], kv[1]["line"] or 0)):
        lines = (f["end"] or 0) - (f["line"] or 0) + 1
        cout = {o if group_of(o).startswith("pc host") else f"{group_of(o)}:{o}" for o in outside_refs.get(name, ())}
        cout |= other_callers.get(name, set())
        if name in table_refs:
            cout.add("function_tables.c (guest fn table)")
        if fnaddr_by.get(name, set()) - {name}:
            cout.add("address taken by " + short(fnaddr_by[name] - {name}, 3))
        cin, cout_ = callees(f, name)
        pcb = rood_pc(f)
        kind = "pc-inc (host C compiled into the unit; routable only once ownership.py also renames in the .inc)" \
            if f["src"].endswith(".inc") else \
            "generated C compiled into the unit (static wrappers: not routable)" if f["src"].startswith("generated") else "c"
        for first, last, directive, body in pcblocks.get(f["src"].split("/")[-1], []):
            if directive.startswith("#ifndef ROOD_PC") and f["line"] and first <= f["line"] <= last \
                    and ("INCLUDE_ASM" in body or "__asm__" in body):
                kind = "c (ROOD_PC replacement of asm)"
        rows.append([name, addr(name), f["src"], lines, kind, "static" if f["static"] else "",
                     counted(callers[name] - {name}), counted(cout, 4), cin, cout_, counted(f["g"]),
                     "; ".join(features(f)) or "-", "; ".join(pcb) or "-", difficulty(f, lines, pcb), TRIAL.get(name, "")])
    for name, info in sorted(asm_funcs.items(), key=lambda kv: (kv[1]["file"], ref.get(kv[0], {}).get("addr", 0))):
        if name in funcs:
            continue
        impl = defined_by.get(name, [])
        cout = set(outside_refs.get(name, ())) - set(impl)
        if name in table_refs:
            cout.add("function_tables.c (guest fn table)")
        pf = pc_impl.get(name)
        if pf:
            lines = (pf["end"] or 0) - (pf["line"] or 0) + 1
            cin, cout_ = callees(pf, name)
            rows.append([name, addr(name), info["file"], lines, f"D2 (pc/src asm port): asm on the PS1; PC oracle is {pf['src']}:{pf['line']}", "",
                         counted(callers[name]), counted(cout, 4), cin, cout_, counted(pf["g"]),
                         "; ".join(["PS1 original is MIPS assembly"] + features(pf)), "port-written C in " + pf["src"],
                         difficulty(pf, lines, []), TRIAL.get(name, "")])
        else:
            implstr = ",".join(impl) if impl else "none: calls trap (pc_unimplemented)"
            n = info["insns"]
            rows.append([name, addr(name), info["file"], n, "D2: asm on the PS1; PC runs " + implstr, "", counted(callers[name]),
                         counted(cout, 4), "see asm", "see asm", "see asm", "MIPS assembly (no C anywhere)",
                         "PC: " + implstr, "M" if n < 60 else "L" if n < 250 else "XL", ""])
    main_funcs = units["main"]["functions"]
    battle_only_slus = set()
    for name in sorted({c for f in funcs.values() for c in f["calls"]} & set(main_funcs)):
        f = main_funcs[name]
        if not (f["file"] or "").lstrip("./").startswith("src/SLUS_010.40/"):
            continue
        others = {o for o in outside_refs.get(name, ()) if o != "main"}
        others |= {"main.c:" + c.split(":", 1)[1] for c in other_callers.get(name, ()) if c.startswith("main:")} - {"main.c:" + name}
        if not others:
            battle_only_slus.add(name)
        kind = "SLUS boundary: stays C, called via c.*" + ("" if others else "; only battle calls it (candidate to port with battle)")
        rows.append([name, addr(name), f["file"].lstrip("./"), (f["end"] or 0) - (f["line"] or 0) + 1, kind,
                     "static" if f["static"] else "", counted(callers[name]), counted(others, 4), "-",
                     short(f["calls"]) or "-", counted(f["g"]), "; ".join(features(dict(f, src=f["file"]))) or "-", "-", "-", ""])
    head = ["function", "address", "file", "lines", "kind", "static", "callers_in_battle", "callers_outside",
            "callees_in_battle", "callees_outside", "globals", "ps1_features", "rood_pc", "difficulty", "trial"]
    (OUT / "FUNCTIONS.tsv").write_text("\n".join("\t".join(esc(x) for x in r) for r in [head] + rows) + "\n")

    # ---- state
    access = collections.defaultdict(lambda: collections.defaultdict(set))
    members, files = collections.defaultdict(set), collections.defaultdict(set)

    def take(fname, f, src):
        unit_vars = units[f["unit"]]["vars"] if f.get("unit") in units else {}

        def key(g):
            if any(i["scope"] == fname.split("@")[0] and i["storage"] == "static" for i in unit_vars.get(g, [])):
                return fname.split("@")[0] + "::" + g
            return g
        for g, modes in f["g"].items():
            for m in modes:
                access[key(g)][m].add(fname)
            files[key(g)].add(src.split("/")[-1])
        for g, ms in f["members"].items():
            members[key(g)] |= set(ms)
    for name, f in funcs.items():
        take(name, f, f["src"])
    for name, f in pc_impl.items():
        take(name, f, f["src"])
    for name in battle_only_slus:
        take(name, dict(main_funcs[name], unit="main"), "main.c")
    decls = collections.defaultdict(list)
    for u, d in units.items():
        for n, infos in d["vars"].items():
            decls[n] += [dict(i, unit=u) for i in infos]
    local_syms = collections.defaultdict(list)  # (object, base name) -> GCC-suffixed local symbols
    for o, d in objs.items():
        for n, t in d["def"].items():
            m = re.fullmatch(r"(\w+)\.(\d+)", n)
            if m and t in "bdrBDR":
                local_syms[(o, m.group(1))].append(n)
    moved_region = {}  # name -> (rood_lp64 region, offset)
    for m in re.finditer(r"\.set (\w+), (rood_lp64_\w+) \+ (\d+)", (tree / "native_imports.c").read_text()):
        moved_region[m.group(1)] = (m.group(2), int(m.group(3)))
    region_names = collections.defaultdict(list)
    for n, (reg, off) in moved_region.items():
        region_names[reg].append(n)
    data_def = {}
    for o, d in objs.items():
        for n, t in d["def"].items():
            if t in "DdBbRrCGgSsV":
                data_def.setdefault(n, (o, t))

    def region(x):
        if x is None:
            return "host"
        if 0x1F800000 <= x < 0x1F800400:
            return "scratchpad"
        if 0x80010000 <= x < BATTLE_LO:
            return "SLUS"
        if BATTLE_LO <= x < BATTLE_DATA_END:
            return "BATTLE data"
        if BATTLE_DATA_END <= x < BATTLE_HI:
            return "BATTLE bss"
        return "MAINMENU/EFFECT slot" if 0x800F9800 <= x < 0x80102800 else "MENU slot/heap"

    def address_of(n):
        if n in ref and ref[n]["kind"] != "FUNC":
            return ref[n]["addr"]
        if n in imports:
            return imports[n]
        if n in lp64:
            return lp64[n]["address"]
        m = re.fullmatch(r"D_(80[0-9A-F]{6}|1F80[0-9A-F]{4})", n)
        return int(m.group(1), 16) if m else None

    meanings = {}
    for line in (OUT / "MEANINGS.tsv").read_text().splitlines()[1:]:
        p = line.split("\t")
        if len(p) == 3:
            meanings[p[0]] = (p[1], p[2])

    state = []
    for g in sorted(access):
        base = g.split("::")[-1]
        if g in funcs or base in ("__func__", "__FUNCTION__"):
            continue
        infos = decls.get(base, [])
        if "::" in g:
            infos = [i for i in infos if i["scope"] == g.split("::")[0] and i["storage"] == "static"]
        local = {i["scope"] for i in infos if i["scope"] and i["storage"] == "static"}
        x = address_of(base)
        if local:
            unit = next((i["unit"] for i in infos if i["scope"] in local), None)
            syms = sorted(local_syms.get((unit, base), []))
            kind = "function-local static in " + ",".join(sorted(local)) + (
                f" (object symbol {' or '.join(syms)} in {unit}.o; the .N suffix is GCC's and not stable)" if syms
                else " (no symbol: GCC folded it into code; port it as a Zig const value)")
            x = x if re.fullmatch(r"D_80[0-9A-F]{6}", base) else None
        elif base in imports:
            kind = "data import: guest RAM at its PS1 address (both builds)"
        elif lp64.get(base, {}).get("moved"):
            kind = "data import: guest RAM on 32-bit; native rood_lp64_* region on 64-bit (pointer words 8 bytes)"
        elif base in data_def:
            o, t = data_def[base]
            if o in battle_obj:
                kind = "BATTLE C data (" + ("const" if t in "rR" else "rood_ovl_battle section, restored per BATTLE.PRG load") + ")"
            elif o == "main":
                kind = "SLUS C const (main.c)" if t in "rR" else "SLUS C data (main.c; rood_state section)"
            elif o in PC_IMPL or o.startswith(("battle_", "actor_", "room_")):
                kind = f"pc asm-port C data ({o})"
            else:
                kind = f"host C ({o})"
            if t in "bdr":
                kind += "; file static (local symbol)"
        else:
            kind = "unresolved (declared only)"
        t64, t32 = types64.get(base, {}), types32.get(base, {})
        ty = t64.get("type") or (infos[0]["type"] if infos else "?")
        s32 = ref[base]["size"] if base in ref and ref[base]["kind"] != "FUNC" and ref[base]["size"] else t32.get("size")
        if s32 is None and base in lp64:
            s32 = max([d.get("ps1_size") or 0 for d in lp64[base]["declarations"]] or [0]) or None
        modes = access[g]
        if is_const_object(ty) and "static" in kind and "folded" not in kind:
            kind += "; const: in .rodata, not restarted per load and not compared state"
        state.append({"symbol": g, "x": x, "region": region(x), "type": ty, "s32": s32, "s64": t64.get("size"),
                      "kind": kind, "defined": ",".join(sorted({i["unit"] for i in infos if i["storage"] != "extern"
                                                                 and i["scope"] is None})),
                      "files": sorted(files[g]), "r": modes["r"] | modes["a"], "w": modes["w"] | modes["rw"],
                      "pr": modes["r*"], "pw": modes["w*"] | modes["rw*"], "members": sorted(members[g]),
                      "native_ptr": t64.get("native_ptr"), "psx_ptr": t64.get("psx_ptr"),
                      "moved": bool(lp64.get(base, {}).get("moved")),
                      "decl_types": sorted({d["type"] for d in lp64.get(base, {}).get("declarations", [])})})

    extents = [(r["x"], r["x"] + r["s32"], r["symbol"]) for r in state if r["x"] is not None and r["s32"]]
    alias = {}
    for r in state:
        if r["x"] is None:
            continue
        for lo, hi, s in extents:
            if s != r["symbol"] and lo <= r["x"] < hi and not (lo == r["x"] and hi - lo <= (r["s32"] or 0)):
                alias[r["symbol"]] = f"inside {s}+0x{r['x'] - lo:X}"
                break
    pc_objs = set(PC_IMPL) | battle_obj
    outside = collections.defaultdict(set)
    for name, info in lp64.items():
        outside[name] |= {u for u in info.get("users", []) if u and u != "battle"}
    for o, d in objs.items():
        if o not in pc_objs and not o.startswith(("battle_", "actor_", "room_")):
            for n in d["und"]:
                outside[n.removesuffix("__battle")].add(o)

    gpu = re.compile(r"POLY_|LINE_|SPRT|TILE|DR_|P_TAG|DRAWENV|DISPENV|RECT\b|u_long \*|P_CODE")
    cd = re.compile(r"CdQueueSlot|cdQueueSlot|_loadFileContext|Lba|lba|CdlFILE")

    def ps1_specific(r):
        why, t, s = [], r["type"] or "", r["symbol"]
        if r["region"] == "scratchpad":
            why.append("scratchpad memory")
        if "(*" in t:
            why.append("function pointers (guest code addresses on the PS1; native tables on the PC)")
        elif r["native_ptr"] or r["psx_ptr"] or "*" in t:
            why.append("holds guest pointers (4-byte PS1 addresses" + ("; 8-byte native slots on 64-bit" if r["moved"] else "") + ")")
        if gpu.search(t) or re.search(r"frameBuf|clut|Clut|vram|Vram|scratch|Otag|primBuf", s):
            why.append("GPU packets/VRAM/OT")
        if cd.search(t) or cd.search(s) or "cdQueueSlot" in r["members"]:
            why.append("CD streaming/disc LBAs")
        if s in alias:
            why.append("splat label " + alias[s])
        return "yes: " + "; ".join(why) if why else "no"

    def lifetime(r):
        k = r["kind"]
        if r["symbol"].startswith("pc_") and k.startswith("host C"):
            return "host (session)"
        if ("static" in k) and is_const_object(r["type"]):
            return "static (const)"
        if r["region"] == "scratchpad":
            return "per-frame (scratchpad)"
        if r["region"] in ("BATTLE data", "BATTLE bss") or "BATTLE C data" in k or (
                "function-local static" in k and not any(f.endswith(".inc") for f in r["files"])):
            return "per-overlay-load"
        if "pc asm-port" in k:
            return "static (host statics of a pc/src asm port)"
        return "static"

    def plan(r):
        k, s, base = r["kind"], r["symbol"], r["symbol"].split("::")[-1]
        now = f"D: v.g.{base}() (psx.global)"
        if "function-local static" in k:
            now = f"D: stays C; build globalises the GCC-suffixed local ({base}.N), then psx.global"
        elif "file static" in k:
            now = "D: stays C; build globalises the local symbol, then psx.global"
        elif "(*" in (r["type"] or ""):
            now += "; call through the table (entries are trampolines)"
        elif r["moved"]:
            now += "; 64-bit: native region, views from DWARF"
        if s in alias:
            now += f"; alias ({alias[s]}): keep the C's spelling"
        if base in moved_region:
            reg, off = moved_region[base]
            others = sorted(n for n in region_names[reg] if n != base)
            now += f"; 64-bit: in {reg} at +{off}" + (
                f" with {short(others, 4)}: never write a pointer word through a 4-byte integer alias" if others else "")
        if r["files"] and all(f in PC_C_FILES for f in r["files"]):
            now = "D2 only (used by pc/src asm ports): " + now
        if len(r["decl_types"]) > 1:
            now += "; units declare it differently (" + " / ".join(r["decl_types"][:3]) + "): each unit's port uses its own view"
        shared, why = outside.get(base, set()), ps1_specific(r)
        if r["region"] == "scratchpad" or "GPU packets" in why:
            later = "E: drop with the ordering-table/packet path (native renderer)"
        elif "CD streaming" in why:
            later = "E: drop (asset loading through the host)"
        elif s.startswith("pc_") or "host C" in k:
            later = "E: settings.zig / host API"
        elif "(*" in (r["type"] or ""):
            later = "E: Zig switch or comptime table of owned functions"
        elif r["moved"]:
            later = "E: native battle state; until then it stays in its rood_lp64 region (64-bit) / at its guest address (32-bit)"
        elif shared:
            later = "E: stays at its guest address while still-C code uses it (" + ", ".join(sorted(shared))[:80] + ")"
        elif r["region"] == "SLUS":
            later = "E: shared game state (save data / main.c): moves with its owner subsystem"
        else:
            later = "E: field of the native battle state"
        return now + " | " + later

    def lst(xs, n=6):
        return counted(xs, n) if xs else "-"

    head = ["symbol", "address", "type", "size_ps1", "size_64", "storage", "defined_in", "used_in_files", "readers",
            "writers", "through_pointer", "meaning", "lifetime", "ps1_specific", "zig_plan"]
    out = []
    for r in sorted(state, key=lambda r: (-len(r["r"] | r["w"] | r["pr"] | r["pw"]), r["symbol"])):
        s, base = r["symbol"], r["symbol"].split("::")[-1]
        meaning, value = meanings.get(base, (None, None))
        if not meaning:
            hints = []
            named = [m for m in r["members"] if not re.fullmatch(r"unk\w*|pad\d*", m)]
            if not re.fullmatch(r"D_[0-9A-F]{8}", base):
                hints.append("named " + base)
            if named:
                hints.append("fields " + ", ".join(named[:6]))
            users = sorted(f for f in r["r"] | r["w"] if not f.startswith("func_"))
            if users:
                hints.append("used by " + ", ".join(users[:4]))
            meaning = "unreviewed: " + "; ".join(hints) if hints else "unknown (unreviewed)"
        unsized = (r["type"] or "").endswith("[]")

        def size(v):
            return "unsized" if unsized else "-" if v is None else str(v)
        through = "; ".join(x for x in [("R " + lst(r["pr"], 4)) if r["pr"] else "",
                                         ("W " + lst(r["pw"], 4)) if r["pw"] else ""] if x) or "-"
        defined = r["defined"] or (
            f"guest RAM ({r['region']})" if r["kind"].startswith("data import: guest RAM at") else
            f"native_imports.c {moved_region.get(base, ('rood_lp64', 0))[0]} (64-bit); guest RAM ({r['region']}) on 32-bit"
            if r["kind"].startswith("data import") else "-")
        out.append([s, f"0x{r['x']:08X}" if r["x"] is not None else "-", r["type"], size(r["s32"]), size(r["s64"]),
                    r["kind"], defined, ", ".join(r["files"]), lst(r["r"]), lst(r["w"]), through, meaning,
                    lifetime(r) + (f"; value {value}" if value else ""), ps1_specific(r), plan(r)])
    (OUT / "STATE.tsv").write_text("\n".join("\t".join(esc(x) for x in r) for r in [head] + out) + "\n")
    print(f"FUNCTIONS.tsv: {len(rows)} rows; STATE.tsv: {len(out)} rows ({sum(1 for r in out if not r[11].startswith(('unreviewed', 'unknown')))} with a meaning)")


if __name__ == "__main__":
    sys.exit(main())
