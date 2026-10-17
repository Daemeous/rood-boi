# Code ownership: owned game code in Zig beside the matched decompilation

Status: design, 2026-10-16. Companion plan: `2026-10-16-ownership-m0-plan.md`
(the foundation and the first trial, battle action execution).

## 1. Why, and what "owned" means

Today the PC game *is* the matched decompilation (`src/`) compiled natively,
with host behaviour added under `#ifdef ROOD_PC` (hooks in `src/`, `.inc`
files included into decomp units, bridges in `pc/src`). That was the right way
to get a faithful port, and it is now the main source of friction:

- every feature that changes game behaviour (the true charging skip, modern
  menus, fast travel, QoL) is a hook inside code we do not own and cannot
  restructure, guarded so `make commit-check` still says "All files match";
- the decomp's shapes (one 18 000-line `146C.c`, `unkXXX` fields, `D_800F19CC`)
  are the shapes our features must be written in;
- every upstream sync can collide with our hooks.

**Owned code** is code that the PC game runs instead of the original, written
by us for the PC, living in a new top-level `game/` directory, in **Zig**. It
starts as a faithful re-expression of an original subsystem, proven equal to
it by differential tests, and from then on it is ours to restructure and
extend. The original stays in `src/`, still matched, still built for the PS1
(`make commit-check`), still linked into the PC binary as the **oracle**: the
reference the owned code is compared with, function by function and tick by
tick.

