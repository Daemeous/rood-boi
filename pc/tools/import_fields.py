#!/usr/bin/env python3
"""Import the struct field names VS-NativePort gave the decomp's types (CC0, its Docs/ledgers).

    python3 pc/tools/import_fields.py LEDGER_DIR                    plan: write the reports, change nothing else
    python3 pc/tools/import_fields.py LEDGER_DIR --apply [--max N] [--confidence high] [--structs A,B]

LEDGER_DIR holds TYPE_LEDGER.csv and FIELD_LEDGER.csv (git show origin/imp/01-ma04-gates:Docs/ledgers/X.csv).
A field is renamed only when all of this holds:
  - the struct is one of ours by name (typedef or tag, defined once or identically everywhere in pc/src, game/),
    and its members, read from our headers, are the ledger's in name, order, count and (per member) type class:
    the decomp is the common source, so the same names in the same order with the same types are the same
    offsets and sizes (a struct we changed, or one named differently, does not match and is skipped with the reason);
  - the ledger row's confidence is one the run applies (default high, as the function and data names were);
  - the old name is a member of no other struct of ours (a rename is then a rename of every `.old` and `->old`),
    is not a bitfield, and the new name is a plain identifier that is no member of any struct of ours, no macro,
    no C or Zig keyword and no other member of the struct.
The new name is the ledger's final_name (camelCase already).

Writes (under pc/names/):
    fields.tsv          applied: struct, old, new, confidence, ledger field id (the RPC's JSON keys, the debug names of the
                        Zig views; tools that compare two builds map new -> old through it)
    fields_skipped.tsv  not applied, with the reason (rewritten each run)
--apply rewrites the member's declaration in pc/src and game headers, `.old` / `->old` / `F(..., old)` member uses in
the C and Zig sources of pc/ and game/, and the Type.field keys of pc/docs/porting/battle/MEANINGS.tsv; the same
rename everywhere, so the C structs and the Zig views generated from them agree. What a regex cannot see (a name
inside a macro argument that is not F's, a string) is left to the compiler: build, and the errors name the places.
"""
import argparse
import collections
import csv
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NAMES = ROOT / "pc/names"
SOURCE_TREES = ("pc/src", "game")
USE_TREES = ("pc", "game")
USE_SUFFIXES = (".c", ".h", ".inc", ".zig", ".cpp", ".hpp")
USE_SKIP = ("pc/docs/", "pc/names/", "pc/assets/", "pc/build/", "pc/third_party/", "pc/src/third_party/", "pc/shaders/")
KEYWORDS = set("""auto break case char const continue default do double else enum extern float for goto if inline int long
register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while
align alignas alignof and asm async await bool break catch comptime defer errdefer error export fn linksection noalias
nosuspend null opaque or orelse packed pub resume suspend test threadlocal try type undefined unreachable usingnamespace var
anytype anyopaque anyerror noreturn true false i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 usize isize c_int c_uint c_long
c_short c_char main self this""".split())


PLACEHOLDER = re.compile(r"unk[0-9A-Fa-f]+(?:_\d+)?")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True).stdout


# ------------------------------------------------------------------ our structs
TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|[A-Za-z_]\w*|\d[\w.]*|->|\.\.\.|[^\s]')
COMMENTS = re.compile(r'"(?:\\.|[^"\\\n])*"|/\*.*?\*/|//[^\n]*', re.S)


def blank_comments(text):
    """Comments and preprocessor lines blanked (offsets kept)."""
    def blank(m):
        t = m.group(0)
        return t if t.startswith('"') else re.sub(r"[^\n]", " ", t)
    text = COMMENTS.sub(blank, text)
    return re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


class Member:
    __slots__ = ("name", "pos", "type", "dims", "bits", "nested")

    def __init__(self, name, pos, type_, dims, bits, nested):
        self.name, self.pos, self.type, self.dims, self.bits, self.nested = name, pos, type_, dims, bits, nested


class Struct:
    def __init__(self, kind, tag, file, start):
        self.kind, self.tag, self.file, self.start = kind, tag, file, start
        self.names = set([tag] if tag else [])
        self.members = []
        self.ok = True  # false when a member could not be read


