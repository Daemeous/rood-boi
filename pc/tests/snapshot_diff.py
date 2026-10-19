#!/usr/bin/env python3
"""Compare two save states (pc/src/snapshot.c files) byte for byte.

    python3 pc/tests/snapshot_diff.py a.snap b.snap [--binary pc/build/boot/rood_battle_probe]

Prints each memory range that differs with the symbols (from the game
binary's nm, guest data imports included) that hold the differing bytes.
Exit status 0 when every compared range is identical. The game stack is
compared separately (its host frames hold wall-clock values); the log is not
compared. Used by snapshot_equivalence.py.
"""
import argparse
import bisect
import pathlib
import struct
import subprocess
import sys

HEADER = struct.Struct("<8sIIQII")  # version 1
HEADER2 = struct.Struct("<8sIIQQII")  # version 2: code and layout keys
CHUNK = struct.Struct("<IIQQ")
CHUNK_END, CHUNK_MEMORY, CHUNK_SPU, CHUNK_LOG = range(4)
# Native statics that hold wall-clock time, not game state (boot_runtime.c:
# the last vsync's time, the elapsed-scanline estimate reads it).
VOLATILE = {"last_vsync"}


def read(path):
    """{"header": (...), "memory": {address: bytes}, "spu": bytes, "stack": (address, bytes)}"""
    data = pathlib.Path(path).read_bytes()
    magic, version = struct.unpack_from("<8sI", data, 0)
    if magic != b"ROODSNAP":
        raise ValueError(f"{path}: not a snapshot")
    if version >= 2:
        _, _, pointer_size, code_hash, _, vsync, _ = HEADER2.unpack_from(data, 0)
    else:
        _, _, pointer_size, code_hash, vsync, _ = HEADER.unpack_from(data, 0)
    context_size = 8 * pointer_size + 8  # snap_context: 8 registers, mxcsr, fpu cw, padding
    offset = (HEADER2 if version >= 2 else HEADER).size + context_size + 8  # + canary
    memory, spu, log = {}, b"", b""
    while True:
        tag, _, address, size = CHUNK.unpack_from(data, offset)
        offset += CHUNK.size
        body = data[offset:offset + size]
        offset += size
        if tag == CHUNK_END:
            break
        if tag == CHUNK_MEMORY:
            memory[address] = body
        elif tag == CHUNK_SPU:
            spu = body
        elif tag == CHUNK_LOG:
            log = body
    return {"vsync": vsync, "code_hash": code_hash, "pointer_size": pointer_size, "memory": memory, "spu": spu,
            "log": log}


class Symbols:
    def __init__(self, binary):
        rows = []
        out = subprocess.run(["nm", "-S", "-n", str(binary)], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4:
                rows.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
            elif len(parts) == 3:
                rows.append((int(parts[0], 16), 0, parts[2]))
        rows.sort()
        self.rows = rows
        self.starts = [r[0] for r in rows]

    def name(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        while i >= 0:
            start, size, name = self.rows[i]
            if size == 0 or address < start + size:
                return f"{name}+{address - start:#x}"
            i -= 1
        return f"{address:#x}"

    def is_volatile(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        return i >= 0 and self.rows[i][2] in VOLATILE and address < self.rows[i][0] + max(self.rows[i][1], 1)


def stack_range(snapshot):
    """The game stack chunk: the one ending at the stack's top (not a known range)."""
    return max(snapshot["memory"].items(), key=lambda kv: kv[0] + len(kv[1]) if kv[0] < 0x80000000 else -1)


def diff(a, b, symbols, limit=40, stack=False):
    """Differences as text lines; [] when the compared ranges are identical."""
    lines = []
    if a["code_hash"] != b["code_hash"]:
        return ["the snapshots come from different builds"]
    a_stack, b_stack = stack_range(a)[0], stack_range(b)[0]
    for address in sorted(set(a["memory"]) | set(b["memory"])):
        if address in (a_stack, b_stack) and not stack:
            continue
        x, y = a["memory"].get(address), b["memory"].get(address)
        if x is None or y is None or len(x) != len(y):
            lines.append(f"range {address:#x}: only in one snapshot or resized")
            continue
        if x == y:
            continue
        differing, shown = 0, []
        i = 0
        while i < len(x):
            if x[i] != y[i] and not symbols.is_volatile(address + i):
                differing += 1
                name = symbols.name(address + i)
                if not shown or shown[-1][0] != name:
                    shown.append([name, address + i, 0])
                shown[-1][2] += 1
            i += 1
        if differing:
            lines.append(f"range {address:#x} ({len(x)} bytes): {differing} bytes differ")
            for name, at, count in shown[:limit]:
                lines.append(f"    {name} ({at:#x}): {count} bytes")
            if len(shown) > limit:
                lines.append(f"    ... {len(shown) - limit} more symbols")
    if a["spu"] != b["spu"]:
        count = sum(1 for p, q in zip(a["spu"], b["spu"]) if p != q) + abs(len(a["spu"]) - len(b["spu"]))
        lines.append(f"SPU state: {count} bytes differ")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--binary", default=str(pathlib.Path(__file__).resolve().parents[2]
                                                 / "pc/build/boot/rood_battle_probe"))
    parser.add_argument("--stack", action="store_true", help="compare the game stack too")
    args = parser.parse_args()
    lines = diff(read(args.a), read(args.b), Symbols(args.binary), stack=args.stack)
    print("\n".join(lines) if lines else "identical")
    return 1 if lines else 0


if __name__ == "__main__":
    sys.exit(main())
