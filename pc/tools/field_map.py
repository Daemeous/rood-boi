#!/usr/bin/env python3
"""Map target addresses to DWARF fields and bounded live root-pointer paths.

GDB inspects types only: it never runs or attaches to the game. `read(address,
size)` supplies live bytes, using the binary's byte order and pointer sizes.
Missing debug info leaves symbol+offset labels. PIE callers must supply the
runtime load_bias. Layouts are cached, pointer values are refreshed each call.
"""
import argparse
import bisect
import json
import pathlib
import subprocess
import tempfile


GDB_SCRIPT = r'''
import gdb, json
req = json.load(open(REQUEST))
types, seen = {}, []
def layout(t, depth=0):
    t = t.strip_typedefs()
    for previous, ident in seen:
        if t == previous:
            return ident
    if depth > 32 or len(types) >= 8192:
        return None
    ident = str(len(types))
    seen.append((t, ident))
    node = types[ident] = {"kind": "scalar"}
    try:
        node["size"] = int(t.sizeof)
        if t.code == gdb.TYPE_CODE_PTR:
            node.update(kind="pointer", target=layout(t.target(), depth+1))
        elif t.code == gdb.TYPE_CODE_ARRAY:
            lo, hi = t.range()
            node.update(kind="array", lower=int(lo), count=int(hi-lo+1),
                        target=layout(t.target(), depth+1))
        elif t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            node.update(kind="record", fields=[])
            for f in t.fields():
                if getattr(f, "is_static", False) or not hasattr(f, "bitpos"):
                    continue
                node["fields"].append({"name": f.name, "bitpos": int(f.bitpos),
                    "bitsize": int(f.bitsize), "type": layout(f.type, depth+1)})
    except Exception:
        pass
    return ident
symbols = {}
for name in req["names"]:
    try:
        # Quoting also handles file-local symbols containing punctuation.
        symbols[name] = layout(gdb.parse_and_eval("'" + name + "'").type)
    except Exception:
        pass
json.dump({"types": types, "symbols": symbols}, open(req["out"], "w"))
'''