def canon(type_text):
    """A member's type as a class that ours and the ledger agree on: sized primitives, 'ptr', or the typedef's name."""
    t = re.sub(r"\b(const|volatile|struct|union|enum|register)\b", " ", type_text)
    t = re.sub(r"\s+", " ", t).strip()
    if "*" in t or t.startswith("psx_ptr"):
        return "ptr"
    t = re.sub(r"\bunsigned (int|char|short|long)\b", r"u_\1", t)
    t = {"u_char": "char", "unsigned char": "char", "u_short": "short", "unsigned short": "short",
         "u_int": "int", "unsigned int": "int", "signed char": "char", "signed": "int", "signed int": "int", "signed short": "short", "signed long": "long",
         "unsigned": "u_int", "unsigned long": "u_long", "long int": "long", "short int": "short",
         "u_long": "u_long", "long long": "long long"}.get(t, t)
    return t


def parse_file(path, rel, structs):
    text = blank_comments(path.read_text(errors="replace"))
    toks = [(m.group(0), m.start()) for m in TOKEN.finditer(text)]
    n = len(toks)

    def find_close(i):  # toks[i] == "{"
        depth = 0
        while i < n:
            if toks[i][0] == "{":
                depth += 1
            elif toks[i][0] == "}":
                depth -= 1
                if depth == 0:
                    return i
            i += 1
        return None

    def parse_struct(i, kind):
        """toks[i] is the word after struct/union. -> (Struct, index after the closing brace) or (None, i)."""
        tag = None
        j = i
        if j < n and re.fullmatch(r"[A-Za-z_]\w*", toks[j][0]) and toks[j][0] not in ("struct", "union"):
            tag = toks[j][0]
            j += 1
        if j >= n or toks[j][0] != "{":
            return None, i
        close = find_close(j)
        if close is None:
            return None, i
        s = Struct(kind, tag, rel, toks[j][1])
        read_members(s, j + 1, close)
        return s, close + 1

    def read_members(s, a, b):
        segs, cur, depth = [], [], 0
        for k in range(a, b):
            w = toks[k][0]
            if w in "{(":
                depth += 1
            elif w in "})":
                depth -= 1
            if w == ";" and depth == 0:
                segs.append(cur)
                cur = []
            else:
                cur.append(k)
        if cur:
            segs.append(cur)
        for seg in segs:
            parse_member(s, seg)

    def parse_member(s, seg):
        words = [toks[k][0] for k in seg]
        if not words:
            return
        nested = None
        rest = seg
        if words[0] in ("struct", "union") or (words[0] in ("const", "volatile") and len(words) > 1):
            kinds = [x for x in range(len(seg)) if words[x] in ("struct", "union")]
            if kinds:
                k0 = kinds[0]
                after = seg[k0 + 1]
                sub, nxt = parse_struct(after, words[k0])
                if sub is not None:
                    nested = sub
                    sub.file = rel
                    rest = [k for k in seg if k >= nxt]
                    if not rest:  # an anonymous member
                        s.members.append(Member(None, None, "anon", "", None, nested))
                        return
                    parse_declarators(s, rest, "nested", nested)
                    return
        parse_declarators(s, rest, None, None)

    def parse_declarators(s, seg, nested_flag, nested):
        # split declarators at top-level commas; the type words are those before the first declarator's name
        parts, cur, depth = [], [], 0
        for k in seg:
            w = toks[k][0]
            if w in "([{":
                depth += 1
            elif w in ")]}":
                depth -= 1
            if w == "," and depth == 0:
                parts.append(cur)
                cur = []
            else:
                cur.append(k)
        parts.append(cur)
        first_type = None
        for pi, part in enumerate(parts):
            words = [toks[k][0] for k in part]
            bits = None
            if ":" in words and "?" not in words:
                ci = words.index(":")
                bits = " ".join(words[ci + 1:])
                part, words = part[:ci], words[:ci]
            # the name: a function pointer's (*name), else the last identifier before any [dims]
            name_i = None
            for x in range(len(words) - 2):
                if words[x] == "(" and words[x + 1] == "*" and re.fullmatch(r"[A-Za-z_]\w*", words[x + 2]):
                    name_i = x + 2
                    break
            dims = ""
            if name_i is None:
                x = len(words)
                depth = 0
                while x > 0:
                    w = words[x - 1]
                    if w == "]":
                        depth += 1
                    elif w == "[":
                        depth -= 1
                    elif depth == 0 and re.fullmatch(r"[A-Za-z_]\w*", w) and w not in KEYWORDS:
                        name_i = x - 1
                        break
                    elif depth == 0 and w not in "*":
                        break
                    x -= 1
                if name_i is None:
                    s.ok = False
                    continue
                dims = "".join(words[name_i + 1:])
            else:
                dims = "fn"
            lead = [w for w in words[:name_i] if w != "(" and w != "*"] if dims == "fn" else words[:name_i]
            stars = "*" if (dims == "fn" or "*" in words[:name_i]) else ""
            if pi == 0:
                first_type = " ".join(words[:name_i]) if dims != "fn" else "ptr"
                base = canon(first_type)
            else:
                base = canon(re.sub(r"\*", "", first_type or "") + (" *" if "*" in words[:name_i] else ""))
                if first_type and "*" in first_type and "*" not in words[:name_i]:
                    base = canon(first_type.replace("*", ""))
            tcanon = "ptr" if dims == "fn" else base
            if nested is not None:
                tcanon = "nested"
            s.members.append(Member(words[name_i], toks[part[name_i]][1], tcanon,
                                    "" if dims == "fn" else dims, bits, nested))

    i = 0
    while i < n:
        w = toks[i][0]
        if w in ("struct", "union") and i + 1 < n:
            s, nxt = parse_struct(i + 1, w)
            if s is not None:
                # typedef names after the closing brace
                k = nxt
                names = []
                while k < n and toks[k][0] not in (";", "{"):
                    if re.fullmatch(r"[A-Za-z_]\w*", toks[k][0]):
                        names.append(toks[k][0])
                    k += 1
                is_typedef = i > 0 and toks[i - 1][0] == "typedef"
                if is_typedef:
                    s.names.update(names)
                structs.append(s)
                i = nxt
                continue
        i += 1


