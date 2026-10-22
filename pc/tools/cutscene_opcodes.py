#!/usr/bin/env python3
"""Write pc/assets/cutscenes/opcodes.json: each event script opcode's handler
name, read from a build's generated _opcodeFunctionTable
(pc/build/boot*/.../event_callbacks.inc, pc/tools/event_callbacks.py). The
cutscene player (pc/src/cutscene.cpp) shows the name beside the running opcode.

    python3 pc/tools/cutscene_opcodes.py [event_callbacks.inc]
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    if len(sys.argv) > 1:
        source = pathlib.Path(sys.argv[1])
    else:
        found = sorted((ROOT / "pc/build").glob("**/event_callbacks.inc"))
        if not found:
            print("no event_callbacks.inc under pc/build: build first (pc/tools/build_boot.py)", file=sys.stderr)
            return 1
        source = found[0]
    text = source.read_text()
    start = text.index("_opcodeFunctionTable[256]")
    body = text[start:text.index("};", start)]
    names = {}
    for m in re.finditer(r"^\s*(\w+), /\* 0x([0-9a-f]{2}) \*/", body, re.M):
        function, op = m.group(1), int(m.group(2), 16)
        if function != "vs_battle_invalidOpcode":
            names[f"0x{op:02x}"] = re.sub(r"^pc_event_", "", function)
    doc = {"about": "Event script opcodes (BATTLE.PRG _opcodeFunctionTable): the handler each opcode dispatches "
                    "to, as the decompilation names it (vs_battle_script_* where its meaning is known). The "
                    "cutscene player shows these beside the running opcode. Regenerate: "
                    "pc/tools/cutscene_opcodes.py.",
           "opcodes": names}
    out = ROOT / "pc/assets/cutscenes/opcodes.json"
    out.write_text(json.dumps(doc, indent=1) + "\n")
    print(f"{out}: {len(names)} opcodes from {source}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