class FieldMap:
    def __init__(self, binary, roots=(), read=None, pointer_size=None, *,
                 load_bias=0, max_depth=12, max_nodes=10000, timeout=30):
        self.binary = str(binary)
        self.roots = list(roots)
        self.read = read
        self.load_bias = load_bias
        self.max_depth, self.max_nodes, self.timeout = max_depth, max_nodes, timeout
        if max_depth < 0 or max_nodes < 1 or timeout <= 0:
            raise ValueError("invalid traversal limits")
        with open(binary, "rb") as stream:
            header = stream.read(6)
        if header[:4] != b"\x7fELF" or header[4] not in (1, 2) or header[5] not in (1, 2):
            raise ValueError("field mapper requires an ELF target")
        self.ptr = 4 if header[4] == 1 else 8
        self.byteorder = "little" if header[5] == 1 else "big"
        if pointer_size is not None and pointer_size != self.ptr:
            raise ValueError("pointer_size disagrees with target ELF")
        output = subprocess.run(["nm", "-S", "-n", "--defined-only", self.binary],
                                capture_output=True, text=True, check=True, timeout=timeout)
        rows = []
        for line in output.stdout.splitlines():
            parts = line.split()
            if len(parts) not in (3, 4):
                continue
            try:
                start = int(parts[0], 16) + load_bias
                size = int(parts[1], 16) if len(parts) == 4 else 0
            except ValueError:
                continue
            rows.append((start, size, parts[-1]))
        self.rows = sorted(rows)
        self.starts = [row[0] for row in self.rows]
        self._addresses = {name: start for start, _, name in self.rows}
        self.types, self.layouts = {}, {}
        self._loaded = set()
        self.diagnostics = []

    def symbol_address(self, name):
        return self._addresses.get(name)

    def symbol(self, address):
        # A zero-sized linker import must not swallow every later address.
        i = bisect.bisect_right(self.starts, address) - 1
        while i >= 0:
            start, size, name = self.rows[i]
            size = size or self.types.get(self.layouts.get(name), {}).get("size", 0)
            if address == start or (size and address < start + size):
                return name, address - start
            i -= 1
        return None, address

    def _load(self, names):
        missing = sorted(set(names) - self._loaded)
        if not missing:
            return
        self._loaded.update(missing)
        try:
            with tempfile.TemporaryDirectory(prefix="rood-field-map-") as directory:
                directory = pathlib.Path(directory)
                req, out = directory / "request.json", directory / "result.json"
                script = directory / "layout.py"
                req.write_text(json.dumps({"names": missing, "out": str(out)}))
                script.write_text("REQUEST = " + repr(str(req)) + "\n" + GDB_SCRIPT)
                subprocess.run(["gdb", "-nx", "-nh", "-batch", "-iex",
                                "set auto-load off", self.binary, "-x", str(script)],
                               check=True, capture_output=True, timeout=self.timeout)
                data = json.loads(out.read_text())
            # Each GDB request numbers types independently.
            prefix = str(len(self._loaded)) + ":"
            for key, node in data["types"].items():
                if node.get("target") is not None:
                    node["target"] = prefix + node["target"]
                for field in node.get("fields", []):
                    if field["type"] is not None:
                        field["type"] = prefix + field["type"]
                self.types[prefix + key] = node
            self.layouts.update({name: prefix + key for name, key in data["symbols"].items()
                                 if key is not None})
        except (OSError, subprocess.SubprocessError, ValueError, KeyError) as error:
            self.diagnostics.append("DWARF unavailable: " + str(error))

    def names(self, addresses):
        addresses = list(dict.fromkeys(addresses))
        if not addresses:
            return {}
        # Linker-defined guest imports often have no nm size. Ask DWARF
        # about the nearest aliases before deciding whether an address fits.
        nearby = []
        for address in addresses:
            i = bisect.bisect_right(self.starts, address) - 1
            if i >= 0:
                start = self.starts[i]
                while i >= 0 and self.starts[i] == start:
                    nearby.append(self.rows[i][2])
                    i -= 1
        self._load(self.roots + nearby)
        matches = {address: self.symbol(address) for address in addresses}
        self._load([name for name, _ in matches.values() if name])
        paths = {address: [] for address in addresses}
        budget = self.max_nodes
        visited = set()

        def walk(ident, base, path, depth, follow):
            nonlocal budget
            if budget <= 0 or depth > self.max_depth or ident not in self.types:
                return
            # Shared/cyclic objects are visited once; direct labels have separate walks.
            key = (ident, base, follow)
            if key in visited:
                return
            visited.add(key)
            budget -= 1
            node = self.types[ident]
            size = node.get("size", 0)
            hits = [a for a in addresses if base <= a < base + size]
            for address in hits:
                paths[address].append((depth, path))
            kind = node["kind"]
            if kind == "pointer" and follow and self.read and size in (4, 8):
                try:
                    raw = self.read(base, size)
                    if len(raw) != size:
                        return
                    pointer = int.from_bytes(raw, self.byteorder)
                except (OSError, ValueError, RuntimeError, TypeError):
                    return
                if pointer:
                    walk(node.get("target"), pointer, "(*" + path + ")", depth+1, True)
            elif kind == "array":
                target = node.get("target")
                stride = self.types.get(target, {}).get("size", 0)
                if stride <= 0:
                    return
                count = max(0, node.get("count", 0))
                indices = range(min(count, budget)) if follow else sorted(
                    {(a-base)//stride for a in hits})
                for i in indices:
                    if budget <= 0:
                        break
                    walk(target, base+i*stride, path+"[%d]" % (i+node.get("lower", 0)),
                         depth+1, follow)
            elif kind == "record":
                for field in node.get("fields", []):
                    if budget <= 0:
                        break
                    offset = field["bitpos"] // 8
                    child = path + "." + field["name"] if field["name"] else path
                    if field["bitsize"]:
                        end = (field["bitpos"] + field["bitsize"] + 7)//8
                        for address in hits:
                            if base+offset <= address < base+end:
                                paths[address].append((depth+1, child))
                    else:
                        walk(field["type"], base+offset, child, depth+1, follow)

        # Static labels first; root paths win equal-depth ties below.
        for name, _ in dict.fromkeys(matches.values()):
            if name:
                walk(self.layouts.get(name), self.symbol_address(name), name, 0, False)
        for root in self.roots:
            base = self.symbol_address(root)
            if base is not None:
                walk(self.layouts.get(root), base, root, 0, True)
        labels = {}
        for address, (name, offset) in matches.items():
            label = f"{name}+{offset:#x}" if name else f"{address:#x}"
            candidates = paths[address]
            if candidates:
                depth = max(d for d, _ in candidates)
                # Unions/bitfields may legitimately name multiple overlapping fields.
                fields = list(dict.fromkeys(p for d, p in candidates if d == depth))
                label += " (" + " | ".join(fields) + ")"
            labels[address] = label
        return labels


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("addresses", nargs="+", type=lambda value: int(value, 0))
    parser.add_argument("--load-bias", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()
    mapper = FieldMap(args.binary, load_bias=args.load_bias)
    print(json.dumps(mapper.names(args.addresses), indent=2))
    for diagnostic in mapper.diagnostics:
        parser.exit(1, diagnostic + "\n")