def our_structs():
    structs = []
    files = [f for f in git("ls-files", *SOURCE_TREES).splitlines() if f.endswith((".h", ".c", ".inc"))
             and not f.startswith("pc/src/third_party/")]
    for f in files:
        try:
            parse_file(ROOT / f, f, structs)
        except (OSError, UnicodeDecodeError):
            pass
    return structs


def flatten(structs):
    """Every struct and every nested struct, the nested ones named by their path from a named parent."""
    out = []

    def walk(s, path):
        out.append((s, path))
        for m in s.members:
            if m.nested is not None:
                walk(m.nested, path + "." + (m.name or "<anon>"))
    for s in structs:
        walk(s, "")
    return out


# ------------------------------------------------------------------ ledger
def load_ledger(ledgers):
    types = {r["id"]: r for r in csv.DictReader(open(ledgers / "TYPE_LEDGER.csv"))}
    fields = collections.defaultdict(list)
    for r in csv.DictReader(open(ledgers / "FIELD_LEDGER.csv")):
        fields[r["type_id"]].append(r)
    for rows in fields.values():
        rows.sort(key=lambda r: int(r["index"]))
    return types, fields


def ledger_canon(type_text):
    if "(unnamed" in type_text:
        return "nested"
    t = re.sub(r"\[\d+\]", "", type_text)  # array dims are compared on their own
    t = re.sub(r"\bstruct\b|\bunion\b|\benum\b", " ", t)
    return canon(t)


def ledger_dims(type_text):
    return "".join(re.findall(r"\[\d+\]", type_text))


