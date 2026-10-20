# Differential address labels and noise candidates (ownership task 19)

`pc/tools/field_map.py` uses the **target ELF**, `nm`, and GDB's DWARF types.
It does not attach to or execute the target. Example:

```python
from field_map import FieldMap
fm = FieldMap(binary, roots=ownership_roots, read=game.read)
labels = fm.names(changed_addresses)
```

Roots are the symbol names in `game/ownership.toml` (or the caller's selected
STATE roots). Read callbacks return exactly the requested bytes or raise an
`OSError`, `ValueError`, or `RuntimeError` for unmapped memory. Pointer bytes
are refreshed on every `names()` call; types are cached. Run against a paused
snapshot so the supplied reader sees consistent state. Heap traversal includes
pointer arrays and nested pointer fields, uses target widths/byte order and
DWARF offsets/strides, and stops on cycles, null/short/failed reads, depth 12,
or 10,000 visited nodes by default. A pointer without a declared array bound
names **one** pointee, not an invented allocation extent. Traversal truncation
may leave a less-specific symbol or hexadecimal label; this is diagnostic
metadata, never proof that an address is unimportant.

For PIE, pass the runtime `load_bias`; addresses otherwise match the linked ELF.
Zero-sized linker imports gain extents from DWARF where available; unknown
addresses must not be claimed by an arbitrary preceding zero-size symbol.
Missing/stripped DWARF preserves symbol+offset labels; process failures are
available in `fm.diagnostics`. Local symbol-name collisions remain ambiguous:
use unique/globalised state symbols for roots. This tool is ELF-only for now.
Padding names its enclosing object. Overlapping bitfields may yield multiple
paths; unions prefer the deepest available path (not a claim about which arm
is active). No target memory contents or game-derived layout exports are saved.

## Noise is not an automatic exemption

`masks.py` records the three candidates from task 19, **not an enabled default**.
`resolve(symbols)` returns no masks. Select candidates explicitly and provide
verified target sizes for symbolic masks:

```python
resolve(symbols, masks=selected_masks, sizes=verified_target_sizes)
```

The plan's literal 0xE8 effect size and 16-byte timestamp are not portable ABI
facts. Native LP64 structures differ from PSX layouts. In addition, the binary
contains several file-local `last_vsync` variables of different types/sizes:
a plain `nm` name dictionary is insufficient to select boot_runtime.c's
`struct timespec`. Supply its verified address and size, or omit that mask.

The effect request candidate includes **meaningful fields** as well as possible
unwritten bytes. Masking its entire object could hide a broken port. Likewise,
no fixed guest-stack tail is proven dead for every corpus scenario. These broad
candidates require scenario-specific evidence before enabling; prefer narrower,
reviewed byte ranges and the shadow backend's two-original-run noise detection.
Do not silently reuse these candidates in shadow comparison. Reports should
retain selected masks and reasons alongside any pass/fail result.

## CPU verification and runner integration

The synthetic fixture contains no game data, requires Linux `cc`, `nm`, and GDB
with Python, and explicitly tests LP64 (including heap pointers above 4 GiB).
Build and test use separate, bounded lock holds:

```sh
fixture=$(mktemp)
flock -w 120 "$ROOD_BUILD_LOCK" timeout 45 \
  python3 pc/tests/field_map_test.py --build-fixture "$fixture"
flock -w 120 "$ROOD_HEAVY_LOCK" timeout 90 \
  python3 pc/tests/field_map_test.py --fixture "$fixture"
rm -f "$fixture"
```

CMake's `field_map_fixture_build` target builds the fixture during Ninja (under
the build lock); the `field_map` CTest only reads that fixture (under the suite
lock). The runner tracks the mapper, test and masks for changed-test selection.
The test checks arrays, native padding/long widths, absolute guest
imports, nested live root pointers, cycles, refreshed/null/failed reads,
recursion budgets, unions/bitfields, load bias, unknown addresses, and explicit
ABI-sized mask resolution. It does not prove real-game root completeness or
that any noise candidate is safe for a particular corpus.
