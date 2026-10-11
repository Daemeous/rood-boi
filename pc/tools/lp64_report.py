"""Summarise the 64-bit (LP64) compile pass of build_boot.py --arch 64.

Writes report.txt (readable) and report.json (per-file diagnostics) next to
the objects. Used only by the measurement pass; the 32-bit build never
imports this module.
"""

import collections
import json
import os
import re

DIAGNOSTIC = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?P<col>\d+): (?P<level>error|warning|fatal error): (?P<msg>.*?)(?: \[(?P<flag>-W[^\]]+)\])?$")


def classify(level, message, flag):
    """A short category for one diagnostic."""
    if flag:
        return flag.replace("-Werror=", "-W")
    if level == "fatal error":
        return "fatal"
    if "cast" in message and "pointer" in message:
        return "pointer cast"
    if "static assertion failed" in message:
        return "static assert"
    return "other"


def parse(stderr):
    diagnostics = []
    for line in stderr.splitlines():
        match = DIAGNOSTIC.match(line)
        if match:
            level = match["level"]
            diagnostics.append(
                {
                    "file": match["file"],
                    "line": int(match["line"]),
                    "level": "error" if level == "fatal error" else level,
                    "category": classify(level, match["msg"], match["flag"]),
                    "message": match["msg"],
                }
            )
    return diagnostics


def group_of(source):
    """Overlay group used to split the conversion between agents."""
    s = os.path.normpath(source).replace("\\", "/")
    if s.startswith("src/BATTLE/") or s.startswith("src/EFFECT/") or s.startswith("src/GIM/"):
        return "BATTLE"
    if s.startswith("src/MENU/"):
        return "MENU"
    if s.startswith("src/TITLE/") or s.startswith("src/ENDING/"):
        return "TITLE/ending"
    if s.startswith("src/SLUS_010.40/") or s.startswith("src/include/") or s.startswith("include/"):
        return "main"
    if s.startswith("pc/third_party/"):
        return "third_party"
    return "pc"


def write_report(out, results):
    files = []
    for kind, source, returncode, stderr in results:
        diagnostics = parse(stderr)
        files.append(
            {
                "source": source,
                "kind": kind,
                "group": group_of(source),
                "compiled": returncode == 0,
                "errors": sum(d["level"] == "error" for d in diagnostics),
                "warnings": sum(d["level"] == "warning" for d in diagnostics),
                "categories": dict(collections.Counter(d["category"] for d in diagnostics if d["level"] == "error")),
                "warning_categories": dict(collections.Counter(d["category"] for d in diagnostics if d["level"] == "warning")),
                "diagnostics": diagnostics,
            }
        )
    (out / "report.json").write_text(json.dumps({"files": files}, indent=1))

    failed = [f for f in files if not f["compiled"]]
    by_category = collections.Counter()
    warn_category = collections.Counter()
    by_location = collections.Counter()
    for f in files:
        by_category.update(f["categories"])
        warn_category.update(f["warning_categories"])
        for d in f["diagnostics"]:
            if d["level"] == "error":
                by_location[d["file"]] += 1
    groups = collections.defaultdict(lambda: [0, 0, 0])
    for f in files:
        g = groups[f["group"]]
        g[0] += 1
        g[1] += not f["compiled"]
        g[2] += f["errors"]

    lines = [
        "64-bit (LP64) compile pass: gcc -m64 -fno-pie -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast",
        f"{len(files)} translation units, {len(files) - len(failed)} compile, {len(failed)} fail, "
        f"{sum(f['errors'] for f in files)} errors, {sum(f['warnings'] for f in files)} warnings",
        "",
        "Errors by category:",
        *[f"  {n:6d}  {c}" for c, n in by_category.most_common()],
        "",
        "Warnings by category (top 15):",
        *[f"  {n:6d}  {c}" for c, n in warn_category.most_common(15)],
        "",
        "By overlay group (units, failing, errors):",
        *[f"  {g:14s} {v[0]:4d} {v[1]:4d} {v[2]:6d}" for g, v in sorted(groups.items())],
        "",
        "Error locations (file the diagnostic points at, top 40):",
        *[f"  {n:6d}  {loc}" for loc, n in by_location.most_common(40)],
        "",
        "Per translation unit (errors, warnings, categories), failing first:",
    ]
    for f in sorted(files, key=lambda f: (f["compiled"], -f["errors"], f["source"])):
        cats = ", ".join(f"{c} {n}" for c, n in sorted(f["categories"].items(), key=lambda x: -x[1]))
        status = "ok  " if f["compiled"] else "FAIL"
        lines.append(f"  {status} {f['errors']:5d} {f['warnings']:5d}  {f['source']}" + (f"  [{cats}]" if cats else ""))
    report = out / "report.txt"
    report.write_text("\n".join(lines) + "\n")
    return report