The migration is the **strangler pattern**: one subsystem at a time is routed
to owned code; everything else keeps calling into the original. In the end
`game/` holds the whole game logic, `src/` is a frozen reference/oracle (built
for the PS1 and for tests, not part of the shipped PC game's behaviour), and
the `#ifdef ROOD_PC` hooks in `src/` are gone.

## 2. Architecture

```
src/        the matched decompilation (PS1 build + oracle objects in the PC build)
            never edited for ownership; ROOD_PC hooks are removed as subsystems move
game/       owned game code (Zig): game logic the PC game runs
  ownership.toml   the routing table: subsystems, their functions, default owner
  common/          psx helpers (guest pointers, bitfields, globals), routing/shadow, panic
  battle/          the BATTLE overlay's owned subsystems (one Zig object per overlay)
    action/        battle action execution (the first trial)
  menu/ ...        later: menu overlays, then input, saves, the rest
pc/         the platform: window, renderers, audio, input devices, RPC, dev tools,
            host-side features (C/C++), build (build_boot.py), tests, Lua runtime
pc/plugins  Lua mods (the extension system's M0): the user-facing extension language
```

Responsibilities:

| Layer | Owns | Language | Changes behaviour? |
|---|---|---|---|
| `src/` | the original game, bit-exact (oracle) | C (decomp) | never (only hook removal) |
| `game/` | the PC game's logic, subsystem by subsystem | Zig | yes, behind settings whose "original" value reproduces the oracle |
| `pc/` | platform, presentation, tooling, host features | C/C++/Python | presentation and host only; game-logic changes move to `game/` |
| `pc/plugins` | mods | Lua | through the API the owned modules expose (events, settings, registry) |

**Lua is the extension language; Zig is the "break glass".** Mods talk to
`game/` through the Lua runtime's events and registries. When something
cannot be done from Lua (performance, a deep change of a state machine), the
fix is a change to the owned Zig module itself, not a native plugin ABI. No
third-party native code is loaded (the Lua plan's `native` capability stays a
refusing stub).

### The one rule that keeps differential testing possible

**Every intentional behaviour change in owned code sits behind a setting whose
"original" value reproduces the oracle exactly** (the project's "tunable, not
hard-coded" rule). The differential runner sets every such setting to its
original value; with that, owned and original must agree byte for byte. The
true charging skip is the first example: with `qol_skip_charging 0` the owned
battle-action module is the original; with it on, it skips.

## 3. Language and toolchain: Zig 0.16.0, pinned

- **Version:** Zig **0.16.0** (the version already installed on the
  development machine; build-obj, translate-c, the targets below and the
  `@extern` relocation options were checked against it on 2026-10-16).
- **Obtained by** `pc/tools/zig_toolchain.py`: the official release tarball
  (`https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz`,
  SHA-256 `70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00`
  as observed on the local copy; the gated step cross-checks it against the
  official `index.json` and the release's minisign signature), extracted once
  into `${XDG_CACHE_HOME:-~/.cache}/rood-boi/zig/0.16.0/` (shared by all
  worktrees; never committed, never under `src/`). `ROOD_ZIG=<path to zig>`
  uses an existing install after checking `zig version`. The first fetch is a
  **gated step**: it needs the user's explicit OK (or `--from-tarball PATH`
  with a tarball the user already has, which is not a download).
- Zig's caches: the global cache next to the toolchain, a local cache per
  build tree (`pc/build/<tree>/zig-cache`). Both are outside the repository's
  tracked files; `.gitignore` also ignores `.zig-cache/` and `zig-out/`
  anywhere, in case someone runs `zig` inside `game/`.
- **Invocation:** `zig build-obj` from `build_boot.py`, not `build.zig`.
  `build_boot.py` owns the whole link (nm-based data imports, overlay-local
  renames, fail-fast traps, LP64 native imports, COFF twins); a Zig object
  must enter that pipeline like any other object. `build.zig` would add a
  second build graph for no gain. (A `build.zig` for `zig test` convenience
  is possible later; tests use `zig test` directly in M0.)

### Targets

| build_boot target | Zig `-target` | CPU | code model / PIC | notes |
|---|---|---|---|---|
| 32-bit (`rood_boot`, `--battle`) | `x86-linux-none` | `i686` (GCC `-m32`'s default: x87 floats, as the C objects) | `-fno-PIE -fno-PIC`: data at guest addresses is `R_386_32`, calls `R_386_PLT32` (what the import classifier expects) | x86-linux is a lower-tier Zig target; we only use LLVM codegen (`-fllvm`), the well-trodden path; the differential suite runs on 32-bit too |
| 64-bit Linux (`--arch 64`) | `x86_64-linux-none` | `x86_64` (baseline) | `-fPIE`; guest globals through `@extern(.., .relocation = .pcrel, .visibility = .hidden)`: `R_X86_64_PC32` exactly like GCC's `-fpie` objects; calls `R_X86_64_PLT32` | linked non-PIE at 0x400000 by gcc |
| Windows (`--target windows`) | `x86_64-windows-gnu` (COFF twin) + `x86_64-linux-none` (ELF twin for the analysis) | `x86_64` | pcrel `@extern`s give plain `IMAGE_REL_AMD64_REL32`; a plain `extern var` gives a `.refptr.` stub (verified), which the overlay-local renames cannot follow, so the build rejects any `.refptr.` in a Zig COFF object | the ELF twin is what nm/objdump/readelf/gdb read, as for C |

Common flags: `-O ReleaseSafe -fllvm -fsingle-threaded -fno-stack-check
-fno-omit-frame-pointer -ffunction-sections -fdata-sections -fno-compiler-rt`,
`-target <os>-none` on Linux (no libc, no OS layer: the object's undefined
symbols are exactly the game's and ours), and a root `panic` that calls
`pc_zig_panic` (C, prints `[zig] panic: <msg>` with the return address, then
the existing crash path). Without the custom panic the object pulls the
std's threaded I/O and grows to megabytes (measured: 2.6 MB vs 18 KB).

**ReleaseSafe everywhere**, shipped builds included: overflow, bounds and
`unreachable` trap with a report instead of silently corrupting guest memory.
The decomp is compiled with `-fwrapv`; owned code must say where it wraps
(`+%`, `-%`, `*%`, `@truncate`). A trap in owned code that the original
"survives" is a divergence the differential test catches.

Linking with GCC/MinGW objects: Zig objects use the platform C ABI
(`callconv(.c)`), so they link into the gcc (`-m32`, `-m64`) and MinGW
(`x86_64-w64-mingw32-g++-posix -static`) links unchanged (verified with a
probe on all three). 64-bit divisions on i386 (`__divdi3`) come from libgcc
as for C; `memcpy`/`memset` that LLVM emits come from the C library;
`___chkstk_ms` on Windows from MinGW's libgcc.

## 4. Typed access: what `@cImport` gives, and what it does not

The user's question was how to `@cImport` the decomp headers. Measured on
Zig 0.16.0 against this tree:

| Finding | Evidence | Consequence |
|---|---|---|
| translate-c rejects `-nostdinc` and fails on the SDK wrappers' `#include_next` (`pc/include/sys/types.h`) | "unrecognized parameter: '-nostdinc'"; "'sys/types.h' not found" | do not hand Zig the include paths; hand it **gcc's preprocessed text** (the exact `.i` build_boot already makes, with the vsString transform and `psx_types.h`) |
| bitfields demote a struct, and anything that embeds it, to `opaque {}` | declarations of `146C.c`: 70 types demoted, including `D_800F19CC_t` (the action record), `vs_action_t`, `vs_main_settings_t` (so `vs_main_settings` is a `@compileError`), `vs_battle_actor`, `D_800F4538_t`, `vs_battle_objectData` | translate-c cannot give us the state's layouts; generate them from DWARF instead |
| types and prototypes declared inside `.c` files (`D_800F19CC_t` in `146C.c`, `void func_800780A8(SVECTOR*);`) are invisible to a header import | `146C.c` declares ~500 prototypes and the action types itself | translate the **routed unit's own declarations** (its `.i` with function bodies stripped), not the headers |
| `psx_ptr(T)` (packed, aligned(4), zero-length `T*[0]`) translates to `extern struct { raw: c_uint, _psx_type_: [0]?*T ... }`; `psx_get`/`psx_set` (statement expressions, `__typeof__`) do not translate | probe | a comptime `psx.get()` in Zig replaces the macros |
| `_Static_assert`s translate to comptime checks, some on demoted types | probe | dropped from the generated module (the layouts are asserted from DWARF instead) |
| a plain `extern var` on `x86_64-windows-gnu` is reached through `.refptr.<name>` | probe | guest globals are reached only through generated accessors using `.relocation = .pcrel` + hidden visibility |
| `long`: the 64-bit game sees 8-byte longs (LP64; the Windows twins get `long long` via `lp64_longs()`), but Zig's `c_long` is 4 bytes on Windows | `Platform_Support.md` | the Windows build uses the 64-bit Linux translation with `c_long`/`c_ulong` rewritten to `c_longlong`/`c_ulonglong` |
| `@cImport` itself is on its way out of the language in favour of build-time translate-c | upstream direction | we never use `@cImport`; build_boot.py runs `zig translate-c` and passes the result as a module |

So "`@cImport` of the decomp headers" becomes three generated modules per
target, each from ground truth:

1. **`c`** (the `@cImport` equivalent): `zig translate-c -target <t>` over
   the routed unit's preprocessed text with function bodies stripped
   (declarations only: every typedef, prototype and extern the unit sees,
   including its `.c`-local ones). Flags are build_boot's `game_cpp_flags`
   for that target (`-nostdinc -Ipc/include -Iinclude/psx -Isrc/include -I.
   -D_LANGUAGE_C -DOBJDIFF -DROOD_PC -DROOD_PC_BOOT_DIAGNOSTIC -include
   pc/src/psx_types.h`, plus `-iquote <tree>/include -DROOD_PC_LP64` on 64-bit),
   applied by gcc, not by Zig. Used for **function prototypes and plain
   types**. Its extern variables are never referenced (Windows `.refptr`).
2. **`views`**: struct layouts, bitfields and globals generated by
   `pc/tools/zig_views.py` (gdb's Python API over the DWARF of the original
   object of the same build, as `import_types.py` already does for LP64
   imports). For every type in the subsystem's manifest it emits an `extern
   struct`/`extern union` with explicit padding, `align(1)` where the C field
   is misaligned, `psx.Ptr(T)` for `psx_ptr` fields, native `?*T` for real
   pointers, a `bitfields` table (byte, bit, width, signedness) for comptime
   accessors, and comptime asserts of every offset and the size against
   DWARF. Globals become accessors `v.g.D_800F19CC()` returning `*T` through a
   pcrel hidden `@extern`. DWARF is the layout the compiler actually used on
   that target (ILP32, LP64 with `psx_ptr`, the LP64 native moves), so the
   views are right by construction on all four builds.
3. **`ownership`**: the routing table as comptime data (from
   `game/ownership.toml`).

Hand-written refinements live in the module (`game/battle/action/types.zig`:
a `Phase` enum over `_cameraMode`, action kind ranges, named field aliases).
Pointers between the two worlds: `c` functions that take demoted types take
`?*c.T` (opaque); `v.cast(c.T, p)` converts a view pointer, checked at
comptime by type name.

`-funsigned-char`: DWARF says `char` is unsigned, so views use `u8`;
translate-c also maps `char` to `u8`. `signed char` is `i8` in both.

### LP64 and the native moves

In the 64-bit build, pointer-bearing data imports are defined natively
(`native_imports.c`, `rood_lp64_*` regions; `pc/docs/LP64_Audit.md`). The
views follow automatically (DWARF of the 64-bit object says 8-byte pointers
for `vs_battle_actors[]`; the symbol resolves to the native region). Two
build checks keep it that way:

- `lp64_native_imports()` reads declarations with gdb from the **C**
  objects only (Zig DWARF would confuse `import_types.py`); every data import
  a Zig object references must also be referenced by a C object of the same
  overlay, or the build stops ("declare it in C first").
- the Zig object's statics: owned modules keep **no mutable statics** (all
  state is guest memory, so save states, rewinds and overlay reloads cover
  it). The build rejects a nonzero `.data`/`.bss` in an overlay's Zig object;
  host-side counters live in `pc/src` C.

## 5. Routing: which object provides a symbol

### The table

`game/ownership.toml` (Python's `tomllib`; build_boot generates the Zig
comptime table and the trampolines from it):

```toml
[subsystem.battle_action]
overlay = "battle"                    # the Zig object: <overlay>_zig.o
unit = "src/BATTLE/BATTLE.PRG/146C.c" # the original unit that defines them
default = "original"                  # original | owned (flipped when proven)
roots = ["D_800F19CC", "vs_battle_actors", "D_800F4538"]   # for the field mapper
functions = [
  { addr = 0x80070F28, name = "func_80070F28" },          # commit
  { addr = 0x8007138C, name = "func_8007138C" },          # cast start
  ...
]
```

Functions are keyed by **PS1 address** (stable across upstream renames); the
name is checked against the reference ELFs (`function_addresses` in
build_boot) so an upstream rename fails the build with the new name.

### The mechanism (minimal, works on ELF and COFF)

For every routed function `F`:

1. **Original:** in the routed unit's preprocessed text (the `.i`, and the
   Windows twin's `.win.i`), the *definition* of `F` is renamed `F__orig`
   (a brace-depth scanner: identifier `F` at file scope, followed by a
   parameter list and then `{`; prototypes and calls are untouched; `static`
   definitions are refused). Calls inside the unit still name `F`, which the
   unit now only declares, so GCC emits external calls: they cannot be
   inlined, cloned (`.constprop`/`.isra`) or resolved locally. `src/` is not
   touched; this is a build step like the existing vsString transform.

   Why not object editing: `objcopy --weaken-symbol=F --add-symbol F__orig=...`
   redirects intra-object calls on ELF (verified), but not on COFF: MinGW's
   assembler resolves a call to a global function in the same object against
   the section symbol (`.text$F`), so the Windows build would silently keep
   calling the original (verified: "1 2 1 2" instead of "1 2 101 102"). And
   on any target GCC may have inlined `F` into its callers.
2. **Owned:** the overlay's Zig object exports `F__owned` (the owned
   implementation) and `F__shadow` (the differential wrapper, §7).
3. **Trampoline:** a generated `routing.c` (top-level `asm`, one block per
   ABI) defines `F`:

   ```
   F:  movzbl pc_owner+IDX(%rip), %r11d   ; i386: movzbl pc_owner+IDX, %eax
       cmpl $1, %r11d ; je F__owned
       cmpl $2, %r11d ; je F__shadow
       jmp F__orig
   ```

   A tail jump: arguments, return values and the stack are untouched, so one
   trampoline serves any signature on cdecl, SysV and Win64 (`%r11` is a
   scratch register that carries no argument; `%al` is left alone for
   variadic calls). Cost: one load, two compares, one jump per call.
4. **The switch:** `unsigned char pc_owner[]` in `pc/src/ownership.c` (host
   state: not in `rood_state_*`, so save states and rewinds do not change who
   owns what). Set from the table's defaults, `ROOD_OWNER=battle_action=owned,...`,
   RPC `owner`, and a developer-overlay combo. Values: 0 original, 1 owned,
   2 shadow (Linux only).

Build checks (fail fast, with the function's name): every routed function is
defined exactly once in its unit and renamed; nothing else defines `F`; the
Zig object defines `F__owned` and `F__shadow` for every routed `F`; the Zig
object's global symbols are plain identifiers; no `.refptr.` (COFF); no
nonzero `.data`/`.bss` in an overlay's Zig object; every data import the Zig
object uses is also used by a C object (LP64).

Overlay naming: an overlay's Zig object is named `<overlay>_zig.o`
(`battle_zig.o`), so `object_overlay()` gives it its overlay's data imports
(the overlay-local renames, `name__battle`), like the overlay's own C. A small
`game_core.o` (no guest imports) is linked into every build, including the
non-battle 32-bit `rood_boot`, so all four targets exercise the toolchain.

Switching owners is safe at any frame boundary because owned code keeps its
state in guest memory, like the original: the differential suite tests this
directly by alternating the owner every tick.

## 6. Finding a subsystem's boundary: the call-graph and state mapper

`pc/tools/subsystem_map.py` answers "what is this subsystem, and what does it
touch", before any code is written:

- **Source analysis** with clang's JSON AST over the unit's preprocessed `.i`
  (the same inputs and clang flags as `lp64_audit.py`): per function, its
  callees, every global it reads and writes with the field path
  (`D_800F19CC->unk854[].actionIndex` read, `_cameraMode` written,
  `vs_main_scoredata.healCount` read-modify-write), and address-taken uses.
- **Object cross-check**: the relocations of each `.text.F` section of the
  compiled object (`-ffunction-sections`), which catch whatever macros and
  `.inc` files add.
- **Closure** from seed functions, following calls inside the unit, stopping
  at `boundary` functions that stay original (actor animation, the effect
  loader, the sound driver).

Output (`pc/build/<tree>/map/<subsystem>.json` and a Markdown summary): the
routed set; **entry points** (functions of the set called from outside it:
the trampolines that matter, and whether any takes a pointer to a caller's
stack, which shadow mode cannot see); the **extern list** (callees the module
needs from `c`); the **state**: globals read and written, with field paths
(the views manifest); globals written by the set and read outside it (its
interface); and `static` functions in the closure (they must be reimplemented
or kept as externs). The mapper's state list is the views manifest: mapper →
manifest → views → Zig.

## 7. Differential testing

Two complementary modes, Linux (32 and 64-bit). One game process per
scenario (a fast resume of a checkpoint save state); every run of that
scenario happens in it, through an in-memory mark and rewind.

### 7.1 Call mode (shadow; pinpoints the first wrong call)

With `pc_owner[sub] = 2`, each top-level call to a routed function goes to
`F__shadow` (generated in Zig at comptime from `F`'s signature: a function
with the same parameters and return type):

1. capture the full game state S (guest RAM, scratchpad, I/O page, the
   native game statics `rood_state_data/bss`, overlay statics, VRAM if it is
   not inside those, the SPU state);
2. fill the dead host stack below the call with pattern P1 (a leaf asm
   routine, so nothing is live there), route the subsystem to *original*, call
   `F__orig` → capture A1; restore S;
3. fill with pattern P2, call `F__orig` again → capture A2; restore S;
4. fill with P1, route to *owned*, call `F__owned` → compare the live state
   with A1, **ignoring the bytes where A1 and A2 differ** (they derive from
   uninitialised stack: the double oracle finds that noise automatically,
   e.g. the never-written fields of the effect request `func_8007138C`
   copies into `D_800F53C0`); compare the return values;
5. restore A1 and continue on the **oracle's timeline**: every call is
   judged from a correct state, so one divergence never cascades.

Nested routed calls during a run follow the run's side (the route byte is 0
or 1 while a run is in progress). Mismatches go to a ring buffer: call index,
vsync, function, range, first differing address, byte count and up to 16
differing runs. RPC `owner {action: "report"}` returns them; the runner maps
addresses to symbols (`nm` of the binary, guest imports included) and to
**fields** (`pc/tools/field_map.py`: gdb walks the DWARF type of the symbol,
or of the root pointer from `roots` that points at the address, to
`D_800F19CC->unk854[1].unk4.limbs[2]`).

Limits, stated: host-side effects (renderer uploads, log lines, audio
output) happen three times and are not compared; entry points that take a
pointer into the caller's stack are flagged by the mapper (none in the trial).

### 7.2 Tick mode (whole runs; proves equivalence over time)

1. Resume the checkpoint save state, run the scenario's setup (RPC:
   warp, MP, learn a spell; deterministic, owner original) and **mark** the
   state in memory (RPC `snapshot {action: "mark"}`: the existing snapshot
   machinery, kept in RAM: guest memory, statics, SPU, fog, the game stack
   and registers).
2. owner = original; start recording input (RPC `record`) and play the
   scenario's presses; then `digest {action: "record", ticks: N}`: N vsyncs,
   and after each one a 64-bit hash per 4 KB page of every state range (masked
   bytes zeroed first), kept in the game.
3. **Rewind** (RPC `snapshot {action: "rewind"}`: restore the mark in place;
   the restore runs on a side stack, then jumps back into the mark's context,
   as the file-based resume does in a new process); owner = owned; replay
   the recording (RPC `replay`, `input_record.c` seeks to the vsync);
   `digest {action: "compare", ticks: N}` reports the **first differing tick**
   and the differing pages.
4. On a difference: rewind, run each owner to that tick and save both states
   to files (the existing `snapshot save`); `snapshot_diff.py` with the masks
   and the field mapper name the region and field.
5. Variants: **alternating** owners every tick (proves owner switches at
   frame boundaries are safe), and **skip-on** runs (§8).

Known noise, in `pc/tests/diff/masks.py` with reasons: the effect request
copy `D_800F53C0` (0xE8 bytes, stack garbage; call mode handles it by the
double oracle, tick mode by the mask), the dead guest stack
`0x801F8000..0x80200000`, the host statics that hold wall time
(`last_vsync`), and the game stack below the stack pointer (never compared).
`ROOD_DETERMINISTIC=1` (virtual scanlines), `ROOD_GI=0`, no audio device,
fixed settings (a scratch `settings.cfg`).

Host state that feeds the game must be the same at the mark and at the end
of each run: the runner asserts the modern menus are closed at both points
and runs with a fixed settings profile. Everything else the game reads lives
in the rewound state.

**Speed:** one process per scenario (no relaunch between its runs), in-memory
rewind (milliseconds), digests computed in the game (no per-tick RPC
round-trip), `--fast` pacing, and file states only for the first difference.

### 7.3 The corpus

`pc/tests/diff/scenarios.py`: each scenario is a setup (RPC, from the
`save_room` checkpoint of the pinned test card) and a list of presses. For
battle actions: Solid Shock on an enemy (spell), a Cure Root on Ashley (item),
a Break Art, a plain attack (an action without an effect), a chain ability
in the attack's window, an enemy's spell (caster ≠ 0), and a cancelled target
selection (the commit's early-return paths). The owned module counts which
of its branches ran (a coverage bitset, RPC `owner {action: "coverage"}`);
the suite fails if a corpus run leaves a listed branch unvisited. Nothing
from the game's data is committed: scenarios are Python, recordings and
states are made per run under `pc/build/`.

## 8. The first trial: battle action execution

### What the research says

From `146C.c` and the charge-true-skip work (`pc/src/charge_skip.c`/`.inc`):
an action with an effect is **committed** in `func_80070F28` (costs, the item
used, hits/damage/statuses rolled, score counted; sets `_cameraMode` 4).
`vs_battle_exec` then runs one phase per vsync while BATTLE's state
`D_800F196C` is 2:

| `_cameraMode` | Function | What happens |
|---|---|---|
| 3 | `func_80078364` | target/ability confirmation; calls the commit |
| 4 | `_handleCombatCameraZoom` | camera converges on caster and target (three zoom styles by `unk2C0E`); when converged calls `func_8007138C` |
| — | `func_8007138C` | starts the cast: animation + turn, cast sound, event flags 0xB7/0xB9, the effect request (`func_800CF0E8`, built on the stack; two original bugs kept), Break Art weapon data, HUD out, name window; sets 5 |
| 5 | `func_80078748` | waits for the name window, the cast loop boundary, object data slot 0, the effect's files; `func_800704B0` sets 6 |
| 6 | `func_800787F0` | waits for the cast flash (`D_800F1868 & 2`); `func_80070278` releases (sets 7) and `func_800CEF74` spawns the effect |
| 7 | `func_80078AB4` | the wind-down: chain/defense ability timing window, then back to the field |
| 8 | `func_80079030` | re-cast (calls `func_8007138C`) |

The wind-up (4–6) decides nothing: the outcome was rolled at commit. That is
why the true skip is safe.

### The owned module

`game/battle/action/` — a state machine over the phases, all state in guest
memory (the original's globals, through views):

```
action.zig    Phase enum (_cameraMode), the routed entry points (exports)
commit.zig    func_80070F28: the commit (calls the hit-rolling helpers, original for now)
cast.zig      func_8007138C: cast start; buildRequest() is a pure function (unit-tested, bugs kept)
zoom.zig      _handleCombatCameraZoom: the three zoom styles
release.zig   func_80078748, func_800704B0, func_800787F0, func_80070278
winddown.zig  func_80078AB4, func_80079030
skip.zig      the true skip: policy (pure, unit-tested) and the converge/release steps
coverage.zig  the branch bitset
```

Routed (entry points): `func_80070F28`, `func_8007138C`,
`_handleCombatCameraZoom`, `func_80078748`, `func_800704B0`, `func_800787F0`,
`func_80070278`, `func_80079030`, `func_80078AB4`. Their helpers (hit
rolling `func_800708EC`/`func_80070CAC`/`func_80070B04`, camera steps
`func_8007820C`/`func_800780A8`/`_computeStepAndDisplacement`, actor
animation, the effect loader) stay original and are called as externs; the
mapper's output decides the next ones to move.

### The true skip, natively

Settings, read through the existing registry variables
(`pc_qol_skip_charging`, `pc_qol_skip_charging_who` in
`project_settings.cpp`; `pc_charge_skip_kinds` in `charge_skip.c`):

- `skip.applies(action, caster)`: the action has an effect
  (`vs_main_actions[i].unk2_0`), `qol_skip_charging` is on, the caster is
  Ashley or `qol_skip_charging_who` is 1, its kind (spell/art/0xB8–0xDF/item)
  is in the kinds mask, and no event script runs (`D_800F4C34 == 0`);
- phase 4: run the camera's per-vsync steps until it converges (bounded),
  in one vsync, with the name window left out (the Information option off for
  that call), then stop the cast sound and apply the turn at once;
- phase 5: release as soon as the effect's files are in and the object data
  is placed.

The host side stays in `pc/src/charge_skip.c` (which vsyncs are hidden, the
temporal-filter cut, counters, the RNG pin); it reads the game side through
`pc_charge_skip_state`, which the Zig module now exports with the same C
signature.

**Transition**, so the skip is proven too: while both exist, the hook in
`vs_battle_exec` (`pc_charge_skip(phase)`) only acts when the subsystem's
owner is *original*; the owned module skips natively. Tick mode with the skip
on compares original+hook against owned: identical states every tick. Then
the hook lines and `charge_skip.inc` are deleted (`146C.c` returns to the
upstream text there), the owner default flips to owned, and with owner
original the skip is unavailable (the setting is greyed with that reason).

## 9. Process rules

- `src/` is never edited by ownership work, except **removing** `ROOD_PC`
  hooks whose behaviour moved to `game/`. `make commit-check` (in the
  `rood-reverse-dev:local` image) must print "All files match" before every
  merge. The PS1 build is unaffected: `game/` and `pc/` are not in its
  Makefile.
- All four PC targets build and link after every task that touches the
  build: `build_boot.py`, `--battle`, `--arch 64`, `--target windows`.
- Thermal: every build and test runs under `flock "$ROOD_HEAVY_LOCK" ...`;
  test suites at `-j 2`; never artificial CPU load. The differential runner
  is one game process.
- Never commit game data: corpus states, recordings, digests, zig caches live
  under `pc/build/` (ignored). `game/` holds only `.zig` and `.toml` sources.
- The publish leak scan: no absolute home paths, no private hosts, no
  denied names in committed files.

## 10. Migration process (per subsystem)

1. **Map** it (`subsystem_map.py`): entry points, externs, state, boundary.
2. **Table** it (`ownership.toml`, default original) with a pass-through
   owned stub (each `F__owned` calls `F__orig`): the routing is proven
   (shadow mode reports nothing) before any logic moves.
3. **Corpus**: scenarios that reach every branch the mapper lists.
4. **Port** function by function, smallest first, each one test-first: shadow
   mode on the corpus reports the function before (stub) and must report
   nothing after; pure parts get `zig test` unit tests.
5. **Prove** over time: tick mode, alternating owners, both architectures.
6. **Re-express** the PC features that were hooks as owned code behind
   settings; prove the "original" setting values still equal the oracle, and
   the feature equals its hook implementation where one existed.
7. **Flip** the default to owned; remove the hooks from `src/`.
8. From then on the owned code is ours: restructure freely; every change is
   checked against the oracle with its settings at original values.

### Order after battle actions

1. **Battle actions** (M0, this trial).
2. **Menus** (MAINMENU, MENU0–F): the modern menus' bridges become owned
   code instead of hooks; the translate-c experiment decides whether whole
   menu overlays are translated mechanically first and cleaned up after.
3. **Input** (pad reading, button roles, the camera stick): the remapping
   and QoL hooks become owned.
4. **Saves** (memory card, `memory_card.c` paths, the save/load menus).
5. **The rest**, by the mapper's coupling: camera, field/room logic, actors
   and AI, effects (PLGs), events/scripts, the sound driver's game side;
   finally `main.c`'s loop.

### The translate-c path

For large, low-risk, UI-heavy code (menus), a mechanical first step may be
cheaper than hand-porting: `zig translate-c` over a whole overlay unit,
routed like any subsystem, proven by the same differential tests, then
cleaned up incrementally. A read-only pilot on MENU1 (the Break Arts menu, 6
functions) with Zig 0.16.0: translation succeeds; 3 of 6 functions are
"demoted to extern" (bitfield member access); the output does not compile
as-is (block-scope `extern` declarations become unused local structs); file
statics become Zig globals. So the path needs: views-backed bitfield access
(hand-fixing the demoted functions), a normalisation pass on the
preprocessed C (hoist block-scope externs), and the overlay's data left in
the C object (statics globalised and referenced from Zig, so both
implementations share the same state). The M0 plan's last, optional task
runs this experiment and reports quality and whether the differential tests
prove it.

## 11. When upstream syncing ends

While subsystems are still original, upstream syncs bring real value (names,
types, struct layouts, matched functions) and the generated `c`/`views`
modules pick it up. Proposal:

- A subsystem's original is **frozen** when its default flips to owned:
  later upstream changes to those functions still sync (the oracle must stay
  matched), but they no longer change the PC game. A sync that renames a
  routed symbol fails the build with the new name (the table is keyed by
  address); views regenerate from DWARF, and Zig code that used a renamed
  field fails to compile (loudly, not silently).
- Upstream syncing **ends** when every subsystem defaults to owned and
  `main.c`'s loop is owned: `src/` is then pinned at a final upstream tag
  (the oracle), still built for the PS1 and for the differential suite.

## 12. Risks

| Risk | Mitigation |
|---|---|
| Zig churn (0.16 → 0.17 breaks syntax, translate-c moves to a package) | pinned version; upgrades are deliberate tasks with the full suite; we use only build-obj, translate-c CLI, `@extern`, `@export`, comptime reflection |
| i386 is a lower-tier Zig target | LLVM backend only; differential and full suites on 32-bit; the 32-bit build may later be retired in favour of 64-bit |
| A call to a routed function bypasses the trampoline (inlining, local resolution, a function pointer taken before routing) | the definition rename makes every call external; build checks; the routing probe exercises intra-unit calls and function pointers on ELF and COFF |
| Rewind restores game state but not host state (UI, renderer caches, input replay cursor) | explicit resyncs (renderer, CD, input replay, fog), the runner's start/end assertions, a rewind-equivalence test that runs first |
| Differential blind spots: host side effects, untested branches, nondeterminism | coverage bitset with required branches; deterministic scanlines; double-oracle noise detection; masks with reasons |
| Owned code diverges on purpose (features) | the settings rule: original values reproduce the oracle; the runner pins them |
| Merge friction with branches editing `146C.c` (charge-true-skip, menus) | the trial starts after charge-true-skip merges; the ownership infra touches no `src/` file |
| Windows: no save states, so no shadow/tick mode there; CodeView debug info | equivalence is proven on Linux 32/64 from the same Zig source; Windows builds, links and runs the smokes |
| Build time | Zig's own cache per tree; views generation cached by the object's content; one Zig object per overlay |
| ReleaseSafe traps in shipped builds | a trap is a bug the oracle did not have; it reports and stops, as `pc_unimplemented` does today |

## 13. Open questions for the user

1. **Zig pin:** 0.16.0 (the version on this machine). For the gated step,
   may the plan use the tarball you already have (by path) instead of
   downloading?
2. **Flip default:** once the differential suite passes on both
   architectures, flip `battle_action` to owned by default and delete the
   charge-skip hooks from `src/`? (With owner original, the skip setting is
   then unavailable.)
3. **Where the owner switch lives:** developer overlay + RPC + `ROOD_OWNER`
   only (proposed), or also a settings line?
4. **Scope of the trial:** include the wind-down (`func_80078AB4`, the
   chain/defense timing window, ~200 lines) in M0, or stop at release
   (phase 6) and commit?
5. **Upstream policy:** freeze a subsystem's original at its flip and end
   syncing when `main.c` is owned (proposed), or stop syncing earlier?
6. **Suite cost:** the differential suite takes minutes; part of the default
   `run.py` set, or opt-in (`run.py diff`) plus a pre-merge requirement for
   ownership branches?
7. **Names:** exports keep decomp names (`func_80070F28__owned`, required by
   routing), internal Zig names are descriptive (`commitAction`). Should
   discovered names also go upstream?
8. **ReleaseSafe** in shipped builds (traps with a report) rather than
   ReleaseFast (undefined behaviour on overflow)?
9. **Lua events** from the owned battle module (`battle.action.committed`,
   `battle.action.released`): M1 (proposed) or in M0?

## 14. Effort (agent time)

Foundation (toolchain, routing, views, mapper, differential runner): about
2–3 agent-days. The battle-action port and the native skip: about 1–2
agent-days. The translate-c experiment: half a day. Each later subsystem:
1–3 agent-days depending on size and coupling, mostly corpus building.