def our_dims(dims):
    return re.sub(r"\[\s*(0[xX][0-9a-fA-F]+|\d+)\s*\]", lambda m: "[%d]" % int(m.group(1), 0), re.sub(r"\s", "", dims))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ledgers", type=pathlib.Path)
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--confidence", default="high")
    ap.add_argument("--max", type=int, default=0, help="apply at most N renames (by struct name, then position)")
    ap.add_argument("--shared", action="store_true",
                    help="also rename old names that other structs have too: the text rename touches their uses, the "
                         "compiler's errors name them, and --fix puts them back")
    ap.add_argument("--declare-only", action="store_true",
                    help="with --apply: rename only the declarations; --fix then finds each use by the compiler's error "
                         "(type-aware: the struct is in the message), so old names that several structs share, "
                         "even renamed differently, work")
    ap.add_argument("--bitfields", action="store_true", help="also rename bitfield members")
    ap.add_argument("--types", action="store_true", help="rename the struct and union names (vs_battle_ActionState) instead of members")
    ap.add_argument("--fix", action="store_true",
                    help="build (build_boot.py --arch 64) and rewrite the uses the compiler rejects, until it builds")
    ap.add_argument("--structs", help="comma list: only these structs")
    ap.add_argument("--all-names", action="store_true",
                    help="also rename members that already have a descriptive name (default: only the decomp's placeholders unkNN)")
    args = ap.parse_args()

    if args.fix:
        return run_fix()
    if args.types:
        return run_types(args)
    structs = our_structs()
    flat = flatten(structs)
    by_name = collections.defaultdict(list)
    for s, path in flat:
        if path == "":
            for nm in s.names:
                by_name[nm].append(s)
    member_owners = collections.defaultdict(set)  # member name -> ids of structs that have it
    for s, path in flat:
        for m in s.members:
            if m.name:
                member_owners[m.name].add(id(s))
    macros = set()
    for f in git("ls-files", *USE_TREES).splitlines():
        if f.endswith((".h", ".c", ".inc", ".cpp")) and not f.startswith(USE_SKIP):
            try:
                for m in re.finditer(r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)", (ROOT / f).read_text(errors="replace")):
                    macros.add(m.group(1))
            except OSError:
                pass

    types, fields = load_ledger(args.ledgers)
    applied = {}  # (struct, old) -> row, from fields.tsv
    fpath = NAMES / "fields.tsv"
    if fpath.exists():
        for l in fpath.read_text().splitlines():
            if l and not l.startswith("#"):
                a = l.split("\t")
                applied[(a[0], a[1])] = a
    skipped, cands = [], []
    seen = {}
    for tid, trow in sorted(types.items()):
        if trow["kind"] not in ("struct", "union") or "(unnamed" in trow["name"]:
            continue
        name = trow["name"]
        if args.structs and name not in args.structs.split(","):
            continue
        rows = fields.get(tid, [])
        ours = by_name.get(name)
        if not ours:
            skipped.append((name, "", "no struct of that name in our tree", ""))
            continue
        # one definition, or all identical in members
        sig = {tuple((m.name, m.type, our_dims(m.dims), m.bits) for m in s.members) for s in ours}
        if len(sig) > 1:
            skipped.append((name, "", "defined differently in several of our files", ""))
            continue
        s = ours[0]
        if not s.ok:
            skipped.append((name, "", "a member of ours could not be read", ""))
            continue
        if any(m.name is None for m in s.members):
            skipped.append((name, "", "has an anonymous member", ""))
            continue
        if len(s.members) != len(rows) or any(m.name != r["name"] for m, r in zip(s.members, rows)):
            skipped.append((name, "", "members differ from the ledger's (names or order or count)", ""))
            continue
        bad = [m.name for m, r in zip(s.members, rows)
               if (ledger_canon(r["type"]) != m.type and not (ledger_canon(r["type"]) == "nested" and m.type == "nested")
                   or (m.type != "nested" and ledger_dims(r["type"]) != our_dims(m.dims)))]
        if key := seen.get(name):
            # the same struct in another ledger row (an overlay's copy): the rows must agree
            for r in rows:
                pass
        seen[name] = tid
        for m, r in zip(s.members, rows):
            if not r["final_name"] or r["final_name"] == m.name:
                continue
            old, new = m.name, r["final_name"]
            if (name, old) in applied:
                continue
            if not args.all_names and not PLACEHOLDER.fullmatch(old):
                skipped.append((name, old, "not a placeholder name (already descriptive: left)", new))
            elif m.name in bad:
                skipped.append((name, old, "type differs from the ledger's (ours " + m.type + m.dims + ", theirs " + r["type"] + ")", new))
            elif m.bits is not None and not args.bitfields:
                skipped.append((name, old, "bitfield (left for its own step)", new))
            elif r["confidence"] not in args.confidence.split(","):
                skipped.append((name, old, "confidence " + r["confidence"] + " (deferred)", new))
            elif not re.fullmatch(r"[a-z][A-Za-z0-9_]*", new) or new in KEYWORDS or new in macros:
                skipped.append((name, old, "name not usable (keyword, macro or not camelCase)", new))
            elif any(x.name == new for x in s.members):
                skipped.append((name, old, "new name is already a member of the struct", new))
            elif not args.declare_only and len(member_owners[old]) != 1 and (not args.shared or any(
                    x.name == new for o2, _p in flat if id(o2) in member_owners[old] for x in o2.members)):
                skipped.append((name, old, "old name is a member of %d structs (a text rename would touch the others)"
                                % len(member_owners[old]), new))
            else:
                cands.append((name, old, new, r["confidence"], r["id"], s, m))
    # the same struct seen in several ledger rows (an overlay's copy of a header): one candidate each
    uniq = {}
    for c in cands:
        uniq.setdefault((c[0], c[1], c[2]), c)
    cands = list(uniq.values())
    final = []
    bynew = collections.Counter((c[0], c[2]) for c in cands)
    byold_struct = collections.Counter((c[0], c[1]) for c in cands)
    byold = collections.defaultdict(set)
    for c in cands:
        byold[c[1]].add(c[2])
    for c in cands:
        if bynew[(c[0], c[2])] > 1:
            skipped.append((c[0], c[1], "two members of the struct get the same new name", c[2]))
        elif byold_struct[(c[0], c[1])] > 1:
            skipped.append((c[0], c[1], "the ledger gives the member two names", c[2]))
        elif not args.declare_only and len(byold[c[1]]) > 1:
            skipped.append((c[0], c[1], "the old name gets different new names", c[2]))
        else:
            final.append(c)
    final2 = []
    if args.declare_only:
        final2 = final
    else:
        newcount = collections.Counter(c[2] for c in final)
        for c in final:
            if newcount[c[2]] > 1:
                skipped.append((c[0], c[1], "another struct's member gets the same new name now", c[2]))
            else:
                final2.append(c)
    final = sorted(final2, key=lambda c: (c[0], c[5].start, c[6].pos))
    if args.max:
        final = final[:args.max]
    print(f"our structs {len(flat)}, candidates {len(cands)}, selected {len(final)}")
    for reason, k in collections.Counter(re.sub(r"\(.*|\d+", "", s[2]) for s in skipped).most_common():
        print(f"  skipped {k:5}  {reason}")
    NAMES.mkdir(exist_ok=True)
    (NAMES / "fields_skipped.tsv").write_text(
        "# struct\told\treason\tdetail (pc/tools/import_fields.py; rewritten each run)\n"
        + "".join("\t".join(s) + "\n" for s in sorted(skipped)))
    if not args.apply:
        for c in final[:20]:
            print("  ", c[0], c[1], "->", c[2], c[3])
        return 0
    return apply(final, applied, args.declare_only)


