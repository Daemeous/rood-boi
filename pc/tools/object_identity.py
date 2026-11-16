#!/usr/bin/env python3
"""Is the compiled code the same apart from symbol names? (the proof of a name import, pc/names/)

    python3 pc/tools/object_identity.py OLD_BUILD_DIR NEW_BUILD_DIR [--names pc/names/symbols.tsv]

Compares every object (*.o) both directories hold: the contents of its allocated sections, its relocations and its
symbol table (value, size, type, binding, section), with the new names of the table turned back into the original
ones in the new objects' text. Debug sections and the string tables hold names and are left out. Exit 0 when
every common object is identical. snapshot_key.o is the one object that differs on purpose: its keys hash the
code's symbols (a save state of an older build is not loaded by a renamed one).
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import symbol_names  # noqa: E402


def original(name, back):
    """The decomp's name of one of ours; name__overlay (the build's per-overlay data) is mapped on its name."""
    if name in back:
        return back[name]
    head, sep, tail = name.rpartition("__")
    return back[head] + sep + tail if sep and head in back else name


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def digest(obj, back):
    """(sections and relocations in order, sorted symbol lines), names turned back."""
    body = []
    dump = run("objdump", "-h", "-s", str(obj))
    # the section table's file offset moves with the string tables
    dump = re.sub(r"(?m)^(\s*\d+ \S+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+)[0-9a-f]+(\s+2\*\*\d+)$", r"\1-\2", dump)
    for chunk in re.split(r"(?m)^(?=Contents of section )", dump):
        name = re.match(r"Contents of section (.*):", chunk)
        if name and name.group(1).startswith((".debug", ".comment", ".note", ".strtab", ".symtab")):
            continue
        body.append(re.sub(r"(?m)^.*file format.*$", "", chunk) if name else
                    "\n".join(l for l in chunk.splitlines() if "file format" not in l and ".debug" not in l))
    for block in re.split(r"(?m)^(?=RELOCATION RECORDS FOR )", run("objdump", "-r", str(obj))):
        if not block.startswith("RELOCATION RECORDS FOR [.debug") and not block.startswith("RELOCATION RECORDS FOR [.eh_frame"):
            body.append("\n".join(l for l in block.splitlines() if "file format" not in l))
    syms = run("objdump", "-t", str(obj)).splitlines()
    body, syms = "\n".join(body), "\n".join(syms)
    if back:
        sub = lambda t: re.sub(r"(?<![A-Za-z0-9])vs_\w+", lambda m: original(m.group(0), back), t)
        body, syms = sub(body), sub(syms)
    # Zig numbers its anonymous constants (__anon_145894) by its string pool's size: each is its rank
    for_ranks = {}
    rank = lambda t: re.sub(r"__anon_\d+", lambda m: "__anon_#%d" % for_ranks.setdefault(m.group(0), len(for_ranks)), t)
    body = rank(body)
    syms = "\n".join(rank(l) for l in syms.splitlines())
    return body, sorted(l for l in syms.splitlines() if "file format" not in l and not l.startswith(obj.name))


def instructions(obj, back):
    """{function: sorted instruction lines} of an object's code, addresses and jump targets left out, names mapped back
    (the instructions of a function as a multiset: GCC orders some loads by the globals' names)."""
    text = run("objdump", "-d", "-r", "--no-show-raw-insn", str(obj))
    if back:
        text = re.sub(r"(?<![A-Za-z0-9])vs_\w+", lambda m: original(m.group(0), back), text)
    functions, name = {}, None
    for line in text.splitlines():
        head = re.match(r"[0-9a-f]+ <(.+)>:$", line)
        if head:
            name = head.group(1)
            functions[name] = []
        elif name and line.strip():
            line = re.sub(r"^\s+[0-9a-f]+:\s+", "", line)
            line = re.sub(r"\s+[0-9a-f]+ <[^>]*>", " <target>", line)  # a jump's target
            line = re.sub(r"^\s+[0-9a-f]+:\s+R_", "R_", line)
            functions[name].append(re.sub(r"\s+", " ", re.sub(r"^[0-9a-f]+ (R_)", r"\1", line.strip())))
    return {k: sorted(v) for k, v in functions.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--names", default=str(symbol_names.SYMBOLS))
    ap.add_argument("--reordered", action="store_true",
                    help="say which differing objects hold the same instructions per function in another order")
    args = ap.parse_args()
    back = {new: old for old, new in symbol_names.load(args.names).items()}
    old_dir, new_dir = pathlib.Path(args.old), pathlib.Path(args.new)
    names = sorted(p.relative_to(old_dir) for p in old_dir.rglob("*.o") if p.name != "snapshot_key.o" and (new_dir / p.relative_to(old_dir)).exists())

    def one(rel):
        return rel, digest(old_dir / rel, {}) == digest(new_dir / rel, back)

    with ThreadPoolExecutor(8) as ex:
        results = list(ex.map(one, names))
    bad = [r for r, ok in results if not ok]
    print(f"{len(results)} objects compared, {len(results) - len(bad)} identical apart from names, {len(bad)} differ")
    reordered = []
    if args.reordered:
        for r in bad:
            if instructions(old_dir / r, {}) == instructions(new_dir / r, back):
                reordered.append(r)
        print(f"{len(reordered)} of the {len(bad)} differing objects hold the same instructions in every function, in "
              "another order (GCC orders loads by the globals' names)")
    for r in bad[:60]:
        if r not in reordered:
            print("  differs:", r)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
