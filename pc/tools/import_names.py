#!/usr/bin/env python3
"""Import the symbol names VS-NativePort gave the decomp's functions and data (CC0, its Docs/ledgers).

    python3 pc/tools/import_names.py LEDGER_DIR                     plan: write the reports, change nothing else
    python3 pc/tools/import_names.py LEDGER_DIR --apply [--overlays A,B] [--max N] [--confidence high]

LEDGER_DIR holds FUNCTION_CATALOG.csv and DATA_LEDGER.csv (git show origin/imp/01-ma04-gates:Docs/ledgers/X.csv).
Only a symbol still called func_8XXXXXXX or D_8XXXXXXX is renamed (a name the decomp already gave is left).
A row matches by the decomp's name (column `name`) and overlay; the name must be unique in the ledger.
The new name is vs_<namespace>_<leaf> in camelCase from their C++ final_name (Battle::Ai::InitializeX ->
vs_battle_aiInitializeX); it must be free in the whole tree (src/ and config/ included) and not repeat.

Writes (all under pc/names/):
    symbols.tsv   applied: kind, overlay, original, new, confidence, source id (the build reads it: symbol_names.py)
    skipped.tsv   not applied, with the reason
    rejected.tsv  by hand: originals whose names are wrong for our code (never applied; edit and re-run)
--apply also renames the symbol in our code: pc/ and game/ text (not src/, config/, dated docs/plans and
docs/porting/reviews, assets), and a Zig file named after it. Re-run with newer ledgers: rows already applied
are kept, new rows are planned.
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
OLD = re.compile(r"\b(?:D|func)_80[0-9A-Fa-f]{6}\b")
OLD_FULL = re.compile(r"(?:D|func)_80[0-9A-F]{6}$")
RENAME_ROOTS = ("pc", "game")
RENAME_SKIP = ("pc/docs/plans/", "pc/docs/porting/reviews/", "pc/names/", "pc/assets/", "pc/build/", "pc/src/third_party/",
               # shader source is compiled (a comment is in the SPIR-V and the embedded text): not names only
               "pc/shaders/", "pc/src/editor_view.cpp")
KEYWORDS = set("auto break case char const continue default do double else enum extern float for goto if inline int long "
               "register restrict return short signed sizeof static struct switch typedef union unsigned void volatile "
               "while".split())
FUNCTION_KINDS = ("C", "asm-file", "INCLUDE_ASM", "inline-asm", "asm-entry", "C-header")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True).stdout


def new_name(final):
    """Battle::Ai::InitializeX -> vs_battle_aiInitializeX; k-prefixed constants lose the k."""
    parts = [p for p in final.split("::") if p]
    if not parts:
        return None
    leaf = parts[-1]
    if re.match(r"k[A-Z]", leaf):
        leaf = leaf[1:]
    rest = parts[1:-1] + [leaf]
    tail = "".join(p[:1].upper() + p[1:] if i else p[:1].lower() + p[1:] for i, p in enumerate(rest))
    if len(parts) == 1:
        return "vs_" + tail
    return f"vs_{parts[0].lower()}_{tail[:1].lower() + tail[1:] if len(rest) == 1 else tail[:1].lower() + tail[1:]}"


def merged_members(objects):
    """The decomp names of the functions the Zig objects merge: LLVM folds identical functions into one body under
    one of their names, and which one depends on the names, so renaming a member moves the body's section and symbol
    (the same code at the same address, but not the object the save states' keys hash). Read from a build's *_zig.o:
    every set of global functions sharing one address. Names are mapped back to the decomp's."""
    sys.path.insert(0, str(ROOT / "pc/tools"))
    import snapshot_layout
    import symbol_names
    canon = symbol_names.canonicaliser()
    out = set()
    for path in sorted(pathlib.Path(objects).glob("*_zig.o")):
        elf = snapshot_layout.Elf(path)
        at = collections.defaultdict(list)
        for name, info, shndx, value, size in elf.symbols:
            if info >> 4 == 1 and info & 0xF == 2 and 0 < shndx < 0xFF00:
                at[(shndx, value)].append(canon(name))
        for names in at.values():
            if len(names) > 1:
                out.update(names)
    return out


def revert(names, rows):
    """Undo the rename of these originals in our code (and move their Zig files back); the rows left."""
    back = {r[3]: r[2] for r in rows if r[2] in names}
    if not back:
        return rows
    new = re.compile(r"(?<![A-Za-z0-9_])(?:" + "|".join(re.escape(n) for n in sorted(back, key=len, reverse=True)) + r")(?![A-Za-z0-9_])")
    for f in tracked_text_files():
        path = ROOT / f
        if not path.exists():
            continue
        try:
            t = path.read_text()
        except UnicodeDecodeError:
            continue
        n = new.sub(lambda m: back[m.group(0)], t)
        if n != t:
            path.write_text(n)
    for f in tracked_text_files():
        p = pathlib.Path(f)
        if p.suffix == ".zig" and p.stem in back and (ROOT / f).exists():
            subprocess.run(["git", "-C", str(ROOT), "mv", f, str(p.with_name(back[p.stem] + ".zig"))], check=True)
    return [r for r in rows if r[2] not in names]


def read_tsv(path):
    if not path.exists():
        return []
    return [l.split("\t") for l in path.read_text().splitlines() if l and not l.startswith("#")]


def tracked_text_files():
    files = []
    for f in git("ls-files", *RENAME_ROOTS).splitlines():
        if f.startswith(RENAME_SKIP) or f.endswith(".json"):
            continue
        files.append(f)
    return files


def confidence(row, kind):
    notes = row.get("notes", "")
    m = re.search(r"conf:(\w+)", notes)
    conf = m.group(1) if m else row.get("confidence", "") or "none"
    if kind == "function" and conf == "high" and not row.get("cpp"):
        conf = "med"  # no native implementation of it uses the name
    return conf


def plan(ledgers, applied, rejected, merged=frozenset()):
    """-> (candidates [(kind, overlay, old, new, conf, id, address)], skipped [(kind, old, reason, detail)])"""
    rows = []
    for r in csv.DictReader(open(ledgers / "FUNCTION_CATALOG.csv")):
        if r["final_name"] and OLD_FULL.match(r["name"]):
            rows.append(("function", r))
    for r in csv.DictReader(open(ledgers / "DATA_LEDGER.csv")):
        if r["final_name"] and OLD_FULL.match(r["name"]):
            rows.append(("data", r))
    by_old = collections.defaultdict(list)
    for kind, r in rows:
        by_old[r["name"]].append((kind, r))
    skipped, cands = [], []
    for old, group in by_old.items():
        kind, r = group[0]
        if old in applied:
            continue
        if old in merged:
            skipped.append((kind, old, "merged by the Zig objects (identical code)", group[0][1]["final_name"]))
            continue
        if old in rejected:
            skipped.append((kind, old, "rejected by hand", rejected[old]))
            continue
        finals = {g[1]["final_name"] for g in group}
        if len(group) > 1:
            skipped.append((kind, old, "ambiguous: several rows (shared address range)",
                            " | ".join(sorted(finals))[:120]))
            continue
        if kind == "function" and r["kind"] not in FUNCTION_KINDS and not r["kind"].startswith("template"):
            skipped.append((kind, old, "kind " + r["kind"], r["final_name"]))
            continue
        nn = new_name(r["final_name"])
        if not nn or not re.fullmatch(r"vs_[a-z0-9]+_[A-Za-z0-9_]+", nn):
            skipped.append((kind, old, "no usable name", r["final_name"]))
            continue
        ident = r["id"] if kind == "function" else r["id"]
        cands.append((kind, r.get("overlay", ""), old, nn, confidence(r, kind), ident, r["final_name"]))
    return cands, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ledgers", type=pathlib.Path)
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--overlays", help="comma list of overlay names as the ledger spells them (BATTLE.PRG,SLUS_010.40)")
    ap.add_argument("--kind", choices=["function", "data"], help="only this kind")
    ap.add_argument("--confidence", default="high", help="comma list of confidences to apply (default high)")
    ap.add_argument("--max", type=int, default=0, help="apply at most N (lowest address first)")
    ap.add_argument("--objects", type=pathlib.Path,
                    help="a build directory (pc/build/boot64): functions the Zig objects merge are left alone, and "
                         "already applied ones are reverted (renaming a member moves the merged body)")
    args = ap.parse_args()

    applied_rows = read_tsv(NAMES / "symbols.tsv")
    merged = merged_members(args.objects) if args.objects else set()
    if args.apply and merged & {r[2] for r in applied_rows}:
        undone = merged & {r[2] for r in applied_rows}
        applied_rows = revert(undone, applied_rows)
        print(f"reverted {len(undone)} applied names the Zig objects merge")
    applied = {r[2]: r[3] for r in applied_rows}
    rejected = {r[0]: r[1] if len(r) > 1 else "" for r in read_tsv(NAMES / "rejected.tsv")}
    cands, skipped = plan(args.ledgers, applied, rejected, merged)

    # Names in use anywhere in the repo (src/ and config/ too, and libc-ish words): a new name must be free.
    used = set(re.findall(r"\bvs_\w+", git("grep", "-h", "-o", "-E", r"\bvs_\w+", "--", ".", ":!pc/names")))
    used |= {r[3] for r in applied_rows}
    wanted = collections.defaultdict(list)
    for c in cands:
        wanted[c[3]].append(c)
    ok = []
    for c in cands:
        kind, overlay, old, nn = c[:4]
        if len(wanted[nn]) > 1:
            skipped.append((kind, old, "duplicate: " + str(len(wanted[nn])) + " originals share the name", nn))
        elif nn in used or nn.lower() in {u.lower() for u in ()}:
            skipped.append((kind, old, "name already in use in the tree", nn))
        else:
            ok.append(c)
    lower = collections.Counter(c[3].lower() for c in ok)
    ok2 = []
    for c in ok:
        if lower[c[3].lower()] > 1:
            skipped.append((c[0], c[2], "differs only in case from another new name", c[3]))
        else:
            ok2.append(c)
    # present in our tree at all?
    present = collections.Counter()
    files = tracked_text_files()
    contents = {}
    for f in files:
        try:
            t = (ROOT / f).read_text()
        except (UnicodeDecodeError, OSError):
            continue
        contents[f] = t
        for m in OLD.findall(t):
            present[m] += 1
    final = []
    for c in ok2:
        if not present[c[2]]:
            skipped.append((c[0], c[2], "not in our code", c[3]))
        else:
            final.append(c)
    sel = [c for c in final if c[4] in args.confidence.split(",")]
    for c in final:
        if c not in sel:
            skipped.append((c[0], c[2], "confidence " + c[4] + " (deferred)", c[3]))
    if args.overlays:
        want = set(args.overlays.split(","))
        sel = [c for c in sel if c[1].split("/")[-1] in want]
    if args.kind:
        sel = [c for c in sel if c[0] == args.kind]
    sel.sort(key=lambda c: c[2])
    if args.max:
        sel = sel[:args.max]

    skip_counts = collections.Counter(re.sub(r": .*|\d+ ", "", s[2]) for s in skipped)
    print(f"candidates {len(cands)}, free and present {len(final)}, selected {len(sel)}")
    for reason, n in skip_counts.most_common():
        print(f"  skipped {n:5}  {reason}")
    NAMES.mkdir(exist_ok=True)
    (NAMES / "skipped.tsv").write_text(
        "# kind\toriginal\treason\tdetail (pc/tools/import_names.py; rewritten each run)\n"
        + "".join("\t".join(s) + "\n" for s in sorted(skipped, key=lambda s: (s[0], s[1]))))
    if not args.apply:
        for c in sel[:15]:
            print("  ", c[2], "->", c[3], c[4])
        return
    mapping = {c[2]: c[3] for c in sel}
    header = ["# kind\toverlay\toriginal\tnew\tconfidence\tsource (pc/tools/import_names.py; the build reads it)\n"]
    rows = applied_rows + [[c[0], c[1], c[2], c[3], c[4], c[5]] for c in sel]
    rows.sort(key=lambda r: (r[0], r[2]))
    (NAMES / "symbols.tsv").write_text("".join(header) + "".join("\t".join(r) + "\n" for r in rows))
    changed = 0
    for f, t in contents.items():
        n = OLD.sub(lambda m: mapping.get(m.group(0), m.group(0)), t)
        if n != t:
            (ROOT / f).write_text(n)
            changed += 1
    moved = 0
    for f in files:
        p = pathlib.Path(f)
        if p.suffix == ".zig" and p.stem in mapping:
            subprocess.run(["git", "-C", str(ROOT), "mv", f, str(p.with_name(mapping[p.stem] + ".zig"))], check=True)
            moved += 1
    print(f"applied {len(sel)}: {changed} files edited, {moved} Zig files moved")


if __name__ == "__main__":
    sys.exit(main())