def apply(final, applied, declare_only=False):
    mapping = {c[1]: c[2] for c in final}
    # 1. declarations (positions per file, from the end so offsets hold)
    by_file = collections.defaultdict(list)
    for c in final:
        by_file[c[5].file].append((c[6].pos, c[1], c[2]))
    for f, items in by_file.items():
        path = ROOT / f
        t = path.read_text()
        for pos, old, new in sorted(items, reverse=True):
            assert t[pos:pos + len(old)] == old, (f, pos, old)
            t = t[:pos] + new + t[pos + len(old):]
        path.write_text(t)
    if declare_only:
        return finish(final, applied, mapping, len(by_file), 0)
    # 2. uses
    old_alt = "|".join(re.escape(o) for o in sorted(mapping, key=len, reverse=True))
    use = re.compile(r"(?<![.])(\.|->)(" + old_alt + r")(?![A-Za-z0-9_])")
    fuse = re.compile(r"(\bF\s*\([^(),]*,[^(),]*,\s*)(" + old_alt + r")(\s*\))")
    smap = collections.defaultdict(dict)  # struct name -> {old: new}: for offsetof(S, old...)
    for c in final:
        for nm in c[5].names:
            smap[nm][c[1]] = c[2]
    ouse = re.compile(r"((?:__builtin_)?offsetof\s*\(\s*(?:struct\s+|union\s+)?)(\w+)(\s*,\s*)(\w+)((?:[.\[][\w.\[\]]*)?\s*\))")

    def offsetof(m):
        head = smap.get(m.group(2), {}).get(m.group(4))
        return m.group(0) if head is None else m.group(1) + m.group(2) + m.group(3) + head + m.group(5)
    zoff = re.compile(r'(@offsetOf\(\s*(?:[\w.]+\.)?)(\w+)(\s*,\s*")(\w+)(")')

    def zoffset(m):
        head = smap.get(m.group(2), {}).get(m.group(4))
        return m.group(0) if head is None else m.group(1) + m.group(2) + m.group(3) + head + m.group(5)
    changed = 0
    for f in git("ls-files", *USE_TREES).splitlines():
        if not f.endswith(USE_SUFFIXES) or f.startswith(USE_SKIP):
            continue
        p = ROOT / f
        try:
            t = p.read_text()
        except (UnicodeDecodeError, OSError):
            continue
        n = use.sub(lambda m: m.group(1) + mapping[m.group(2)], t)
        n = fuse.sub(lambda m: m.group(1) + mapping[m.group(2)] + m.group(3), n)
        n = ouse.sub(offsetof, n)
        n = zoff.sub(zoffset, n)
        if n != t:
            p.write_text(n)
            changed += 1
    return finish(final, applied, mapping, len(by_file), changed)


