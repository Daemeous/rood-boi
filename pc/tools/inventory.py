#!/usr/bin/env python3
"""Regenerate the remaining port assembly inventory from current source."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
rows = []
for path in sorted((root / "src").rglob("*.c")):
    for match in re.finditer(
        r'INCLUDE_ASM\s*\(\s*"[^"]*"\s*,\s*(\w+)\s*\)', path.read_text()
    ):
        rows.append((str(path.relative_to(root)), match.group(1)))
print("# Current assembly inventory\n")
print(
    f"{len(rows)} raw INCLUDE_ASM source occurrences (including conditional/commented code); not a preprocessed remaining-function count.\n"
)
print("| Source | Function |\n|---|---|")
for path, function in rows:
    print(f"| `{path}` | `{function}` |")
print("\n## Assembly source files\n")
for path in sorted((root / "src").rglob("*.s")):
    print(f"- `{path.relative_to(root)}`")
