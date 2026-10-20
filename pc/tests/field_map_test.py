#!/usr/bin/env python3
"""CPU-only synthetic ELF/DWARF tests. See diff/README.md for two-lock invocation."""
import argparse
import pathlib
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from field_map import FieldMap
from diff.masks import MASKS, resolve

FIXTURE_SOURCE = r'''
struct Leaf { char tag; long value; int cells[3]; };
struct Node { char pad; struct Leaf leaf[2]; struct Node *next; };
struct Node nodes[2];
struct Node *roots[2];
struct Node *root;
union Choice { int integer; unsigned char bytes[4]; } choice;
struct Bits { unsigned a:3; unsigned b:5; unsigned rest:24; } bits;
int plain;
extern struct Leaf imported;
struct Leaf *use_import(void) { return &imported; }
int main(void) { return sizeof(struct Node) == 0; }
'''
BINARY = None


class FieldMapTests(unittest.TestCase):
    def mapper(self, **kwargs):
        return FieldMap(BINARY, **kwargs)

    def test_lp64_nested_array_and_padding(self):
        fm = self.mapper()
        self.assertEqual(fm.ptr, 8)
        base = fm.symbol_address("nodes")
        # Native LP64: Leaf size 32, Node leaf offset 8, next offset 72.
        labels = fm.names([base + 8 + 32 + 8, base + 8 + 32 + 16 + 8, base + 1])
        self.assertIn("nodes[0].leaf[1].value", labels[base+48])
        self.assertIn("nodes[0].leaf[1].cells[2]", labels[base+64])
        self.assertTrue(labels[base+1].endswith("(nodes[0])"))
        self.assertFalse(fm.diagnostics, fm.diagnostics)

    def test_zero_size_import_uses_dwarf_extent(self):
        fm = self.mapper()
        self.assertIn("imported.value", fm.names([0x70000008])[0x70000008])
        self.assertEqual(fm.names([0x70000080])[0x70000080], "0x70000080")

    def test_live_roots_nested_pointer_and_refresh(self):
        memory = {}
        reads = []
        def read(address, size):
            reads.append((address, size))
            return memory.get(address, 0).to_bytes(size, "little")
        fm = self.mapper(roots=["roots", "root"], read=read)
        base = fm.symbol_address("roots")
        # Heap addresses exceed 32 bits; second array slot is eight bytes away.
        first, second = 0x100001000, 0x100002000
        memory[base+8] = first
        memory[first+72] = second
        memory[second+72] = first  # cycle
        address = second+48
        self.assertIn("(*(*roots[1]).next).leaf[1].value", fm.names([address])[address])
        self.assertLess(len(reads), 20)
        self.assertTrue(all(size == 8 for _, size in reads))
        memory[base+8] = 0
        self.assertEqual(fm.names([address])[address], hex(address))

    def test_failed_and_short_reads(self):
        for reader in (lambda a, n: b"", lambda a, n: (_ for _ in ()).throw(OSError("unmapped"))):
            fm = self.mapper(roots=["root"], read=reader)
            self.assertEqual(fm.names([0x100001000])[0x100001000], "0x100001000")

    def test_limits_and_pointer_size(self):
        with self.assertRaises(ValueError):
            self.mapper(pointer_size=4)
        fm = self.mapper(max_depth=0)
        base = fm.symbol_address("nodes")
        self.assertTrue(fm.names([base+48])[base+48].endswith("(nodes)"))
        fm = self.mapper(max_nodes=1)
        self.assertIn("(nodes)", fm.names([base+48])[base+48])

    def test_typed_fixed_address_root(self):
        # A block addressed by number (the scratchpad): TYPE@ADDRESS names its fields.
        fm = self.mapper(roots=["Leaf@0x1f8000a0"])
        label = fm.names([0x1f8000a0 + 8])[0x1f8000a0 + 8]
        self.assertIn("Leaf@0x1f8000a0.value", label)
        self.assertIn("Leaf@0x1f8000a0.cells[1]", fm.names([0x1f8000a0 + 20])[0x1f8000a0 + 20])

    def test_union_and_bitfields(self):
        fm = self.mapper()
        base = fm.symbol_address("choice")
        # Both alternatives are valid; deepest byte-array field is selected.
        self.assertIn("choice.bytes[1]", fm.names([base+1])[base+1])
        base = fm.symbol_address("bits")
        label = fm.names([base])[base]
        self.assertIn("bits.a", label)
        self.assertIn("bits.b", label)
        self.assertNotIn("bits.rest", label)

    def test_relocation_and_unknown(self):
        fm = self.mapper(load_bias=0x100000000)
        base = fm.symbol_address("plain")
        self.assertIn("plain+0x1 (plain)", fm.names([base+1])[base+1])
        self.assertEqual(fm.names([0])[0], "0x0")
        self.assertEqual(fm.names([]), {})

    def test_masks_explicit_target_sized(self):
        symbols = {"D_800F53C0": 0x1234, "last_vsync": 0x5678}
        self.assertEqual(resolve(symbols), [])
        with self.assertRaises(ValueError):
            resolve(symbols, masks=MASKS)
        self.assertEqual(resolve(symbols, masks=MASKS,
                                 sizes={"D_800F53C0": 256, "last_vsync": 8}),
                         [(0x1234, 256), (0x801F8000, 0x8000), (0x5678, 8)])
        self.assertEqual(resolve({}, masks=MASKS), [(0x801F8000, 0x8000)])
        self.assertTrue(all(mask.reason for mask in MASKS))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-fixture", type=pathlib.Path)
    parser.add_argument("--fixture", type=pathlib.Path)
    args = parser.parse_args()
    if args.build_fixture:
        subprocess.run(["cc", "-m64", "-g", "-O0", "-fno-pie", "-no-pie", "-x", "c", "-",
                        "-Wl,--defsym,imported=0x70000000", "-o", str(args.build_fixture)],
                       input=FIXTURE_SOURCE, text=True, check=True, timeout=30)
    elif args.fixture:
        BINARY = args.fixture
        unittest.main(argv=[sys.argv[0]])
    else:
        parser.error("use --build-fixture PATH, then --fixture PATH")