def finish(final, applied, mapping, headers, changed):
    # 3. MEANINGS.tsv's Type.field keys
    meanings = ROOT / "pc/docs/porting/battle/MEANINGS.tsv"
    per = {(c[0], c[1]): c[2] for c in final}
    if meanings.exists():
        lines = meanings.read_text().split("\n")
        out = []
        for l in lines:
            first = l.split("\t", 1)[0]
            m = re.fullmatch(r"(\w+)\.(\w+)", first)
            if m and (m.group(1), m.group(2)) in per:
                l = m.group(1) + "." + per[(m.group(1), m.group(2))] + l[len(first):]
            out.append(l)
        meanings.write_text("\n".join(out))
    rows = [list(v) for v in applied.values()] + [[c[0], c[1], c[2], c[3], c[4]] for c in final]
    rows.sort()
    (NAMES / "fields.tsv").write_text("# struct\told\tnew\tconfidence\tledger field id (pc/tools/import_fields.py)\n"
                                      + "".join("\t".join(r) + "\n" for r in rows))
    print(f"applied {len(final)} renames: {headers} headers, {changed} sources edited")
    return 0


# ------------------------------------------------------------------ --types: the struct and union names
TYPE_PLACEHOLDER = re.compile(r"_?(?:D|func)_(?:80|1F80)[0-9A-Fa-f]{6}\w*")


def type_name(final):
    """Battle::Ai::DirectionDelta -> vs_battle_AiDirectionDelta (the global of the same leaf, vs_battle_aiDirectionDelta,
    stays distinct by the capital)."""
    parts = [p for p in final.split("::") if p]
    if len(parts) < 2:
        return None
    rest = "".join(p[:1].upper() + p[1:] for p in parts[1:])
    return f"vs_{parts[0].lower()}_{rest}"


def run_types(args):
    structs = our_structs()
    flat = flatten(structs)
    by_name = collections.defaultdict(list)
    for s, path in flat:
        if path == "":
            for nm in s.names:
                by_name[nm].append(s)
    types, fields = load_ledger(args.ledgers)
    applied_new = {}  # (struct, ledger's member name) -> the name an earlier run gave it
    if (NAMES / "fields.tsv").exists():
        for l in (NAMES / "fields.tsv").read_text().splitlines():
            if l and not l.startswith("#"):
                a = l.split("\t")
                applied_new[(a[0], a[1])] = a[2]
    rows = []
    for l in (NAMES / "types.tsv").read_text().splitlines() if (NAMES / "types.tsv").exists() else []:
        if l and not l.startswith("#"):
            rows.append(l.split("\t"))
    done = {r[0] for r in rows}
    used = set(re.findall(r"\bvs_\w+", git("grep", "-h", "-o", "-E", r"\bvs_\w+", "--", ".", ":!pc/names")))
    used |= {r[1] for r in rows}
    skipped, cands = [], []
    for tid, tr in sorted(types.items()):
        if tr["kind"] not in ("struct", "union") or not TYPE_PLACEHOLDER.fullmatch(tr["name"]):
            continue
        name, new = tr["name"], type_name(tr["final_name"]) if tr["final_name"] else None
        if name in done:
            continue
        if args.structs and name not in args.structs.split(","):
            continue
        ours = by_name.get(name)
        if not ours:
            skipped.append((name, "", "no struct of that name in our tree", new or ""))
            continue
        sig = {tuple((m.name, m.type, our_dims(m.dims), m.bits) for m in s.members) for s in ours}
        s = ours[0]
        rowsf = fields.get(tid, [])
        if len(sig) > 1 or not s.ok or any(m.name is None for m in s.members):
            skipped.append((name, "", "defined differently or unreadable", new or ""))
        elif len(s.members) != len(rowsf) or any(
                m.name != r["name"] and applied_new.get((name, r["name"])) != m.name for m, r in zip(s.members, rowsf)):
            skipped.append((name, "", "members differ from the ledger's", new or ""))
        elif tr["confidence"] not in args.confidence.split(","):
            skipped.append((name, "", "confidence " + tr["confidence"] + " (deferred)", new or ""))
        elif not new or not re.fullmatch(r"vs_[a-z0-9]+_[A-Z][A-Za-z0-9]*", new):
            skipped.append((name, "", "no usable name", tr["final_name"]))
        elif new in used:
            skipped.append((name, "", "name already in use", new))
        else:
            cands.append((name, new, tr["confidence"], tid))
    # one new name for several old names, or one old name with several: drop
    cnt = collections.Counter(c[1] for c in cands)
    cands = [c for c in cands if cnt[c[1]] == 1]
    present = collections.Counter()
    texts = {}
    for f in git("ls-files", *USE_TREES).splitlines():
        if f.startswith(("pc/docs/plans/", "pc/docs/porting/reviews/", "pc/names/", "pc/assets/", "pc/build/", "pc/third_party/",
                         "pc/src/third_party/", "pc/shaders/")) or f.endswith(".json"):
            continue
        try:
            texts[f] = (ROOT / f).read_text()
        except (UnicodeDecodeError, OSError):
            continue
    sel = []
    for name, new, conf, tid in cands:
        if any(re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", x) for x in texts.values()):
            sel.append((name, new, conf, tid))
        else:
            skipped.append((name, "", "not in our code", new))
    if args.max:
        sel = sel[:args.max]
    print(f"types: {len(sel)} selected; skipped {len(skipped)}")
    (NAMES / "types_skipped.tsv").write_text("# type\t\treason\tdetail (import_fields.py --types)\n"
                                           + "".join("\t".join(s) + "\n" for s in sorted(skipped)))
    if not args.apply:
        for c in sel[:20]:
            print("  ", *c)
        return 0
    mapping = {c[0]: c[1] for c in sel}
    pat = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(re.escape(o) for o in sorted(mapping, key=len, reverse=True)) + r")(?![A-Za-z0-9_])")
    changed = 0
    for f, x in texts.items():
        n = pat.sub(lambda m: mapping[m.group(1)], x)
        if n != x:
            (ROOT / f).write_text(n)
            changed += 1
    allrows = rows + [[c[0], c[1], c[2], c[3]] for c in sel]
    allrows.sort()
    (NAMES / "types.tsv").write_text("# old\tnew\tconfidence\tledger type id (pc/tools/import_fields.py --types)\n"
                                    + "".join("\t".join(r) + "\n" for r in allrows))
    print(f"renamed {len(sel)} types in {changed} files")
    return 0


# ------------------------------------------------------------------ --fix: the compiler finds the uses
GCC_ERR = re.compile(r"^(\S+?):(\d+):(\d+): error: [\u2018'](?:struct |union )?(\w+)[\u2019'] has no member named [\u2018'](\w+)[\u2019']", re.M)
ZIG_ERR = re.compile(r"^(\S+?\.zig):(\d+):(\d+): error: no (?:field|member) named '(\w+)' in (?:struct|union) '(?:\w+\.)?(\w+)'", re.M)


def fix_once(text, per):
    """Rewrite each use the compiler named: file:line:col, the struct and the old name. -> (fixed, unresolved)."""
    hits = {}
    for m in GCC_ERR.finditer(text):
        f, line, col, struct, old = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)
        hits[(f, line, col, old)] = struct
    for m in ZIG_ERR.finditer(text):
        f, line, col, old, struct = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)
        hits[(f, line, col, old)] = struct
    fixed, unresolved = 0, []
    byfile = collections.defaultdict(list)
    for (f, line, col, old), struct in hits.items():
        byfile[f].append((line, col, old, struct))
    for f, items in byfile.items():
        path = ROOT / f
        if not path.exists():
            unresolved += [(f, l, c, o, s, "no such file") for l, c, o, s in items]
            continue
        lines = path.read_text().split("\n")
        # the same line fixed right to left so columns hold
        for line, col, old, struct in sorted(items, key=lambda x: (x[0], -x[1])):
            new = per.get((struct, old))
            if new is None:
                unresolved.append((f, line, col, old, struct, "not a rename of ours"))
                continue
            text_line = lines[line - 1]
            word = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])")
            spots = [m.start() for m in word.finditer(text_line)]
            if not spots:
                # a unit built from several files can count a line or two off: the nearest line that holds the name
                for d in (1, -1, 2, -2):
                    if 0 < line + d <= len(lines) and word.search(lines[line + d - 1]):
                        line += d
                        text_line = lines[line - 1]
                        spots = [m.start() for m in word.finditer(text_line)]
                        break
            if col - 1 in spots:
                at = col - 1
            elif len(spots) == 1:
                at = spots[0]
            elif len(spots) > 1 and sorted(abs(s - (col - 1)) for s in spots)[0] <= 2 and \
                    sorted(abs(s - (col - 1)) for s in spots)[0] < sorted(abs(s - (col - 1)) for s in spots)[1]:
                at = min(spots, key=lambda s: abs(s - (col - 1)))  # the compiler's column is within a character or two
            else:
                unresolved.append((f, line, col, old, struct, "the line holds %d uses and the column does not name one" % len(spots)))
                continue
            lines[line - 1] = text_line[:at] + new + text_line[at + len(old):]
            fixed += 1
        path.write_text("\n".join(lines))
    return fixed, unresolved


def run_fix(rounds=300):
    per = {}
    for l in (NAMES / "fields.tsv").read_text().splitlines():
        if l and not l.startswith("#"):
            a = l.split("\t")
            per[(a[0], a[1])] = a[2]
    for n in range(rounds):
        proc = subprocess.run(["python3", "pc/tools/play_mode.py", "--", "python3", "pc/tools/build_boot.py", "--arch", "64"],
                              cwd=ROOT, capture_output=True, text=True)
        text = proc.stdout + proc.stderr
        report = ROOT / "pc/build/boot64/report.txt"
        if proc.returncode and report.exists():
            text += report.read_text()
        if proc.returncode == 0:
            print(f"round {n}: builds", flush=True)
            return 0
        # the views pass compiles each unit again (-O0 -g) and its errors are not in the report: check the .i files
        # the build just wrote
        units = sorted((ROOT / "pc/build/boot64").glob("*.i"))
        check = subprocess.run("xargs -P 12 -n 1 sh -c 'gcc -m64 -w -fsyntax-only \"$0\" 2>&1 | grep error'",
                               input="\n".join(map(str, units)), shell=True, cwd=ROOT, capture_output=True, text=True)
        text += check.stdout
        fixed, unresolved = fix_once(text, per)
        print(f"round {n}: {fixed} uses rewritten, {len(unresolved)} unresolved", flush=True)
        for u in unresolved[:10]:
            print("   ", *u)
        if not fixed:
            print(text[-1500:])
            return 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
