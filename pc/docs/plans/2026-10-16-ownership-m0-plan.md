# Code ownership Milestone 0: the Zig foundation and owned battle actions — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Give rood-boi a foundation for game code we own, in Zig, in a new top-level `game/` directory, beside the matched decompilation: a pinned Zig toolchain on all four build targets, per-subsystem routing between the original (`src/`) and owned implementations with both linked in, typed access to game state generated from the original objects, a subsystem mapper, and a differential test runner that proves owned == original call by call and tick by tick from save states in one game process. Then use it for the first trial: battle action execution (`_cameraMode` 4–8, the commit and the release, `146C.c`), ported by hand, with the true charging skip re-expressed natively and the `ROOD_PC` hooks from `charge-true-skip` removed from `src/`. Last, optionally, judge `zig translate-c` on one whole menu overlay.

**Architecture:** `game/ownership.toml` lists subsystems and their functions (keyed by PS1 address). `build_boot.py` renames each routed function's *definition* in the original unit's preprocessed text to `F__orig` (calls keep `F`), links a generated asm trampoline `F` that tail-jumps to `F__orig`, `F__owned` or `F__shadow` by a per-subsystem byte `pc_owner[]` (`pc/src/ownership.c`), and compiles one Zig object per overlay (`battle_zig.o`) with `zig build-obj` into the existing nm/objcopy pipeline (ELF and COFF twins). Zig sees the game through three generated modules: `c` (`zig translate-c` of the unit's declarations, from gcc's preprocessing), `views` (struct layouts, bitfields and globals from the original object's DWARF, via gdb) and `ownership` (the table, comptime). The differential runner (`pc/tests/diff/`) drives one game over RPC: in-memory mark/rewind of the full game state, input record/replay, per-tick page digests computed in the game, and a shadow mode where every routed call runs original twice and owned once from the same state and is compared (the double oracle masks uninitialised-stack noise).

**Tech Stack:** Zig 0.16.0 (pinned, official tarball, SHA-256), Python 3.12 (`tomllib`, gdb's Python API, clang JSON AST), GCC 13 (`-m32`, `-m64`), MinGW-w64 GCC 13 (posix), GNU binutils, gdb, clang 18, CMake/CTest, the dev RPC (`pc/src/dev_rpc.c`, `pc/tools/rood_rpc.py`), save states (`pc/src/snapshot.c`), `input_record.c`, `pc/tests/run.py`.

Design: `2026-10-16-ownership-design.md` (copy it to `pc/docs/plans/` in Task 1).

---

## Decisions this plan makes (beyond the brief)

| Decision | Why |
|---|---|
| **Zig 0.16.0**, fetched by `pc/tools/zig_toolchain.py` into `${XDG_CACHE_HOME:-~/.cache}/rood-boi/zig/` (shared by worktrees), `ROOD_ZIG` for an existing install, `--from-tarball` for a tarball already on disk | The version on the development machine; checked against it: build-obj for the three targets, translate-c, `@extern` relocation options. A cache outside the repository is never committed and survives `pc/build` wipes. |
| **`zig build-obj` called by `build_boot.py`**, not `build.zig` | The objects must enter build_boot's symbol pipeline (data imports, overlay-local renames, traps, LP64 moves, COFF twins); a second build graph would add nothing. |
| **Targets** `x86-linux-none -mcpu=i686 -fno-PIE -fno-PIC` (32-bit), `x86_64-linux-none -fPIE` (64-bit and the Windows ELF twin), `x86_64-windows-gnu` (COFF twin); `-O ReleaseSafe -fllvm -fsingle-threaded -fno-stack-check -fno-omit-frame-pointer -ffunction-sections -fdata-sections -fno-compiler-rt`; a root `panic` calling `pc_zig_panic` | Verified on 2026-10-16: relocations match GCC's (32-bit `R_386_32` data / `R_386_PLT32` calls; 64-bit `R_X86_64_PC32` / `R_X86_64_PLT32`; COFF `REL32`), the objects link into gcc and MinGW links. Without the custom panic and `-none` OS the object pulls the std's I/O (2.6 MB vs 18 KB). |
| **Guest globals only through `psx.global()`** (`@extern` with `.relocation = .pcrel, .visibility = .hidden`) | A plain `extern var` on `x86_64-windows-gnu` goes through `.refptr.<name>` (verified), which the overlay-local renames cannot follow. The build rejects `.refptr.` in Zig COFF objects. |
| **Routing by renaming the definition in the preprocessed text** (`F` → `F__orig` where it is defined; calls keep `F`), asm tail-jump trampolines `F` | Object-level weakening redirects intra-object calls on ELF but **not on COFF** (MinGW resolves them against `.text$F`; verified), and GCC may inline `F` into its callers. With the definition renamed, every call to `F` in the unit is external: no inlining, no clones, the same on all targets. A tail jump serves every signature. `src/` is untouched. |
| **Tables keyed by PS1 address** with the name checked | Upstream renames fail the build with the new name instead of silently unrouting. |
| **One Zig object per overlay, named `<overlay>_zig.o`**, plus `game_core.o` in every build | `object_overlay()` gives `battle_zig.o` BATTLE's overlay-local data imports, like the overlay's C; the core object makes even the non-battle `rood_boot` build exercise the toolchain. |
| **Owned modules keep no mutable statics** (build check: no nonzero `.data`/`.bss` in an overlay's Zig object) | All state stays in guest memory: save states, rewinds, overlay reloads and the differential comparison cover it; owner switches are safe at any frame boundary. Host-side counters (coverage, diagnostics) live in C. |
| **Types: `c` = translate-c of the routed unit's declarations** (its `.i` with bodies stripped, from gcc's preprocessing), **`views` = layouts from DWARF**, never `@cImport` | Measured on 0.16.0: translate-c rejects `-nostdinc`, fails on `#include_next`, demotes 70 types of `146C.c` to `opaque` (bitfields: the action record, `vs_action_t`, `vs_main_settings_t`, actors), and headers lack the `.c`-local types and prototypes. DWARF is the layout GCC used on that very target (ILP32, LP64 with `psx_ptr`, the native moves). |
| **Windows `c` module = the 64-bit Linux translation with `c_long`→`c_longlong`** | The Windows twins see Linux's 8-byte `long` (`lp64_longs()`); Zig's `c_long` is 4 bytes on Windows. |
| **ReleaseSafe everywhere**; wrapping spelled out (`+%`, `psx.conv`) | The decomp uses `-fwrapv`; owned code says where it wraps, and every other overflow traps with a report instead of corrupting memory. |
| **Shadow mode = original twice (two dead-stack patterns) + owned once, continue on the original's result** | The bytes where the two original runs differ are derived from uninitialised stack (e.g. the effect request copied to `D_800F53C0`): masked automatically, per call. Continuing on the oracle's state means one divergence never cascades. |
| **In-memory mark/rewind** reusing `snapshot.c`'s ranges and register context, restored from a side stack | "One game instance": milliseconds per run instead of a process launch, and the same machinery the file-based resume already proves. |
| **Per-tick digests in the game** (64-bit FNV per 4 KB page, masks zeroed) | No RPC round-trip per tick; the first differing tick and pages come back in one reply; full state files only for the first difference. |
| **The intentional-change rule**: every behaviour change in owned code sits behind a setting whose original value reproduces the oracle; the runner pins them | The only way differential testing survives features; the true skip is the first case. |
| **The skip moves in two steps**: the hook acts only for owner original, the owned module skips natively, tick mode proves them equal with the skip on; then the hooks go | The skip itself gets an oracle (its hook) before the hook is deleted. |

## Ground rules for every task

- Work in a git worktree (Task 1), never build in the main checkout.
- Every build and test runs under `flock "$ROOD_HEAVY_LOCK" ...` (a lock shared by all agents; never a literal temp path in committed files); test suites at `-j 2`. Never generate artificial CPU load.
- `src/` is not edited, except Task 29 **removing** the charge-skip `ROOD_PC` hooks. `make commit-check` must print "All files match" before the merge request (Task 30).
- Never commit game data (`data/`, `disks/`, `build/`, frames, saves, save states, recordings, digests, `pc/user/`, Zig caches). Corpus files live under `pc/build/diff-corpus/`.
- Committed files never mention other game ports by name, never contain absolute home-directory paths, private hosts or personal names (the publish leak scan, `pc/tools/publish.sh`).
- The Zig download (Task 3) needs the user's explicit OK in chat. No other task downloads anything.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Windows/MinGW: host C units get `pc/src/win_compat.h` force-included; no new identifier may be a Windows macro (`DELETE`, `ERROR`, `IN`, `OUT`, `near`, `far`, `small`, `interface`). Save states, rewind and shadow mode are Linux-only (`snapshot.c`'s `_WIN32` stubs); Windows builds, links and refuses owner mode `shadow`.
- Zig: guest globals and host C globals are reached only through `psx.global`/`psx.globalMany` (or the generated `v.g.*` accessors); calls through `c.*`. No `extern var` in `game/`.
- Merge `main` before the merge request; `146C.c` and `pc/src/charge_skip.*` are shared with the charge-skip work: this plan starts after `charge-true-skip` is merged (Task 1 checks).

## File structure

Created:

| File | Responsibility |
|---|---|
| `game/ownership.toml` | Subsystems: overlay, original unit, default owner, field-mapper roots, routed functions (address + name), views manifest. |
| `game/common/common.zig` | The `common` module: re-exports `psx`, `route`, `Panic`. |
| `game/common/panic.zig` | `Panic = std.debug.FullPanic(fail)` → `pc_zig_panic`. |
| `game/common/psx.zig` | Guest pointers (`Ptr(T)`, `get`), bitfields (`getBits`/`setBits`), C integer conversion (`conv`), `global`/`globalMany`, `cptr` (view → `c` pointer, comptime-checked), raw index (`at`). Unit tests. |
| `game/common/route.zig` | Comptime export of `F__owned` and `F__shadow` for every routed function of a subsystem; the shadow wrapper generated from `F`'s signature. |
| `game/common/settings.zig` | The settings registry variables owned code reads (`pc_qol_skip_charging`, `pc_qol_skip_charging_who`, `pc_charge_skip_kinds`). |
| `game/core/root.zig` | `game_core.o`: the ABI version and the panic; linked in every build. |
| `game/battle/root.zig` | `battle_zig.o`: BATTLE's owned subsystems; exports. |
| `game/battle/action/types.zig` | `Phase` (`_cameraMode`), the current action, kinds, `hasEffect`. |
| `game/battle/action/release.zig` | `func_80078748`, `func_800704B0`, `func_800787F0`, `func_80070278`. |
| `game/battle/action/zoom.zig` | `_handleCombatCameraZoom`. |
| `game/battle/action/cast.zig` | `func_8007138C`; `buildTargets` (pure over an environment; unit-tested, the two original bugs kept). |
| `game/battle/action/commit.zig` | `func_80070F28`, `func_80079030`. |
| `game/battle/action/winddown.zig` | `func_80078AB4` (the chain/defense timing window). |
| `game/battle/action/skip.zig` | The true charging skip: policy (pure, unit-tested), phase 4 converge, phase 5 early release; exports for `charge_skip.c`. |
| `game/battle/action/coverage.zig` | Branch ids; writes the host coverage bitset. |
| `game/battle/action/tests.zig` | `zig test` entry for the module's pure parts. |
| `pc/tools/zig_toolchain.py` | The pin, verification, unpacking, `ensure()`. |
| `pc/tools/ownership.py` | Reads/validates the table; `rename_definitions`, `strip_bodies`; generates `routing.c` and `ownership.zig`. |
| `pc/tools/zig_views.py` | gdb dump of types/globals (DWARF) → Zig `views` module. |
| `pc/tools/zig_c.py` | Decls-only translate-c → the `c` module (post-processed). |
| `pc/tools/subsystem_map.py` | Call graph and globals read/written (clang AST + relocations) → JSON/Markdown, views manifest. |
| `pc/tools/field_map.py` | Address → `symbol.field[i].field` via gdb and the roots. |
| `pc/src/ownership.c` | `pc_owner[]`, defaults/env/RPC API, `pc_zig_panic`, coverage bitset. |
| `pc/src/state_diff.c` | State captures, page digests (record/compare), shadow-mode captures/compare/report, the dead-stack fill (asm). |
| `pc/tests/zig_toolchain_test.py` | Offline test of the pin logic with a fake tarball. |
| `pc/tests/ownership_test.py` | Table validation, `rename_definitions`, `strip_bodies`, routing source. |
| `pc/tests/routing_probe/` (`unit.c`, `owned.c`, `main.c`, `run.py`) | The routing mechanism on i386, x86-64 and MinGW: intra-unit calls, external calls, function pointers. |
| `pc/tests/zig_views_test.py` | Views from a synthetic C unit on 32/64-bit, checked by `zig test`. |
| `pc/tests/subsystem_map_test.py` | The mapper on a synthetic unit. |
| `pc/tests/ownership_smoke.py` | One game: `owner` RPC, routing live, rewind equivalence, shadow pass-through, fault injection. |
| `pc/tests/diff/masks.py`, `scenarios.py`, `differ.py` | Noise masks with reasons; the battle corpus; the differential runner. |
| `pc/docs/Ownership.md` | The architecture, the process, the tools (Task 30). |

Modified:

| File | Change |
|---|---|
| `pc/tools/build_boot.py` | Zig toolchain and stamp; routed-unit transform; views/c generation; Zig objects; `routing.c`; checks. |
| `pc/src/snapshot.c` | `pc_state_ranges()` exported; in-memory mark/rewind on a side stack. |
| `pc/src/input_record.c` | Record/replay started and sought over RPC; rewind hook. |
| `pc/src/dev_rpc.c` | Methods `owner`, `digest`, `replay`, `record`; `snapshot` actions `mark`, `rewind`. |
| `pc/src/dev_overlay.cpp` | "Ownership" section: an owner combo per subsystem. |
| `pc/src/charge_skip.inc` → deleted in Task 29; `pc/src/charge_skip.c` | Task 28: guard; Task 29: reads the Zig module's exports. |
| `src/BATTLE/BATTLE.PRG/146C.c` | Task 29 only: the charge-skip hook lines removed (back to the upstream text). |
| `pc/src/ui/ui_settings.cpp` | The charging rows greyed with a reason when battle actions are original. |
| `pc/tools/rood_rpc.py` | `Game.owner()`, `Game.mark()`, `Game.rewind()`, `Game.digest()`, `Game.replay()`, `Game.record()`. |
| `pc/tests/run.py` | Suites `ownership_smoke`, `diff_battle_action`. |
| `pc/CMakeLists.txt` | Tests `zig_toolchain`, `ownership`, `routing_probe`, `zig_views`, `subsystem_map`, `zig_unit`. |
| `.gitignore` | `.zig-cache/`, `zig-out/`. |
| `pc/README.md`, `THIRD_PARTY.md` | Ownership section and test rows; Zig's MIT notice for the std code linked. |

---

### Task 1: Worktree and baseline

**Files:** none changed (the design doc is copied in).

- [ ] **Step 1: Check the charging work is merged**

```bash
git log --oneline main | grep -i -m3 'charg'
grep -n 'pc_charge_skip' src/BATTLE/BATTLE.PRG/146C.c
```

Expected: a merge of `charge-true-skip` in the log, and the hook lines (`int pc_charge_skip(int phase);`, `if (pc_charge_skip(4))`, `if (pc_charge_skip(5))`, `#include "pc/src/charge_skip.inc"`). If they are not on `main` yet, stop and report: Tasks 28–29 depend on their final shape. (If that branch's final shape differs from the one described in the design doc §8, adapt Tasks 28–29 to it; the rest of the plan does not depend on it.)

- [ ] **Step 2: Branch in a worktree**

Use superpowers:using-git-worktrees. From the main checkout:

```bash
git worktree add .claude/worktrees/ownership-m0 -b claude/ownership-m0 main
cd .claude/worktrees/ownership-m0
ln -s ../../../data data && ln -s ../../../disks disks
cp -a ../../../build build
mkdir -p pc/docs/plans && cp <scratchpad>/2026-10-16-ownership-design.md <scratchpad>/2026-10-16-ownership-m0-plan.md pc/docs/plans/
```

Expected: `ls -d data disks build` lists all three; the two documents are in `pc/docs/plans/`.

- [ ] **Step 3: Record the baseline of all four targets**

```bash
cmake -S pc -B pc/build -G Ninja
for args in "" "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Built|64-bit pass|error' ; done
```

Expected: four `Built ...: N data imports, M fail-fast function imports` lines (rood_boot, rood_battle_probe, boot64/rood_battle_probe, win64/rood.exe). Write the four (N, M) pairs into the task notes: later tasks compare against them (routing must not add traps).

- [ ] **Step 4: Commit check baseline**

```bash
flock "$ROOD_HEAVY_LOCK" docker run --rm --cpus=2 -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work rood-reverse-dev:local make commit-check 2>&1 | tail -2
```

Expected: `All files match`. (Then restore `build/` as the verify notes say if `make clean` touched it: `cp -a ../../../build build`.)

- [ ] **Step 5: Commit the documents**

```bash
git add pc/docs/plans/2026-10-16-ownership-design.md pc/docs/plans/2026-10-16-ownership-m0-plan.md
git commit -m "Ownership M0: design and plan

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The pinned toolchain (`pc/tools/zig_toolchain.py`), test first

**Files:**
- Create: `pc/tests/zig_toolchain_test.py`, `pc/tools/zig_toolchain.py`
- Modify: `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`pc/tests/zig_toolchain_test.py`:

```python
#!/usr/bin/env python3
"""pc/tools/zig_toolchain.py without the network: a fake release tarball is
verified, unpacked into a scratch cache and found; a wrong checksum, a path
escaping the archive and a wrong ROOD_ZIG version are refused."""
import hashlib
import io
import os
import pathlib
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import zig_toolchain as zt  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def fake_tarball(directory, name, version, evil=False):
    """A .tar.xz like the official one: <stem>/zig printing the version."""
    stem = name.removesuffix(".tar.xz")
    path = directory / name
    with tarfile.open(path, "w:xz") as tar:
        script = f"#!/bin/sh\n[ \"$1\" = version ] && echo {version}\n".encode()
        info = tarfile.TarInfo(f"{stem}/zig")
        info.size, info.mode = len(script), 0o755
        tar.addfile(info, io.BytesIO(script))
        if evil:
            info = tarfile.TarInfo("../escaped")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
    return path, hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory() as scratch:
    scratch = pathlib.Path(scratch)
    os.environ["ROOD_ZIG_CACHE"] = str(scratch / "cache")
    os.environ.pop("ROOD_ZIG", None)
    name = f"zig-x86_64-linux-{zt.VERSION}.tar.xz"
    tarball, digest = fake_tarball(scratch, name, zt.VERSION)
    zt.PIN["x86_64-linux"] = (name, digest)
    check(zt.find() is None, "nothing installed: find() is None")
    try:
        zt.ensure()
        check(False, "ensure() without an install stops")
    except SystemExit as e:
        check("--from-tarball" in str(e) and "--fetch" in str(e), "ensure() says how to get it")
    zig = zt.unpack(tarball)
    check(zig.exists() and zt.version_of(zig) == zt.VERSION, f"unpacked and runs: {zig}")
    check(zt.find() == zig, "find() returns the unpacked zig")
    check(str(scratch / "cache") in str(zig), "unpacked under ROOD_ZIG_CACHE, not the repository")
    (scratch / "w").mkdir()
    wrong, _ = fake_tarball(scratch / "w", name, zt.VERSION)
    zt.PIN["x86_64-linux"] = (name, "0" * 64)
    try:
        zt.unpack(wrong)
        check(False, "a wrong checksum is refused")
    except SystemExit as e:
        check("SHA-256" in str(e), "a wrong checksum is refused")
    (scratch / "e").mkdir()
    evil, evil_digest = fake_tarball(scratch / "e", name, zt.VERSION, evil=True)
    zt.PIN["x86_64-linux"] = (name, evil_digest)
    os.environ["ROOD_ZIG_CACHE"] = str(scratch / "cache2")
    try:
        zt.unpack(evil)
        check(False, "a path outside the archive's directory is refused")
    except SystemExit as e:
        check("outside" in str(e), "a path outside the archive's directory is refused")
    check(not (scratch / "escaped").exists(), "nothing written outside the cache")
    other, _ = fake_tarball(scratch / "e", "zig-old.tar.xz", "0.15.2")
    with tarfile.open(other) as tar:
        tar.extractall(scratch / "old", filter="data")
    os.environ["ROOD_ZIG"] = str(scratch / "old/zig-old/zig")
    try:
        zt.find()
        check(False, "ROOD_ZIG with another version is refused")
    except SystemExit as e:
        check("0.15.2" in str(e), "ROOD_ZIG with another version is refused")

print("FAILED" if failures else "all zig toolchain checks passed")
sys.exit(1 if failures else 0)
```

Add to `pc/CMakeLists.txt`, after the `event_callbacks` test:

```cmake
# The pinned Zig toolchain's verification and cache (pc/tools/zig_toolchain.py), offline.
add_test(NAME zig_toolchain COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/zig_toolchain_test.py)
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_toolchain --output-on-failure
```

Expected: FAIL with `ModuleNotFoundError: No module named 'zig_toolchain'`.

- [ ] **Step 3: Write `pc/tools/zig_toolchain.py`**

```python
#!/usr/bin/env python3
"""The pinned Zig toolchain for the owned game code in game/ (pc/docs/Ownership.md).

    python3 pc/tools/zig_toolchain.py                     the zig binary's path (exit 2: not installed)
    python3 pc/tools/zig_toolchain.py --from-tarball F    verify a release tarball already on disk, unpack it
    python3 pc/tools/zig_toolchain.py --fetch             download the pinned tarball from ziglang.org
                                                          (~55 MB; only with the user's OK), then unpack it

A tarball is checked against PIN's SHA-256 before anything is unpacked, and is
unpacked into $ROOD_ZIG_CACHE (default ${XDG_CACHE_HOME:-~/.cache}/rood-boi/zig),
never into the repository; worktrees share it. ROOD_ZIG=/path/to/zig uses an
existing install whose `zig version` is the pinned one. Zig's global cache
lives next to the toolchain (global_cache()).
"""
import argparse
import hashlib
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

VERSION = "0.16.0"
# host -> (archive, SHA-256). Never guess a checksum: a new host's entry is
# copied from https://ziglang.org/download/index.json and checked against the
# release's minisign signature (Task 3).
PIN = {
    "x86_64-linux": ("zig-x86_64-linux-0.16.0.tar.xz",
                     "70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00"),
}
URL = "https://ziglang.org/download/{version}/{archive}"


def host():
    machine = {"amd64": "x86_64", "x86_64": "x86_64"}.get(platform.machine().lower(), platform.machine().lower())
    return f"{machine}-{'windows' if os.name == 'nt' else 'linux'}"


def cache_root():
    if os.environ.get("ROOD_ZIG_CACHE"):
        return pathlib.Path(os.environ["ROOD_ZIG_CACHE"])
    base = os.environ.get("XDG_CACHE_HOME") or str(pathlib.Path.home() / ".cache")
    return pathlib.Path(base) / "rood-boi" / "zig"


def _pin():
    if host() not in PIN:
        raise SystemExit(f"Zig {VERSION}: no pinned archive for host {host()} (add it to PIN, Task 3)")
    return PIN[host()]


def _install_dir():
    archive, _ = _pin()
    return cache_root() / VERSION / archive.removesuffix(".tar.xz").removesuffix(".zip")


def global_cache():
    return cache_root() / VERSION / "global-cache"


def version_of(zig):
    result = subprocess.run([str(zig), "version"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def find():
    """The pinned zig, or None. ROOD_ZIG must be the pinned version."""
    override = os.environ.get("ROOD_ZIG")
    if override:
        found = version_of(override)
        if found != VERSION:
            raise SystemExit(f"ROOD_ZIG={override} is Zig {found}, not the pinned {VERSION}")
        return pathlib.Path(override)
    zig = _install_dir() / ("zig.exe" if os.name == "nt" else "zig")
    return zig if zig.exists() and version_of(zig) == VERSION else None


def ensure():
    zig = find()
    if zig is None:
        raise SystemExit(
            f"Zig {VERSION} is not installed for this build. Either\n"
            f"  python3 pc/tools/zig_toolchain.py --from-tarball PATH   (a {_pin()[0]} you already have), or\n"
            f"  python3 pc/tools/zig_toolchain.py --fetch               (downloads it from ziglang.org), or\n"
            f"  ROOD_ZIG=/path/to/zig                                    (an existing {VERSION} install)")
    return zig


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def unpack(tarball):
    """Verify `tarball` against the pin and unpack it into the cache; returns the zig binary."""
    archive, digest = _pin()
    actual = sha256(tarball)
    if actual != digest:
        raise SystemExit(f"{tarball}: SHA-256 {actual}, the pin says {digest}: refusing to unpack")
    target = _install_dir()
    stem = target.name
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as scratch:
        with tarfile.open(tarball) as tar:
            for member in tar.getmembers():
                if not (member.name == stem or member.name.startswith(stem + "/")):
                    raise SystemExit(f"{tarball}: {member.name} is outside {stem}/: refusing to unpack")
            tar.extractall(scratch, filter="data")
        if target.exists():
            shutil.rmtree(target)
        os.replace(pathlib.Path(scratch) / stem, target)
    zig = target / ("zig.exe" if os.name == "nt" else "zig")
    if version_of(zig) != VERSION:
        raise SystemExit(f"{zig}: does not report version {VERSION}")
    return zig


def fetch():
    archive, _ = _pin()
    url = URL.format(version=VERSION, archive=archive)
    cache_root().mkdir(parents=True, exist_ok=True)
    download = cache_root() / archive
    print(f"Downloading {url} ...", flush=True)
    with urllib.request.urlopen(url) as response, open(download, "wb") as f:
        shutil.copyfileobj(response, f)
    try:
        return unpack(download)
    finally:
        download.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from-tarball", type=pathlib.Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    if args.from_tarball:
        print(unpack(args.from_tarball))
    elif args.fetch:
        print(fetch())
    else:
        zig = find()
        if zig is None:
            print(f"Zig {VERSION} is not installed (see --help)", file=sys.stderr)
            return 2
        print(zig)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the test**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_toolchain --output-on-failure
```

Expected: `all zig toolchain checks passed`, test passed.

- [ ] **Step 5: Commit**

```bash
git add pc/tools/zig_toolchain.py pc/tests/zig_toolchain_test.py pc/CMakeLists.txt
git commit -m "Zig toolchain pin: verified, unpacked into the user cache, never the repo

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: GATED — obtain Zig 0.16.0

**Files:** none in the repository (the toolchain lands in the user's cache).

- [ ] **Step 1: Ask the user (stop here until they answer)**

Ask in chat, verbatim:

> The ownership work needs Zig 0.16.0. Options: (a) I verify and unpack the tarball you already have (`zig-x86_64-linux-0.16.0.tar.xz`, 55 MB; give me its path), no download; (b) I download it from `https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz` (55 MB) into `~/.cache/rood-boi/zig/`; (c) you point `ROOD_ZIG` at an existing 0.16.0 install. The pinned SHA-256 is `70e49664…ba3d00`. Which one?

Do not run `--fetch` without a clear yes for (b).

- [ ] **Step 2: Cross-check the pin against the official index (only with network permission; otherwise note it as pending)**

```bash
python3 - <<'EOF'
import json, urllib.request
index = json.load(urllib.request.urlopen("https://ziglang.org/download/index.json"))
print(index["0.16.0"]["x86_64-linux"]["shasum"])
EOF
```

Expected: `70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00`. If it differs, stop and report (do not change the pin to whatever the index says without the user). If `minisign` is installed, also verify the tarball's `.minisig` with Zig's published public key (from ziglang.org/download).

- [ ] **Step 3: Install by the chosen route**

```bash
python3 pc/tools/zig_toolchain.py --from-tarball PATH_THE_USER_GAVE   # (a)
python3 pc/tools/zig_toolchain.py --fetch                             # (b), only after a yes
python3 pc/tools/zig_toolchain.py                                     # all routes: prints the zig path
"$(python3 pc/tools/zig_toolchain.py)" version
```

Expected: a path under `~/.cache/rood-boi/zig/0.16.0/` (or the `ROOD_ZIG` path) and `0.16.0`. `git status --short` shows nothing new.

---

### Task 4: `game/` skeleton, the panic, `psx.zig` and its unit tests

**Files:**
- Create: `game/common/common.zig`, `game/common/panic.zig`, `game/common/psx.zig`, `game/core/root.zig`
- Modify: `.gitignore`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing unit tests first (in `psx.zig`, with stub declarations)**

`game/common/psx.zig` (first version: declarations and tests; the bodies are `@compileError("todo")`, so the tests fail to compile):

```zig
//! Guest memory helpers for owned code (pc/docs/Ownership.md): the PS1's
//! 4-byte guest pointers, bitfields at DWARF positions, C's integer
//! conversions, and guest/host globals reached the way GCC's objects reach
//! them (PC-relative, no .refptr stubs on Windows).
const std = @import("std");
const builtin = @import("builtin");

pub fn Ptr(comptime T: type) type {
    _ = T;
    @compileError("todo");
}
pub fn get(field: anytype) void {
    _ = field;
    @compileError("todo");
}
pub fn conv(comptime T: type, x: anytype) T {
    _ = x;
    @compileError("todo");
}

test "Ptr is 4 bytes and round-trips a low address" {
    const Node = extern struct { next: Ptr(u32), x: i16, y: i16 };
    try std.testing.expectEqual(@as(usize, 8), @sizeOf(Node));
    try std.testing.expectEqual(@as(usize, 4), @offsetOf(Node, "x"));
    var n = Node{ .next = .{ .raw = 0 }, .x = 1, .y = 2 };
    try std.testing.expect(get(n.next) == null);
    n.next.raw = 0x1000;
    try std.testing.expectEqual(@as(usize, 0x1000), @intFromPtr(get(n.next).?));
}

test "get passes native pointers through" {
    var x: u32 = 5;
    const p: ?*u32 = &x;
    try std.testing.expectEqual(p, get(p));
}

test "conv is C's integer conversion" {
    try std.testing.expectEqual(@as(u16, 0xFFFE), conv(u16, @as(c_int, -2)));
    try std.testing.expectEqual(@as(i8, -1), conv(i8, @as(u32, 0x1FF)));
    try std.testing.expectEqual(@as(u32, 0xFFFF_FFFF), conv(u32, @as(i8, -1)));
    try std.testing.expectEqual(@as(c_int, 200), conv(c_int, @as(u8, 200)));
}

test "bitfields at DWARF positions" {
    const T = extern struct {
        id: u8,
        _bits_1: [2]u8,
        pub const bitfields = .{
            .flag = Bit{ .byte = 1, .bit = 0, .width = 1, .signed = false },
            .kind = Bit{ .byte = 1, .bit = 1, .width = 3, .signed = false },
            .wide = Bit{ .byte = 1, .bit = 4, .width = 9, .signed = false },
            .neg = Bit{ .byte = 2, .bit = 5, .width = 3, .signed = true },
        };
    };
    var t = T{ .id = 0, ._bits_1 = .{ 0, 0 } };
    setBits(&t, "flag", 1);
    setBits(&t, "kind", 5);
    setBits(&t, "wide", 0x1A5);
    try std.testing.expectEqual(@as(u1, 1), getBits(&t, "flag"));
    try std.testing.expectEqual(@as(u3, 5), getBits(&t, "kind"));
    try std.testing.expectEqual(@as(u9, 0x1A5), getBits(&t, "wide"));
    setBits(&t, "neg", -2);
    try std.testing.expectEqual(@as(i3, -2), getBits(&t, "neg"));
    try std.testing.expectEqual(@as(u9, 0x1A5 & 0x1FF), getBits(&t, "wide") & 0x1FF);
}
```

`.gitignore` gains:

```
# Zig caches (the build's live under pc/build)
.zig-cache/
zig-out/
```

`pc/CMakeLists.txt`, after the `zig_toolchain` test:

```cmake
# Owned game code's pure parts (game/): zig test on the 32- and 64-bit targets the game builds for.
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/zig_toolchain.py
    OUTPUT_VARIABLE ROOD_ZIG OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE ROOD_ZIG_MISSING)
if(ROOD_ZIG_MISSING EQUAL 0)
    foreach(target x86_64-linux-musl x86-linux-musl)
        add_test(NAME zig_unit_${target} COMMAND ${ROOD_ZIG} test -target ${target} -O ReleaseSafe
            ${CMAKE_CURRENT_SOURCE_DIR}/../game/common/psx.zig)
    endforeach()
else()
    message(STATUS "Zig not installed (pc/tools/zig_toolchain.py): zig unit tests skipped")
endif()
```

- [ ] **Step 2: Run them and see them fail**

```bash
cmake -S pc -B pc/build -G Ninja && flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_unit --output-on-failure
```

Expected: both fail with `error: todo`.

- [ ] **Step 3: Implement `psx.zig`**

Replace the three stubs with:

```zig
/// A PS1-layout pointer field (pc/src/psx_types.h psx_ptr(T)): the guest
/// address in 4 bytes on every target. Guest memory is mapped low, so the
/// zero-extended address is the host pointer.
pub fn Ptr(comptime T: type) type {
    return extern struct {
        raw: u32 align(4),
        pub const Target = T;
        pub fn ptr(self: @This()) ?*T {
            return if (self.raw == 0) null else @ptrFromInt(self.raw);
        }
        pub fn set(self: *@This(), p: ?*T) void {
            self.raw = addr(p);
        }
    };
}

/// A pointer as a guest address (psx_types.h psx_addr): fatal above 4 GB.
pub fn addr(p: anytype) u32 {
    const value: usize = switch (@typeInfo(@TypeOf(p))) {
        .optional => if (p) |q| @intFromPtr(q) else 0,
        else => @intFromPtr(p),
    };
    if (comptime @sizeOf(usize) > 4) {
        if (value > std.math.maxInt(u32)) badPointer(value);
    }
    return @truncate(value);
}

extern fn pc_psx_bad_pointer(pointer: usize) callconv(.c) noreturn;
fn badPointer(value: usize) noreturn {
    pc_psx_bad_pointer(value);
}

fn GetResult(comptime F: type) type {
    return switch (@typeInfo(F)) {
        .optional, .pointer => F,
        .@"struct" => if (@hasDecl(F, "Target")) ?*F.Target else @compileError("psx.get: " ++ @typeName(F) ++ " is not a pointer field"),
        else => @compileError("psx.get: " ++ @typeName(F) ++ " is not a pointer field"),
    };
}

/// psx_get: a pointer field's target, whether the field is a native pointer
/// (32-bit build, runtime structs) or a 4-byte guest pointer (Ptr(T)).
pub fn get(field: anytype) GetResult(@TypeOf(field)) {
    return switch (@typeInfo(@TypeOf(field))) {
        .optional, .pointer => field,
        else => field.ptr(),
    };
}

/// C's conversion of integer x to type T (modulo 2^bits when narrowing;
/// sign- or zero-extension by x's own signedness when widening).
pub fn conv(comptime T: type, x: anytype) T {
    const X = @TypeOf(x);
    const xi = @typeInfo(X).int;
    const ti = @typeInfo(T).int;
    const UX = std.meta.Int(.unsigned, xi.bits);
    const UT = std.meta.Int(.unsigned, ti.bits);
    if (ti.bits <= xi.bits) {
        return @bitCast(@as(UT, @truncate(@as(UX, @bitCast(x)))));
    }
    const Wide = std.meta.Int(xi.signedness, ti.bits);
    return @bitCast(@as(Wide, x));
}

/// A bitfield's position, from DWARF (DW_AT_data_bit_offset): byte offset
/// in the struct, bit within that byte, width, signedness.
pub const Bit = struct { byte: usize, bit: u3, width: u16, signed: bool };

fn BitInt(comptime b: Bit) type {
    return std.meta.Int(if (b.signed) .signed else .unsigned, b.width);
}
fn bitSpec(comptime P: type, comptime name: []const u8) Bit {
    const S = @typeInfo(P).pointer.child;
    return @field(S.bitfields, name);
}

/// A bitfield's value (the struct type's `bitfields` table names it).
pub fn getBits(p: anytype, comptime name: []const u8) BitInt(bitSpec(@TypeOf(p), name)) {
    const b = comptime bitSpec(@TypeOf(p), name);
    const n = comptime (b.bit + b.width + 7) / 8;
    const bytes: [*]const u8 = @ptrCast(p);
    var word: u64 = 0;
    inline for (0..n) |i| word |= @as(u64, bytes[b.byte + i]) << (8 * i);
    const U = std.meta.Int(.unsigned, b.width);
    return @bitCast(@as(U, @truncate(word >> b.bit)));
}

/// Store a bitfield (read-modify-write of the bytes it spans).
pub fn setBits(p: anytype, comptime name: []const u8, value: BitInt(bitSpec(@TypeOf(p), name))) void {
    const b = comptime bitSpec(@TypeOf(p), name);
    const n = comptime (b.bit + b.width + 7) / 8;
    const bytes: [*]u8 = @ptrCast(p);
    var word: u64 = 0;
    inline for (0..n) |i| word |= @as(u64, bytes[b.byte + i]) << (8 * i);
    const U = std.meta.Int(.unsigned, b.width);
    const mask: u64 = @as(u64, std.math.maxInt(U)) << b.bit;
    word = (word & ~mask) | (@as(u64, @as(U, @bitCast(value))) << b.bit);
    inline for (0..n) |i| bytes[b.byte + i] = @truncate(word >> (8 * i));
}

/// A global of the game or the host (a data import at its guest address, a
/// native LP64 import, or a C variable), reached PC-relative like GCC's
/// objects reach it: never through a .refptr stub (Windows).
pub inline fn global(comptime T: type, comptime name: []const u8) *T {
    return @extern(*T, .{ .name = name, .relocation = .pcrel, .visibility = .hidden });
}
/// The same for an array of unknown length (extern T name[]).
pub inline fn globalMany(comptime T: type, comptime name: []const u8) [*]T {
    return @ptrCast(@extern(*T, .{ .name = name, .relocation = .pcrel, .visibility = .hidden }));
}

/// The k-th element before or after `base` without a bounds check: C's
/// `base[k]` where the original indexes out of its declared bounds
/// (e.g. mappedChainAbilities[n - 1] with n == 0 reads the byte before).
pub inline fn at(base: anytype, k: isize) *std.meta.Elem(@TypeOf(base)) {
    const E = std.meta.Elem(@TypeOf(base));
    const first: [*]E = @ptrCast(base);
    return @ptrFromInt(@as(usize, @bitCast(@as(isize, @bitCast(@intFromPtr(first))) + k * @as(isize, @sizeOf(E)))));
}

/// A view pointer as the `c` module's pointer type for the same C type
/// (translate-c's [*c]T, or ?*T for a demoted opaque type). Checked at
/// comptime: same type name, and the same size where c knows it.
pub fn cptr(comptime CT: type, p: anytype) [*c]CT {
    const VT = @typeInfo(@TypeOf(p)).pointer.child;
    comptime {
        const vn = @typeName(VT);
        const cn = @typeName(CT);
        const vs = vn[(std.mem.lastIndexOfScalar(u8, vn, '.') orelse 0)..];
        const cs = cn[(std.mem.lastIndexOfScalar(u8, cn, '.') orelse 0)..];
        if (!std.mem.eql(u8, vs, cs) and !std.mem.eql(u8, vs, ".struct_" ++ cs[1..]) and !std.mem.eql(u8, ".struct_" ++ vs[1..], cs))
            @compileError("psx.cptr: " ++ vn ++ " is not " ++ cn);
        if (@typeInfo(CT) != .@"opaque" and @sizeOf(CT) != @sizeOf(VT))
            @compileError("psx.cptr: " ++ vn ++ " and " ++ cn ++ " differ in size");
    }
    return @ptrCast(p);
}
```

`game/common/panic.zig`:

```zig
//! The panic handler of every owned object: a report through the port's
//! crash path (ownership.c pc_zig_panic), not the std's I/O.
const std = @import("std");

extern fn pc_zig_panic(msg: [*]const u8, len: usize, return_address: usize) callconv(.c) noreturn;

fn fail(msg: []const u8, first_trace_address: ?usize) noreturn {
    pc_zig_panic(msg.ptr, msg.len, first_trace_address orelse @returnAddress());
}

pub const Panic = std.debug.FullPanic(fail);
```

`game/common/common.zig`:

```zig
pub const psx = @import("psx.zig");
pub const Panic = @import("panic.zig").Panic;
```

`game/core/root.zig`:

```zig
//! game_core.o: linked into every build (pc/docs/Ownership.md). ownership.c
//! logs the ABI version at start, so every target exercises the toolchain.
const common = @import("common");
pub const panic = common.Panic;

/// Bumped when the routing ABI between C and owned code changes.
export const rood_game_abi: u32 = 1;
```

- [ ] **Step 4: Run the tests**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_unit --output-on-failure
```

Expected: `zig_unit_x86_64-linux-musl` and `zig_unit_x86-linux-musl` pass (`All 4 tests passed.` each).

- [ ] **Step 5: Commit**

```bash
git add game .gitignore pc/CMakeLists.txt
git commit -m "game/: common Zig helpers (guest pointers, bitfields, C conversions, globals), panic, core root

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Zig objects in `build_boot.py` on all four targets

**Files:**
- Create: `pc/src/ownership.c` (first part: the panic and the ABI log)
- Modify: `pc/tools/build_boot.py`

- [ ] **Step 1: `pc/src/ownership.c` (first part)**

```c
/* Owned game code (game/, Zig; pc/docs/Ownership.md): the host side.
 * This part: the owned objects' panic handler and the ABI check. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void pc_unimplemented(const char* what) __attribute__((noreturn));
extern const uint32_t rood_game_abi; /* game/core/root.zig */
#define ROOD_GAME_ABI 1

/* game/common/panic.zig: a safety check failed in owned code (overflow,
 * bounds, unreachable, a null unwrap). */
__attribute__((noreturn)) void pc_zig_panic(const char* msg, size_t len, uintptr_t return_address)
{
    fprintf(stderr, "[zig] panic: %.*s (return address %#llx)\n", (int)len, msg,
        (unsigned long long)return_address);
    fflush(stderr);
    pc_unimplemented("owned code panicked (see the [zig] line)");
}

__attribute__((constructor)) static void check_abi(void)
{
    if (rood_game_abi != ROOD_GAME_ABI) {
        fprintf(stderr, "[ownership] game_core ABI %u, the host expects %u\n", rood_game_abi, ROOD_GAME_ABI);
        abort();
    }
}
```

- [ ] **Step 2: The toolchain, the stamp and the Zig compile helper in `build_boot.py`**

After `from build_cache import ObjectCache, ResultCache, Stamp, tool_identity` add the toolchain, and extend the stamp's tools and trees:

```python
sys.path.insert(0, str(root / "pc/tools"))
import zig_toolchain  # noqa: E402

# The pinned Zig for the owned code in game/ (pc/docs/Ownership.md).
ZIG = str(zig_toolchain.ensure())
```

In `stamp_tools` append `ZIG`; in the list of stamped tools (`[(f"pc/tools/{tool}.py", None) for tool in [...]]`) append `"zig_toolchain"`, and add `("game", None)` to `stamp_trees`. Tasks 6, 12 and 13 append `"ownership"`, `"zig_views"` and `"zig_c"` to that list when they create those tools.

Add, after `compile_generated()`:

```python
# Owned code (game/, Zig): one object per overlay plus game_core, compiled by
# the pinned Zig into this tree's objects (and, for --target windows, the
# COFF twin from the same source). Flags: pc/docs/Ownership.md "Targets".
ZIG_COMMON = ["-O", "ReleaseSafe", "-fllvm", "-fsingle-threaded", "-fno-stack-check",
              "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections"]
ZIG_ELF = ("x86_64-linux-none", "x86_64", ["-fPIE"]) if arch64 else ("x86-linux-none", "i686", ["-fno-PIE", "-fno-PIC"])
ZIG_COFF = ("x86_64-windows-gnu", "x86_64", [])


def zig_command(target, modules, root_name, output):
    """zig build-obj for `modules` ({name: (path, [deps])}), root first; the
    per-module flags are repeated for each module (-M resets them)."""
    triple, cpu, pic = target
    args = [ZIG, "build-obj", "-fno-compiler-rt", f"-femit-bin={output}",
            "--cache-dir", str(out / "zig-cache"), "--global-cache-dir", str(zig_toolchain.global_cache())]
    for name in [root_name] + sorted(m for m in modules if m != root_name):
        path, deps = modules[name]
        args += ["-target", triple, "-mcpu", cpu, *ZIG_COMMON, *pic]
        for dep in deps:
            args += ["--dep", dep]
        args.append(f"-M{name}={path}")
    return args


_ZIG_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def check_zig_object(obj, overlay):
    """Fail fast on what the pipeline cannot handle (pc/docs/Ownership.md):
    odd global names, .refptr stubs (COFF), overlay statics."""
    if not coff:
        for line in query(["nm", "-g", "-P", str(obj)]).splitlines():
            name = line.rsplit(" ", 3)[0]
            if not _ZIG_NAME.fullmatch(name):
                raise SystemExit(f"{obj.name}: global symbol {name!r} is not a plain identifier")
        if overlay:
            for line in query(["readelf", "-SW", str(obj)]).splitlines():
                section = re.match(r"\s*\[\s*\d+\]\s+(\.(data|bss)(\.\S+)?)\s+\w+\s+\w+\s+\w+\s+(\w+)", line)
                # .data.rel.ro: constant tables with relocations (read-only after linking), not state
                if section and int(section.group(4), 16) and not section.group(1).startswith(".data.rel.ro"):
                    raise SystemExit(f"{obj.name}: owned code keeps no mutable statics ({section.group(1)}); "
                                     "keep state in guest memory or host C")
    if win:
        symbols = query([MINGW + "nm", str(twin(obj))])
        if ".refptr." in symbols:
            bad = sorted({s.split(".refptr.")[1] for s in symbols.split() if ".refptr." in s})
            raise SystemExit(f"{twin(obj).name}: .refptr stubs for {', '.join(bad)}: reach globals with psx.global()")


def compile_zig(name, modules, root_name, overlay=None):
    """Compile an owned object (and its COFF twin); returns its path."""
    obj = out / f"{name}.o"
    if not coff:
        run(zig_command(ZIG_ELF, modules, root_name, obj))
    if win:
        run(zig_command(ZIG_COFF, modules, root_name, twin(obj)))
    check_zig_object(obj, overlay)
    print(f"Zig: {obj.name} ({ZIG_ELF[0]}{', ' + ZIG_COFF[0] if win else ''})")
    return obj
```

(`zig build-obj` keeps its own cache in `pc/build/<tree>/zig-cache`: an unchanged module recompiles in well under a second. The object cache is for gcc/g++ only.)

Compile `game_core` and `ownership.c` with the host units. In the `for name in ["audio_libspu", ...]` list add `"ownership"`, and after that loop:

```python
objects.append(compile_zig("game_core", {
    "root": ("game/core/root.zig", ["common"]),
    "common": ("game/common/common.zig", []),
}, "root"))
```

- [ ] **Step 3: Build all four targets**

```bash
for args in "" "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error|Error' ; done
```

Expected, per target: `Zig: game_core.o (x86-linux-none)` (32-bit twice), `Zig: game_core.o (x86_64-linux-none)`, `Zig: game_core.o (x86_64-linux-none, x86_64-windows-gnu)`, then the `Built ...` line with the same data-import and trap counts as Task 1 (game_core imports nothing from the game; `pc_unimplemented` and `pc_psx_bad_pointer` are defined natively).

- [ ] **Step 4: The no-op build still holds**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/build_noop_test.py
```

Expected: `pc/build/boot: up to date (nothing changed since its last build)` and the no-op test passes. Touch `game/core/root.zig` and rebuild: it rebuilds (the stamp covers `game/`).

- [ ] **Step 5: Run a game and see the core is linked**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 rpc_smoke
nm pc/build/boot/rood_battle_probe | grep -E ' (rood_game_abi|pc_zig_panic)$'
```

Expected: `rpc_smoke` passes; both symbols listed.

- [ ] **Step 6: Commit**

```bash
git add pc/tools/build_boot.py pc/src/ownership.c
git commit -m "build_boot: compile owned Zig objects (game_core) on all four targets, with pipeline checks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: The ownership table and its reader

**Files:**
- Create: `game/ownership.toml`, `pc/tools/ownership.py` (first part), `pc/tests/ownership_test.py` (first part)
- Modify: `pc/CMakeLists.txt`, `pc/tools/build_boot.py` (stamped tools: `"ownership"`)

- [ ] **Step 1: The table**

`game/ownership.toml`:

```toml
# Owned game code (pc/docs/Ownership.md). Each subsystem is a set of the
# original's functions that the PC game can run from owned code (game/)
# instead. Functions are keyed by their PS1 address; the name is checked
# against the reference ELFs, so an upstream rename fails the build.
#
#   overlay   the Zig object that implements it: game/<overlay>/root.zig -> <overlay>_zig.o
#   unit      the original unit that defines every listed function
#   default   who runs it unless ROOD_OWNER, RPC owner or the overlay says otherwise:
#             original | owned
#   roots     pointer globals the field mapper follows (pc/tools/field_map.py)

[subsystem.battle_action]
overlay = "battle"
unit = "src/BATTLE/BATTLE.PRG/146C.c"
default = "original"
roots = ["D_800F19CC", "vs_battle_actors", "D_800F4538", "vs_battle_characterState"]
functions = [
    { addr = 0x80070F28, name = "func_80070F28" },            # commit (costs, hits rolled), sets phase 4
    { addr = 0x8007138C, name = "func_8007138C" },            # cast start: animation, sound, effect request
    { addr = 0x800784AC, name = "_handleCombatCameraZoom" },  # phase 4: the camera converges
    { addr = 0x80078748, name = "func_80078748" },            # phase 5: wait for the cast to be ready
    { addr = 0x800704B0, name = "func_800704B0" },            # -> phase 6
    { addr = 0x800787F0, name = "func_800787F0" },            # phase 6: wait for the flash, release
    { addr = 0x80070278, name = "func_80070278" },            # release: turn, ability window, phase 7
    { addr = 0x80079030, name = "func_80079030" },            # phase 8: re-cast
    { addr = 0x80078AB4, name = "func_80078AB4" },            # phase 7: the chain/defense timing window
]
```

(If the user answered "stop at release" to the design doc's question 4, drop the last line and Task 26.)

- [ ] **Step 2: Write the failing test**

`pc/tests/ownership_test.py` (first part; Tasks 7 and 8 add cases):

```python
#!/usr/bin/env python3
"""pc/tools/ownership.py: the table, its validation, the routed-definition
rename, the declaration-only strip and the generated routing sources."""
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def raises(fn, text):
    try:
        fn()
    except (ValueError, SystemExit) as e:
        return text in str(e)
    return False


TOML = """
[subsystem.alpha]
overlay = "battle"
unit = "src/A.c"
default = "original"
roots = ["g"]
functions = [ { addr = 0x80010000, name = "f_one" }, { addr = 0x80010010, name = "f_two" } ]

[subsystem.beta]
overlay = "battle"
unit = "src/B.c"
default = "owned"
functions = [ { addr = 0x80020000, name = "g_one" } ]
"""

with tempfile.TemporaryDirectory() as d:
    path = pathlib.Path(d) / "ownership.toml"
    path.write_text(TOML)
    table = ownership.load(path)
    check([s.name for s in table.subsystems] == ["alpha", "beta"], "subsystems in file order")
    check([s.index for s in table.subsystems] == [0, 1], "indices are the pc_owner[] slots")
    check(table.routed_names("src/A.c") == {"f_one", "f_two"}, "routed names of a unit")
    check(table.routed_names("src/C.c") == set(), "a unit without routes")
    addresses = {"f_one": 0x80010000, "f_two": 0x80010010, "g_one": 0x80020000}
    check(table.problems(addresses, {"src/A.c", "src/B.c"}) == [], "a consistent table validates")
    renamed = dict(addresses, f_two=None, f_two_renamed=0x80010010)
    del renamed["f_two"]
    problems = table.problems(renamed, {"src/A.c", "src/B.c"})
    check(any("0x80010010" in p and "f_two_renamed" in p for p in problems),
          f"an upstream rename names the new name: {problems}")
    check(any("src/B.c" in p for p in table.problems(addresses, {"src/A.c"})),
          "a unit that is not compiled is reported (routing needs its definitions)")
    path.write_text(TOML.replace('default = "owned"', 'default = "mine"'))
    check(raises(lambda: ownership.load(path), "default"), "an unknown default is refused")
    path.write_text(TOML + '\n[subsystem.gamma]\noverlay="battle"\nunit="src/A.c"\ndefault="original"\n'
                    'functions=[{addr=0x80010000, name="f_one"}]\n')
    check(raises(lambda: ownership.load(path), "f_one"), "a function routed twice is refused")

print("FAILED" if failures else "all ownership checks passed")
sys.exit(1 if failures else 0)
```

`pc/CMakeLists.txt`:

```cmake
# The ownership table, the routed-definition rename and the generated routing (pc/tools/ownership.py).
add_test(NAME ownership COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ownership_test.py)
```

- [ ] **Step 3: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja && flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R '^ownership$' --output-on-failure
```

Expected: FAIL, `No module named 'ownership'`.

- [ ] **Step 4: `pc/tools/ownership.py` (first part)**

```python
#!/usr/bin/env python3
"""Owned game code (pc/docs/Ownership.md): the routing table game/ownership.toml
and the build steps that route a function between the original (src/) and
owned code (game/):

  - rename_definitions: in a unit's preprocessed text, the routed functions'
    definitions become name__orig; calls, prototypes and address-of uses keep
    the name, which the trampoline defines;
  - strip_bodies: the unit's declarations only (the input of zig translate-c);
  - routing_source: the trampolines (asm per ABI) and the subsystem tables;
  - ownership_zig: the table as a comptime Zig module.
"""
import dataclasses
import pathlib
import re
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OWNERS = ("original", "owned")
MAX_SUBSYSTEMS = 64  # pc_owner[] in ownership.c


@dataclasses.dataclass
class Subsystem:
    name: str
    index: int
    overlay: str
    unit: str
    default: str
    roots: list
    functions: list  # [(address, name)]
    views: dict


@dataclasses.dataclass
class Table:
    subsystems: list

    def routed_names(self, unit):
        return {name for s in self.subsystems if s.unit == unit for _, name in s.functions}

    def routes(self):
        """[(name, subsystem index)] in table order."""
        return [(name, s.index) for s in self.subsystems for _, name in s.functions]

    def problems(self, function_addresses, compiled_units):
        """Inconsistencies with the reference ELFs (name -> address) and the
        units this build compiles; [] when the table can be used."""
        by_address = {a: n for n, a in function_addresses.items() if a is not None}
        out = []
        for s in self.subsystems:
            if s.unit not in compiled_units:
                out.append(f"{s.name}: its unit {s.unit} is not compiled in this build")
            for address, name in s.functions:
                actual = by_address.get(address)
                if actual != name:
                    out.append(f"{s.name}: {address:#x} is {actual or 'no function'}, the table says {name}"
                               + (f" (renamed upstream? use {actual})" if actual else ""))
        return out


def load(path=ROOT / "game/ownership.toml"):
    data = tomllib.loads(pathlib.Path(path).read_text())
    subsystems, seen = [], {}
    for index, (name, entry) in enumerate(data.get("subsystem", {}).items()):
        default = entry.get("default", "original")
        if default not in OWNERS:
            raise ValueError(f"{name}: default {default!r} is not one of {', '.join(OWNERS)}")
        functions = [(int(f["addr"]), f["name"]) for f in entry.get("functions", [])]
        for _, fn in functions:
            if fn in seen:
                raise ValueError(f"{fn} is routed by both {seen[fn]} and {name}")
            seen[fn] = name
        subsystems.append(Subsystem(name, index, entry["overlay"], entry["unit"], default,
                                    list(entry.get("roots", [])), functions, dict(entry.get("views", {}))))
    if len(subsystems) > MAX_SUBSYSTEMS:
        raise ValueError(f"{len(subsystems)} subsystems: pc_owner[] holds {MAX_SUBSYSTEMS}")
    return Table(subsystems)
```

Add `"ownership"` to build_boot's stamped tools.

- [ ] **Step 5: Run the test**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R '^ownership$' --output-on-failure
```

Expected: `all ownership checks passed`.

- [ ] **Step 6: Commit**

```bash
git add game/ownership.toml pc/tools/ownership.py pc/tests/ownership_test.py pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "Ownership table (game/ownership.toml) keyed by PS1 address, and its reader

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Renaming routed definitions and stripping bodies (preprocessed C)

**Files:**
- Modify: `pc/tools/ownership.py`, `pc/tests/ownership_test.py`

- [ ] **Step 1: Add the failing cases**

Append to `pc/tests/ownership_test.py`, before the final `print`:

```python
UNIT = b'''# 1 "unit.c"
typedef struct { int a; } S;
int F(int);
int F2(int x) { return x; }
static int helper(int x) { return F(x) + 1; }
int F(int x)
{
    const char* s = "F(1) { not code }";
    return x * 2;
}
int (*table[])(int) = { F, F2 };
int G(void) { return F(1) + F(2) + helper(3); }
int K(a, b)
    int a;
    int b;
{ return a + b; }
__attribute__((noinline)) int H(void) { return F(0); }
'''

text, counts = ownership.rename_definitions(UNIT, {"F", "K", "H"})
check(counts == {"F": 1, "K": 1, "H": 1}, f"one definition each renamed: {counts}")
check(b"int F__orig(int x)\n{" in text, "the definition of F is renamed")
check(b"int F(int);" in text, "the prototype keeps F")
check(text.count(b"F(1)") == 2 and b"return F(x) + 1" in text, "calls keep F (and the string is untouched)")
check(b"{ F, F2 }" in text, "address-of uses keep F (function pointer tables route too)")
check(b"int F2(int x)" in text, "a name that only starts with F is untouched")
check(b"int K__orig(a, b)" in text, "a K&R definition is renamed")
check(b"int H__orig(void)" in text, "a definition with a leading attribute is renamed")
check(raises(lambda: ownership.rename_definitions(UNIT, {"helper"}), "static"),
      "a static definition cannot be routed")
_, missing = ownership.rename_definitions(UNIT, {"absent"})
check(missing == {"absent": 0}, "an absent definition counts 0 (build_boot stops on it)")

decls = ownership.strip_bodies(UNIT)
check(b"return" not in decls and b"int F(int x);" in decls and b"int G(void);" in decls,
      "strip_bodies keeps every declaration and drops the bodies")
check(b"typedef struct { int a; } S;" in decls and b"{ F, F2 }" in decls,
      "strip_bodies keeps struct bodies and initialisers")
check(b'# 1 "unit.c"' not in decls, "strip_bodies drops linemarkers")
```

- [ ] **Step 2: Run and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R '^ownership$' --output-on-failure
```

Expected: FAIL, `module 'ownership' has no attribute 'rename_definitions'`.

- [ ] **Step 3: Implement the scanner**

Append to `pc/tools/ownership.py`:

```python
# Preprocessed C as tokens: string and character literals (kept whole, so
# braces inside them do not count), linemarkers, identifiers, single chars.
_TOKEN = re.compile(rb'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|^#[^\n]*|[A-Za-z_]\w*|\S', re.M)


def _tokens(text):
    return [(m.start(), m.end(), m.group()) for m in _TOKEN.finditer(text)]


def _matching(tokens, i, open_=b"(", close=b")"):
    depth = 0
    for j in range(i, len(tokens)):
        if tokens[j][2] == open_:
            depth += 1
        elif tokens[j][2] == close:
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced " + open_.decode())


def _skip_attributes(tokens, k):
    while k < len(tokens) and tokens[k][2] in (b"__attribute__", b"__asm__", b"asm"):
        k = _matching(tokens, k + 1) + 1
    return k


def _body_start(tokens, k):
    """After a declarator's parameter list (and attributes) at k: the index of
    the body's '{' when this is a function definition (also K&R, whose
    parameter declarations come first), else None."""
    if k >= len(tokens):
        return None
    if tokens[k][2] == b"{":
        return k
    if not re.fullmatch(rb"[A-Za-z_]\w*", tokens[k][2]):
        return None  # ';' ',' '=' ')' ...: a declaration
    depth = 0
    for j in range(k, len(tokens)):
        t = tokens[j][2]
        if t == b"(":
            depth += 1
        elif t == b")":
            depth -= 1
        elif depth == 0 and t == b";":
            continue  # K&R parameter declarations end in ';'
        elif depth == 0 and t == b"{":
            return j
        elif depth == 0 and t in (b"=", b","):
            return None
    return None


def _file_scope_functions(text):
    """(name token index, '(' index, body '{' index, declaration start index)
    for every function definition at file scope."""
    tokens = _tokens(text)
    found, depth, start, i = [], 0, 0, 0
    while i < len(tokens):
        t = tokens[i][2]
        if t.startswith(b"#"):
            start = i + 1 if depth == 0 and start == i else start
            i += 1
            continue
        if t == b"{":
            depth += 1
        elif t == b"}":
            depth -= 1
            if depth == 0:
                start = i + 1
        elif t == b";" and depth == 0:
            start = i + 1
        elif depth == 0 and re.fullmatch(rb"[A-Za-z_]\w*", t) and i + 1 < len(tokens) and tokens[i + 1][2] == b"(":
            close = _matching(tokens, i + 1)
            body = _body_start(tokens, _skip_attributes(tokens, close + 1))
            if body is not None:
                found.append((i, i + 1, body, start))
                end = _matching(tokens, body, b"{", b"}")
                i, start = end + 1, end + 1
                continue
            i = close + 1
            continue
        i += 1
    return tokens, found


def rename_definitions(text, names):
    """Rename the file-scope definitions of `names` to name__orig in
    preprocessed C (bytes). Returns (text, {name: definitions renamed})."""
    tokens, found = _file_scope_functions(text)
    wanted = {n.encode(): n for n in names}
    counts = {n: 0 for n in names}
    edits = []
    for name_index, _, _, start in found:
        token = tokens[name_index][2]
        if token not in wanted:
            continue
        if b"static" in [t[2] for t in tokens[start:name_index]]:
            raise ValueError(f"{wanted[token]}: a static function cannot be routed (make the owned module "
                             "reimplement it, or route its global callers)")
        counts[wanted[token]] += 1
        edits.append((tokens[name_index][0], tokens[name_index][1], token + b"__orig"))
    for begin, end, new in reversed(edits):
        text = text[:begin] + new + text[end:]
    return text, counts


def strip_bodies(text):
    """The unit's declarations only: every function body becomes ';',
    linemarkers go (the input of zig translate-c)."""
    tokens, found = _file_scope_functions(text)
    pieces, last = [], 0
    for _, _, body, _ in found:
        end = _matching(tokens, body, b"{", b"}")
        pieces += [text[last:tokens[body][0]], b";\n"]
        last = tokens[end][1]
    pieces.append(text[last:])
    return re.sub(rb"(?m)^#[^\n]*\n?", b"", b"".join(pieces))
```

- [ ] **Step 4: Run the tests**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R '^ownership$' --output-on-failure
```

Expected: `all ownership checks passed`.

- [ ] **Step 5: Check it on the real unit (read-only)**

```bash
python3 - <<'EOF'
import sys; sys.path.insert(0, "pc/tools")
import ownership, pathlib
text = pathlib.Path("pc/build/boot/battle_146C.i").read_bytes()
names = ownership.load().routed_names("src/BATTLE/BATTLE.PRG/146C.c")
_, counts = ownership.rename_definitions(text, names)
print(counts)
print(len(ownership.strip_bodies(text).splitlines()), "declaration lines")
EOF
```

Expected: every routed name with count 1, and several thousand declaration lines.

- [ ] **Step 6: Commit**

```bash
git add pc/tools/ownership.py pc/tests/ownership_test.py
git commit -m "ownership.py: rename routed definitions in preprocessed C; declarations-only strip

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Trampolines, the owner switch, and the routing probe on three ABIs

**Files:**
- Create: `pc/tests/routing_probe/unit.c`, `owned.c`, `main.c`, `run.py`
- Modify: `pc/tools/ownership.py`, `pc/tests/ownership_test.py`, `pc/src/ownership.c`, `pc/CMakeLists.txt`

- [ ] **Step 1: The probe (the failing test)**

`pc/tests/routing_probe/unit.c` (an "original" unit; its calls to `F` must reach the trampoline even at `-O2`, where GCC would otherwise inline `F` into `G`):

```c
int log_[16], n;
void F(int a) { log_[n++] = a; }
void G(void) { F(1); F(2); }
void (*table[])(int) = { F };
```

`pc/tests/routing_probe/owned.c`:

```c
extern int log_[16], n;
void F__owned(int a) { log_[n++] = 100 + a; }
void F__shadow(int a) { log_[n++] = 200 + a; }
```

`pc/tests/routing_probe/main.c`:

```c
#include <stdio.h>
extern unsigned char pc_owner[];
extern int log_[16], n;
extern void (*table[])(int);
void G(void);
void F(int);
unsigned char pc_owner[64];
int main(void)
{
    G();             /* original: 1 2 */
    pc_owner[0] = 1;
    G();             /* owned, from inside the unit: 101 102 */
    table[0](7);     /* owned, through a pointer the unit took: 107 */
    pc_owner[0] = 2;
    F(3);            /* shadow: 203 */
    for (int i = 0; i < n; i++)
        printf("%d ", log_[i]);
    printf("\n");
    return 0;
}
```

`pc/tests/routing_probe/run.py`:

```python
#!/usr/bin/env python3
"""The routing mechanism (pc/docs/Ownership.md) on i386, x86-64 and MinGW:
a unit compiled at -O2 with its routed definition renamed, the generated
trampoline, and calls from inside the unit, from outside and through a
function pointer reach the owner pc_owner[] selects. Prints the log per ABI;
MinGW's runs under wine when present (else the link and the relocations are
checked)."""
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

EXPECTED = "1 2 101 102 107 203"
table = ownership.Table([ownership.Subsystem("probe", 0, "battle", "unit.c", "original", [],
                                             [(0, "F")], {})])
failures = []
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    for abi, cc, flags, exe in [("i386", "gcc", ["-m32", "-no-pie", "-fno-pie"], "probe32"),
                                ("sysv64", "gcc", ["-m64", "-no-pie", "-fpie"], "probe64"),
                                ("win64", "x86_64-w64-mingw32-gcc-posix", ["-mcmodel=small", "-static"], "probe.exe")]:
        if not shutil.which(cc):
            print(f"skip {abi}: no {cc}")
            continue
        pre = subprocess.run([cc, "-E", str(HERE / "unit.c")], capture_output=True, check=True).stdout
        text, counts = ownership.rename_definitions(pre, {"F"})
        assert counts == {"F": 1}, counts
        (d / f"unit-{abi}.i").write_bytes(text)
        (d / f"routing-{abi}.c").write_text(ownership.routing_source(table, abi))
        objects = []
        for name, source in [("unit", d / f"unit-{abi}.i"), ("routing", d / f"routing-{abi}.c"),
                             ("owned", HERE / "owned.c"), ("main", HERE / "main.c")]:
            obj = d / f"{name}-{abi}.o"
            subprocess.run([cc, *[f for f in flags if f not in ("-no-pie", "-static")], "-O2",
                            "-ffunction-sections", "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run([cc, *flags, *objects, "-o", str(d / exe)], check=True)
        if abi == "win64" and not shutil.which("wine"):
            dump = subprocess.run(["x86_64-w64-mingw32-objdump", "-dr", objects[0]], capture_output=True,
                                  text=True).stdout
            ok = "REL32\tF\n" in dump.replace("  ", "\t") or "IMAGE_REL_AMD64_REL32\tF" in dump
            print(f"{abi}: (no wine) G's calls relocate against F: {ok}")
            if not ok:
                failures.append(abi)
            continue
        runner = ["wine"] if abi == "win64" else []
        got = subprocess.run([*runner, str(d / exe)], capture_output=True, text=True).stdout.strip()
        print(f"{abi}: {got}")
        if got != EXPECTED:
            failures.append(f"{abi}: {got!r}, expected {EXPECTED!r}")
print("FAILED: " + "; ".join(failures) if failures else "routing works on every ABI")
sys.exit(1 if failures else 0)
```

`pc/CMakeLists.txt`:

```cmake
# Routing (pc/docs/Ownership.md): definition rename + trampolines on i386, x86-64 and MinGW.
add_test(NAME routing_probe COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_probe/run.py)
```

Add to `pc/tests/ownership_test.py` (before the final `print`):

```python
t = ownership.Table([ownership.Subsystem("s", 0, "battle", "u.c", "original", [], [(0, "F")], {}),
                     ownership.Subsystem("t", 1, "battle", "u.c", "owned", [], [(0, "G")], {})])
for abi in ("i386", "sysv64", "win64"):
    src = ownership.routing_source(t, abi)
    check("jmp F__orig" in src and "je F__owned" in src and "je F__shadow" in src, f"{abi}: F's trampoline")
    check(("pc_owner+1(%rip)" in src) == (abi != "i386") and ("pc_owner+1," in src) == (abi == "i386"),
          f"{abi}: G reads its own slot")
    check((".type F, @function" in src) == (abi != "win64"), f"{abi}: ELF symbol types only on ELF")
    check("%al" not in src and ("%r11d" in src) == (abi != "i386"), f"{abi}: no argument register clobbered")
    check('"s", "t"' in src and "pc_subsystem_defaults[] = { 0, 1 }" in src, f"{abi}: subsystem tables")
zig = ownership.ownership_zig(t)
check('.{ .name = "F", .subsystem = 0 }' in zig and "pub const battle_action" not in zig, "the comptime table")
```

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R 'ownership|routing_probe' --output-on-failure
```

Expected: FAIL, `no attribute 'routing_source'`.

- [ ] **Step 3: The generators**

Append to `pc/tools/ownership.py`:

```python
def routing_source(table, abi):
    """The trampolines (a tail jump by pc_owner[subsystem]: 0 F__orig,
    1 F__owned, 2 F__shadow) and the subsystem tables, as C with top-level
    asm. abi: i386 | sysv64 | win64. Only %eax (i386) or %r11 (x86-64) is
    used: neither carries an argument, and %al (variadic calls) is kept."""
    out = ["/* Generated by build_boot.py from game/ownership.toml (pc/tools/ownership.py):",
           " * one trampoline per routed function; pc_owner[]: ownership.c. */"]
    for name, index in table.routes():
        if abi == "i386":
            body = [f"movzbl pc_owner+{index}, %eax", "cmpl $1, %eax", f"je {name}__owned",
                    "cmpl $2, %eax", f"je {name}__shadow", f"jmp {name}__orig"]
        else:
            body = [f"movzbl pc_owner+{index}(%rip), %r11d", "cmpl $1, %r11d", f"je {name}__owned",
                    "cmpl $2, %r11d", f"je {name}__shadow", f"jmp {name}__orig"]
        head = [".text", f".globl {name}"] + ([] if abi == "win64" else [f".type {name}, @function"]) \
            + [".p2align 4", f"{name}:"]
        tail = [] if abi == "win64" else [f".size {name}, .-{name}"]
        asm = "\\n".join(head + ["    " + b for b in body] + tail)
        out.append(f'__asm__("{asm}\\n");')
    names = ", ".join(f'"{s.name}"' for s in table.subsystems) or "0"
    defaults = ", ".join(str(OWNERS.index(s.default)) for s in table.subsystems) or "0"
    routes = ", ".join(f'"{n}"' for n, _ in table.routes()) or "0"
    route_subsystems = ", ".join(str(i) for _, i in table.routes()) or "0"
    out += [f"const unsigned pc_subsystem_count = {len(table.subsystems)};",
            f"const char* const pc_subsystem_names[] = {{ {names} }};",
            f"const unsigned char pc_subsystem_defaults[] = {{ {defaults} }};",
            f"const unsigned pc_route_count = {len(table.routes())};",
            f"const char* const pc_route_names[] = {{ {routes} }};",
            f"const unsigned char pc_route_subsystems[] = {{ {route_subsystems} }};"]
    return "\n".join(out) + "\n"


def ownership_zig(table):
    """The table for owned code (comptime): subsystem slots and routes."""
    lines = ["//! Generated by build_boot.py from game/ownership.toml (pc/tools/ownership.py).",
             "pub const Route = struct { name: []const u8, subsystem: u8 };",
             "pub const Subsystem = enum(u8) {"]
    lines += [f"    {s.name} = {s.index}," for s in table.subsystems]
    lines += ["};", "pub const routes = [_]Route{"]
    lines += [f'    .{{ .name = "{n}", .subsystem = {i} }},' for n, i in table.routes()]
    lines += ["};",
              "/// The routed function names of one subsystem.",
              "pub fn namesOf(comptime s: Subsystem) []const []const u8 {",
              "    comptime var out: []const []const u8 = &.{};",
              "    inline for (routes) |r| {",
              "        if (r.subsystem == @intFromEnum(s)) out = out ++ [_][]const u8{r.name};",
              "    }",
              "    return out;",
              "}"]
    return "\n".join(lines) + "\n"
```

- [ ] **Step 4: The owner switch in `pc/src/ownership.c`**

Append:

```c
/* ---- who runs each subsystem ----
 * routing.c (generated) holds the trampolines and the tables; the trampolines
 * read pc_owner[subsystem]: 0 original (src/), 1 owned (game/), 2 shadow (both,
 * compared: state_diff.c). Host state: not saved by save states or rewinds. */
#include <string.h>
extern const unsigned pc_subsystem_count;
extern const char* const pc_subsystem_names[];
extern const unsigned char pc_subsystem_defaults[];
extern int pc_shadow_available(void) __attribute__((weak)); /* state_diff.c (Linux) */

unsigned char pc_owner[64];
static const char* const owner_labels[] = { "original", "owned", "shadow" };

int pc_owner_count(void) { return (int)pc_subsystem_count; }
const char* pc_owner_subsystem(int i) { return i >= 0 && i < (int)pc_subsystem_count ? pc_subsystem_names[i] : NULL; }
const char* pc_owner_label(int owner) { return owner >= 0 && owner <= 2 ? owner_labels[owner] : "?"; }
int pc_owner_get(int i) { return i >= 0 && i < (int)pc_subsystem_count ? pc_owner[i] : -1; }
int pc_owner_find(const char* name)
{
    for (unsigned i = 0; i < pc_subsystem_count; i++)
        if (!strcmp(pc_subsystem_names[i], name))
            return (int)i;
    return -1;
}
int pc_owner_parse(const char* label)
{
    for (int o = 0; o < 3; o++)
        if (!strcmp(owner_labels[o], label))
            return o;
    return -1;
}
/* 0 when set; -1 unknown subsystem or owner; -2 shadow unavailable here. Call
 * between frames (the RPC, the overlay): owned code keeps no state of its own,
 * so a switch at a frame boundary is safe. */
int pc_owner_set(int i, int owner)
{
    if (i < 0 || i >= (int)pc_subsystem_count || owner < 0 || owner > 2)
        return -1;
    if (owner == 2 && !(pc_shadow_available && pc_shadow_available()))
        return -2;
    if (pc_owner[i] != owner)
        fprintf(stderr, "[ownership] %s: %s\n", pc_subsystem_names[i], owner_labels[owner]);
    pc_owner[i] = (unsigned char)owner;
    return 0;
}

/* ROOD_OWNER=battle_action=owned,menus=original (applied over the defaults). */
__attribute__((constructor)) static void owner_defaults(void)
{
    for (unsigned i = 0; i < pc_subsystem_count && i < sizeof pc_owner; i++)
        pc_owner[i] = pc_subsystem_defaults[i];
    const char* env = getenv("ROOD_OWNER");
    char item[96];
    while (env && *env) {
        size_t len = strcspn(env, ",");
        snprintf(item, sizeof item, "%.*s", (int)len, env);
        env += len + (env[len] == ',');
        char* eq = strchr(item, '=');
        int i = eq ? (*eq = 0, pc_owner_find(item)) : -1;
        int owner = eq ? pc_owner_parse(eq + 1) : -1;
        if (i < 0 || owner < 0)
            fprintf(stderr, "[ownership] ROOD_OWNER: ignored %s\n", item);
        else
            pc_owner[i] = (unsigned char)owner; /* shadow is checked when the game starts (state_diff.c) */
    }
}
```

- [ ] **Step 5: Run the tests**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R 'ownership|routing_probe' --output-on-failure
```

Expected: `ownership` passes; `routing_probe` prints `i386: 1 2 101 102 107 203`, `sysv64: 1 2 101 102 107 203`, `win64: 1 2 101 102 107 203` (or the no-wine relocation line) and `routing works on every ABI`.

- [ ] **Step 6: Commit**

```bash
git add pc/tools/ownership.py pc/tests/ownership_test.py pc/tests/routing_probe pc/src/ownership.c pc/CMakeLists.txt
git commit -m "Routing: tail-jump trampolines per ABI, pc_owner[] switch, probe on i386/x86-64/MinGW

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Route battle actions through a pass-through owned module (all four targets)

The owned side first only calls the original (`F__owned` → `F__orig`), so routing is proven in the real game before any logic moves.

**Files:**
- Create: `game/common/route.zig`, `game/battle/root.zig`
- Modify: `game/common/common.zig`, `pc/tools/build_boot.py`

- [ ] **Step 1: `game/common/route.zig`**

```zig
//! Routing (pc/docs/Ownership.md): the exports the trampolines jump to.
//! For each routed function F of a subsystem, the owned module provides F
//! (an implementation with F's C signature) and this file exports
//!   F__owned   that implementation;
//!   F__shadow  the differential wrapper (state_diff.c; Task 18).
//! F__orig is the original, renamed by build_boot.py in its unit.
const std = @import("std");

/// The original implementation of `name` (its renamed definition).
pub fn original(comptime name: []const u8, comptime F: type) *const F {
    return @extern(*const F, .{ .name = name ++ "__orig" });
}

/// A function with F's C signature that calls `body(args)`.
fn withSignature(comptime F: type, comptime body: fn (std.meta.ArgsTuple(F)) @typeInfo(F).@"fn".return_type.?) F {
    const info = @typeInfo(F).@"fn";
    const R = info.return_type.?;
    const P = struct {
        fn t(comptime i: usize) type {
            return info.params[i].type.?;
        }
    };
    return switch (info.params.len) {
        0 => struct {
            fn f() callconv(.c) R {
                return body(.{});
            }
        }.f,
        1 => struct {
            fn f(a0: P.t(0)) callconv(.c) R {
                return body(.{a0});
            }
        }.f,
        2 => struct {
            fn f(a0: P.t(0), a1: P.t(1)) callconv(.c) R {
                return body(.{ a0, a1 });
            }
        }.f,
        3 => struct {
            fn f(a0: P.t(0), a1: P.t(1), a2: P.t(2)) callconv(.c) R {
                return body(.{ a0, a1, a2 });
            }
        }.f,
        4 => struct {
            fn f(a0: P.t(0), a1: P.t(1), a2: P.t(2), a3: P.t(3)) callconv(.c) R {
                return body(.{ a0, a1, a2, a3 });
            }
        }.f,
        else => @compileError("route: more than 4 parameters: extend withSignature"),
    };
}

fn Shadow(comptime subsystem: u8, comptime name: []const u8, comptime F: type, comptime owned: F) type {
    _ = subsystem;
    _ = owned;
    const R = @typeInfo(F).@"fn".return_type.?;
    return struct {
        // Task 18 replaces this with the comparison; until then shadow runs the original.
        fn run(args: std.meta.ArgsTuple(F)) R {
            return @call(.auto, original(name, F), args);
        }
        pub const call = withSignature(F, run);
    };
}

/// Export F__owned and F__shadow for every routed function of a subsystem;
/// `Impl` declares each one (pub, callconv(.c), the original's signature).
pub fn exportAll(comptime subsystem: u8, comptime names: []const []const u8, comptime Impl: type) void {
    inline for (names) |name| {
        if (!@hasDecl(Impl, name)) @compileError(name ++ " is routed but the owned module does not define it");
        const f = @field(Impl, name);
        const F = @TypeOf(f);
        @export(&f, .{ .name = name ++ "__owned" });
        @export(&Shadow(subsystem, name, F, f).call, .{ .name = name ++ "__shadow" });
    }
}
```

`game/common/common.zig` gains `pub const route = @import("route.zig");`.

- [ ] **Step 2: `game/battle/root.zig` (pass-through)**

```zig
//! battle_zig.o: BATTLE's owned subsystems (pc/docs/Ownership.md).
const common = @import("common");
const ownership = @import("ownership");
pub const panic = common.Panic;
const orig = common.route.original;

/// Battle action execution: for now every function runs the original.
const BattleAction = struct {
    pub fn func_80070F28(arg0: c_int) callconv(.c) void { orig("func_80070F28", @TypeOf(func_80070F28))(arg0); }
    pub fn func_8007138C() callconv(.c) void { orig("func_8007138C", @TypeOf(func_8007138C))(); }
    pub fn _handleCombatCameraZoom() callconv(.c) void { orig("_handleCombatCameraZoom", @TypeOf(_handleCombatCameraZoom))(); }
    pub fn func_80078748() callconv(.c) void { orig("func_80078748", @TypeOf(func_80078748))(); }
    pub fn func_800704B0() callconv(.c) void { orig("func_800704B0", @TypeOf(func_800704B0))(); }
    pub fn func_800787F0() callconv(.c) void { orig("func_800787F0", @TypeOf(func_800787F0))(); }
    pub fn func_80070278() callconv(.c) void { orig("func_80070278", @TypeOf(func_80070278))(); }
    pub fn func_80079030() callconv(.c) void { orig("func_80079030", @TypeOf(func_80079030))(); }
    pub fn func_80078AB4() callconv(.c) void { orig("func_80078AB4", @TypeOf(func_80078AB4))(); }
};

comptime {
    common.route.exportAll(@intFromEnum(ownership.Subsystem.battle_action),
        ownership.namesOf(.battle_action), BattleAction);
}
```

- [ ] **Step 3: Wire it into `build_boot.py`**

After `parsed = parser.parse_args()` processing (before the sources loop) load and keep the table:

```python
import ownership as ownership_tool  # noqa: E402  (pc/tools/ownership.py)

ownership_table = ownership_tool.load(root / "game/ownership.toml")
# Only subsystems whose unit this build compiles are routed (rood_boot has no BATTLE).
routed_units = {s.unit for s in ownership_table.subsystems}
```

In the sources loop, right after the 32/64-bit `encoded = result_cache.run(...)` line and before `pre.write_bytes(encoded)`:

```python
        routed = ownership_table.routed_names(source)
        if routed:
            encoded, counts = ownership_tool.rename_definitions(encoded, routed)
            bad = {n: c for n, c in counts.items() if c != 1}
            if bad:
                raise SystemExit(f"{source}: routed definitions found {bad} times (expected once each)")
```

and the same for the twin, where `win_pre.write_bytes(...)` is written:

```python
        win_text = lp64_longs(result_cache.run(...))  # (the existing expression)
        if ownership_table.routed_names(source):
            win_text, _ = ownership_tool.rename_definitions(win_text, ownership_table.routed_names(source))
        win_pre.write_bytes(win_text)
```

After the `function_addresses` table is built (the "Full reference ELF" loop), restrict the table to compiled units and validate:

```python
compiled_units = {source for source, _ in sources}
active = ownership_tool.Table([s for s in ownership_table.subsystems if s.unit in compiled_units])
problems = active.problems(function_addresses, compiled_units)
if problems:
    raise SystemExit("game/ownership.toml: " + "; ".join(problems))
```

Keep the subsystem indices of the full table (the slots are stable across builds): use `ownership_table` for `routing_source`, and only compile overlay objects for `active` subsystems. Generate and compile the routing and the battle object after the host units (next to `game_core`):

```python
zig_dir = out / "zig"
zig_dir.mkdir(exist_ok=True)
(zig_dir / "ownership.zig").write_text(ownership_tool.ownership_zig(ownership_table))
routing_c = out / "routing.c"
routing_c.write_text(ownership_tool.routing_source(active, "sysv64" if arch64 else "i386"))
if not coff:  # (not compile_generated: its twin would be compiled from the ELF asm)
    run(["gcc", *flags, "-c", str(routing_c), "-o", str(out / "routing.o")])
if win:  # the COFF twin's trampolines: no ELF symbol types
    (out / "routing_win.c").write_text(ownership_tool.routing_source(active, "win64"))
    run(win_command(["gcc", *flags, "-c", str(out / "routing_win.c"), "-o", str(out / "routing.o")]))
objects.append(out / "routing.o")
for overlay in sorted({s.overlay for s in active.subsystems}):
    objects.append(compile_zig(f"{overlay}_zig", {
        "root": (f"game/{overlay}/root.zig", ["common", "ownership"]),
        "common": ("game/common/common.zig", []),
        "ownership": (str(zig_dir / "ownership.zig"), []),
    }, "root", overlay=overlay))
```

(In M0 the battle builds route the whole table and `rood_boot` routes nothing, so `active` is all or none. If a build ever compiles only some routed units, write the subsystem tables from the full table (the `pc_owner[]` slots are `Subsystem.index`) and trampolines for the compiled ones only.)

After the `defined`/`undefined` scan (the loop that fills `defined`, `text_defined`, `undefined`, `calls`), check the routing is complete:

```python
for name, _ in active.routes():
    for needed in (name, name + "__orig", name + "__owned", name + "__shadow"):
        if needed not in text_defined:
            raise SystemExit(f"routing: {needed} is not defined (game/ownership.toml, the owned module)")
```

- [ ] **Step 4: Build all four targets; the counts are unchanged**

```bash
for args in "" "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error|Error|routing' ; done
nm pc/build/boot/rood_battle_probe | grep -E ' func_80070F28(__orig|__owned|__shadow)?$'
```

Expected: `Zig: battle_zig.o (...)` on the three battle targets (not on `rood_boot`), the four `Built` lines with Task 1's data-import and trap counts, and four `T` symbols for `func_80070F28`.

- [ ] **Step 5: The game behaves the same with the pass-through owner**

```bash
flock "$ROOD_HEAVY_LOCK" env ROOD_OWNER=battle_action=owned python3 pc/tests/run.py -j 2 skip_charging rpc_smoke
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 ROOD_OWNER=battle_action=owned python3 pc/tests/run.py -j 2 --arch 64 skip_charging
```

Expected: both pass (the game logs `[ownership]` nothing for defaults; with the env each run's log has no `ignored`).

- [ ] **Step 6: Commit**

```bash
git add game pc/tools/build_boot.py
git commit -m "Route battle actions (146C.c) through trampolines to a pass-through owned module

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: `owner` over RPC, in the overlay and in `rood_rpc.py`; `ownership_smoke`

**Files:**
- Create: `pc/tests/ownership_smoke.py` (first part)
- Modify: `pc/src/dev_rpc.c`, `pc/src/dev_overlay.cpp`, `pc/tools/rood_rpc.py`, `pc/tests/run.py`

- [ ] **Step 1: The failing smoke**

`pc/tests/ownership_smoke.py`:

```python
#!/usr/bin/env python3
"""Owned code's routing in one running game (pc/docs/Ownership.md):

  - RPC owner lists the subsystems with their owner and routes; ROOD_OWNER
    applies at start; set/get round-trips; unknown names fail;
  - (Task 15) the in-memory rewind is exact; (Task 18) shadow mode reports
    nothing for the pass-through module and reports an injected fault.

    python3 pc/tests/ownership_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]
"""
import argparse
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", default=str(OUT / "ownership_smoke"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    env = {"ROOD_GI": "0", "ROOD_DETERMINISTIC": "1", "ROOD_OWNER": "battle_action=owned"}
    g = launch_at("save_room", card=args.card, fast=True, log=out / "game.log", extra_env=env)
    try:
        listing = g.owner()
        battle = next((s for s in listing["subsystems"] if s["name"] == "battle_action"), None)
        check(battle is not None and battle["routes"] >= 8, f"battle_action listed with its routes: {battle}")
        check(battle and battle["owner"] == "owned", "ROOD_OWNER applied at start")
        g.owner("battle_action", "original")
        check(g.owner()["subsystems"][battle["index"]]["owner"] == "original", "set original")
        try:
            g.owner("no_such_subsystem", "owned")
            check(False, "an unknown subsystem fails")
        except RpcError:
            check(True, "an unknown subsystem fails")
        g.run(120)
        check("[crash]" not in pathlib.Path(g.log).read_text(errors="replace"), "no crash")
    finally:
        g.close()
    print("FAILED" if failures else "ownership smoke passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`'s `suites()`, next to the other card suites:

```python
            Suite("ownership_smoke", [PY, str(TESTS / "ownership_smoke.py"), "--card", str(args.save)], 300,
                  note="owned code: owner RPC and ROOD_OWNER, in-memory rewind exact, shadow mode clean "
                       "for the pass-through and catching an injected fault"),
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
```

Expected: FAIL, `AttributeError: 'Game' object has no attribute 'owner'`.

- [ ] **Step 3: The RPC method**

In `pc/src/dev_rpc.c`, document it in the method list comment (next to `snapshot`):

```c
 *   owner {}                     owned code (pc/docs/Ownership.md): the subsystems,
 *                                their owner (original, owned, shadow) and routes
 *   owner {subsystem, owner}     set one (between frames; shadow: Linux only)
```

declare the ownership API near the other externs:

```c
extern int pc_owner_count(void), pc_owner_get(int), pc_owner_find(const char*), pc_owner_parse(const char*);
extern int pc_owner_set(int, int);
extern const char* pc_owner_subsystem(int);
extern const char* pc_owner_label(int);
extern const unsigned pc_route_count;
extern const unsigned char pc_route_subsystems[];
```

and add the method in `handle_method` (before `wait`):

```c
    } else if (!strcmp(method, "owner")) {
        char name[64] = "", label[16] = "";
        if (params && string_value(member(params, "subsystem"), name, sizeof name)) {
            string_value(member(params, "owner"), label, sizeof label);
            int i = pc_owner_find(name), o = pc_owner_parse(label);
            int status = i < 0 || o < 0 ? -1 : pc_owner_set(i, o);
            if (status == -2) {
                fail(fd, id, -32000, "owner shadow needs save states (Linux)");
                return;
            }
            if (status) {
                fail(fd, id, -32602, "owner: subsystem from owner {}, owner original|owned|shadow");
                return;
            }
        }
        int n = snprintf(result, sizeof result, "{\"subsystems\":[");
        for (int i = 0; i < pc_owner_count() && n < (int)sizeof result - 160; i++) {
            int routes = 0;
            for (unsigned r = 0; r < pc_route_count; r++)
                routes += pc_route_subsystems[r] == i;
            n += snprintf(result + n, sizeof result - n, "%s{\"index\":%d,\"name\":\"%s\",\"owner\":\"%s\",\"routes\":%d}",
                i ? "," : "", i, pc_owner_subsystem(i), pc_owner_label(pc_owner_get(i)), routes);
        }
        snprintf(result + n, sizeof result - n, "]}");
        reply(fd, id, result);
```

(Subsystems without routes in this build, e.g. in `rood_boot`, list `routes: 0`; setting them is harmless.)

- [ ] **Step 4: `rood_rpc.py` and the overlay**

`pc/tools/rood_rpc.py`, in `class Game`:

```python
    def owner(self, subsystem=None, owner=None):
        """Owned code (pc/docs/Ownership.md): the subsystems and their owners,
        after setting `subsystem` to `owner` (original, owned, shadow) if given."""
        if subsystem is None:
            return self.call("owner")
        return self.call("owner", subsystem=subsystem, owner=owner)
```

`pc/src/dev_overlay.cpp`, in the developer panel next to the other gameplay controls (the "Battle charging" combo is a good neighbour):

```cpp
extern "C" int pc_owner_count(void), pc_owner_get(int), pc_owner_set(int, int);
extern "C" const char* pc_owner_subsystem(int);
...
    if (ImGui::CollapsingHeader("Ownership")) {
        for (int i = 0; i < pc_owner_count(); i++) {
            int owner = pc_owner_get(i);
            ImGui::PushID(i);
            if (ImGui::Combo(pc_owner_subsystem(i), &owner, "Original (src/)\0Owned (game/)\0Shadow (compare)\0")
                && pc_owner_set(i, owner) == -2)
                owner = pc_owner_get(i); /* shadow needs save states */
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Who runs this subsystem (pc/docs/Ownership.md). Shadow runs both on every\n"
                                  "call and logs differences (RPC owner {action: \"report\"}).");
            ImGui::PopID();
        }
    }
```

- [ ] **Step 5: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
```

Expected: `ok:` lines and `ownership smoke passed`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/dev_rpc.c pc/src/dev_overlay.cpp pc/tools/rood_rpc.py pc/tests/ownership_smoke.py pc/tests/run.py
git commit -m "owner: RPC, developer overlay, rood_rpc helper, ownership_smoke

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: The call-graph and state mapper (`subsystem_map.py`)

**Files:**
- Create: `pc/tools/subsystem_map.py`, `pc/tests/subsystem_map_test.py`
- Modify: `pc/CMakeLists.txt`

- [ ] **Step 1: The failing test**

`pc/tests/subsystem_map_test.py`:

```python
#!/usr/bin/env python3
"""pc/tools/subsystem_map.py on a synthetic unit: closure from seeds, entry
points, externs, globals read/written with field paths, statics flagged."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import subsystem_map  # noqa: E402

UNIT = r"""
struct In { short b; };
struct S { int a; struct In in[4]; };
extern struct S* gs;
extern int mode;
int counter;
int ext(int);
static int helper(int x) { return x + 1; }
void leaf(void) { mode = 4; }
void mid(int x) { gs->in[x].b = 3; counter++; leaf(); }
void top(void) { if (gs->a) mid(ext(helper(1))); }
void outside(void) { top(); }
void boundary_fn(void) { counter = 0; }
void top2(void) { boundary_fn(); }
"""
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


with tempfile.TemporaryDirectory() as d:
    unit = pathlib.Path(d) / "unit.i"
    unit.write_bytes(subprocess.run(["gcc", "-E", "-x", "c", "-"], input=UNIT.encode(), capture_output=True,
                                    check=True).stdout)
    m = subsystem_map.map_unit(unit, seeds=["top", "top2"], boundary=["boundary_fn"], target="i386")
    check(set(m["closure"]) == {"top", "mid", "leaf", "helper", "top2"}, f"closure: {sorted(m['closure'])}")
    check(m["entries"] == ["top", "top2"], f"entry points (seeds, and functions called from outside the set): "
          f"{m['entries']}")
    check(m["externs"] == ["boundary_fn", "ext"], f"externs: {m['externs']}")
    check(m["statics"] == ["helper"], f"static functions in the closure: {m['statics']}")
    f = m["functions"]
    check("gs->a" in f["top"]["reads"], f"top reads gs->a: {f['top']['reads']}")
    check("gs->in[].b" in f["mid"]["writes"], f"mid writes gs->in[].b: {f['mid']['writes']}")
    check("counter" in f["mid"]["writes"] and "counter" in f["mid"]["reads"], "counter++ is read and written")
    check(f["leaf"]["writes"] == ["mode"], f"leaf writes mode: {f['leaf']['writes']}")
    check(m["state"]["writes"] == ["counter", "gs->in[].b", "mode"], f"the set's writes: {m['state']['writes']}")
    check(m["globals"] == ["counter", "gs", "mode"], f"globals for the views manifest: {m['globals']}")

print("FAILED" if failures else "subsystem map checks passed")
sys.exit(1 if failures else 0)
```

`pc/CMakeLists.txt`:

```cmake
# The call-graph and state mapper (pc/tools/subsystem_map.py) on a synthetic unit.
add_test(NAME subsystem_map COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/subsystem_map_test.py)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja && flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R subsystem_map --output-on-failure
```

Expected: FAIL, `No module named 'subsystem_map'`.

- [ ] **Step 3: Write `pc/tools/subsystem_map.py`**

```python
#!/usr/bin/env python3
"""What a subsystem is and what it touches (pc/docs/Ownership.md, "Finding a
subsystem's boundary"), from a unit's preprocessed source and its object:

    python3 pc/tools/subsystem_map.py battle_action [--arch 32|64] [--seed F ...] [--boundary F ...]

Source: clang's JSON AST of the unit's .i (only the bodies of candidate
functions are kept, so the AST stays small), per function: callees, globals
read and written with field paths, statics. Object: the relocations of each
function's .text.<name> section (catches what macros and .inc files add).
Writes pc/build/<tree>/map/<subsystem>.json and .md and prints the summary.
Seeds default to the subsystem's routed functions (game/ownership.toml).
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

CLANG_TARGET = {"i386": "i386-unknown-linux-gnu", "x86_64": "x86_64-unknown-linux-gnu"}
IDENT = re.compile(rb"[A-Za-z_]\w*")


def candidate_calls(text):
    """{function: identifiers it calls} from the token stream (an over-approximation)."""
    tokens, found = ownership._file_scope_functions(text)
    calls = {}
    for name_index, _, body, _ in found:
        end = ownership._matching(tokens, body, b"{", b"}")
        names = {tokens[j][2].decode() for j in range(body, end)
                 if IDENT.fullmatch(tokens[j][2]) and j + 1 < len(tokens) and tokens[j + 1][2] == b"("}
        calls[tokens[name_index][2].decode()] = names
    return calls


def keep_bodies(text, keep):
    """The unit with only `keep`'s function bodies (others become prototypes)."""
    tokens, found = ownership._file_scope_functions(text)
    pieces, last = [], 0
    for name_index, _, body, _ in found:
        if tokens[name_index][2].decode() in keep:
            continue
        end = ownership._matching(tokens, body, b"{", b"}")
        pieces += [text[last:tokens[body][0]], b";\n"]
        last = tokens[end][1]
    pieces.append(text[last:])
    return b"".join(pieces)


def clang_ast(text, target):
    result = subprocess.run(["clang", "-target", CLANG_TARGET[target], "-fsyntax-only", "-w", "-std=gnu17",
                             "-funsigned-char", "-fno-builtin", "-Xclang", "-ast-dump=json", "-x", "c", "-"],
                            input=text, capture_output=True)
    if not result.stdout:
        raise SystemExit("clang: " + result.stderr.decode(errors="replace")[:2000])
    return json.loads(result.stdout)


def _strip(node):
    while node.get("kind") in ("ImplicitCastExpr", "ParenExpr", "CStyleCastExpr") and node.get("inner"):
        node = node["inner"][0]
    return node


class Access:
    def __init__(self, globals_):
        self.globals = globals_  # VarDecl id -> name
        self.reads, self.writes, self.calls, self.addr = set(), set(), set(), set()

    def path(self, node):
        """(field path, index expressions) when node is a chain rooted at a global."""
        node = _strip(node)
        kind = node.get("kind")
        if kind == "DeclRefExpr":
            decl = node.get("referencedDecl", {})
            name = self.globals.get(decl.get("id"))
            return (name, []) if name else (None, [])
        if kind == "MemberExpr":
            base, extra = self.path(node["inner"][0])
            if base:
                return base + ("->" if node.get("isArrow") else ".") + node["name"], extra
            return None, extra
        if kind == "ArraySubscriptExpr":
            base, extra = self.path(node["inner"][0])
            return (base + "[]" if base else None), extra + [node["inner"][1]]
        if kind == "UnaryOperator" and node.get("opcode") == "*":
            base, extra = self.path(node["inner"][0])
            return ("*" + base if base else None), extra
        return None, []

    def visit(self, node, mode="read"):
        node = node if isinstance(node, dict) else {}
        kind = node.get("kind")
        if kind == "CallExpr":
            callee = _strip(node["inner"][0])
            if callee.get("kind") == "DeclRefExpr" and callee.get("referencedDecl", {}).get("kind") == "FunctionDecl":
                self.calls.add(callee["referencedDecl"]["name"])
            else:
                self.visit(node["inner"][0])
            for arg in node["inner"][1:]:
                self.visit(arg)
            return
        if kind in ("BinaryOperator", "CompoundAssignOperator") and node.get("opcode", "").endswith("=") \
                and node.get("opcode") not in ("==", "!=", "<=", ">="):
            self.visit(node["inner"][0], "write" if node["opcode"] == "=" else "readwrite")
            self.visit(node["inner"][1])
            return
        if kind == "UnaryOperator" and node.get("opcode") in ("++", "--"):
            self.visit(node["inner"][0], "readwrite")
            return
        if kind == "UnaryOperator" and node.get("opcode") == "&":
            path, extra = self.path(node["inner"][0])
            if path:
                self.addr.add(path)
                for e in extra:
                    self.visit(e)
                return
        if kind in ("DeclRefExpr", "MemberExpr", "ArraySubscriptExpr") or \
                (kind == "UnaryOperator" and node.get("opcode") == "*"):
            path, extra = self.path(node)
            if path:
                if mode in ("read", "readwrite"):
                    self.reads.add(path)
                if mode in ("write", "readwrite"):
                    self.writes.add(path)
                # the pointer itself is read on the way to a field
                root = re.match(r"\*?(\w+)(->)?", path)
                if root and root.group(2):
                    self.reads.add(root.group(1))
                for e in extra:
                    self.visit(e)
                return
        for child in node.get("inner", []):
            self.visit(child)


def map_unit(unit, seeds, boundary=(), target="i386", obj=None):
    text = pathlib.Path(unit).read_bytes()
    candidates = candidate_calls(text)
    closure, todo = set(), [s for s in seeds if s in candidates]
    while todo:  # over-approximate closure, then refine with the AST
        f = todo.pop()
        if f in closure or f in boundary:
            continue
        closure.add(f)
        todo += [c for c in candidates.get(f, ()) if c in candidates and c not in boundary]
    ast = clang_ast(keep_bodies(text, closure), target)
    top = ast.get("inner", [])
    globals_ = {n["id"]: n["name"] for n in top if n.get("kind") == "VarDecl"}
    statics = {n["name"] for n in top if n.get("kind") == "FunctionDecl" and n.get("storageClass") == "static"}
    functions = {}
    for n in top:
        if n.get("kind") == "FunctionDecl" and n["name"] in closure and any(
                c.get("kind") == "CompoundStmt" for c in n.get("inner", [])):
            a = Access(globals_)
            for c in n["inner"]:
                a.visit(c)
            functions[n["name"]] = {"calls": sorted(a.calls), "reads": sorted(a.reads), "writes": sorted(a.writes),
                                    "address_taken": sorted(a.addr)}
    closure = set(functions)
    callers = {}
    for name, info in candidates.items():
        for c in info:
            callers.setdefault(c, set()).add(name)
    entries = sorted(f for f in closure if f in seeds or callers.get(f, set()) - closure)
    externs = sorted({c for f in closure for c in functions[f]["calls"]} - closure)
    reads = sorted({r for f in closure for r in functions[f]["reads"]})
    writes = sorted({w for f in closure for w in functions[f]["writes"]})
    roots = sorted({re.match(r"\*?(\w+)", p).group(1) for p in reads + writes})
    result = {"closure": sorted(closure), "entries": entries, "externs": externs,
              "statics": sorted(closure & statics), "functions": functions,
              "state": {"reads": reads, "writes": writes}, "globals": roots}
    if obj:
        result["relocations"] = relocation_check(obj, functions)
    return result


def relocation_check(obj, functions):
    """Symbols each function's section references that the AST did not see."""
    dump = subprocess.run(["objdump", "-r", str(obj)], capture_output=True, text=True).stdout
    extra, section = {}, None
    for line in dump.splitlines():
        head = re.match(r"RELOCATION RECORDS FOR \[\.text\.(\w+)\]", line)
        if head:
            section = head.group(1)
            continue
        rel = re.match(r"[0-9a-f]+\s+R_\S+\s+([\w.]+)", line)
        if rel and section in functions:
            name = rel.group(1).split("-")[0]
            info = functions[section]
            seen = set(info["calls"]) | {re.match(r"\*?(\w+)", p).group(1) for p in info["reads"] + info["writes"]
                                         + info["address_taken"]}
            if not name.startswith(".") and name not in seen and name.removesuffix("__orig") not in seen:
                extra.setdefault(section, set()).add(name)
    return {k: sorted(v) for k, v in extra.items()}


def markdown(name, m):
    lines = [f"# {name}", "", f"Closure ({len(m['closure'])}): " + ", ".join(m["closure"]), "",
             "Entry points (route these): " + ", ".join(m["entries"]), "",
             "Externs (stay original, called through `c`): " + ", ".join(m["externs"]), ""]
    if m["statics"]:
        lines += ["Static functions in the closure (reimplement or keep as externs): " + ", ".join(m["statics"]), ""]
    lines += ["| function | reads | writes |", "|---|---|---|"]
    for f, info in sorted(m["functions"].items()):
        lines.append(f"| {f} | {', '.join(info['reads'])} | {', '.join(info['writes'])} |")
    for f, names in sorted(m.get("relocations", {}).items()):
        lines.append(f"\nObject-only references of {f}: {', '.join(names)}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subsystem")
    parser.add_argument("--arch", choices=["32", "64"], default="32")
    parser.add_argument("--seed", action="append")
    parser.add_argument("--boundary", action="append", default=[])
    parser.add_argument("--unit", type=pathlib.Path, help="the .i to map (default: the build's)")
    args = parser.parse_args()
    table = ownership.load()
    sub = next(s for s in table.subsystems if s.name == args.subsystem)
    tree = ROOT / ("pc/build/boot64" if args.arch == "64" else "pc/build/boot")
    stem = sub.overlay + "_" + pathlib.Path(sub.unit).stem
    seeds = args.seed or [n for _, n in sub.functions]
    m = map_unit(args.unit or tree / f"{stem}.i", seeds, args.boundary, "x86_64" if args.arch == "64" else "i386",
                 obj=tree / f"{stem}.o")
    directory = tree / "map"
    directory.mkdir(exist_ok=True)
    (directory / f"{sub.name}.json").write_text(json.dumps(m, indent=1, sort_keys=True) + "\n")
    (directory / f"{sub.name}.md").write_text(markdown(sub.name, m))
    print(markdown(sub.name, m))


if __name__ == "__main__":
    main()
```

(Routed definitions are named `F__orig` in the `.i` build_boot wrote; `map_unit` sees them as `F__orig`. For the battle map, run it on a `.i` produced with routing off — Step 5 — or pass `--seed func_80070F28__orig ...`. The mapper also strips `__orig` when comparing relocations.)

- [ ] **Step 4: Run the test**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R subsystem_map --output-on-failure
```

Expected: `subsystem map checks passed`. (If clang's JSON names a field differently in clang 18, e.g. `isArrow`, adapt `Access.path` and keep the test.)

- [ ] **Step 5: Map battle actions (the boundary for Tasks 13 and 22–26)**

```bash
python3 - <<'EOF'
import pathlib, sys; sys.path.insert(0, "pc/tools")
import ownership
p = pathlib.Path("pc/build/boot/battle_146C.i")
text = p.read_bytes().replace(b"__orig(", b"(")   # the original names back, for mapping only
pathlib.Path("pc/build/boot/map-146C.i").write_bytes(text)
EOF
python3 pc/tools/subsystem_map.py battle_action --unit pc/build/boot/map-146C.i \
  --boundary func_800708EC --boundary func_80070CAC --boundary func_80070B04 \
  --boundary func_8007820C --boundary func_800780A8 --boundary _computeStepAndDisplacement \
  --boundary func_80078828 | tee pc/build/boot/map/battle_action.txt
```

Expected: the closure is exactly the nine routed functions; no static functions; externs include `func_800CF0E8`, `func_8009E2E0`, `vs_main_panSfx`, `vs_battle_setStateFlag`, `func_800CEEBC`; the writes include `_cameraMode`, `D_800F19CC->unk854[].…`, `vs_main_scoredata.healCount`, `vs_main_projectionDistance`, `vs_gametime_tickspeed`. Keep `pc/build/boot/map/battle_action.json`: Task 13's manifest is its `globals` plus the types they reach.

- [ ] **Step 6: Commit**

```bash
git add pc/tools/subsystem_map.py pc/tests/subsystem_map_test.py pc/CMakeLists.txt
git commit -m "subsystem_map.py: call graph, entry points, externs and field-level state of a subsystem

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 12: Typed views from DWARF (`zig_views.py`)

**Files:**
- Create: `pc/tools/zig_views.py`, `pc/tests/zig_views_test.py`
- Modify: `pc/CMakeLists.txt`, `pc/tools/build_boot.py` (stamped tools: `"zig_views"`)

- [ ] **Step 1: The failing test**

`pc/tests/zig_views_test.py`:

```python
#!/usr/bin/env python3
"""pc/tools/zig_views.py: a synthetic unit with bitfields (unsigned chars,
-funsigned-char), a psx_ptr (4 bytes on both builds), a union, arrays, a
native pointer, a nested anonymous struct and a packed struct is compiled for
32 and 64 bits; its DWARF becomes Zig views; a C program writes a filled
instance's bytes and `zig test` reads every field back through the views
(the generated comptime asserts also check every offset and size)."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import zig_toolchain  # noqa: E402
import zig_views  # noqa: E402

UNIT = r"""
#include "pc/src/psx_types.h"
typedef struct { unsigned char lo : 3; unsigned char hi : 5; signed char s : 4; } Bits;
typedef struct Node Node;
struct Node { psx_ptr(Node) next; short x, y; };
typedef struct {
    char id;
    Bits bits;
    union { int i; short h[2]; } u;
    short arr[3];
    Node* native;
    Node node;
    struct { int z; } inner;
} Rec;
typedef struct __attribute__((packed)) { char c; short s; } Packed;
Rec rec;
Packed packed;
"""
FILL = r"""
#include <stdio.h>
#include <string.h>
int main(void) {
    memset(&rec, 0, sizeof rec);
    rec.id = 200; rec.bits.lo = 5; rec.bits.hi = 17; rec.bits.s = -3;
    rec.u.h[0] = 0x1234; rec.u.h[1] = -2; rec.arr[2] = 77;
    rec.native = (Node*)(unsigned long)0x4000; psx_set_raw(rec.node.next, Node*, 0x1000);
    rec.node.y = -9; rec.inner.z = 123456;
    for (unsigned i = 0; i < sizeof rec; i++) printf("%u,", ((unsigned char*)&rec)[i]);
    return 0;
}
"""
TEST = """
const std = @import("std");
const psx = @import("psx");
const v = @import("views");
const bytes align(16) = [_]u8{ %s };
test "fields through the views" {
    const r: *const v.Rec = @ptrCast(&bytes);
    try std.testing.expectEqual(@as(u8, 200), r.id);
    try std.testing.expectEqual(@as(u3, 5), psx.getBits(&r.bits, "lo"));
    try std.testing.expectEqual(@as(u5, 17), psx.getBits(&r.bits, "hi"));
    try std.testing.expectEqual(@as(i4, -3), psx.getBits(&r.bits, "s"));
    try std.testing.expectEqual(@as(i16, 0x1234), r.u.h[0]);
    try std.testing.expectEqual(@as(i16, -2), r.u.h[1]);
    try std.testing.expectEqual(@as(i16, 77), r.arr[2]);
    try std.testing.expectEqual(@as(usize, 0x4000), @intFromPtr(r.native.?));
    try std.testing.expectEqual(@as(usize, 0x1000), @intFromPtr(psx.get(r.node.next).?));
    try std.testing.expectEqual(@as(i16, -9), r.node.y);
    try std.testing.expectEqual(@as(i32, 123456), r.inner.z);
    try std.testing.expectEqual(@as(usize, 1), @offsetOf(v.Packed, "s"));
}
"""
failures = []
zig = str(zig_toolchain.ensure())
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "unit.c").write_text(UNIT)
    (d / "fill.c").write_text('#include "unit.c"\n' + FILL)
    for bits, flags, target in [("32", ["-m32"], "x86-linux-musl"),
                                ("64", ["-m64", "-DROOD_PC_LP64"], "x86_64-linux-musl")]:
        common = [*flags, "-g", "-funsigned-char", "-DROOD_PC", f"-I{ROOT}", f"-I{d}"]
        obj = d / f"unit{bits}.o"
        subprocess.run(["gcc", *common, "-c", str(d / "unit.c"), "-o", str(obj)], check=True)
        subprocess.run(["gcc", *common, str(d / "fill.c"), "-o", str(d / f"fill{bits}")], check=True)
        values = subprocess.run([str(d / f"fill{bits}")], capture_output=True, text=True, check=True).stdout
        dump = zig_views.dump_with_gdb(obj, ["Rec", "Packed", "Node"], ["rec", "packed"], d / f"dump{bits}.json")
        (d / f"views{bits}.zig").write_text(zig_views.emit(dump, psx_import="psx"))
        (d / f"test{bits}.zig").write_text(TEST % values.rstrip(","))
        result = subprocess.run([zig, "test", "-target", target, "--dep", "psx", "--dep", "views",
                                 f"-Mroot={d / f'test{bits}.zig'}", "--dep", "psx", f"-Mviews={d / f'views{bits}.zig'}",
                                 f"-Mpsx={ROOT / 'game/common/psx.zig'}"], capture_output=True, text=True)
        ok = result.returncode == 0
        print(("ok:   " if ok else "FAIL: ") + f"{bits}-bit views read every field")
        if not ok:
            print(result.stderr[-3000:])
            print((d / f"views{bits}.zig").read_text()[:3000])
            failures.append(bits)
print("FAILED" if failures else "zig views checks passed")
sys.exit(1 if failures else 0)
```

`pc/CMakeLists.txt` (inside the `if(ROOD_ZIG_MISSING EQUAL 0)` block):

```cmake
    # Views generated from DWARF (pc/tools/zig_views.py) read a C-filled instance on 32 and 64 bits.
    add_test(NAME zig_views COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/zig_views_test.py)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja && flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_views --output-on-failure
```

Expected: FAIL, `No module named 'zig_views'`.

- [ ] **Step 3: Write `pc/tools/zig_views.py`**

```python
#!/usr/bin/env python3
"""Typed views of game state for owned Zig code (pc/docs/Ownership.md), from
the DWARF of the original object of the same build: the layouts GCC used on
that target (ILP32; LP64 with psx_ptr; the native moves), so the views are
right on every build by construction.

Two halves:
  - inside gdb (dump): the requested types and globals as JSON (fields with
    bit positions and sizes, signedness, arrays, pointers, psx_ptr);
  - emit(dump): the Zig module: extern structs/unions with explicit padding,
    align(1) where C is misaligned, psx.Ptr(T) for psx_ptr fields, a
    `bitfields` table per struct (psx.getBits/setBits), comptime asserts of
    every offset and size, and `g.<global>()` accessors (psx.global).
Pointer targets outside the requested set are ?*anyopaque.
"""
import json
import pathlib
import re
import subprocess
import sys

try:
    import gdb  # noqa: F401  (inside gdb)
    IN_GDB = True
except ImportError:
    IN_GDB = False


# ---------------------------------------------------------------- gdb half
def _dump(type_names, global_names):
    wanted, order, types = set(type_names), [], {}

    def is_signed(t):
        try:
            return bool(t.is_signed)
        except AttributeError:
            return not re.match(r"(unsigned|u_|_Bool|bool)", str(t.strip_typedefs()))

    def is_psx_ptr(t):
        names = [f.name for f in t.fields()]
        return names == ["raw", "psx_type_"]

    def ref(t, context):
        if t.code == gdb.TYPE_CODE_TYPEDEF:
            target = t.strip_typedefs()
            if target.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
                if is_psx_ptr(target):
                    return {"kind": "psx_ptr", "target": ref(target.fields()[1].type.target().target(), context)}
                return named(str(t), target)
            return ref(target, context)
        code = t.code
        if code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_CHAR, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_ENUM):
            return {"kind": "int", "size": t.sizeof, "signed": is_signed(t)}
        if code == gdb.TYPE_CODE_FLT:
            return {"kind": "float", "size": t.sizeof}
        if code == gdb.TYPE_CODE_PTR:
            target = t.target()
            stripped = target.strip_typedefs()
            if stripped.code == gdb.TYPE_CODE_FUNC:
                return {"kind": "fnptr"}
            if target.code == gdb.TYPE_CODE_TYPEDEF and str(target) in wanted:
                return {"kind": "ptr", "target": ref(target, context)}
            if stripped.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION) and stripped.tag in wanted:
                return {"kind": "ptr", "target": named(stripped.tag, stripped)}
            if stripped.code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_CHAR):
                return {"kind": "ptr", "target": ref(stripped, context)}
            return {"kind": "ptr", "target": {"kind": "opaque", "name": str(target)}}
        if code == gdb.TYPE_CODE_ARRAY:
            low, high = t.range()
            return {"kind": "array", "count": max(0, high - low + 1), "elem": ref(t.target(), context)}
        if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            if is_psx_ptr(t):
                return {"kind": "psx_ptr", "target": ref(t.fields()[1].type.target().target(), context)}
            return named(t.tag or context, t)
        if code == gdb.TYPE_CODE_VOID:
            return {"kind": "opaque", "name": "void"}
        raise RuntimeError(f"{context}: unsupported type {t} (code {code})")

    def named(name, struct):
        if name not in types:
            types[name] = None  # reserve (recursive types)
            fields = []
            for f in struct.fields():
                fields.append({"name": f.name, "bitpos": f.bitpos, "bitsize": f.bitsize,
                               "type": ref(f.type, f"{name}_{f.name}")})
            types[name] = {"size": struct.sizeof, "union": struct.code == gdb.TYPE_CODE_UNION, "fields": fields}
            order.append(name)
        return {"kind": "named", "name": name}

    for name in type_names:
        t = gdb.lookup_type(name)
        ref(t, name)
    globals_ = {}
    for name in global_names:
        globals_[name] = ref(gdb.parse_and_eval(name).type, name)
    pointer_size = gdb.lookup_type("void").pointer().sizeof
    return {"pointer_size": pointer_size, "order": order, "types": types, "globals": globals_}


def dump_with_gdb(obj, type_names, global_names, out):
    """Run the dump in gdb on `obj`; returns the JSON (also written to `out`)."""
    request = pathlib.Path(str(out) + ".request")
    request.write_text(json.dumps({"types": list(type_names), "globals": list(global_names), "out": str(out)}))
    subprocess.run(["gdb", "-batch", "-nx", str(obj), "-ex",
                    f"python import sys; sys.argv = ['zig_views', {str(request)!r}]",
                    "-x", str(pathlib.Path(__file__).resolve())], check=True, capture_output=True)
    return json.loads(pathlib.Path(out).read_text())


# ---------------------------------------------------------------- emitter
KEYWORDS = {"addrspace", "align", "allowzero", "and", "anyframe", "anytype", "asm", "break", "callconv", "catch",
            "comptime", "const", "continue", "defer", "else", "enum", "errdefer", "error", "export", "extern", "fn",
            "for", "if", "inline", "linksection", "noalias", "noinline", "nosuspend", "opaque", "or", "orelse",
            "packed", "pub", "resume", "return", "struct", "suspend", "switch", "test", "threadlocal", "try",
            "union", "unreachable", "var", "volatile", "while", "type", "void", "bool", "anyopaque", "null",
            "undefined", "true", "false"}


def zid(name):
    return f'@"{name}"' if name in KEYWORDS or not re.fullmatch(r"[A-Za-z_]\w*", name) else name


class Emitter:
    def __init__(self, dump, psx_import):
        self.d, self.psx = dump, psx_import
        self.ptr = dump["pointer_size"]

    def ztype(self, r):
        k = r["kind"]
        if k == "int":
            return ("i" if r["signed"] else "u") + str(8 * r["size"])
        if k == "float":
            return "f32" if r["size"] == 4 else "f64"
        if k == "ptr":
            t = r["target"]
            return "?*anyopaque" if t["kind"] == "opaque" else "?*" + self.ztype(t)
        if k == "fnptr":
            return "?*const anyopaque"
        if k == "psx_ptr":
            t = r["target"]
            return f"psx.Ptr({'anyopaque' if t['kind'] == 'opaque' else self.ztype(t)})"
        if k == "array":
            return f"[{r['count']}]{self.ztype(r['elem'])}"
        if k == "named":
            return zid(r["name"])
        return "anyopaque"

    def size(self, r):
        k = r["kind"]
        if k in ("int", "float"):
            return r["size"]
        if k in ("ptr", "fnptr"):
            return self.ptr
        if k == "psx_ptr":
            return 4
        if k == "array":
            return r["count"] * self.size(r["elem"])
        return self.d["types"][r["name"]]["size"]

    def align(self, r):
        k = r["kind"]
        if k in ("int", "float"):
            return r["size"]
        if k in ("ptr", "fnptr"):
            return self.ptr
        if k == "psx_ptr":
            return 4
        if k == "array":
            return self.align(r["elem"])
        return self.struct_align(r["name"])

    def struct_align(self, name):
        info = self.d["types"][name]
        if info.get("packed"):
            return 1
        return max([self.align(f["type"]) for f in info["fields"] if not f["bitsize"]] + [1])

    def struct(self, name):
        info = self.d["types"][name]
        lines, asserts, bits = [], [], []
        if info["union"]:
            for f in info["fields"]:
                lines.append(f"    {zid(f['name'])}: {self.ztype(f['type'])},")
            lines.append(f"    _bytes: [{info['size']}]u8,")
            asserts.append(f"std.debug.assert(@sizeOf(@This()) == {info['size']});")
            return self.wrap(name, "extern union", lines, asserts, bits)
        cursor, natural = 0, True
        normal = sorted((f for f in info["fields"] if not f["bitsize"]), key=lambda f: f["bitpos"])
        bitfields = [f for f in info["fields"] if f["bitsize"]]
        bit_bytes = sorted({b for f in bitfields for b in range(f["bitpos"] // 8, (f["bitpos"] + f["bitsize"] + 7) // 8)})
        runs = []  # contiguous byte ranges of bitfield storage
        for b in bit_bytes:
            if runs and runs[-1][1] == b:
                runs[-1][1] = b + 1
            else:
                runs.append([b, b + 1])
        items = sorted([(f["bitpos"] // 8, "field", f) for f in normal] + [(s, "bits", e) for s, e in runs],
                       key=lambda x: x[0])
        for offset, kind, item in items:
            if offset > cursor:
                lines.append(f"    _pad_{cursor}: [{offset - cursor}]u8,")
            if kind == "bits":
                lines.append(f"    _bits_{offset}: [{item - offset}]u8,")
                cursor = item
                continue
            f = item
            aligned = offset % self.align(f["type"]) == 0
            natural = natural and aligned
            lines.append(f"    {zid(f['name'])}: {self.ztype(f['type'])}{'' if aligned else ' align(1)'},")
            asserts.append(f'std.debug.assert(@offsetOf(@This(), "{f["name"]}") == {offset});')
            cursor = offset + self.size(f["type"])
        if info["size"] > cursor:
            lines.append(f"    _pad_{cursor}: [{info['size'] - cursor}]u8,")
        asserts.append(f"std.debug.assert(@sizeOf(@This()) == {info['size']});")
        for f in bitfields:
            signed = f["type"].get("signed", False)
            bits.append(f"        .{zid(f['name'])} = {self.psx}.Bit{{ .byte = {f['bitpos'] // 8}, "
                        f".bit = {f['bitpos'] % 8}, .width = {f['bitsize']}, .signed = {str(signed).lower()} }},")
        return self.wrap(name, "extern struct", lines, asserts, bits)

    def wrap(self, name, kind, lines, asserts, bits):
        out = [f"pub const {zid(name)} = {kind} {{"] + lines
        if bits:
            out += ["    pub const bitfields = .{"] + bits + ["    };"]
        out += ["    comptime {"] + [f"        {a}" for a in asserts] + ["    }", "};"]
        return "\n".join(out)

    def globals_(self):
        out = ["pub const g = struct {"]
        for name, r in sorted(self.d["globals"].items()):
            if r["kind"] == "array" and r["count"] == 0:
                out.append(f"    pub inline fn {zid(name)}() [*]{self.ztype(r['elem'])} "
                           f'{{ return psx.globalMany({self.ztype(r["elem"])}, "{name}"); }}')
            else:
                out.append(f"    pub inline fn {zid(name)}() *{self.ztype(r)} "
                           f'{{ return psx.global({self.ztype(r)}, "{name}"); }}')
        out.append("};")
        return "\n".join(out)


def emit(dump, psx_import="common"):
    e = Emitter(dump, "psx")
    head = ["//! Generated by build_boot.py (pc/tools/zig_views.py) from the original object's DWARF: do not edit.",
            "const std = @import(\"std\");",
            f"const psx = @import(\"{psx_import}\")" + (".psx;" if psx_import == "common" else ";")]
    # A struct whose natural Zig layout cannot match is retried as packed (align(1) everywhere).
    body = [e.struct(name) for name in dump["order"]]
    return "\n".join(head + body + [e.globals_()]) + "\n"


if IN_GDB:
    request = json.loads(pathlib.Path(sys.argv[1]).read_text())
    result = _dump(request["types"], request["globals"])
    pathlib.Path(request["out"]).write_text(json.dumps(result, indent=1))
```

(The packed `Packed` case: `s` at offset 1 is emitted `align(1)`, the struct's alignment then comes out as 2 in Zig while GCC's is 1; the size assert (3 vs Zig's 4) fails. Handle it in `struct()`: when the computed `round_up(cursor, struct_align)` differs from DWARF's size, set `info["packed"] = True` and emit every field `align(1)`. Write that branch, rerun the test: the `Packed` assert must pass.)

- [ ] **Step 4: Run the test**

```bash
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_views --output-on-failure
```

Expected: `ok:   32-bit views read every field`, `ok:   64-bit views read every field`, `zig views checks passed`. On the 32-bit build `Node.next` is a native pointer in DWARF and on 64 a `psx.Ptr`; `psx.get` reads both.

- [ ] **Step 5: Commit**

```bash
git add pc/tools/zig_views.py pc/tests/zig_views_test.py pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "zig_views.py: typed views of game state from DWARF (bitfields, psx_ptr, padding, asserts)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: The `c` and `views` modules for battle actions, in the build

**Files:**
- Create: `pc/tools/zig_c.py`
- Modify: `game/ownership.toml`, `pc/tools/build_boot.py`, `game/battle/root.zig`

- [ ] **Step 1: The manifest**

From Task 11's map (`globals` and the types those reach), add to `[subsystem.battle_action]` in `game/ownership.toml`:

```toml
[subsystem.battle_action.views]
# Types and globals the owned module reads or writes (pc/tools/subsystem_map.py battle_action):
# generated per target from the DWARF of the unit's object (pc/tools/zig_views.py).
types = ["D_800F19CC_t", "D_800F19CC_t2", "_hitEntity_t", "vs_action_t", "vs_battle_actor",
         "vs_battle_actor2", "D_800F4538_t", "D_800F18EC_t", "vs_battle_objectData", "func_8006EBF8_t",
         "actionEffectRequest", "actionEffectTarget", "SVECTOR", "VECTOR", "MATRIX", "_sphericalCamera",
         "vs_main_settings_t", "_sphericalValues", "D_800F53B8_t"]
globals = ["_cameraMode", "D_800F1868", "D_800F196C", "D_800F19CC", "D_800F18EC", "D_800F4538", "D_800F4C34",
           "D_8005FFAF", "D_8005FFB0", "vs_main_actions", "vs_battle_actors", "vs_battle_characterState",
           "vs_main_settings", "vs_main_scoredata", "vs_main_buttonsPressed", "vs_battle_cameraCurrentSpherical",
           "vs_main_projectionDistance", "vs_gametime_tickspeed", "vs_scratch", "vs_battle_objectDataSlots",
           "randArr", "randIndex", "D_800F531C", "D_800F54B0"]
```

(Names the map reports and this list lacks: add them. A name gdb cannot find in `battle_146C.o` stops the build with gdb's message: then the global is not referenced by that unit, and its declaring unit must be named in the manifest instead.)

- [ ] **Step 2: `pc/tools/zig_c.py`**

```python
#!/usr/bin/env python3
"""The `c` module for owned Zig code (pc/docs/Ownership.md): every declaration
an original unit sees (its typedefs, prototypes and externs, including the
ones the .c file declares itself), from the exact text GCC compiled (the
unit's .i), as `zig translate-c` makes of it.

    translate(zig, unit_i, out, target, windows=False)

Post-processing: the translated _Static_asserts go (some assert sizes of
types translate-c demotes to opaque: the views assert layouts instead);
warning comments lose the scratch path; for Windows, c_long/c_ulong become
c_longlong/c_ulonglong (the game's Windows twins see Linux's 8-byte long,
build_boot.py lp64_longs). Extern variables are left in but must never be
used (psx.global instead: no .refptr on Windows).
"""
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ownership  # noqa: E402

_ASSERT = re.compile(r"comptime \{\n\s*if \(.*?\) @compileError\(\"static assertion failed.*?\n\}\n?", re.S)


def translate(zig, unit_i, out, target, windows=False):
    decls = pathlib.Path(out).with_suffix(".decls.h")
    decls.write_bytes(ownership.strip_bodies(pathlib.Path(unit_i).read_bytes()))
    result = subprocess.run([str(zig), "translate-c", "-target", target, str(decls)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"zig translate-c {unit_i}: {result.stderr[-2000:]}")
    text = _ASSERT.sub("", result.stdout)
    text = text.replace(str(decls), pathlib.Path(unit_i).name)
    if windows:
        text = re.sub(r"\bc_ulong\b", "c_ulonglong", re.sub(r"\bc_long\b", "c_longlong", text))
    pathlib.Path(out).write_text("//! Generated by build_boot.py (pc/tools/zig_c.py): do not edit.\n" + text)
    return out
```

- [ ] **Step 3: Generate both modules in `build_boot.py` and pass them to the overlay object**

In the overlay loop of Task 9, before `compile_zig`:

```python
import zig_c  # noqa: E402
import zig_views  # noqa: E402

for overlay in sorted({s.overlay for s in active.subsystems}):
    subs = [s for s in active.subsystems if s.overlay == overlay]
    units = sorted({s.unit for s in subs})
    if len(units) != 1:
        raise SystemExit(f"{overlay}: owned subsystems from several units ({units}): M0 supports one unit "
                         "per overlay (one c/views module)")
    stem = next(name for source, name in sources if source == units[0])
    unit_obj, unit_i = out / f"{stem}.o", out / f"{stem}.i"
    types = sorted({t for s in subs for t in s.views.get("types", [])})
    names = sorted({n for s in subs for n in s.views.get("globals", [])})
    key = result_cache.key(["zig_views", *types, "|", *names], [unit_obj, root / "pc/tools/zig_views.py"])
    cached = result_cache.get(key)
    if cached is None:
        dump = zig_views.dump_with_gdb(unit_obj, types, names, zig_dir / f"{overlay}_views.json")
        result_cache.put(key, json.dumps(dump).encode())
    else:
        dump = json.loads(cached)
    (zig_dir / f"{overlay}_views.zig").write_text(zig_views.emit(dump))
    zig_c.translate(ZIG, unit_i, zig_dir / f"{overlay}_c.zig",
                    "x86_64-linux-none" if arch64 else "x86-linux-none", windows=False)
    if win:
        zig_c.translate(ZIG, unit_i, zig_dir / f"{overlay}_c_win.zig", "x86_64-linux-none", windows=True)
    modules = {
        "root": (f"game/{overlay}/root.zig", ["common", "ownership", "views", "c"]),
        "common": ("game/common/common.zig", []),
        "ownership": (str(zig_dir / "ownership.zig"), []),
        "views": (str(zig_dir / f"{overlay}_views.zig"), ["common"]),
        "c": (str(zig_dir / f"{overlay}_c.zig"), []),
    }
    objects.append(compile_zig(f"{overlay}_zig", modules, "root", overlay=overlay,
                               win_modules=dict(modules, c=(str(zig_dir / f"{overlay}_c_win.zig"), [])) if win else None))
```

and give `compile_zig` a `win_modules=None` parameter used for the COFF twin (`zig_command(ZIG_COFF, win_modules or modules, ...)`). Add `"zig_c"` and `"zig_views"` to the stamped tools. (`json` is already imported inside `lp64_native_imports`; import it at the top of build_boot.py.) The `unit_i` is the routed `.i` (`F__orig` definitions), so `c` declares both `F` (the trampoline) and `F__orig`.

Also exclude Zig objects from the LP64 type queries and require their data imports to be declared in C. In `lp64_native_imports()`, in the loop that fills `wanted`:

```python
    for obj in objects:
        names = [line.split()[-1] for line in query([*NM, "-u", str(aobj(obj))]).splitlines()]
        for n in names:
            users.setdefault(n, set()).add(object_overlay(obj))
        if obj.stem.endswith("_zig"):
            continue  # owned code: its imports' types come from the C objects (pc/docs/Ownership.md)
        ...
```

and after `declarations` are collected:

```python
    zig_only = sorted(declared_name(n) for obj in objects if obj.stem.endswith("_zig")
                      for n in (line.split()[-1] for line in query([*NM, "-u", str(aobj(obj))]).splitlines())
                      if n in data and declared_name(n) not in declarations)
    if zig_only:
        raise SystemExit("owned code imports data no C object declares (declare it in C first, "
                         "so the LP64 move sees its type): " + ", ".join(zig_only))
```

- [ ] **Step 4: Use them from the root (compile check)**

At the end of `game/battle/root.zig`:

```zig
const v = @import("views");
const c = @import("c");
comptime {
    // The views and the c module are well-formed on this target (every generated assert holds).
    _ = v.D_800F19CC_t;
    _ = v.g.D_800F19CC;
    _ = c.func_800CF0E8;
}
```

- [ ] **Step 5: Build all four targets**

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error|Error' ; done
grep -c 'pub const' pc/build/boot/zig/battle_views.zig; grep -n 'unk2_0 =' pc/build/boot/zig/battle_views.zig | head -2
```

Expected: three `Zig: battle_zig.o` lines, the three `Built` lines unchanged; dozens of view types; `.unk2_0 = psx.Bit{ .byte = 2, .bit = 0, .width = 1, .signed = false }` in `vs_action_t`'s table. A failing generated assert names the struct and field: fix the generator (not the view).

- [ ] **Step 6: Commit**

```bash
git add game pc/tools/zig_c.py pc/tools/build_boot.py
git commit -m "Battle actions: c (translate-c of the unit's declarations) and views (DWARF) modules per target

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: State ranges, captures and page digests (`state_diff.c`)

**Files:**
- Create: `pc/src/state_diff.h`, `pc/src/state_diff.c`
- Modify: `pc/src/snapshot.c`, `pc/src/boot_runtime.c`, `pc/tools/build_boot.py`

- [ ] **Step 1: Export the ranges from `snapshot.c`**

`pc/src/state_diff.h`:

```c
/* The game's state as memory ranges (snapshot.c), captured, digested and
 * compared (state_diff.c): the differential tests of owned code
 * (pc/docs/Ownership.md). Linux only, like save states. */
#ifndef PC_STATE_DIFF_H
#define PC_STATE_DIFF_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    char* start;
    size_t size;
    const char* name;
} pc_state_range;
/* Guest RAM, scratchpad, I/O, the native game statics, overlay statics, and
 * native VRAM when it is outside those; returns the count. */
int pc_state_ranges(pc_state_range* out, int max);
#endif
```

In `snapshot.c` (inside the non-Windows part, after `ranges()`):

```c
#include "state_diff.h"
int pc_state_ranges(pc_state_range* out, int max)
{
    range r[MAX_RANGES];
    int n = ranges(r), count = 0;
    for (int i = 0; i < n && count < max; i++)
        out[count++] = (pc_state_range) { r[i].start, r[i].size, r[i].name };
    const char* vram = (const char*)pc_native_vram();
    int inside = 0;
    for (int i = 0; i < n; i++)
        inside |= vram >= r[i].start && vram < r[i].start + r[i].size;
    if (!inside && count < max)
        out[count++] = (pc_state_range) { (char*)vram, 1024 * 512 * 2, "vram" };
    return count;
}
```

and in the `_WIN32` stub part: `int pc_state_ranges(pc_state_range* out, int max) { (void)out; (void)max; return 0; }` (with the same include).

- [ ] **Step 2: `pc/src/state_diff.c` (captures and digests)**

```c
/* Differential testing of owned code (pc/docs/Ownership.md): the game's
 * state captured, restored, digested per tick and compared.
 *
 *   images     the ranges (state_diff.h) and the SPU, back to back;
 *   masks      known noise (pc/tests/diff/masks.py): bytes zeroed before
 *              digesting, skipped when comparing;
 *   digests    a 64-bit FNV-1a per 4 KB page per range after every vsync
 *              (pc_state_diff_vsync, from pc_tick): RPC digest record keeps
 *              them, digest compare checks a replay against them and keeps
 *              the first differing tick and pages.
 * Host-only state; Linux only (the ranges are empty elsewhere). */
#include "state_diff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_spu_state_size(void) __attribute__((weak));
extern void pc_spu_state_save(void* out) __attribute__((weak));
extern int pc_spu_state_load(const void* in, int size) __attribute__((weak));

#define MAX_STATE_RANGES 170
#define PAGE 4096
typedef struct {
    char* data;
    size_t size, capacity;
} image;

static pc_state_range state_ranges[MAX_STATE_RANGES];
static int range_count = -1;
static size_t state_bytes;

static void init_ranges(void)
{
    if (range_count >= 0)
        return;
    range_count = pc_state_ranges(state_ranges, MAX_STATE_RANGES);
    state_bytes = 0;
    for (int i = 0; i < range_count; i++)
        state_bytes += state_ranges[i].size;
}
static int spu_bytes(void) { return pc_spu_state_size ? pc_spu_state_size() : 0; }

static void image_capture(image* m)
{
    init_ranges();
    size_t need = state_bytes + (size_t)spu_bytes();
    if (m->capacity < need) {
        free(m->data);
        m->data = malloc(need);
        m->capacity = m->data ? need : 0;
    }
    if (!m->data) {
        fprintf(stderr, "[state_diff] out of memory (%zu bytes)\n", need);
        abort();
    }
    char* p = m->data;
    for (int i = 0; i < range_count; i++, p += state_ranges[i - 1].size)
        memcpy(p, state_ranges[i].start, state_ranges[i].size);
    if (spu_bytes())
        pc_spu_state_save(p);
    m->size = need;
}
static void image_restore(const image* m)
{
    const char* p = m->data;
    for (int i = 0; i < range_count; i++, p += state_ranges[i - 1].size)
        memcpy(state_ranges[i].start, p, state_ranges[i].size);
    if (spu_bytes())
        pc_spu_state_load(p, spu_bytes());
}

/* ---- masks ---- */
typedef struct {
    uintptr_t start;
    size_t size;
} mask;
static mask masks[64];
static int mask_count;
void pc_state_mask_clear(void) { mask_count = 0; }
int pc_state_mask_add(uintptr_t start, size_t size)
{
    if (mask_count == (int)(sizeof masks / sizeof *masks))
        return -1;
    masks[mask_count++] = (mask) { start, size };
    return 0;
}
static int masked(uintptr_t address)
{
    for (int i = 0; i < mask_count; i++)
        if (address - masks[i].start < masks[i].size)
            return 1;
    return 0;
}

/* ---- digests ---- */
static uint64_t fnv(const unsigned char* p, size_t n, uint64_t h)
{
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
static int page_count(void)
{
    init_ranges();
    int n = 0;
    for (int i = 0; i < range_count; i++)
        n += (int)((state_ranges[i].size + PAGE - 1) / PAGE);
    return n + 1; /* + the SPU */
}
/* One digest per page into out[page_count()]. */
static void digest(uint64_t* out)
{
    static unsigned char scratch[PAGE];
    int k = 0;
    for (int i = 0; i < range_count; i++)
        for (size_t at = 0; at < state_ranges[i].size; at += PAGE) {
            size_t n = state_ranges[i].size - at < PAGE ? state_ranges[i].size - at : PAGE;
            const unsigned char* page = (const unsigned char*)state_ranges[i].start + at;
            int any = 0;
            for (int m = 0; m < mask_count && !any; m++)
                any = masks[m].start < (uintptr_t)page + n && (uintptr_t)page < masks[m].start + masks[m].size;
            if (any) {
                memcpy(scratch, page, n);
                for (size_t b = 0; b < n; b++)
                    if (masked((uintptr_t)page + b))
                        scratch[b] = 0;
                page = scratch;
            }
            out[k++] = fnv(page, n, 1469598103934665603ull);
        }
    static image spu;
    int size = spu_bytes();
    if (size) {
        if ((int)spu.capacity < size) {
            free(spu.data);
            spu.data = malloc((size_t)size);
            spu.capacity = spu.data ? (size_t)size : 0;
        }
        pc_spu_state_save(spu.data);
    }
    out[k] = size ? fnv((unsigned char*)spu.data, (size_t)size, 1469598103934665603ull) : 0;
}
/* The page index as "range+offset" (for the report). */
static void page_name(int k, char* out, int size)
{
    for (int i = 0; i < range_count; i++) {
        int pages = (int)((state_ranges[i].size + PAGE - 1) / PAGE);
        if (k < pages) {
            snprintf(out, (size_t)size, "%s+%#zx@%#llx", state_ranges[i].name, (size_t)k * PAGE,
                (unsigned long long)(uintptr_t)(state_ranges[i].start + (size_t)k * PAGE));
            return;
        }
        k -= pages;
    }
    snprintf(out, (size_t)size, "spu");
}

enum { DIGEST_OFF, DIGEST_RECORD, DIGEST_COMPARE };
static int digest_mode, digest_alternate = -1;
static uint64_t* recorded;     /* ticks x pages */
static int recorded_ticks, digest_tick, digest_capacity, pages;
static int first_tick = -1;
static char first_pages[1024];
extern unsigned char pc_owner[];

/* RPC digest: action record (capacity ticks), compare (alternate: a
 * subsystem index whose owner flips between original and owned every
 * vsync), stop; result as JSON. */
int pc_digest_start(int compare, int capacity, int alternate)
{
    init_ranges();
    if (!range_count)
        return -1;
    pages = page_count();
    if (!compare) {
        free(recorded);
        recorded = malloc((size_t)capacity * (size_t)pages * sizeof *recorded);
        if (!recorded)
            return -1;
        digest_capacity = capacity, recorded_ticks = 0;
    }
    digest_tick = 0, first_tick = -1, first_pages[0] = 0;
    digest_alternate = compare ? alternate : -1;
    digest_mode = compare ? DIGEST_COMPARE : DIGEST_RECORD;
    return 0;
}
void pc_digest_stop(void)
{
    if (digest_mode == DIGEST_RECORD)
        recorded_ticks = digest_tick;
    digest_mode = DIGEST_OFF;
}
int pc_digest_result(char* out, int size)
{
    return snprintf(out, (size_t)size, "{\"recorded\":%d,\"ticks\":%d,\"pages\":%d,\"first_tick\":%d,\"first_pages\":[%s]}",
        recorded_ticks, digest_tick, pages, first_tick, first_pages);
}
/* boot_runtime.c pc_tick, once per vsync before the RPC: the state the last
 * frame left. */
void pc_state_diff_vsync(void)
{
    if (digest_mode == DIGEST_OFF)
        return;
    static uint64_t* now;
    static int now_pages;
    if (now_pages < pages) {
        free(now);
        now = malloc((size_t)pages * sizeof *now);
        now_pages = now ? pages : 0;
    }
    digest(now);
    if (digest_mode == DIGEST_RECORD) {
        if (digest_tick < digest_capacity)
            memcpy(recorded + (size_t)digest_tick * pages, now, (size_t)pages * sizeof *now);
        ++digest_tick;
    } else {
        if (digest_tick < recorded_ticks && first_tick < 0) {
            const uint64_t* want = recorded + (size_t)digest_tick * pages;
            int n = 0, listed = 0;
            for (int k = 0; k < pages; k++)
                if (want[k] != now[k]) {
                    if (first_tick < 0)
                        first_tick = digest_tick;
                    if (listed < 24) {
                        char name[96];
                        page_name(k, name, sizeof name);
                        n += snprintf(first_pages + n, sizeof first_pages - (size_t)n, "%s\"%s\"", listed ? "," : "", name);
                        ++listed;
                    }
                }
        }
        ++digest_tick;
        if (digest_alternate >= 0)
            pc_owner[digest_alternate] = (unsigned char)(digest_tick & 1);
    }
}
```

(The shadow part is Task 18.) Add `"state_diff"` to build_boot's host unit list (after `"snapshot"`), not to `STATE_HOST_UNITS`.

In `pc/src/boot_runtime.c`'s `pc_tick`, before `pc_rpc_poll();`:

```c
    if (pc_state_diff_vsync)
        pc_state_diff_vsync(); /* differential tests: the state the last frame left (state_diff.c) */
```

with `extern void pc_state_diff_vsync(void) __attribute__((weak));` beside the other weak externs.

- [ ] **Step 3: Build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows | tail -1
```

Expected: both `Built ...` lines (Windows links the stubs).

- [ ] **Step 4: Commit**

```bash
git add pc/src/state_diff.h pc/src/state_diff.c pc/src/snapshot.c pc/src/boot_runtime.c pc/tools/build_boot.py
git commit -m "state_diff.c: state images, masks and per-vsync page digests (record/compare)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: In-memory mark and rewind (one game instance)

**Files:**
- Modify: `pc/src/snapshot.c`, `pc/src/dev_rpc.c`, `pc/tools/rood_rpc.py`, `pc/tests/ownership_smoke.py`

- [ ] **Step 1: The failing check (rewind is exact)**

In `pc/tests/ownership_smoke.py`'s `main()`, after the owner checks and before `g.run(120)`:

```python
        # In-memory rewind: the same 300 vsyncs twice from a mark give the same state.
        g.mark()
        g.run(300)
        first = g.call("snapshot", action="hash")
        g.rewind()
        check(g.status()["vsync"] == first["vsync"] - 300, "rewind returns to the mark's vsync")
        g.run(300)
        second = g.call("snapshot", action="hash")
        check(first == second, f"rewind is exact: {first} vs {second}")
        g.rewind()
        g.rewind()  # twice from the same mark
        check(g.status()["vsync"] == first["vsync"] - 300, "the mark survives rewinds")
```

Run it: FAIL, `'Game' object has no attribute 'mark'`.

- [ ] **Step 2: Mark and rewind in `snapshot.c`**

In the non-Windows part, after `pc_snapshot_point`:

```c
/* ---- in-memory mark and rewind (RPC snapshot mark / rewind) ----
 * The same state as a save state, kept in this process: a mark is taken
 * where a save is (dev_rpc.c's paused loop, on the game stack); a rewind
 * copies it back from a side stack (the game stack is overwritten) and jumps
 * into the mark's context, so the mark call returns again (1). Host state is
 * not rewound: the renderer, CD, input replay and fog resync (pc/docs/Ownership.md). */
extern void pc_input_rewound(void) __attribute__((weak)); /* input_record.c */
static struct {
    int valid;
    snap_context context;
    uintptr_t canary;
    char* memory;
    size_t memory_size;
    char* stack;
    uintptr_t sp;
    size_t stack_bytes;
    void* spu;
    int spu_size;
    void* fog;
    int fog_size;
} mark;

static int mark_copy(int restore)
{
    range r[MAX_RANGES];
    int n = ranges(r);
    size_t need = 0;
    for (int i = 0; i < n; i++)
        need += r[i].size;
    if (!restore && mark.memory_size < need) {
        free(mark.memory);
        mark.memory = malloc(need);
        mark.memory_size = mark.memory ? need : 0;
    }
    if (!mark.memory)
        return -1;
    char* p = mark.memory;
    for (int i = 0; i < n; i++, p += r[i - 1].size)
        restore ? memcpy(r[i].start, p, r[i].size) : memcpy(p, r[i].start, r[i].size);
    return 0;
}

__attribute__((noinline)) int pc_snapshot_mark_point(char* error, int esize)
{
    if (pc_snap_save_context(&mark.context))
        return 1; /* a rewind landed here */
    size_t stack_size;
    char* stack = pc_game_stack(&stack_size);
    uintptr_t sp = SAVED_SP(&mark.context), top = (uintptr_t)stack + stack_size;
    if (!stack || sp < (uintptr_t)stack || sp >= top) {
        snprintf(error, (size_t)esize, "not on the game stack");
        return -1;
    }
    if (mark_copy(0)) {
        snprintf(error, (size_t)esize, "out of memory");
        return -1;
    }
    free(mark.stack);
    mark.stack_bytes = (size_t)(top - sp);
    mark.stack = malloc(mark.stack_bytes);
    memcpy(mark.stack, (void*)sp, mark.stack_bytes);
    mark.sp = sp;
    mark.spu_size = pc_spu_state_size ? pc_spu_state_size() : 0;
    free(mark.spu);
    mark.spu = mark.spu_size ? malloc((size_t)mark.spu_size) : NULL;
    if (mark.spu)
        pc_spu_state_save(mark.spu);
    mark.fog_size = pc_fog_state_size ? pc_fog_state_size() : 0;
    free(mark.fog);
    mark.fog = mark.fog_size ? malloc((size_t)mark.fog_size + 1) : NULL;
    if (mark.fog)
        pc_fog_state_save(mark.fog);
    mark.canary = read_canary();
    mark.valid = 1;
    return 0;
}

__attribute__((noreturn)) void pc_snap_call_on_stack(void* top, void (*fn)(void));
#ifdef __x86_64__
__asm__(".text\n.globl pc_snap_call_on_stack\n.type pc_snap_call_on_stack, @function\n"
        "pc_snap_call_on_stack:\n    movq %rdi, %rsp\n    andq $-16, %rsp\n    callq *%rsi\n    ud2\n"
        ".size pc_snap_call_on_stack, .-pc_snap_call_on_stack\n");
#else
__asm__(".text\n.globl pc_snap_call_on_stack\n.type pc_snap_call_on_stack, @function\n"
        "pc_snap_call_on_stack:\n    movl 4(%esp), %eax\n    movl 8(%esp), %ecx\n    movl %eax, %esp\n"
        "    andl $-16, %esp\n    calll *%ecx\n    ud2\n.size pc_snap_call_on_stack, .-pc_snap_call_on_stack\n");
#endif

__attribute__((noreturn)) static void rewind_on_side_stack(void)
{
    mark_copy(1);
    memcpy((void*)mark.sp, mark.stack, mark.stack_bytes);
    if (mark.spu)
        pc_spu_state_load(mark.spu, mark.spu_size);
    if (mark.fog && pc_fog_state_load)
        pc_fog_state_load(mark.fog, mark.fog_size);
    if (pc_cd_restored)
        pc_cd_restored();
    if (pc_hw_resync)
        pc_hw_resync();
    if (pc_input_rewound)
        pc_input_rewound();
    write_canary(mark.canary);
    pc_snap_jump(&mark.context, 1);
}

/* Returns only on failure (no mark); otherwise the mark's call returns 1. */
int pc_snapshot_rewind(char* error, int esize)
{
    if (!mark.valid) {
        snprintf(error, (size_t)esize, "no mark (snapshot {action: \"mark\"} first)");
        return -1;
    }
    enum { SIDE = 1 << 20 };
    static char* side;
    if (!side && !(side = malloc(SIDE))) {
        snprintf(error, (size_t)esize, "out of memory");
        return -1;
    }
    pc_snap_call_on_stack(side + SIDE, rewind_on_side_stack);
}
```

`_WIN32` stubs: `int pc_snapshot_mark_point(char* e, int n) { snprintf(e, (size_t)n, "save states need Linux on x86"); return -1; }` and the same for `pc_snapshot_rewind`.

- [ ] **Step 3: The RPC actions (`dev_rpc.c`)**

Document under `snapshot`:

```c
 *   snapshot {action: "mark"}    keep the whole game state in memory (one mark)
 *   snapshot {action: "rewind"}  restore the mark in this process; paused
```

Beside `snapshot_client`:

```c
extern int pc_snapshot_mark_point(char* error, int size);
extern int pc_snapshot_rewind(char* error, int size);
static struct client *mark_client, *rewind_client, *rewound_client;
static char mark_id[64], rewind_id[64], rewound_id[64];
```

In the `snapshot` method, before the final `else fail(...)`:

```c
        } else if (!strcmp(action, "mark") || !strcmp(action, "rewind")) {
            if (!pc_snapshot_available() || !current_client || strlen(id) >= sizeof mark_id) {
                fail(fd, id, -32000, "save states are unavailable in this process (see the log)");
                return;
            }
            /* Taken by pc_rpc_poll once this request's handling is over (take_mark). */
            if (action[0] == 'm')
                mark_client = current_client, strcpy(mark_id, id);
            else
                rewind_client = current_client, strcpy(rewind_id, id);
```

and the error text of the final `fail` becomes `"snapshot action: save, mark, rewind, hash or available"`. After `take_snapshot()`:

```c
/* A pending mark or rewind (between frames, on the game stack). A rewind
 * jumps back into the mark's call, which returns 1: the reply then goes to
 * the rewind's client (host statics are not rewound). */
static void take_mark(void)
{
    char error[256] = "", result[96];
    if (mark_client) {
        struct client* c = mark_client;
        mark_client = NULL;
        int status = pc_snapshot_mark_point(error, (int)sizeof error);
        if (status == 1) {
            c = rewound_client;
            rewound_client = NULL;
            paused = 1;
            step_budget = 0;
            if (c && c->fd >= 0) {
                snprintf(result, sizeof result, "{\"rewound\":true,\"vsync\":%u}", pc_vsync_count());
                rpc_set_blocking(c->fd, 1);
                reply(c->fd, rewound_id, result);
                rpc_set_blocking(c->fd, 0);
            }
            return;
        }
        rpc_set_blocking(c->fd, 1);
        if (status == 0) {
            snprintf(result, sizeof result, "{\"marked\":true,\"vsync\":%u}", pc_vsync_count());
            reply(c->fd, mark_id, result);
        } else
            fail(c->fd, mark_id, -32000, error);
        rpc_set_blocking(c->fd, 0);
    }
    if (rewind_client) {
        rewound_client = rewind_client;
        strcpy(rewound_id, rewind_id);
        rewind_client = NULL;
        pc_snapshot_rewind(error, (int)sizeof error); /* returns only on failure */
        struct client* c = rewound_client;
        rewound_client = NULL;
        rpc_set_blocking(c->fd, 1);
        fail(c->fd, rewound_id, -32000, error);
        rpc_set_blocking(c->fd, 0);
    }
}
```

Call `take_mark();` right after each of the two `take_snapshot();` calls in `pc_rpc_poll`.

`pc/tools/rood_rpc.py`, in `class Game`:

```python
    def mark(self):
        """Keep the whole game state in memory (pc/src/snapshot.c); paused."""
        return self.call("snapshot", action="mark")

    def rewind(self):
        """Restore the mark in this process; the game stays paused at the mark's vsync."""
        return self.call("snapshot", action="rewind")
```

- [ ] **Step 4: Run the smoke on both architectures**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 ownership_smoke
```

Expected: `ok:   rewind is exact: {...} vs {...}` with identical hashes (ram, scratch, io, state, overlays, vram), and the mark surviving rewinds, on both. If the hashes differ, the differing component names the state the rewind misses: add its resync (as `pc_hw_resync`), never a mask.

- [ ] **Step 5: Commit**

```bash
git add pc/src/snapshot.c pc/src/dev_rpc.c pc/tools/rood_rpc.py pc/tests/ownership_smoke.py
git commit -m "Save states in memory: RPC snapshot mark/rewind in one process, exact (smoke)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Input recording and replay over RPC, rewind-aware

**Files:**
- Modify: `pc/src/input_record.c`, `pc/src/dev_rpc.c`, `pc/tools/rood_rpc.py`, `pc/tests/ownership_smoke.py`

- [ ] **Step 1: The failing check**

Append to the smoke (after the rewind checks):

```python
        # Record presses from the mark, rewind, replay them: the same state.
        rec = out / "walk.rec"
        g.mark()
        g.record(rec)
        g.hold(["up"], 90)
        g.press("circle", vsyncs=3, settle=30)
        g.run(60)
        g.record(None)
        walked = g.call("snapshot", action="hash")
        g.rewind()
        g.replay(rec)
        g.run(walked["vsync"] - g.status()["vsync"])
        check(g.call("snapshot", action="hash") == walked, "a replay after a rewind reproduces the recorded run")
        g.replay(None)
```

Run: FAIL, no `record`.

- [ ] **Step 2: `input_record.c`**

Refactor `start()`'s replay loading into `static int load_replay(const char* file)` (the existing loop, resetting `event_count = next_button = next_stick = 0`, `replay_end = 0`, `replay_buttons = 0`, `replay_stick[0] = replay_stick[1] = 0` first; `mode = 2` on success) and the recording opening into `static int open_recording(const char* file)` (header, `last_buttons = ~0u`, `last_stick = {1 << 20, 1 << 20}`, `mode = 1`), so `start()` calls them for the environment variables. Then add:

```c
static char rpc_path[1024];
/* RPC record {path}: record from this vsync (path "" or null stops and
 * writes the end line). 0 when started or stopped. */
int pc_input_record_rpc(const char* file)
{
    start();
    if (mode == 1 && (!file || !*file)) {
        finish_recording();
        mode = 0;
        return 0;
    }
    if (!file || !*file || mode == 2)
        return -1; /* (not while replaying) */
    if (mode == 1)
        finish_recording();
    snprintf(rpc_path, sizeof rpc_path, "%s", file);
    path = rpc_path;
    return open_recording(rpc_path) == 1 ? 0 : -1;
}
/* RPC replay {path}: replay that recording, from wherever its events are
 * (keyed by vsync); "" or null stops it (live input again). */
int pc_input_replay_rpc(const char* file)
{
    start();
    if (!file || !*file) {
        mode = 0;
        return 0;
    }
    if (mode == 1)
        return -1;
    snprintf(rpc_path, sizeof rpc_path, "%s", file);
    path = rpc_path;
    return load_replay(rpc_path) == 2 ? 0 : -1;
}
/* snapshot.c: the game went back in time; a replay seeks again from its
 * start (replay_to only moves forward). */
void pc_input_rewound(void)
{
    if (mode == 2 || mode == 3) {
        next_button = next_stick = 0;
        replay_buttons = 0;
        replay_stick[0] = replay_stick[1] = 0;
        mode = 2;
    }
}
```

(`atexit(finish_recording)` stays registered once, in `open_recording`, guarded by a static flag.)

- [ ] **Step 3: RPC and `rood_rpc.py`**

`dev_rpc.c` (documented beside `input`):

```c
 *   record {path}                record the pad from now (input_record.c);
 *                                path "" stops
 *   replay {path}                replay a recording (keyed by vsync); "" stops
```

```c
    } else if (!strcmp(method, "record") || !strcmp(method, "replay")) {
        extern int pc_input_record_rpc(const char*), pc_input_replay_rpc(const char*);
        char file[1024] = "";
        if (params)
            string_value(member(params, "path"), file, sizeof file);
        int status = method[2] == 'c' ? pc_input_record_rpc(file) : pc_input_replay_rpc(file);
        if (status) {
            fail(fd, id, -32000, "record/replay: cannot (a replay and a recording exclude each other; see the log)");
            return;
        }
        snprintf(result, sizeof result, "{\"%s\":\"%s\"}", method, json_str(file));
        reply(fd, id, result);
```

`rood_rpc.py`:

```python
    def record(self, path):
        """Record the pad from now into `path` (None stops; pc/src/input_record.c)."""
        return self.call("record", path=str(path) if path else "")

    def replay(self, path):
        """Replay a recording (keyed by vsync, so after a rewind too); None stops."""
        return self.call("replay", path=str(path) if path else "")
```

- [ ] **Step 4: Run the smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
```

Expected: `ok:   a replay after a rewind reproduces the recorded run`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/input_record.c pc/src/dev_rpc.c pc/tools/rood_rpc.py pc/tests/ownership_smoke.py
git commit -m "Input record/replay over RPC; a replay seeks again after a rewind

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: Digests over RPC (`digest`) and masks

**Files:**
- Modify: `pc/src/dev_rpc.c`, `pc/tools/rood_rpc.py`, `pc/tests/ownership_smoke.py`

- [ ] **Step 1: The failing check**

Append to the smoke:

```python
        # Per-tick digests: a recorded run compared with its replay after a rewind: no difference;
        # with a byte poked on tick 10 of the replay: first difference at tick 10, in guest RAM.
        g.mark()
        g.digest("record", ticks=200)
        g.record(rec)
        g.hold(["left"], 60)
        g.run(140)
        g.record(None)
        recorded = g.digest("stop")["recorded"]
        g.rewind()
        g.replay(rec)
        g.digest("compare")
        g.run(recorded)
        same = g.digest("result")
        check(same["first_tick"] == -1 and same["ticks"] == recorded, f"replay digests match: {same}")
        g.rewind()
        g.replay(rec)
        g.digest("compare")
        g.run(10)
        g.write(0x80001000, b"\x5a")  # PS1 kernel RAM, unused on the PC: changed after tick 10's digest
        g.run(recorded - 10)
        poked = g.digest("result")
        check(poked["first_tick"] == 10 and any(p.startswith("ram+0x1000") for p in poked["first_pages"]),
              f"a poked byte is found at its tick and page: {poked}")
        g.replay(None)
```

(The poke lands after the 10th compare digest, so tick index 10, the 11th digest, is the first to differ.)

- [ ] **Step 2: The RPC method**

```c
 *   digest {action, ticks, alternate, masks}  per-vsync state digests
 *                                (state_diff.c): record (ticks: capacity),
 *                                compare (alternate: a subsystem index whose
 *                                owner flips every vsync), stop, result;
 *                                masks [[address, size], ...] (noise)
```

```c
    } else if (!strcmp(method, "digest")) {
        extern int pc_digest_start(int, int, int), pc_digest_result(char*, int), pc_state_mask_add(uintptr_t, size_t);
        extern void pc_digest_stop(void), pc_state_mask_clear(void);
        char action[16] = "result";
        long long ticks = 4000, alternate = -1;
        if (params) {
            string_value(member(params, "action"), action, sizeof action);
            number_value(member(params, "ticks"), &ticks);
            number_value(member(params, "alternate"), &alternate);
            const char* list = member(params, "masks");
            if (list) {
                pc_state_mask_clear();
                for (const char* p = strchr(list, '['); p && (p = strchr(p + 1, '[')); ) {
                    unsigned long long address, size;
                    if (sscanf(p + 1, "%llu , %llu", &address, &size) == 2)
                        pc_state_mask_add((uintptr_t)address, (size_t)size);
                }
            }
        }
        int status = 0;
        if (!strcmp(action, "record") || !strcmp(action, "compare"))
            status = pc_digest_start(action[0] == 'c', (int)ticks, (int)alternate);
        else if (!strcmp(action, "stop"))
            pc_digest_stop();
        if (status) {
            fail(fd, id, -32000, "digest: unavailable (save states need Linux) or out of memory");
            return;
        }
        pc_digest_result(result, (int)sizeof result);
        reply(fd, id, result);
```

`rood_rpc.py`:

```python
    def digest(self, action, **params):
        """Per-vsync state digests (pc/src/state_diff.c): record, compare, stop, result."""
        return self.call("digest", action=action, **params)
```

- [ ] **Step 3: Run the smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
```

Expected: both new `ok:` lines.

- [ ] **Step 4: Commit**

```bash
git add pc/src/dev_rpc.c pc/tools/rood_rpc.py pc/tests/ownership_smoke.py
git commit -m "RPC digest: per-vsync page digests recorded and compared in the game, with masks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18: Shadow mode — every routed call run twice by the original and once owned, compared

**Files:**
- Modify: `pc/src/state_diff.c`, `game/common/route.zig`, `pc/src/dev_rpc.c`, `pc/src/ownership.c`, `pc/tests/ownership_smoke.py`

- [ ] **Step 1: The failing checks**

Append to the smoke:

```python
        # Shadow mode: the pass-through module equals the original on every call; an injected fault
        # (state_diff.c ROOD_SHADOW_FAULT) is reported with its address.
        g.owner("battle_action", "shadow")
        g.call("owner", action="report", reset=True)
        g.hold(["up"], 120)  # (the save room has no enemies: few or no battle calls; Task 20's corpus drives them)
        g.run(600)
        report = g.call("owner", action="report")
        check(report["mismatches"] == [], f"shadow: the pass-through matches the original: {report}")
        g.owner("battle_action", "original")
```

(The fault-injection check needs a battle action; Task 20 adds it with the corpus's helpers.)

Run: FAIL, `owner: subsystem from owner {}...` for `action`.

- [ ] **Step 2: The shadow part of `state_diff.c`**

```c
/* ---- shadow mode (pc_owner[s] == 2): every top-level routed call runs
 * the original twice (dead stack filled with two patterns) and the owned
 * code once, from the same state; the bytes where the two original runs
 * differ come from uninitialised stack and are ignored; the game goes on
 * with the original's result (route.zig Shadow). ---- */
extern unsigned char pc_owner[];
int pc_shadow_available(void)
{
    init_ranges();
    return range_count > 0;
}

static image before, first, second;
static int shadow_depth, shadow_subsystem;
static const char* shadow_name;
static unsigned shadow_calls, shadow_mismatches;
static const char* fault_name;
static uintptr_t fault_address;
extern unsigned pc_vsync_count(void);

typedef struct {
    unsigned call, vsync;
    const char* function;
    char range[24];
    uintptr_t first;
    unsigned bytes, runs;
    uintptr_t run_at[16];
    unsigned run_len[16];
    int returns_differ;
} mismatch;
static mismatch log_[64];
static unsigned log_count;

/* Start a shadow call: 0 when the caller should just run the original
 * (nested, or no save states here). */
int pc_shadow_begin(int subsystem, const char* name)
{
    if (shadow_depth || !pc_shadow_available())
        return 0;
    ++shadow_depth, ++shadow_calls;
    shadow_subsystem = subsystem, shadow_name = name;
    image_capture(&before);
    pc_owner[subsystem] = 0;
    return 1;
}

static void compare_with_first(void)
{
    mismatch m = { shadow_calls, pc_vsync_count(), shadow_name };
    const char *a = first.data, *b = second.data;
    for (int i = 0; i < range_count; i++) {
        const char* live = state_ranges[i].start;
        size_t size = state_ranges[i].size;
        for (size_t at = 0; at < size; at += PAGE) {
            size_t n = size - at < PAGE ? size - at : PAGE;
            if (!memcmp(live + at, a + at, n))
                continue;
            for (size_t k = at; k < at + n; k++) {
                if (live[k] == a[k] || a[k] != b[k] || masked((uintptr_t)(live + k)))
                    continue; /* equal, or the original itself is not deterministic here */
                if (!m.bytes) {
                    snprintf(m.range, sizeof m.range, "%s", state_ranges[i].name);
                    m.first = (uintptr_t)(live + k);
                }
                ++m.bytes;
                if (m.runs && m.run_at[m.runs - 1] + m.run_len[m.runs - 1] == (uintptr_t)(live + k))
                    ++m.run_len[m.runs - 1];
                else if (m.runs < 16)
                    m.run_at[m.runs] = (uintptr_t)(live + k), m.run_len[m.runs++] = 1;
            }
        }
        a += size, b += size;
    }
    int spu = spu_bytes();
    if (spu) {
        static image now;
        image_capture(&now);
        if (memcmp(now.data + state_bytes, a, (size_t)spu)) {
            for (int k = 0; k < spu; k++)
                if (now.data[state_bytes + k] != a[k] && a[k] == b[k]) {
                    if (!m.bytes)
                        snprintf(m.range, sizeof m.range, "spu"), m.first = (uintptr_t)k;
                    ++m.bytes;
                }
        }
    }
    if (m.bytes) {
        ++shadow_mismatches;
        if (log_count < sizeof log_ / sizeof *log_)
            log_[log_count++] = m;
        if (shadow_mismatches <= 8)
            fprintf(stderr, "[shadow] %s call %u (vsync %u): %u bytes differ in %s, first at %#llx\n", m.function,
                m.call, m.vsync, m.bytes, m.range, (unsigned long long)m.first);
    }
}

/* After run 1 (original): keep its result, back to the start. After run 2
 * (original, other dead-stack pattern): keep it, back to the start, owned
 * next. After run 3 (owned): compare, then continue from run 1's result. */
void pc_shadow_after(int stage)
{
    if (stage == 1) {
        image_capture(&first);
        image_restore(&before);
    } else if (stage == 2) {
        image_capture(&second);
        image_restore(&before);
        pc_owner[shadow_subsystem] = 1;
    } else {
        if (fault_name && !strcmp(fault_name, shadow_name) && fault_address)
            *(volatile unsigned char*)fault_address ^= 0x40; /* ROOD_SHADOW_FAULT: the tester's own test */
        compare_with_first();
        image_restore(&first);
        pc_owner[shadow_subsystem] = 2;
    }
}
void pc_shadow_returns(int equal)
{
    if (!equal && log_count) {
        ++shadow_mismatches;
        if (log_[log_count - 1].call == shadow_calls)
            log_[log_count - 1].returns_differ = 1;
        else if (log_count < sizeof log_ / sizeof *log_)
            log_[log_count++] = (mismatch) { shadow_calls, pc_vsync_count(), shadow_name, "return", 0, 0, 0, {0}, {0}, 1 };
    }
    --shadow_depth;
}

/* Fill the dead stack below the caller with `pattern` (a leaf: nothing of
 * its own is live there). ROOD_SHADOW_STACK bytes, default 64 KB. */
size_t pc_shadow_stack_bytes = 64 * 1024;
void pc_shadow_fill_dead_stack(unsigned pattern);
#ifdef __x86_64__
__asm__(".text\n.globl pc_shadow_fill_dead_stack\n.type pc_shadow_fill_dead_stack, @function\n"
        "pc_shadow_fill_dead_stack:\n"
        "    movl %edi, %eax\n"
        "    movq pc_shadow_stack_bytes(%rip), %rcx\n"
        "    leaq -8(%rsp), %rdi\n"
        "    subq %rcx, %rdi\n"
        "    rep stosb\n"
        "    ret\n"
        ".size pc_shadow_fill_dead_stack, .-pc_shadow_fill_dead_stack\n");
#elif defined(__i386__)
__asm__(".text\n.globl pc_shadow_fill_dead_stack\n.type pc_shadow_fill_dead_stack, @function\n"
        "pc_shadow_fill_dead_stack:\n"
        "    pushl %edi\n"
        "    movl 8(%esp), %eax\n"
        "    movl pc_shadow_stack_bytes, %ecx\n"
        "    leal -4(%esp), %edi\n"
        "    subl %ecx, %edi\n"
        "    rep stosb\n"
        "    popl %edi\n"
        "    ret\n"
        ".size pc_shadow_fill_dead_stack, .-pc_shadow_fill_dead_stack\n");
#else
void pc_shadow_fill_dead_stack(unsigned pattern) { (void)pattern; }
#endif

__attribute__((constructor)) static void shadow_env(void)
{
    const char* fault = getenv("ROOD_SHADOW_FAULT"); /* name:address */
    static char name[64];
    if (fault && sscanf(fault, "%63[^:]:%llx", name, (unsigned long long*)&fault_address) >= 1)
        fault_name = name;
    const char* bytes = getenv("ROOD_SHADOW_STACK");
    if (bytes && atol(bytes) > 0)
        pc_shadow_stack_bytes = (size_t)atol(bytes);
}

/* RPC owner {action: "report"}: the mismatches so far (reset: clear them). */
int pc_shadow_report(char* out, int size, int reset)
{
    int n = snprintf(out, (size_t)size, "{\"calls\":%u,\"count\":%u,\"mismatches\":[", shadow_calls, shadow_mismatches);
    for (unsigned i = 0; i < log_count && n < size - 400; i++) {
        mismatch* m = &log_[i];
        n += snprintf(out + n, (size_t)(size - n), "%s{\"call\":%u,\"vsync\":%u,\"function\":\"%s\",\"range\":\"%s\","
            "\"first\":%llu,\"bytes\":%u,\"returns_differ\":%s,\"runs\":[", i ? "," : "", m->call, m->vsync,
            m->function, m->range, (unsigned long long)m->first, m->bytes, m->returns_differ ? "true" : "false");
        for (unsigned r = 0; r < m->runs; r++)
            n += snprintf(out + n, (size_t)(size - n), "%s[%llu,%u]", r ? "," : "", (unsigned long long)m->run_at[r], m->run_len[r]);
        n += snprintf(out + n, (size_t)(size - n), "]}");
    }
    n += snprintf(out + n, (size_t)(size - n), "]}");
    if (reset)
        log_count = 0, shadow_calls = shadow_mismatches = 0;
    return n;
}
```

The x86-64 fill writes `[rsp - 8 - N, rsp - 8)`: below the return address, nothing live. `rep stosb` uses `%rdi`, which is caller-saved in SysV. (On Windows this file's shadow is never reached: `pc_shadow_available` is 0.)

- [ ] **Step 3: The Zig wrapper (`route.zig`)**

Replace `Shadow` with:

```zig
extern fn pc_shadow_begin(subsystem: c_int, name: [*:0]const u8) callconv(.c) c_int;
extern fn pc_shadow_after(stage: c_int) callconv(.c) void;
extern fn pc_shadow_returns(equal: c_int) callconv(.c) void;
extern fn pc_shadow_fill_dead_stack(pattern: c_uint) callconv(.c) void;

fn sameBytes(a: anytype, b: @TypeOf(a)) bool {
    if (@TypeOf(a) == void) return true;
    return std.mem.eql(u8, std.mem.asBytes(&a), std.mem.asBytes(&b));
}

fn Shadow(comptime subsystem: u8, comptime name: []const u8, comptime F: type, comptime owned: F) type {
    const R = @typeInfo(F).@"fn".return_type.?;
    return struct {
        fn run(args: std.meta.ArgsTuple(F)) R {
            const orig = original(name, F);
            if (pc_shadow_begin(subsystem, name ++ "") == 0) return @call(.auto, orig, args);
            pc_shadow_fill_dead_stack(0x5A);
            const a = @call(.never_inline, orig, args);
            pc_shadow_after(1);
            pc_shadow_fill_dead_stack(0xA5);
            _ = @call(.never_inline, orig, args);
            pc_shadow_after(2);
            pc_shadow_fill_dead_stack(0x5A);
            const b = @call(.never_inline, owned, args);
            pc_shadow_after(3);
            pc_shadow_returns(@intFromBool(sameBytes(a, b)));
            return a;
        }
        pub const call = withSignature(F, run);
    };
}
```

(`name ++ ""` is a comptime string literal with a 0 sentinel: `[*:0]const u8`.)

- [ ] **Step 4: `owner` RPC actions `report` and `coverage`**

In the `owner` method, before the listing:

```c
        char action[16] = "";
        if (params)
            string_value(member(params, "action"), action, sizeof action);
        if (!strcmp(action, "report")) {
            extern int pc_shadow_report(char*, int, int) __attribute__((weak));
            int reset = 0;
            bool_value(member(params, "reset"), &reset);
            if (pc_shadow_report)
                pc_shadow_report(result, (int)sizeof result, reset);
            else
                snprintf(result, sizeof result, "{\"calls\":0,\"count\":0,\"mismatches\":[]}");
            reply(fd, id, result);
            return;
        }
```

(Document `owner {action: "report", reset}` in the method list. `result` is `LINE_MAX_BYTES`; the report stays within it by the `n < size - 400` guard.)

In `pc/src/ownership.c`'s `owner_defaults`, shadow requested by `ROOD_OWNER` is checked later; add to `pc_owner_set`'s users nothing more. At start, `state_diff.c` refuses nothing: on Windows `pc_shadow_begin` returns 0 and shadow runs the original only.

- [ ] **Step 5: Run the smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ownership_smoke
```

Expected: `ok:   shadow: the pass-through matches the original: {"calls": N, "count": 0, ...}` with N > 0 if the walk met the room's enemy (if N is 0 the check is still green; Task 20's corpus is what drives battle actions).

- [ ] **Step 6: Commit**

```bash
git add pc/src/state_diff.c game/common/route.zig pc/src/dev_rpc.c pc/src/ownership.c pc/tests/ownership_smoke.py
git commit -m "Shadow mode: routed calls run original twice and owned once from one state, compared

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: Addresses to fields (`field_map.py`) and the noise masks

**Files:**
- Create: `pc/tools/field_map.py`, `pc/tests/diff/__init__.py` (empty), `pc/tests/diff/masks.py`

- [ ] **Step 1: `pc/tests/diff/masks.py`**

```python
"""Known noise in differential runs (pc/docs/Ownership.md), each with its
reason. Shadow mode finds uninitialised-stack noise by itself (two original
runs); tick mode needs these. A mask is a last resort: state the reason."""
import dataclasses


@dataclasses.dataclass
class Mask:
    where: object  # symbol name or guest address
    size: int
    reason: str


MASKS = [
    Mask("D_800F53C0", 0xE8, "the effect request func_8007138C builds on the stack with fields it never "
                             "writes, copied there by func_800CF0E8 (func_800CF0E8_t): host stack garbage"),
    Mask(0x801F8000, 0x8000, "the dead part of the guest stack: host stack garbage lands there; the game "
                             "does not read it"),
    Mask("last_vsync", 16, "boot_runtime.c: the wall-clock time of the last vsync (host timing)"),
]


def resolve(symbols):
    """[(address, size)] for the digest RPC; `symbols` maps names to addresses (rood_rpc.symbols())."""
    out = []
    for m in MASKS:
        address = m.where if isinstance(m.where, int) else symbols.get(m.where)
        if address is not None:
            out.append((address, m.size))
    return out
```

- [ ] **Step 2: `pc/tools/field_map.py`**

```python
#!/usr/bin/env python3
"""Name the field an address falls in (pc/docs/Ownership.md): a symbol from
the binary's symbol table (guest data imports included), then gdb walks the
DWARF type of that symbol, or of the object a root pointer points at
(game/ownership.toml roots, their values read from the running game), down
to e.g. `D_800F19CC->unk854[1].unk4.limbs[2]`.

    fm = FieldMap(binary, roots, read)        read(address, size) -> bytes
    fm.names([address, ...]) -> {address: "symbol+0x10 (path)"}
"""
import bisect
import json
import pathlib
import subprocess
import sys
import tempfile

GDB_SCRIPT = r'''
import gdb, json, sys
request = json.load(open(sys.argv[1]))
def walk(t, offset, path):
    t = t.strip_typedefs()
    while True:
        if t.code == gdb.TYPE_CODE_ARRAY:
            e = t.target().strip_typedefs()
            i = offset // max(e.sizeof, 1)
            path += "[%d]" % i
            offset -= i * e.sizeof
            t = e
        elif t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            hit = None
            for f in t.fields():
                start = f.bitpos // 8
                if start <= offset < start + max(f.type.sizeof, 1):
                    hit = f
            if hit is None or hit.name is None:
                return path
            path += "." + hit.name
            offset -= hit.bitpos // 8
            t = hit.type.strip_typedefs()
        else:
            return path
out = {}
for key, (expr, offset) in request["queries"].items():
    try:
        t = gdb.parse_and_eval(expr).type
        out[key] = walk(t, offset, expr)
    except Exception as e:
        out[key] = None
sizes = {}
for root in request["roots"]:
    try:
        t = gdb.parse_and_eval(root).type.strip_typedefs()
        if t.code == gdb.TYPE_CODE_ARRAY:
            sizes[root] = [t.range()[1] + 1, t.target().strip_typedefs().target().strip_typedefs().sizeof]
        else:
            sizes[root] = [0, t.target().strip_typedefs().sizeof]
    except Exception:
        pass
json.dump({"paths": out, "roots": sizes}, open(request["out"], "w"))
'''


class FieldMap:
    def __init__(self, binary, roots=(), read=None, pointer_size=4):
        self.binary, self.roots, self.read, self.ptr = str(binary), list(roots), read, pointer_size
        rows = []
        for line in subprocess.run(["nm", "-S", "-n", self.binary], capture_output=True, text=True).stdout.splitlines():
            parts = line.split()
            if len(parts) == 4:
                rows.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
            elif len(parts) == 3:
                rows.append((int(parts[0], 16), 0, parts[2]))
        rows.sort()
        self.rows, self.starts = rows, [r[0] for r in rows]

    def symbol(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        while i >= 0:
            start, size, name = self.rows[i]
            if size == 0 or address < start + size:
                return name, address - start
            i -= 1
        return None, address

    def _gdb(self, queries, roots):
        with tempfile.TemporaryDirectory() as d:
            req, out = pathlib.Path(d) / "req.json", pathlib.Path(d) / "out.json"
            script = pathlib.Path(d) / "walk.py"
            script.write_text(GDB_SCRIPT)
            req.write_text(json.dumps({"queries": queries, "roots": roots, "out": str(out)}))
            subprocess.run(["gdb", "-batch", "-nx", self.binary, "-ex",
                            f"python import sys; sys.argv = ['walk', {str(req)!r}]", "-x", str(script)],
                           capture_output=True)
            return json.loads(out.read_text()) if out.exists() else {"paths": {}, "roots": {}}

    def names(self, addresses):
        queries, labels = {}, {}
        root_info = self._gdb({}, self.roots)["roots"] if self.read else {}
        for address in addresses:
            for root, (count, size) in root_info.items():
                base = self.symbol_address(root)
                if base is None:
                    continue
                for k in range(max(count, 1)):
                    pointer = int.from_bytes(self.read(base + k * self.ptr, self.ptr), "little")
                    if pointer and pointer <= address < pointer + size:
                        expr = f"(*{root}[{k}])" if count else f"(*{root})"
                        queries[str(address)] = [expr, address - pointer]
            if str(address) not in queries:
                name, offset = self.symbol(address)
                labels[address] = f"{name}+{offset:#x}" if name else f"{address:#x}"
                if name:
                    queries[str(address)] = [name, offset]
        paths = self._gdb(queries, [])["paths"] if queries else {}
        for address in addresses:
            path = paths.get(str(address))
            labels[address] = (labels.get(address, f"{address:#x}") + (f" ({path})" if path else ""))
        return labels

    def symbol_address(self, name):
        for start, _, n in self.rows:
            if n == name:
                return start
        return None


if __name__ == "__main__":
    fm = FieldMap(sys.argv[1])
    print(json.dumps(fm.names([int(a, 0) for a in sys.argv[2:]]), indent=1))
```

- [ ] **Step 3: Try it on a known address**

```bash
python3 pc/tools/field_map.py pc/build/boot/rood_battle_probe $(nm pc/build/boot/rood_battle_probe | awk '$3=="vs_main_scoredata"{print "0x"$1}')
```

Expected: `"vs_main_scoredata+0x0 (vs_main_scoredata.<first field>)"`.

- [ ] **Step 4: Commit**

```bash
git add pc/tools/field_map.py pc/tests/diff
git commit -m "field_map.py: addresses to symbol fields (DWARF, root pointers); differential noise masks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 20: The battle corpus and the differential runner

**Files:**
- Create: `pc/tests/diff/scenarios.py`, `pc/tests/diff/differ.py`
- Modify: `pc/tests/run.py`, `pc/tests/ownership_smoke.py` (the fault check)

- [ ] **Step 1: `pc/tests/diff/scenarios.py`**

```python
"""The battle-action corpus (pc/docs/Ownership.md "The corpus"): each
scenario is a setup (RPC, deterministic, owner original) from the save_room
checkpoint and a play function whose presses are recorded once and replayed
for every other run. The game state at the mark has the modern menus closed;
every play ends with them closed (host UI state is not rewound)."""
import struct

ROOM = (9, 18)          # two enemies that keep their distance (skip_charging_smoke)
APPROACH = 676
SOLID_SHOCK, CURE_ROOT = 97, 323
ACTION, ACTION_UNLOCKED = 52, 13
HP, MP = 0x18, 0x1C
CLOSED = 0x3F


def ptr_size(game):
    return 8 if game.call("status").get("pointer_size", 4) == 8 else 4


def setup_room(g, sym):
    """Warp with enemies, learn Solid Shock, MP 99, modern menus on."""
    g.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
    for _ in range(80):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.run(APPROACH)
    g.call("ui_menu", modern=True)
    g.call("skip_charging", mode=0)
    a = sym["vs_main_actions"] + SOLID_SHOCK * ACTION + ACTION_UNLOCKED
    g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
    size = ptr_size(g)
    state = int.from_bytes(g.read(sym["vs_battle_characterState"], size), "little")
    a2 = int.from_bytes(g.read(state + 0x3C, 4), "little")
    g.write(a2 + MP, struct.pack("<hh", 99, 99))
    g.run(2)


def _shell(g, tab):
    for _ in range(6):
        if g.call("ui_menu").get("visible"):
            break
        g.press("triangle", vsyncs=3, settle=30)
    for _ in range(8):
        if g.call("ui_menu").get("tab") == tab:
            return
        g.press("r1", vsyncs=3, settle=4)
    raise TimeoutError(f"the {tab} tab")


def _confirm_target(g, sym):
    g.press("circle", vsyncs=3, settle=40)
    for _ in range(6):
        g.call("input", buttons=["circle"], vsyncs=3)
        g.run(4)
        if g.read(sym["_cameraMode"], 1)[0] not in (1, 3):
            break
        g.run(36)


def play_spell(g, sym):
    """Solid Shock on the enemy in range (a spell: phases 4-7, enemy hit)."""
    _shell(g, "magick")
    for _ in range(6):
        if g.call("ui_menu")["magick"].get("sub") == "warlock":
            break
        g.press("r2", vsyncs=3, settle=4)
    spells = next(s["spells"] for s in g.call("ui_menu")["magick"]["schools"] if s.get("id") == "warlock")
    row = next(i for i, x in enumerate(spells) if x["id"] == SOLID_SHOCK)
    for _ in range(24):
        if g.call("ui_menu")["magick"]["cursor"] == row:
            break
        g.press("down", vsyncs=3, settle=4)
    _confirm_target(g, sym)


def play_item(g, sym):
    """A Cure Root on Ashley (an item: phase 4 zoom style, misc count down)."""
    _shell(g, "items")
    for _ in range(8):
        if g.call("ui_menu").get("items", {}).get("category") == "misc":
            break
        g.press("r2", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=4)
    for _ in range(10):
        m = g.call("ui_menu")["items"]
        if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
            break
        g.press("down", vsyncs=3, settle=4)
    _confirm_target(g, sym)


def play_attack(g, sym):
    """Draw the weapon and attack the enemy in range (an action without an
    effect: func_8007138C releases at once; chain window in phase 7)."""
    g.press("circle", vsyncs=3, settle=20)   # draw (field: Circle draws with default button roles)
    g.press("circle", vsyncs=3, settle=40)   # the battle ring opens on the target
    _confirm_target(g, sym)


def play_attack_chain(g, sym):
    """An attack with a chain ability pressed in the timing window."""
    play_attack(g, sym)
    for _ in range(40):
        if g.read(sym["_cameraMode"], 1)[0] == 7:
            break
        g.run(1)
    g.press("triangle", vsyncs=2, settle=2)


def play_cancel(g, sym):
    """Open the target ring and cancel (the commit's early-return paths)."""
    g.press("circle", vsyncs=3, settle=20)
    g.press("circle", vsyncs=3, settle=40)
    g.press("cross", vsyncs=3, settle=40)


def play_wait_enemy(g, sym):
    """Stand in battle mode while the enemy acts (caster != 0)."""
    g.press("circle", vsyncs=3, settle=20)
    g.run(900)


SCENARIOS = {
    "spell": (setup_room, play_spell, 720),
    "item": (setup_room, play_item, 720),
    "attack": (setup_room, play_attack, 360),
    "chain": (setup_room, play_attack_chain, 360),
    "cancel": (setup_room, play_cancel, 120),
    "enemy": (setup_room, play_wait_enemy, 120),
}
```

(Button roles: the scenarios assume the default roles; the runner launches with a scratch `ROOD_SETTINGS` holding `qol_confirm_cross 0` and `qol_field_cross_attack 0`. If a press sequence does not reach its phase, fix the scenario with the RPC `ui_menu`/`_cameraMode` reads; the coverage gate in Task 27 tells which branches each one reaches.)

- [ ] **Step 2: `pc/tests/diff/differ.py`**

```python
#!/usr/bin/env python3
"""Differential test of an owned subsystem against the original (pc/docs/Ownership.md):

    python3 pc/tests/diff/differ.py battle_action [--mode call,tick,alternate] [--scenario spell ...]
        [--skip-on] [--card pc/build/test-card1.mcd] [--out DIR]     (64-bit: ROOD_ARCH=64, as run.py --arch 64 sets)

One game process per scenario (a fast resume of the save_room checkpoint):
the scenario's setup runs with the owner original and the state is marked in
memory; the oracle run records the presses and a digest per vsync; then
  call       owner shadow: every routed call compared (state_diff.c); report
             with fields;
  tick       owner original records per-vsync digests while the scenario's
             presses are recorded; rewind; owner owned replays them and is
             compared every vsync; on a difference both states at the first
             differing tick are saved and diffed (snapshot_diff, masks, fields);
  alternate  as tick, the owner flipping every vsync (owner switches are safe);
--skip-on: tick and alternate with qol_skip_charging 1 (Task 28: the hook
against the native skip). Exit 0 when every run matches.
"""
import argparse
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
sys.path.insert(0, str(ROOT / "pc/tests/diff"))
import rood_rpc  # noqa: E402
import snapshot_diff  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from field_map import FieldMap  # noqa: E402
import masks as noise  # noqa: E402
import ownership  # noqa: E402
import scenarios  # noqa: E402

failures = []


def say(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def first_difference(g, out, name, rec, tick, subsystem, fm, mask_list):
    """Save both owners' states at `tick` and name what differs."""
    paths = {}
    for owner in ("original", "owned"):
        g.rewind()
        g.owner(subsystem, owner)
        g.replay(rec)
        g.run(tick + 1)
        paths[owner] = out / f"{name}-{owner}-{tick}.snap"
        g.call("snapshot", action="save", path=str(paths[owner]))
    a, b = snapshot_diff.read(paths["original"]), snapshot_diff.read(paths["owned"])
    for start, size in mask_list:
        for snap in (a, b):
            for base, body in list(snap["memory"].items()):
                if base <= start < base + len(body):
                    lo = start - base
                    snap["memory"][base] = body[:lo] + bytes(min(size, len(body) - lo)) + body[lo + size:]
    lines = snapshot_diff.diff(a, b, snapshot_diff.Symbols(rood_rpc.BINARY))
    addresses = [int(l.split("(")[1].split(")")[0], 16) for l in lines if l.startswith("    ") and "(0x" in l][:12]
    labels = fm.names(addresses)
    for line in lines[:40]:
        print("      " + line)
    for address, label in labels.items():
        print(f"      {address:#x}: {label}")
    g.rewind()


def run_scenario(name, args, env, out, sym, modes, roots):
    """Every run of one scenario in one game process (a fast resume of the checkpoint)."""
    setup, play, ticks = scenarios.SCENARIOS[name]
    g = launch_at("save_room", card=args.card, fast=True, log=out / f"{name}.log", extra_env=env)
    try:
        index = next(s["index"] for s in g.owner()["subsystems"] if s["name"] == args.subsystem)
        fm = FieldMap(rood_rpc.BINARY, roots, g.read, 8 if os.environ.get("ROOD_ARCH") == "64" else 4)
        mask_list = noise.resolve(sym)
        g.digest("stop", masks=[[a, size] for a, size in mask_list])
        g.owner(args.subsystem, "original")
        setup(g, sym)
        if args.skip_on:
            g.call("skip_charging", mode=1)
        say(not g.call("ui_menu").get("visible"), f"{name}: menus closed at the scenario's start")
        g.mark()
        rec = out / f"{name}.rec"
        # The oracle run: the presses are recorded, a digest is kept per vsync.
        g.digest("record", ticks=6000)
        g.record(rec)
        play(g, sym)
        g.run(ticks)
        g.record(None)
        total = g.digest("stop")["recorded"]
        say(not g.call("ui_menu").get("visible"), f"{name}: menus closed at the end")
        for mode in modes:
            g.rewind()
            if mode == "call":
                if args.skip_on:
                    continue  # the skip differs from the original by design: tick mode compares it with its hook
                g.owner(args.subsystem, "shadow")
                g.call("owner", action="report", reset=True)
                g.replay(rec)
                g.run(total)
                report = g.call("owner", action="report")
                if not say(report["count"] == 0, f"{name}: call mode, {report['calls']} calls compared"):
                    labels = fm.names([m["first"] for m in report["mismatches"][:8] if m["range"] != "spu"])
                    for m in report["mismatches"][:8]:
                        print(f"      call {m['call']} vsync {m['vsync']} {m['function']}: {m['bytes']} bytes in "
                              f"{m['range']}, first {labels.get(m['first'], hex(m['first']))}"
                              + (" (return value differs)" if m["returns_differ"] else ""))
            else:
                g.owner(args.subsystem, "owned")
                g.replay(rec)
                g.digest("compare", alternate=index if mode == "alternate" else -1)
                g.run(total)
                result = g.digest("result")
                ok = result["first_tick"] < 0
                say(ok, f"{name}: {mode} mode, {total} vsyncs" + ("" if ok else
                    f": first difference at tick {result['first_tick']} in {result['first_pages'][:4]}"))
                if not ok:
                    first_difference(g, out, f"{name}-{mode}", rec, result["first_tick"], args.subsystem, fm,
                                     mask_list)
            g.replay(None)
            g.owner(args.subsystem, "original")
        log = pathlib.Path(g.log).read_text(errors="replace")
        say("[crash]" not in log and "[zig] panic" not in log, f"{name}: no crash, no owned-code panic")
    finally:
        g.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subsystem")
    parser.add_argument("--mode", default="call,tick,alternate")
    parser.add_argument("--scenario", action="append")
    parser.add_argument("--skip-on", action="store_true")
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", default=str(rood_rpc.OUT / "diff"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    settings = out / "settings.cfg"  # pinned: every intentional change at its original value
    settings.write_text("qol_confirm_cross 0\nqol_field_cross_attack 0\nqol_skip_charging 0\n")
    (out / "lights").mkdir(exist_ok=True)
    (out / "prefabs").mkdir(exist_ok=True)
    env = {"ROOD_GI": "0", "ROOD_DETERMINISTIC": "1", "ROOD_SETTINGS": str(settings),
           "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs")}
    roots = next(s for s in ownership.load().subsystems if s.name == args.subsystem).roots
    sym = rood_rpc.symbols()
    for name in args.scenario or list(scenarios.SCENARIOS):
        run_scenario(name, args, env, out, sym, args.mode.split(","), roots)
    print("FAILED" if failures else f"{args.subsystem}: owned == original on every run")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

One process per scenario (a fast resume of the checkpoint's save state), and every run of that scenario (the oracle, call, tick, alternate, the first-difference saves) in it through the in-memory mark: no relaunch between runs.

- [ ] **Step 3: The suite**

`pc/tests/run.py`:

```python
            Suite("diff_battle_action", [PY, str(TESTS / "diff/differ.py"), "battle_action", "--card", str(args.save)],
                  1800, note="owned battle actions == the original: shadow calls, per-vsync digests and "
                             "alternating owners over the battle corpus (pc/tests/diff)"),
```

And add the fault check to `ownership_smoke.py` (`import rood_rpc` at the top; `sys.path` gains `pc/tests/diff`), in `main()` after the first game's `finally`:

```python
    import scenarios
    sym = rood_rpc.symbols()
    fault = {"ROOD_SHADOW_FAULT": f"func_800787F0:{sym['D_800F1868']:#x}", "ROOD_OWNER": "battle_action=shadow"}
    g = launch_at("save_room", card=args.card, fast=True, log=out / "fault.log", extra_env={**env, **fault})
    try:
        g.owner("battle_action", "original")
        scenarios.setup_room(g, sym)
        g.owner("battle_action", "shadow")
        scenarios.play_spell(g, sym)
        g.run(720)
        report = g.call("owner", action="report")
        check(any(m["function"] == "func_800787F0" and m["first"] == sym["D_800F1868"] for m in report["mismatches"]),
              "shadow mode reports an injected fault at its address")
    finally:
        g.close()
```

- [ ] **Step 4: Run it against the pass-through module**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 diff_battle_action ownership_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 diff_battle_action
```

Expected: every scenario `ok:` in call, tick and alternate modes (the owned side is the original), `owned == original on every run`, and the smoke's fault reported. This is the runner's own acceptance: it reports nothing for identical code and something for a planted fault. If tick mode differs here, the difference is nondeterminism (find it, fix determinism or add a mask with a reason), not the module.

- [ ] **Step 5: Commit**

```bash
git add pc/tests/diff pc/tests/run.py pc/tests/ownership_smoke.py
git commit -m "Differential runner: battle corpus; call, tick and alternating modes in one process

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 21: The battle-action module's skeleton, coverage and the coverage gate (red)

**Files:**
- Create: `game/battle/action/types.zig`, `game/battle/action/coverage.zig`, `game/common/settings.zig`
- Modify: `game/battle/root.zig`, `pc/src/ownership.c`, `pc/src/dev_rpc.c`, `pc/tests/diff/scenarios.py`, `pc/tests/diff/differ.py`, `game/common/common.zig`

- [ ] **Step 1: Coverage on the host side**

`pc/src/ownership.c`:

```c
/* Owned code's branch coverage (game/battle/action/coverage.zig): one byte
 * per branch id, set when the branch runs; names in game_coverage_names
 * (newline-separated, exported by the owned object). Host state. */
unsigned char pc_owned_coverage[1024];
extern const char game_coverage_names[] __attribute__((weak));

/* RPC owner {action: "coverage", reset}: {"hit": [...], "missed": [...]}. */
int pc_owned_coverage_json(char* out, int size, int reset)
{
    int n = snprintf(out, (size_t)size, "{\"hit\":[");
    const char* names = &game_coverage_names ? game_coverage_names : "";
    for (int pass = 0; pass < 2; pass++) {
        if (pass)
            n += snprintf(out + n, (size_t)(size - n), "],\"missed\":[");
        int id = 0, first = 1;
        for (const char* p = names; *p && n < size - 80; id++) {
            const char* end = strchr(p, '\n');
            int len = end ? (int)(end - p) : (int)strlen(p);
            if ((pc_owned_coverage[id] != 0) == (pass == 0)) {
                n += snprintf(out + n, (size_t)(size - n), "%s\"%.*s\"", first ? "" : ",", len, p);
                first = 0;
            }
            p += len + (end != NULL);
        }
    }
    n += snprintf(out + n, (size_t)(size - n), "]}");
    if (reset)
        memset(pc_owned_coverage, 0, sizeof pc_owned_coverage);
    return n;
}
```

`dev_rpc.c`, in the `owner` method's action dispatch:

```c
        if (!strcmp(action, "coverage")) {
            extern int pc_owned_coverage_json(char*, int, int);
            int reset = 0;
            bool_value(member(params, "reset"), &reset);
            pc_owned_coverage_json(result, (int)sizeof result, reset);
            reply(fd, id, result);
            return;
        }
```

- [ ] **Step 2: The gate (it fails: the pass-through module runs no owned branch)**

`pc/tests/diff/scenarios.py` gains the branches the corpus must reach:

```python
# Owned branches the corpus must run (game/battle/action/coverage.zig); the rest are reported.
REQUIRED = ["cast_ready", "wait_cast_ready", "flash_over", "release_spell_item", "release_plain",
            "zoom_0", "zoom_converged", "cast_spell", "cast_item", "commit_arg_other", "commit_weapon_score",
            "commit_cancel_back", "recast", "winddown_window"]
```

`differ.py`: after the scenarios loop in `main()`, one more short game reads the coverage gathered by every scenario's owned runs... coverage is per process, so collect it per scenario instead: at the end of `run_scenario` (before `g.close()`):

```python
        cov = g.call("owner", action="coverage")
        COVERED.update(cov["hit"])
        ALL_BRANCHES.update(cov["hit"] + cov["missed"])
```

with module globals `COVERED, ALL_BRANCHES = set(), set()`, and in `main()` after the loop:

```python
    missing = [b for b in scenarios.REQUIRED if b not in COVERED]
    say(not missing, f"coverage: every required owned branch ran ({len(COVERED)}/{len(ALL_BRANCHES)} hit)"
        + (f"; missing {missing}" if missing else ""))
    print("      not reached by the corpus:", sorted(ALL_BRANCHES - COVERED))
```

Run against the pass-through:

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call
```

Expected: every equivalence line `ok:` and `FAIL: coverage: ... missing [...]` (no owned code ran yet).

- [ ] **Step 3: The module's shared files**

`game/common/settings.zig`:

```zig
//! Settings owned code reads: the registry's variables (project_settings.cpp
//! entries; System > Gameplay), never copies. Reached like guest globals.
const psx = @import("psx.zig");

/// qol_skip_charging: 0 off, 1 skip.
pub fn skipCharging() c_int {
    return psx.global(c_int, "pc_qol_skip_charging").*;
}
/// qol_skip_charging_who: 0 Ashley's actions, 1 everyone's.
pub fn skipChargingWho() c_int {
    return psx.global(c_int, "pc_qol_skip_charging_who").*;
}
/// charge_skip.c: which kinds Skip applies to (RPC skip_charging kinds; developer mask).
pub fn skipKinds() c_int {
    return psx.global(c_int, "pc_charge_skip_kinds").*;
}
/// Who runs a subsystem (ownership.c pc_owner[]): 0 original, 1 owned, 2 shadow.
pub fn owner(slot: u8) u8 {
    return psx.global([64]u8, "pc_owner")[slot];
}
```

`game/common/common.zig` gains `pub const settings = @import("settings.zig");`.

`game/battle/action/types.zig`:

```zig
//! Battle action state shared by the phases (pc/docs/Ownership.md §8): all of
//! it in guest memory, through the views.
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const g = v.g;

/// BATTLE's phase while an action runs (_cameraMode, with D_800F196C == 2).
pub const Phase = enum(u32) {
    idle = 0,
    pick = 1,
    target = 2,
    confirm = 3,
    zoom = 4,
    cast = 5,
    flash = 6,
    release = 7,
    recast = 8,
    _,
};
pub fn phase() Phase {
    return @enumFromInt(g._cameraMode().*);
}
pub fn setPhase(p: Phase) void {
    g._cameraMode().* = @intFromEnum(p);
}

/// The action block (D_800F19CC, allocated while BATTLE runs).
pub fn block() *v.D_800F19CC_t {
    return g.D_800F19CC().*.?;
}
/// The action being executed: unk854[unk0 & 3] (unk0 counts chained actions).
pub fn current(b: *v.D_800F19CC_t) *v.D_800F19CC_t2 {
    return &b.unk854[b.unk0 & 3];
}
/// Who acts (0 is Ashley).
pub fn caster(a: *const v.D_800F19CC_t2) u8 {
    return psx.conv(u8, a.unk4.unk0.targetActor);
}

pub const Kind = enum { spell, art, b8, item };
/// func_8007138C's ranges of action indices.
pub fn kind(index: u16) Kind {
    return if (index < 0x8D) .spell else if (index < 0xB8) .art else if (index < 0xE0) .b8 else .item;
}
/// vs_main_actions[index].unk2_0: the action has an effect, so a wind-up.
pub fn hasEffect(index: u16) bool {
    return psx.getBits(&g.vs_main_actions()[index], "unk2_0") != 0;
}
```

`game/battle/action/coverage.zig`:

```zig
//! Branch coverage of the owned battle-action module (RPC owner {action:
//! "coverage"}): the differential corpus must run the branches
//! pc/tests/diff/scenarios.py REQUIRED lists. Host state (ownership.c).
const std = @import("std");
const psx = @import("common").psx;

pub const Branch = enum(u16) {
    cast_ready, wait_cast_ready, flash_over,
    release_flag_b8, release_plain, release_art, release_b8, release_spell_item, release_chain, release_chain_other,
    zoom_idle, zoom_0, zoom_1, zoom_2, zoom_other, zoom_converged,
    cast_no_effect, cast_spell, cast_art, cast_b8, cast_item, cast_break_art_object, cast_information,
    commit_cancel_enemy, commit_cancel_plain, commit_cancel_back, commit_ability_null, commit_misc_item,
    commit_enemy, commit_arg_0, commit_arg_2, commit_arg_other, commit_weapon_score, commit_heal_score,
    recast,
    winddown_window, winddown_input, winddown_chain, winddown_defense, winddown_early, winddown_late,
    winddown_abort,
    skip_zoom, skip_cast,
};

pub inline fn hit(comptime b: Branch) void {
    psx.global([1024]u8, "pc_owned_coverage")[@intFromEnum(b)] = 1;
}

const names = blk: {
    var text: []const u8 = "";
    for (std.meta.fieldNames(Branch)) |n| text = text ++ n ++ "\n";
    break :blk text;
};
/// The branch names, newline-separated, in id order (a byte array: no relocations).
export const game_coverage_names: [names.len + 1]u8 = (names ++ "\x00")[0 .. names.len + 1].*;
```

- [ ] **Step 4: The root imports the module (still pass-through)**

`game/battle/root.zig`: keep `BattleAction` as is for now, and add (so the files compile on every target from here on):

```zig
const types = @import("action/types.zig");
const coverage = @import("action/coverage.zig");
comptime {
    _ = types.hasEffect;
    _ = coverage.game_coverage_names;
}
```

- [ ] **Step 5: Build all battle targets, run the gate (still red), commit**

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call --scenario spell | tail -3
git add game pc/src/ownership.c pc/src/dev_rpc.c pc/tests/diff
git commit -m "Battle action module skeleton: phases, action block, kinds; coverage and the corpus gate

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Expected: builds clean; the gate line FAILs (missing every required branch). From here each port turns some of it green.

---

### Task 22: Port the release phases (`func_800704B0`, `func_80078748`, `func_800787F0`, `func_80070278`)

**Files:**
- Create: `game/battle/action/release.zig`, `game/battle/action/skip.zig` (stub)
- Modify: `game/battle/root.zig`

- [ ] **Step 1: `skip.zig` stub (Task 28 fills it)**

```zig
//! The true charging skip (Task 28). Until then: never.
pub fn now() bool {
    return false;
}
pub fn zoom() void {}
pub fn waitCast() void {}
```

- [ ] **Step 2: `release.zig`**

```zig
//! The cast's last steps and the release (146C.c func_80078748,
//! func_800704B0, func_800787F0, func_80070278).
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");
const skip = @import("skip.zig");

/// func_800704B0: the cast is ready; wait for its flash (phase 6).
pub fn castReady() callconv(.c) void {
    cov.hit(.cast_ready);
    t.setPhase(.flash);
    c.func_800CF920();
}

/// func_80078748 (phase 5): ready when the name window is gone, the cast
/// animation is at its loop boundary, object data slot 0 is free and the
/// effect's files are in.
pub fn waitCast() callconv(.c) void {
    if (skip.now()) return skip.waitCast();
    const a = t.current(t.block());
    if (c.func_800CB45C() == 0 and
        (c.func_800A0BE0(t.caster(a)) & 0x08000100) == 0x08000100 and
        c.vs_battle_getEmptyObjectDataSlot() == 0 and
        c.func_800CEEBC() != 0)
    {
        cov.hit(.wait_cast_ready);
        castReady();
    }
}

/// func_800787F0 (phase 6): release once the cast flash is over, and spawn
/// the effect (its script applies the hits rolled at commit).
pub fn waitFlash() callconv(.c) void {
    if ((g.D_800F1868().* & 2) == 0) {
        cov.hit(.flash_over);
        release();
        _ = c.func_800CEF74(0);
    }
}

/// func_80070278: the release. The caster turns to the aim (or plays its
/// art/chain animation), the chain/defense timing window opens, the cast
/// sounds stop, phase 7.
pub fn release() callconv(.c) void {
    const b = t.block();
    b.battleAbilityTargetWindow = 0;
    b.battleAbilityTimer = 0;
    const a = t.current(b);
    const who: c_int = t.caster(a);
    const index = a.actionIndex;
    if ((@as(u32, index) -% 0x16) < 0x20 and a.unk4.unk40 == 0 and who == 0) {
        cov.hit(.release_flag_b8);
        c.vs_battle_setStateFlag(0xB8, 1);
    }
    const chained = b.unk0;
    const aim = psx.cptr(c.SVECTOR, &b.unk8.unk844);
    if (chained == 0) {
        if (!t.hasEffect(index)) {
            cov.hit(.release_plain);
            c.func_8009DD00(who, aim, a.unk48);
            b.battleAbilityTargetWindow = psx.conv(u16, c.func_8009F8DC(who) *% 2);
        } else {
            switch (t.kind(index)) {
                .art => {
                    cov.hit(.release_art);
                    c.func_8009F298(who, aim, 1);
                },
                .b8 => {
                    cov.hit(.release_b8);
                    c.func_8009EFEC(who, aim, 1);
                },
                .spell, .item => {
                    cov.hit(.release_spell_item);
                    c.func_8009E2E0(who, aim, 1);
                },
            }
            b.battleAbilityTargetWindow = 0;
            g.vs_gametime_tickspeed().* = 4;
        }
    } else {
        if (index < 0x28) {
            cov.hit(.release_chain);
            // The original reads mappedChainAbilities[unk2C03 - 1] unchecked.
            const mapped = psx.at(&g.vs_main_settings().mappedChainAbilities, @as(isize, b.unk2C03) - 1).*;
            c.func_8009EC9C(who, aim, @as(c_int, mapped) - 0x16, psx.conv(c_int, chained));
        } else {
            cov.hit(.release_chain_other);
            c.func_8009EE9C(who, aim, 0);
        }
        b.battleAbilityTargetWindow = psx.conv(u16, c.func_8009F8DC(who) *% 2);
    }
    c.func_80045D64(0x7E, 0x34);
    c.func_80045D64(0x7E, 0x35);
    c.func_80045D64(0x7E, 0x36);
    c.func_800CB50C();
    b.unk2C03 = 0;
    t.setPhase(.release);
}
```

- [ ] **Step 3: Route them to the owned code**

In `game/battle/root.zig`'s `BattleAction`, replace the four pass-throughs:

```zig
const release = @import("action/release.zig");
...
    pub const func_800704B0 = release.castReady;
    pub const func_80078748 = release.waitCast;
    pub const func_800787F0 = release.waitFlash;
    pub const func_80070278 = release.release;
```

(`exportAll` takes `@field(Impl, name)`: a `pub const` alias of a function works like a `pub fn`.)

- [ ] **Step 4: Build all battle targets and run the differential test**

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call,tick
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --mode call,tick
```

Expected: every equivalence line `ok:` on both; the coverage line still FAILs but now lists `cast_ready`, `wait_cast_ready`, `flash_over`, `release_*` as hit. A mismatch report names the function, the call, and the field (e.g. `D_800F19CC->battleAbilityTargetWindow`): fix the port, never the oracle.

- [ ] **Step 5: Commit**

```bash
git add game
git commit -m "Owned battle actions: the release phases (func_800704B0, func_80078748, func_800787F0, func_80070278)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 23: Port the camera zoom (`_handleCombatCameraZoom`)

**Files:**
- Create: `game/battle/action/zoom.zig`, `game/battle/action/cast.zig` (forwarder until Task 24)
- Modify: `game/battle/root.zig`

- [ ] **Step 1: `cast.zig` forwarder**

```zig
//! The cast's start (Task 24 ports it). Until then: the original, through
//! the trampoline (so owner and shadow routing still apply).
const c = @import("c");
pub fn start() callconv(.c) void {
    c.func_8007138C();
}
```

- [ ] **Step 2: `zoom.zig`**

```zig
//! Phase 4: the camera converges on the caster and the target, then the cast
//! starts (146C.c _handleCombatCameraZoom). Three styles by the action
//! block's unk2C0E (set at commit from the action's flagsD_4): converge;
//! zoom in (projection to 0x300, distance down to 0x600); pull out
//! (distance up to 0x900).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");
const cast = @import("cast.zig");
const skip = @import("skip.zig");

/// _handleCombatCameraZoom.
pub fn zoom() callconv(.c) void {
    if (skip.now()) return skip.zoom();
    step();
}

fn moving(cam: *const v._sphericalCamera) bool {
    return cam.delta.mode == 1 or cam.delta.mode == 3;
}

/// Converge one vsync; when the camera has arrived, start the cast.
fn convergeThenCast(cam: *v._sphericalCamera) void {
    var target = std.mem.zeroes(v.SVECTOR);
    var step_ = std.mem.zeroes(v.VECTOR);
    var displacement = std.mem.zeroes(v.VECTOR);
    c.func_800780A8(psx.cptr(c.SVECTOR, &target));
    _ = c._computeStepAndDisplacement(psx.cptr(c.SVECTOR, &target), psx.cptr(c.VECTOR, &step_),
        psx.cptr(c.VECTOR, &displacement), 1, 0x28);
    if ((displacement.vx | displacement.vy | displacement.vz) == 0 and cam.delta.yaw == 0 and moving(cam)) {
        cov.hit(.zoom_converged);
        cast.start();
    }
}

/// One vsync of phase 4 (also the skip's step).
pub fn step() void {
    const cam = g.vs_battle_cameraCurrentSpherical();
    if (!moving(cam)) {
        cov.hit(.zoom_idle);
        return;
    }
    switch (t.block().unk2C0E) {
        0 => {
            cov.hit(.zoom_0);
            c.func_8007820C(1);
            convergeThenCast(cam);
        },
        1 => {
            cov.hit(.zoom_1);
            c.func_8007820C(1);
            const projection = g.vs_main_projectionDistance();
            projection.* +%= 0x40;
            if (projection.* >= 0x300) projection.* = 0x300;
            c.SetGeomScreen(projection.*);
            if (cam.values.distance > 0x600) {
                cam.values.distance -%= 0xC0;
                if (cam.values.distance < 0x600) cam.values.distance = 0x600;
            }
            if (projection.* == 0x300) convergeThenCast(cam);
        },
        2 => {
            cov.hit(.zoom_2);
            c.func_8007820C(1);
            if (cam.values.distance < 0x900) {
                cam.values.distance +%= 0xC0;
                if (cam.values.distance > 0x900) cam.values.distance = 0x900;
            }
            if (cam.values.distance == 0x900) convergeThenCast(cam);
        },
        else => if (cam.delta.yaw == 0 and moving(cam)) {
            cov.hit(.zoom_other);
            cast.start();
        },
    }
}
```

(The original's `sp18`, `sp20`, `sp30` are uninitialised locals that `func_800780A8` and `_computeStepAndDisplacement` fill; zeroing them changes nothing visible: the shadow's double oracle shows if a byte of them escapes.)

- [ ] **Step 3: Route it**

`root.zig`: `const zoom = @import("action/zoom.zig");` and `pub const _handleCombatCameraZoom = zoom.zoom;`.

- [ ] **Step 4: Build and diff (both architectures)**

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call,tick
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --mode call,tick
```

Expected: all equivalence lines `ok:`; `zoom_0`, `zoom_converged` (and `zoom_1`/`zoom_2` if a corpus action uses those styles) now hit.

- [ ] **Step 5: Commit**

```bash
git add game
git commit -m "Owned battle actions: the camera zoom (_handleCombatCameraZoom)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 24: Port the cast start (`func_8007138C`), its target list unit-tested first

**Files:**
- Create: `game/battle/action/tests.zig`
- Modify: `game/battle/action/cast.zig`, `pc/CMakeLists.txt`, `game/battle/root.zig`

- [ ] **Step 1: The failing unit tests (the target list, with the original's two bugs)**

`game/battle/action/tests.zig`:

```zig
//! Unit tests of the battle-action module's pure parts (zig test, against
//! the views generated by the last battle build; pc/CMakeLists.txt).
const std = @import("std");
const v = @import("views");
const cast = @import("cast.zig");
const skip = @import("skip.zig");

const Fake = struct {
    present: [16]bool = .{false} ** 16,
    aim: [16]c_int = .{0} ** 16,
    pub fn part(_: *const Fake, actor: c_int, limb: c_int) i8 {
        return @intCast(@mod(actor * 10 + limb, 128));
    }
    pub fn aimLimb(self: *const Fake, actor: c_int) c_int {
        return self.aim[@intCast(actor)];
    }
    pub fn bystander(self: *const Fake, i: usize) bool {
        return self.present[i];
    }
};

fn hitOn(a: *v.D_800F19CC_t2, i: usize, actor: u8, limb: i8) void {
    a.unk4C[i].unk40 = 0;
    a.unk4C[i].unk0.targetActor = actor;
    a.unk4C[i].unk0.targetLimb = limb;
}

test "two hits on one actor make one target with part 0" {
    var a = std.mem.zeroes(v.D_800F19CC_t2);
    var r = std.mem.zeroes(v.actionEffectRequest);
    a.unk4A = 2;
    hitOn(&a, 0, 2, 1);
    hitOn(&a, 1, 2, 3);
    const env = Fake{};
    cast.buildTargets(&env, &a, &r);
    try std.testing.expectEqual(@as(i8, 1), r.count);
    try std.testing.expectEqual(@as(u8, 2), r.targets[0].value.actor.actor);
    try std.testing.expectEqual(@as(i8, 0), r.targets[0].value.actor.part);
    try std.testing.expectEqual(@as(u16, 4), r.targets[0].type);
}

test "a position hit becomes a type 5 target with its position" {
    var a = std.mem.zeroes(v.D_800F19CC_t2);
    var r = std.mem.zeroes(v.actionEffectRequest);
    a.unk4A = 1;
    a.unk4C[0].unk40 = 4;
    const p: *v.SVECTOR = @ptrCast(&a.unk4C[0].unk0);
    p.vx = 100;
    p.vz = -7;
    const env = Fake{};
    cast.buildTargets(&env, &a, &r);
    try std.testing.expectEqual(@as(u16, 5), r.targets[0].type);
    try std.testing.expectEqual(@as(i16, 100), r.targets[0].value.position.vx);
    try std.testing.expectEqual(@as(i16, -7), r.targets[0].value.position.vz);
}

test "original bug 1: the aim-limb test reads the hit at the output index" {
    var a = std.mem.zeroes(v.D_800F19CC_t2);
    var r = std.mem.zeroes(v.actionEffectRequest);
    a.unk4A = 3;
    hitOn(&a, 0, 2, 3);
    hitOn(&a, 1, 2, -1); // a duplicate of actor 2: skipped, but its limb is what entry 2 tests
    hitOn(&a, 2, 5, 4);
    var env = Fake{};
    env.aim[5] = 6;
    cast.buildTargets(&env, &a, &r);
    try std.testing.expectEqual(@as(i8, 2), r.count);
    try std.testing.expectEqual(@as(u8, 5), r.targets[1].value.actor.actor);
    try std.testing.expectEqual(env.part(5, 6), r.targets[1].value.actor.part); // not part(5, 4)
}

test "original bug 2: bystanders are tested against the hit list at the actor's index" {
    var a = std.mem.zeroes(v.D_800F19CC_t2);
    var r = std.mem.zeroes(v.actionEffectRequest);
    a.unk4A = 2;
    hitOn(&a, 0, 2, 0); // the target: actor 2
    a.unk4C[1].unk40 = 4; // a position hit whose actor byte reads 1
    a.unk4C[1].unk0.targetActor = 1;
    var env = Fake{};
    for (0..4) |i| env.present[i] = true;
    cast.buildTargets(&env, &a, &r);
    // caster 0 is excluded; actor 1 is wrongly excluded; actor 2 (the target) wrongly included.
    try std.testing.expectEqual(@as(u8, 2), r.otherCount);
    try std.testing.expectEqual(@as(u8, 2), r.others[0]);
    try std.testing.expectEqual(@as(u8, 3), r.others[1]);
}
```

(The field names are the decomp's as the views generate them; if a name differs, the compile error names it: follow the views.)

`pc/CMakeLists.txt` (inside the Zig block):

```cmake
    # The battle-action module's pure parts against the last 32-bit battle build's generated views.
    set(ROOD_BATTLE_ZIG ${CMAKE_CURRENT_SOURCE_DIR}/build/boot/zig)
    add_test(NAME zig_battle_action COMMAND ${ROOD_ZIG} test -target x86-linux-musl -O ReleaseSafe
        --dep common --dep views --dep c -Mroot=${CMAKE_CURRENT_SOURCE_DIR}/../game/battle/action/tests.zig
        -Mcommon=${CMAKE_CURRENT_SOURCE_DIR}/../game/common/common.zig
        --dep common -Mviews=${ROOD_BATTLE_ZIG}/battle_views.zig
        -Mc=${ROOD_BATTLE_ZIG}/battle_c.zig)
```

Run:

```bash
cmake -S pc -B pc/build -G Ninja && flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_battle_action --output-on-failure
```

Expected: FAIL, `cast.zig` has no `buildTargets`.

- [ ] **Step 2: `cast.zig`**

```zig
//! The cast's start (146C.c func_8007138C): the caster's animation and turn
//! toward the aim, the cast sound, the event flags, the effect request
//! (buildTargets: both of the original's bugs kept), a Break Art's weapon
//! data, the HUD out, the name window; phase 5.
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");
const release = @import("release.zig");

/// The effect's targets and the bystanders, as the original computes them.
/// `env` gives the actors (the game's, or a test's):
///   part(actor, limb) i8        func_800A152C(actor, limb, 2)
///   aimLimb(actor) c_int        vs_battle_actors[actor]->unk3C->unk36
///   bystander(i) bool           vs_battle_actors[i] && unk40 is 1 or 2
pub fn buildTargets(env: anytype, a: *const v.D_800F19CC_t2, request: *v.actionEffectRequest) void {
    var count: usize = 0;
    var i: usize = 0;
    while (i < a.unk4A) : (i += 1) {
        const hit = &a.unk4C[i];
        if (hit.unk40 != 0) {
            request.targets[count].type = 5;
            request.targets[count].value.position = @as(*const v.SVECTOR, @ptrCast(&hit.unk0)).*;
            count += 1;
            continue;
        }
        var duplicate = false;
        var j: usize = 0;
        while (j < count) : (j += 1) {
            if (a.unk4C[j].unk40 == 0 and request.targets[j].value.actor.actor == hit.unk0.targetActor) {
                request.targets[j].value.actor.part = 0;
                duplicate = true;
            }
        }
        if (duplicate) continue;
        const target = &request.targets[count];
        target.type = 4;
        target.value.actor.actor = hit.unk0.targetActor;
        target.value.actor.reserved = 0;
        target.value.actor.part = env.part(hit.unk0.targetActor, hit.unk0.targetLimb);
        // BUG (original): tests the limb of the entry at the output index.
        if (a.unk4C[count].unk0.targetLimb == -1) {
            target.value.actor.part = env.part(hit.unk0.targetActor, env.aimLimb(hit.unk0.targetActor));
        } else {
            target.value.actor.part = env.part(hit.unk0.targetActor, hit.unk0.targetLimb);
        }
        count += 1;
    }
    request.count = @intCast(count);
    var k: usize = 0;
    while (k < 16) : (k += 1) {
        if (!env.bystander(k)) continue;
        var duplicate = false;
        var j: usize = 0;
        while (j < a.unk4A) : (j += 1) {
            // BUG (original): indexes the hit list with the actor index.
            if (a.unk4C[j].unk40 == 0 and k == a.unk4C[k].unk0.targetActor) {
                duplicate = true;
                break;
            }
        }
        if (k == t.caster(a)) duplicate = true;
        if (!duplicate) {
            request.others[request.otherCount] = @intCast(k);
            request.otherCount += 1;
        }
    }
}

const Game = struct {
    pub fn part(_: *const Game, actor: c_int, limb: c_int) i8 {
        return psx.conv(i8, c.func_800A152C(actor, limb, 2));
    }
    pub fn aimLimb(_: *const Game, actor: c_int) c_int {
        const x = g.vs_battle_actors()[@intCast(actor)].?;
        return @as(c_int, psx.get(x.unk3C).?.unk36);
    }
    pub fn bystander(_: *const Game, i: usize) bool {
        const x = g.vs_battle_actors()[i] orelse return false;
        return x.unk40 == 1 or x.unk40 == 2;
    }
};

/// func_8007138C.
pub fn start() callconv(.c) void {
    const b = t.block();
    b.battleAbilityInputSuccessful = 0;
    const character = g.vs_battle_characterState().*.?;
    b.battleAbilityInputAttempted = if ((psx.get(character.unk3C).?.statuses & 0x8000) != 0) 1 else 0;
    const a = t.current(b);
    if (!t.hasEffect(a.actionIndex)) {
        cov.hit(.cast_no_effect);
        return release.release();
    }
    const who: c_int = t.caster(a);
    var position = std.mem.zeroes(v.func_8006EBF8_t);
    _ = c.func_800A1108(who, &position);
    var sound = std.mem.zeroes(v.SVECTOR);
    sound.vx = position.unk0.unk4.vx;
    sound.vz = position.unk0.unk4.vz;
    sound.vy = position.unk0.unk4.vy;
    const aim = psx.cptr(c.SVECTOR, &b.unk8.unk844);
    const at = psx.cptr(c.SVECTOR, &sound);
    var mode: c_int = 1;
    switch (t.kind(a.actionIndex)) {
        .spell => {
            cov.hit(.cast_spell);
            c.func_8009E2E0(who, aim, 0);
            c.vs_main_panSfx(0x7E, 0x34, at);
            if (who == 0) c.vs_battle_setStateFlag(0xB7, 1);
            mode = 2;
        },
        .art => {
            cov.hit(.cast_art);
            c.func_8009F298(who, aim, 0);
            c.vs_main_panSfx(0x7E, 0x36, at);
            mode = 3;
        },
        .b8 => {
            cov.hit(.cast_b8);
            c.func_8009EFEC(who, aim, 0);
            c.vs_main_panSfx(0x7E, 0x34, at);
            if (who == 0) c.vs_battle_setStateFlag(0xB9, 1);
            mode = 3;
        },
        .item => {
            cov.hit(.cast_item);
            c.func_8009E2E0(who, aim, 0);
            c.vs_main_panSfx(0x7E, 0x35, at);
            mode = 1;
        },
    }
    var request = std.mem.zeroes(v.actionEffectRequest);
    request.effect = g.vs_main_actions()[a.actionIndex].unk1;
    request.otherCount = 0;
    request.source.type = 4;
    request.source.value.actor.actor = t.caster(a);
    request.source.value.actor.reserved = 0;
    request.source.value.actor.part = psx.conv(i8, c.func_800A152C(who, 255, 2));
    const game = Game{};
    buildTargets(&game, a, &request);
    request.position.vx = a.unk844.vx;
    request.position.vy = a.unk844.vy;
    request.position.vz = a.unk844.vz;
    if (request.effect == 0x27 and a.unk4C[0].unk40 == 4) request.effect = 0x24;
    _ = c.func_800CF0E8(@ptrCast(&request), mode, a.unk2);
    if (a.actionIndex >= 0x8D and a.actionIndex < 0xE0) breakArtObject(a);
    c.func_800CB654(1);
    if (g.vs_main_settings().information != 0) {
        cov.hit(.cast_information);
        c.vs_battle_displaySceneMessage(0xB, a.actionIndex, 1);
    }
    t.setPhase(.cast);
}

/// A Break Art's (or 0xB8..0xDF action's) weapon or shield model for the effect.
fn breakArtObject(a: *const v.D_800F19CC_t2) void {
    cov.hit(.cast_break_art_object);
    const who = t.caster(a);
    const actor = g.vs_battle_actors()[who].?;
    var object = std.mem.zeroes(v.vs_battle_objectData);
    object.dataType = 7;
    object.index = who;
    object.modelId = 0;
    const slot: [*]const u16 = @ptrCast(&actor.unkC); // ((u_short*)&actor->unkC)[0], [1]
    const armor = &psx.get(actor.unk3C).?.armor[slot[0]][slot[1]];
    if (a.actionIndex < 0xB8 or object.index != 0) {
        object.actorId = 5;
        object.modelId = psx.conv(@TypeOf(object.modelId), psx.getBits(armor, "unk2_4"));
    } else {
        object.actorId = 4;
        object.modelId = psx.conv(@TypeOf(object.modelId), (a.actionIndex - 0xB8) % 4);
    }
    object.material = actor.equippedWeaponCategory;
    _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
}
```

(Where the views give `object.dataType` etc. as bitfields — `vs_battle_objectData` has bitfields, `vs_battle_objectData_flags` — use `psx.setBits(&object, "dataType", 7)` instead: the compile error says which. The original's `object` and `request` leave some fields uninitialised; the zeroes here are compared through the double oracle in call mode and the `D_800F53C0` mask in tick mode.)

- [ ] **Step 3: Unit tests pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_battle_action --output-on-failure
```

Expected: `All 4 tests passed.`

- [ ] **Step 4: Route it, diff it**

`root.zig`: `const cast = @import("action/cast.zig");` and `pub const func_8007138C = cast.start;`.

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call,tick
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --mode call,tick
```

Expected: all `ok:`; `cast_spell`, `cast_item` hit.

- [ ] **Step 5: Commit**

```bash
git add game pc/CMakeLists.txt
git commit -m "Owned battle actions: the cast start (func_8007138C); target list unit-tested with the original's bugs

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 25: Port the commit (`func_80070F28`) and the re-cast (`func_80079030`)

**Files:**
- Create: `game/battle/action/commit.zig`
- Modify: `game/battle/root.zig`

- [ ] **Step 1: `commit.zig`**

```zig
//! The commit (146C.c func_80070F28): everything the action decides is
//! decided here (costs paid, the item used, hits/damage/statuses rolled by
//! the original helpers, the score counted); then phase 4. The wind-up that
//! follows decides nothing (which is why it can be skipped).
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");
const cast = @import("cast.zig");

/// func_80070F28(arg0): 0, 1 or 2 pick the hit-rolling helper; with arg0 set
/// and the confirmation 0xFF (cancelled), the action is withdrawn.
pub fn commit(arg0: c_int) callconv(.c) void {
    c.func_8006EF10();
    const b = t.block();
    const confirmation = b.unk298C;
    c.func_800C16DC();
    c.func_800C06E0();
    c.func_800C0738();
    c.func_800C05B4();
    const action = &b.unk8;
    if (arg0 != 0 and confirmation == 0xFF) {
        if (t.caster(action) != 0) {
            cov.hit(.commit_cancel_enemy);
            c.func_800CB114();
            c.func_800E4C64(t.caster(action));
            action.actionIndex = 0;
            c.func_8006FBCC(0);
            return;
        }
        if (!t.hasEffect(action.actionIndex)) {
            cov.hit(.commit_cancel_plain);
            c.func_800CB114();
            action.actionIndex = 0;
            c.func_8006FBCC(0);
            return;
        }
        cov.hit(.commit_cancel_back);
        action.actionIndex = 0;
        c.func_800704D8();
        return;
    }
    b.unk4 = 1;
    b.unk0 = 0;
    if (t.caster(action) == 0) {
        // D_800F18EC is NULL until the ability menu first opens; the original
        // then reads and clears PS1 low RAM (the 0x80000000 mirror).
        const ability: *v.D_800F18EC_t = g.D_800F18EC().* orelse blk: {
            cov.hit(.commit_ability_null);
            break :blk @ptrFromInt(0x80000000);
        };
        if (ability.unk2 != 7 and ability.unk2 == 8) {
            cov.hit(.commit_misc_item);
            c.vs_battle_decreaseMiscCount(ability.executeAbility.u16[1]);
        }
        ability.unk2 = 0;
    } else {
        cov.hit(.commit_enemy);
        c.func_800D82A8(t.caster(action));
    }
    c.func_800CB114();
    switch (arg0) {
        0 => {
            cov.hit(.commit_arg_0);
            _ = c.func_80070CAC();
        },
        2 => {
            cov.hit(.commit_arg_2);
            _ = c.func_80070B04();
        },
        else => {
            cov.hit(.commit_arg_other);
            _ = c.func_800708EC();
        },
    }
    _ = c.func_80085B10(action.actionIndex, @ptrCast(&b.unk854), @ptrCast(action), 1);
    weaponAttackScore(b);
    const window_style = psx.getBits(&g.vs_main_actions()[action.actionIndex], "flagsD_4");
    b.unk2C0E = if (c.func_80077F70() >= 0x1C1 and window_style == 1) 0 else window_style;
    b.unk2C02 = 0;
    b.previousChainInput = 0;
    b.unk2C03 = 0;
    b.unk2C07 = 0;
    if (action.actionIndex >= 55) {
        g.D_8005FFAF().* = 0;
        switch (action.actionIndex) {
            0x4D, 0x60, 0xE0...0xE3, 0xE8...0xEB => {
                const score = g.vs_main_scoredata();
                if (score.healCount < 0x3E8) {
                    cov.hit(.commit_heal_score);
                    score.healCount += 1;
                }
            },
            else => {},
        }
    }
    t.setPhase(.zoom);
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        if (i != t.caster(action)) c.func_8009E5C4(i);
    }
    c.vs_main_setClutState(1);
    c.func_8007B1B8(8, 4, 0, 0, 0);
    c.func_8006C250();
    c.func_80095B70(1);
    c.func_8008B4C8(0);
    c.func_8008B4BC(0);
}

/// Ashley's weapon attacks per weapon category (the score screen).
fn weaponAttackScore(b: *v.D_800F19CC_t) void {
    const action = &b.unk8;
    const index = action.actionIndex;
    if (t.caster(action) != 0) return;
    if (!(index == 1 or (@as(u32, index) -% 0xB8) < 0x28)) return;
    if (action.unk4C[0].unk40 != 0) return;
    const target = g.vs_battle_actors()[action.unk4C[0].unk0.targetActor].?;
    if (target.unk27 == 0x80) return;
    cov.hit(.commit_weapon_score);
    const category = g.vs_battle_characterState().*.?.equippedWeaponCategory;
    const attacks = &g.vs_main_scoredata().weaponAttacks;
    const slot: usize = if (category == 0xA) 0 else category;
    if (attacks[slot] <= 0xFFFE) attacks[slot] += 1;
}

/// func_80079030 (phase 8): cast again.
pub fn recast() callconv(.c) void {
    cov.hit(.recast);
    cast.start();
}
```

(`_ = c.f()` also accepts a `void` result; the `c` prototypes decide. The original assigns `unk2C03 = previousChainInput = 0`; both zero.)

- [ ] **Step 2: Route, build, diff**

`root.zig`: `const commit = @import("action/commit.zig");`, `pub const func_80070F28 = commit.commit;`, `pub const func_80079030 = commit.recast;`.

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call,tick
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --mode call,tick
```

Expected: all `ok:`; `commit_*` branches hit by the spell, item, attack and cancel scenarios. The modern menus' item use (`items_bridge.c` calls `func_80070F28`) goes through the trampoline too: the `item` scenario covers it.

- [ ] **Step 3: Commit**

```bash
git add game
git commit -m "Owned battle actions: the commit (func_80070F28) and the re-cast (func_80079030)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 26: Port the wind-down (`func_80078AB4`, the chain/defense timing window)

(Skip this task, and drop the function from `game/ownership.toml`, if the user chose "stop at release" — design doc question 4.)

**Files:**
- Create: `game/battle/action/winddown.zig`
- Modify: `game/battle/root.zig`

- [ ] **Step 1: `winddown.zig`**

```zig
//! Phase 7 (146C.c func_80078AB4): the chain/defense ability timing window
//! after the action's hit, then back to the field (func_8006FD1C) once the
//! actor's animation allows.
const common = @import("common");
const psx = common.psx;
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");

const PADRup: u32 = 1 << 4;
const PADRright: u32 = 1 << 5;
const PADRleft: u32 = 1 << 7;
const ABILITY_BUTTONS = PADRup | PADRright | PADRleft;

pub fn windDown() callconv(.c) void {
    const b = t.block();
    const a = t.current(b);
    const who: c_int = t.caster(a);
    const settings = g.vs_main_settings();
    const tick = g.vs_gametime_tickspeed().*;
    var margin: c_int = if (c.vs_battle_getStateFlag(1) != 0) 8 else 10;
    if (who != 0 and a.actionIndex >= 54) margin += 4;
    margin = margin -% psx.conv(c_int, b.unk0 / 3);
    if (margin < 2) margin = 2;
    const window: c_int = b.battleAbilityTargetWindow;
    const timer: c_int = b.battleAbilityTimer;
    if (window != 0 and timer == window - tick * 2 and c.func_80078828(0) != 0) {
        cov.hit(.winddown_window);
        c.func_80096FF0(@divTrunc(margin * 2, tick));
    }
    b.battleAbilityTimer = psx.conv(u16, @as(c_int, b.battleAbilityTimer) +% tick);
    const pressed: u32 = g.vs_main_buttonsPressed().all;
    if ((pressed & ABILITY_BUTTONS) != 0) {
        cov.hit(.winddown_input);
        const button: c_int = if ((pressed & PADRleft) != 0) 3 else if ((pressed & PADRup) != 0) 2 else 1;
        const chain = psx.at(&settings.mappedChainAbilities, button - 1).*;
        const defense = psx.at(&settings.mappedDefenseAbilities, button - 1).*;
        const mapped = a.unk4.unk40 == 0 and (if (who == 0) chain != 0 else defense != 0);
        if (mapped) {
            if (c.func_80078828(button) != 0) {
                if (window == 0) {
                    c.vs_battle_renderBattleAbilityTimingResult(1);
                    b.battleAbilityInputAttempted = 1;
                } else if (a.unk4.unk40 == 0) {
                    timing(b, a, who, button, window, margin, timer, chain, defense);
                    if (b.battleAbilityInputSuccessful == 0) g.D_8005FFB0().* = 0;
                } else {
                    cov.hit(.winddown_abort);
                    if ((g.D_800F1868().* & 3) == 0) c.func_8006FD1C();
                    return;
                }
            } else if (b.battleAbilityInputAttempted == 0) {
                b.battleAbilityInputSuccessful = 0;
                b.battleAbilityInputAttempted = 1;
            }
        }
    }
    if (a.unk4.unk40 == 0) {
        if (c.func_8009E4B0(who) != 0) return;
        if (b.battleAbilityInputAttempted != 0 or b.unk2C03 == 0) c.func_8009E5C4(who);
        if (a.unk4C[0].unk40 == 0) {
            const target = g.vs_battle_actors()[a.unk4C[0].unk0.targetActor].?;
            if (psx.get(target.unk3C).?.currentHP == 0) c.func_8009E5C4(who);
        }
    }
    if ((g.D_800F1868().* & 3) == 0) c.func_8006FD1C();
}

/// The press's timing against the window: too early, early, in the window
/// (a chain or a defense ability is taken once per window), too late.
fn timing(b: *v.D_800F19CC_t, a: *v.D_800F19CC_t2, who: c_int, button: c_int, window: c_int, margin: c_int,
          timer: c_int, chain: u8, defense: u8) void {
    if (window - margin >= timer) {
        cov.hit(.winddown_early);
        c.vs_battle_renderBattleAbilityTimingResult(0);
        b.battleAbilityInputSuccessful = 0;
        b.battleAbilityInputAttempted = 1;
    } else if (window >= timer) {
        c.vs_battle_renderBattleAbilityTimingResult(1);
        b.battleAbilityInputSuccessful = 0;
        b.battleAbilityInputAttempted = 1;
    } else if (window + margin >= timer) {
        c.vs_battle_renderBattleAbilityTimingResult(2);
        if (b.battleAbilityInputAttempted != 0) return;
        const fresh = (~@as(c_int, b.unk2C02)) & @as(c_int, ABILITY_BUTTONS);
        if ((g.vs_main_buttonsPressed().all & psx.conv(u32, fresh)) == 0) return;
        if (who == 0) {
            if (b.previousChainInput != button and chain != 0) {
                cov.hit(.winddown_chain);
                b.previousChainInput = psx.conv(@TypeOf(b.previousChainInput), button);
                b.unk2C03 = psx.conv(@TypeOf(b.unk2C03), button);
                b.battleAbilityInputSuccessful = 1;
            } else {
                b.battleAbilityInputSuccessful = 0;
            }
            b.battleAbilityInputAttempted = 1;
            return;
        }
        var i: usize = 0;
        while (i < a.unk4A) : (i += 1) {
            // (The original tests entry 0 on every pass.)
            if (a.unk4C[0].unk0.targetActor == 0 and a.unk4C[0].unk40 == 0) {
                if (b.previousChainInput != button and defense != 0) {
                    cov.hit(.winddown_defense);
                    b.previousChainInput = psx.conv(@TypeOf(b.previousChainInput), button);
                    b.unk2C03 = psx.conv(@TypeOf(b.unk2C03), button);
                    b.battleAbilityInputSuccessful = 2;
                    b.battleAbilityInputAttempted = 1;
                    const score = g.vs_main_scoredata();
                    if (score.chainStreak <= 0xFFFE) score.chainStreak += 1;
                } else {
                    b.battleAbilityInputSuccessful = 0;
                    b.battleAbilityInputAttempted = 1;
                }
                break;
            }
        }
    } else {
        cov.hit(.winddown_late);
        c.vs_battle_renderBattleAbilityTimingResult(if (window + margin * 2 >= timer) 3 else 4);
        b.battleAbilityInputSuccessful = 0;
        b.battleAbilityInputAttempted = 1;
    }
}
```

(Check against `146C.c` lines 7108–7314 while porting: the original's `button` for "Rright only" is 1; `isMapped` reads `mappedDefenseAbilities` only for an enemy caster. The order of evaluation of `chain`/`defense` here reads both arrays up front: both are plain reads of `vs_main_settings`, no side effects.)

- [ ] **Step 2: Route, build, diff**

`root.zig`: `const winddown = @import("action/winddown.zig");`, `pub const func_80078AB4 = winddown.windDown;`. Remove the now-empty `BattleAction` pass-through lines.

```bash
for args in "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --mode call,tick
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --mode call,tick
```

Expected: all `ok:`; `winddown_window`, `winddown_input` and (with the `chain` scenario) `winddown_chain` or `winddown_early`/`winddown_late` hit.

- [ ] **Step 3: Commit**

```bash
git add game
git commit -m "Owned battle actions: the wind-down (func_80078AB4, chain/defense timing window)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 27: The whole module proven: coverage green, every mode, both architectures

**Files:**
- Modify: `pc/tests/diff/scenarios.py` (only if a required branch is not reached)

- [ ] **Step 1: Every mode, every scenario, both architectures**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 diff_battle_action ownership_smoke skip_charging
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 diff_battle_action ownership_smoke skip_charging
```

Expected: `diff_battle_action` passes on both: call, tick and **alternate** modes `ok:` for every scenario, and `ok:   coverage: every required owned branch ran`. The "not reached by the corpus" list is reviewed: each entry either gets a scenario (enemy cast with `qol_skip_charging_who`, a Break Art, chain) or a line in the task notes saying why it is unreachable from the card's save.

- [ ] **Step 2: Run the game owned for a while (soak)**

```bash
flock "$ROOD_HEAVY_LOCK" env ROOD_OWNER=battle_action=owned python3 pc/tests/run.py -j 2 soak enemy_soak
```

Expected: the soaks pass, no `[zig] panic` in their logs.

- [ ] **Step 3: Commit (corpus changes only, if any)**

```bash
git add pc/tests/diff
git commit -m "Battle corpus: reaches every required owned branch on 32- and 64-bit

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 28: The true charging skip, natively — proven against its hook

**Files:**
- Modify: `game/battle/action/skip.zig`, `pc/src/charge_skip.inc`, `pc/src/charge_skip.c`, `game/battle/action/tests.zig`

- [ ] **Step 1: The failing unit tests (the policy)**

Append to `game/battle/action/tests.zig`:

```zig
test "skip policy: off, Ashley only by default, everyone when asked, kinds, events" {
    const on = skip.Policy{ .mode = 1, .who = 0, .kinds = 15, .event_running = false };
    try std.testing.expect(skip.applies(on, 97, true, 0)); // Solid Shock, Ashley
    try std.testing.expect(!skip.applies(on, 97, false, 0)); // no effect, no wind-up
    try std.testing.expect(!skip.applies(on, 97, true, 3)); // an enemy's
    try std.testing.expect(skip.applies(.{ .mode = 1, .who = 1, .kinds = 15, .event_running = false }, 97, true, 3));
    try std.testing.expect(!skip.applies(.{ .mode = 0, .who = 1, .kinds = 15, .event_running = false }, 97, true, 0));
    try std.testing.expect(!skip.applies(.{ .mode = 1, .who = 0, .kinds = 15 & ~@as(c_int, 8), .event_running = false },
        0xE0, true, 0)); // items masked out
    try std.testing.expect(!skip.applies(.{ .mode = 1, .who = 0, .kinds = 15, .event_running = true }, 97, true, 0));
    try std.testing.expectEqual(@as(c_int, 2), skip.kindBit(0x8D));
    try std.testing.expectEqual(@as(c_int, 4), skip.kindBit(0xB8));
}
```

Run `ctest -R zig_battle_action`: FAIL (`skip.Policy` missing).

- [ ] **Step 2: `skip.zig`**

```zig
//! The true charging skip (settings qol_skip_charging, qol_skip_charging_who;
//! pc/docs/Ownership.md §8), natively. An action with an effect is decided at
//! commit; its wind-up (phases 4-6) decides nothing. With the skip, phase 4
//! runs the camera's per-vsync steps until it converges, in one vsync, with
//! no name window, then stops the cast sound and applies the caster's turn at
//! once; phase 5 releases as soon as the effect's files are in and the
//! object data is placed. What is presented (hidden vsyncs, the temporal
//! filters' restart) stays host-side: pc/src/charge_skip.c.
const common = @import("common");
const psx = common.psx;
const settings = common.settings;
const ownership = @import("ownership");
const v = @import("views");
const c = @import("c");
const g = v.g;
const t = @import("types.zig");
const cov = @import("coverage.zig");
const zoom_ = @import("zoom.zig");
const release = @import("release.zig");

pub const Policy = struct { mode: c_int, who: c_int, kinds: c_int, event_running: bool };

/// RPC skip_charging kinds: 1 spells, 2 Break Arts, 4 actions 0xB8..0xDF, 8 items.
pub fn kindBit(index: u16) c_int {
    return switch (t.kind(index)) {
        .spell => 1,
        .art => 2,
        .b8 => 4,
        .item => 8,
    };
}

/// Whether the skip applies to an action (pure).
pub fn applies(p: Policy, index: u16, has_effect: bool, caster: u8) bool {
    return has_effect and p.mode != 0 and (caster == 0 or p.who != 0) and
        (p.kinds & kindBit(index)) != 0 and !p.event_running;
}

fn policy() Policy {
    return .{
        .mode = settings.skipCharging(),
        .who = settings.skipChargingWho(),
        .kinds = settings.skipKinds(),
        .event_running = g.D_800F4C34().* != 0,
    };
}

/// This vsync's wind-up (an action with an effect in phases 4..6), if any.
pub const Wind = struct { phase: c_int, actor: c_int, action: c_int, skipping: bool };
pub fn state() ?Wind {
    if (g.D_800F196C().* != 2) return null;
    const b = g.D_800F19CC().* orelse return null;
    const phase = g._cameraMode().*;
    if (phase < 4 or phase > 6) return null;
    const a = t.current(b);
    if (a.actionIndex >= 256 or !t.hasEffect(a.actionIndex)) return null;
    const who = t.caster(a);
    return .{ .phase = @intCast(phase), .actor = who, .action = a.actionIndex,
              .skipping = applies(policy(), a.actionIndex, true, who) };
}

pub fn now() bool {
    const w = state() orelse return false;
    return w.skipping;
}

extern fn pc_charge_skip_note_iterations(n: c_int) callconv(.c) void; // charge_skip.c (diagnostics)

/// The camera's view for what this vsync draws, as the frame loop builds it.
fn view() void {
    const cam = g.vs_battle_cameraCurrentSpherical();
    c._setCameraPositionFromAngles(psx.cptr(c._sphericalValues, &cam.values));
    c.func_80069D78();
    c.func_8007ACB0();
    const m = psx.cptr(c.MATRIX, &g.vs_scratch().viewMatrix);
    c.SetRotMatrix(m);
    c.SetTransMatrix(m);
}

/// The caster's turn toward the aim (func_8009E2E0) at once: what the actor
/// update (44F14.c) would add over its remaining vsyncs.
fn turnAtOnce() void {
    const who = t.caster(t.current(t.block()));
    const actor = g.D_800F4538()[who] orelse return;
    const u = &actor.unk0;
    if (u.unk18 == 0) return;
    u.unk24 = psx.conv(@TypeOf(u.unk24), (@as(c_int, u.unk24) + @as(c_int, u.unk3C)) & 0xFFF);
    u.facing = psx.conv(@TypeOf(u.facing), (@as(c_int, u.facing) + @as(c_int, u.unk3E)) & 0xFFF);
    u.unk28 = psx.conv(@TypeOf(u.unk28), (@as(c_int, u.unk28) + @as(c_int, u.unk40)) & 0xFFF);
    u.unk3C = 0;
    u.unk3E = 0;
    u.unk40 = 0;
    u.unk18 = 0;
}

/// Phase 4, skipped.
pub fn zoom() void {
    cov.hit(.skip_zoom);
    const s = g.vs_main_settings();
    const information = s.information;
    s.information = 0; // func_8007138C shows the name window only with Information on
    zoom_.step();
    var i: c_int = 0;
    while (i < 1024 and t.phase() == .zoom) : (i += 1) { // (a safety bound, not a tunable)
        c.func_8006E158();
        view();
        zoom_.step();
    }
    s.information = information;
    pc_charge_skip_note_iterations(i + 1);
    if (t.phase() != .cast) return; // not converged: the next vsync goes on
    c.func_80045D64(0x7E, 0x34); // no cast sound (func_80070278 stops the same three at the release)
    c.func_80045D64(0x7E, 0x35);
    c.func_80045D64(0x7E, 0x36);
    turnAtOnce();
    view();
}

/// Phase 5, skipped: the effect's files and the object data are all the release needs.
pub fn waitCast() void {
    cov.hit(.skip_cast);
    if (c.vs_battle_getEmptyObjectDataSlot() == 0 and c.func_800CEEBC() != 0) release.castReady();
}
```

`charge_skip.c` (host) gains the diagnostics setter (the variable moves here from the `.inc` in Task 29; for now define the setter only, writing a static):

```c
static int skip_iterations;
void pc_charge_skip_note_iterations(int n) { skip_iterations = n; }
```

- [ ] **Step 3: The hook acts only for the original owner (transition)**

`pc/src/charge_skip.inc`, at the top of `pc_charge_skip(int phase)`:

```c
    /* The owned battle-action module skips natively (game/battle/action/skip.zig);
     * this hook only serves the original until it is removed. */
    extern int pc_owner_get(int), pc_owner_find(const char*);
    static int slot = -2;
    if (slot == -2)
        slot = pc_owner_find("battle_action");
    if (slot >= 0 && pc_owner_get(slot) != 0)
        return 0;
```

- [ ] **Step 4: Unit tests, build, and the skip proven against its hook**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R zig_battle_action --output-on-failure
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action --skip-on --mode tick,alternate --scenario spell --scenario item
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 | tail -1
flock "$ROOD_HEAVY_LOCK" env ROOD_ARCH=64 python3 pc/tests/diff/differ.py battle_action --skip-on --mode tick,alternate --scenario spell --scenario item
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/diff/differ.py battle_action
```

Expected: unit tests pass; with the skip on, original+hook == owned native every vsync (tick) and with owners alternating, on both architectures; `skip_zoom`, `skip_cast` hit; and the skip-off run still all `ok:` (the setting's original value reproduces the oracle).

- [ ] **Step 5: Commit**

```bash
git add game pc/src/charge_skip.inc pc/src/charge_skip.c
git commit -m "Owned battle actions: the true charging skip, natively; equal to its hook every vsync

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 29: Remove the hooks from `src/`; owned by default

**Files:**
- Modify: `src/BATTLE/BATTLE.PRG/146C.c` (hook lines removed), `game/battle/action/skip.zig` (exports), `pc/src/charge_skip.c`, `game/ownership.toml`, `pc/src/ui/ui_settings.cpp`, `pc/src/dev_overlay.cpp`
- Delete: `pc/src/charge_skip.inc`

- [ ] **Step 1: The test aids move to the module (exports with the `.inc`'s C signatures)**

Append to `skip.zig`:

```zig
// ---- charge_skip.c's view of the game (the .inc's functions, same C signatures) ----

/// Whether this vsync is in an action's wind-up, and whether Skip applies
/// (only when battle actions are owned: the original has no skip).
export fn pc_charge_skip_state(phase: *c_int, skipping: *c_int, actor: *c_int, action: *c_int) c_int {
    const w = state() orelse return 0;
    phase.* = w.phase;
    actor.* = w.actor;
    action.* = w.action;
    const owned = settings.owner(@intFromEnum(ownership.Subsystem.battle_action)) != 0;
    skipping.* = @intFromBool(w.skipping and owned);
    return 1;
}

/// The view and the caster's facing (RPC skip_charging test aid): lookAt xyz,
/// position xyz, the camera's distance and yaw, the facing.
export fn pc_charge_skip_view(out: *[9]c_int, actor: c_int) void {
    const s = g.vs_scratch();
    const cam = g.vs_battle_cameraCurrentSpherical();
    out[0] = s.camera.lookAt.vx;
    out[1] = s.camera.lookAt.vy;
    out[2] = s.camera.lookAt.vz;
    out[3] = s.camera.position.vx;
    out[4] = s.camera.position.vy;
    out[5] = s.camera.position.vz;
    out[6] = cam.values.distance;
    out[7] = cam.values.yaw;
    const a = if (actor >= 0 and actor < 17) g.D_800F4538()[@intCast(actor)] else null;
    out[8] = if (a) |x| @as(c_int, x.unk0.facing) & 0xFFF else -1;
}

/// BATTLE's state (D_800F196C) and camera mode, for the random number pin.
export fn pc_charge_skip_battle_state(camera: *c_int) c_int {
    camera.* = @intCast(g._cameraMode().*);
    return if (g.D_800F19CC().* != null) @intCast(g.D_800F196C().*) else -1;
}

/// The random numbers (main.c vs_main_getRand) from `seed`, as _initRand seeds them at boot.
export fn pc_charge_skip_reseed(seed: c_uint) void {
    c.srand(seed);
    const table = g.randArr();
    for (table) |*entry| entry.* = c.rand();
    g.randIndex().* = 96;
}

/// Diagnostics (RPC skip_charging trace): this vsync's _cameraMode, the
/// effect loader's state, object slot 0 in use, D_800F1868, and the
/// background effects' ids and kinds.
export fn pc_charge_skip_trace(out: [*]u8, size: c_int) c_int {
    const buffer = out[0..@intCast(size)];
    var w = std.Io.Writer.fixed(buffer);
    w.print("{d}:{d}:{d}:{d}", .{ g._cameraMode().*, g.D_800F531C().*,
        @intFromBool(g.vs_battle_objectDataSlots()[0].dataType != 0), g.D_800F1868().* }) catch {};
    var e = g.D_800F54B0().*;
    while (e) |effect| : (e = psx.get(effect.next)) {
        w.print(",{d}/{d}", .{ effect.unkD1C.unk38, psx.getBits(effect, "unk14_8") }) catch break;
    }
    const n = w.end;
    if (n < buffer.len) buffer[n] = 0;
    return @intCast(n);
}
```

(`const std = @import("std");` at the top. `std.Io.Writer.fixed` is 0.15+'s fixed-buffer writer; if 0.16 spells it differently, use `std.fmt.bufPrint` in a loop. `dataType` and `unk14_8` may be bitfields in the views: then `psx.getBits`. `pc_charge_skip_view` field types: `@as(c_int, ...)` where the compiler asks.)

- [ ] **Step 2: Remove the hooks**

In `src/BATTLE/BATTLE.PRG/146C.c` remove exactly the charge-skip lines (the shape seen in Task 1; adapt if it differs): the prototype `int pc_charge_skip(int phase); /* pc/src/charge_skip.inc: battle charging skip */` (the `widescreen.h` include in that `#ifdef ROOD_PC` block stays), the two blocks

```c
#ifdef ROOD_PC
                                if (pc_charge_skip(4))
                                    break;
#endif
```

(and its `(5)` twin), and `#include "pc/src/charge_skip.inc"` (the `scene_submit.inc` include stays). Delete `pc/src/charge_skip.inc`. In `pc/src/charge_skip.c`, define the diagnostics variable the `.inc` held:

```c
int pc_charge_skip_iterations; /* phase 4's camera steps last time (RPC diagnostics; skip.zig) */
void pc_charge_skip_note_iterations(int n) { pc_charge_skip_iterations = n; }
```

(replacing Task 28's static).

```bash
grep -c 'charge_skip' src/BATTLE/BATTLE.PRG/146C.c
git diff main -- src/ | grep '^[-+]' | grep -v '^\(---\|+++\)'
```

Expected: `0`, and the `src/` diff against `main` is only the removed lines.

- [ ] **Step 3: Owned by default; the setting says why it is unavailable otherwise**

`game/ownership.toml`: `default = "owned"` for `battle_action`.

`pc/src/ui/ui_settings.cpp`: the "Battle charging" row (and "Whose actions") is greyed while battle actions are original, with the reason "Needs owned battle actions (developer overlay > Ownership)", using the row's existing greyed-with-reason mechanism (as other rows that depend on a mode). `pc/src/dev_overlay.cpp`: the "Battle charging" combo's tooltip gains the same sentence.

- [ ] **Step 4: Build all four targets, run the suites**

```bash
for args in "" "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 skip_charging diff_battle_action ownership_smoke ui_settings
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 skip_charging diff_battle_action
```

Expected: four builds with Task 1's trap counts (none of the removed lines were imports); `skip_charging_smoke` passes with the owned default (the skip is now the module's); `diff_battle_action` passes (call/tick/alternate with the skip off: still the oracle). The skip-on comparison against the hook is no longer possible (the hook is gone): it was proven in Task 28; the skip's outcome checks stay in `skip_charging_smoke`.

- [ ] **Step 5: Commit check**

```bash
flock "$ROOD_HEAVY_LOCK" docker run --rm --cpus=2 -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work rood-reverse-dev:local make commit-check 2>&1 | tail -2
```

Expected: `All files match`. (Restore `build/` afterwards if `make clean` touched it.)

- [ ] **Step 6: Commit**

```bash
git add -A src/BATTLE/BATTLE.PRG/146C.c pc/src/charge_skip.c pc/src/charge_skip.inc game pc/src/ui/ui_settings.cpp pc/src/dev_overlay.cpp
git commit -m "Battle actions owned by default; the charge-skip hooks leave src/ (146C.c back to upstream there)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 30: Verify everything, document, hand off

**Files:**
- Create: `pc/docs/Ownership.md`
- Modify: `pc/README.md`, `THIRD_PARTY.md`, `pc/tools/windows_dist.py`

- [ ] **Step 1: `pc/docs/Ownership.md`**

From the design doc, for readers of the repository (present tense, no plan language): what owned code is and the rule about intentional changes; the layout (`src/`, `game/`, `pc/`, `pc/plugins`); the toolchain (pin, `zig_toolchain.py`, `ROOD_ZIG`, where the caches live, the flags per target and why); the table (`game/ownership.toml`, keyed by address); routing (the definition rename, the trampolines, `pc_owner[]`, `ROOD_OWNER`, RPC `owner`, the overlay's Ownership section); typed access (`c`, `views`, `psx.zig` helpers, `psx.global` only, `-funsigned-char`, `-fwrapv` → wrapping operators, ReleaseSafe); the mapper; differential testing (shadow mode with the double oracle, tick and alternate modes, mark/rewind, digests, record/replay, masks with reasons, coverage, the corpus); how to own the next subsystem (the ten steps of design §10); the battle-action module and the native skip; limits (Linux-only differential, host side effects, one unit per overlay in M0).

- [ ] **Step 2: README, third-party notices, Windows distribution**

`pc/README.md`: a short "Owned code (game/, Zig)" section linking `pc/docs/Ownership.md`, the toolchain setup line, and the test table rows (`zig_toolchain`, `ownership`, `routing_probe`, `zig_views`, `subsystem_map`, `zig_unit_*`, `zig_battle_action`, `ownership_smoke`, `diff_battle_action`).

`THIRD_PARTY.md`: a Zig row: "Zig standard library (MIT): parts compiled into the owned objects (panic handling, memory helpers); the compiler itself is a build tool, not distributed."

`pc/tools/windows_dist.py`: copy the pinned toolchain's `LICENSE` into the folder's `licenses/zig-LICENSE.txt` (from `zig_toolchain.find().parent / "LICENSE"`).

- [ ] **Step 3: Every target, every suite**

```bash
for args in "" "--battle" "--arch 64" "--target windows"; do
  flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py $args 2>&1 | grep -E '^Zig:|^Built|error' ; done
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -j 2 --output-on-failure | tail -3
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64
```

Expected: four `Built` lines with Task 1's trap counts; CTest `100% tests passed`; both suite tables all passed (or the same known failures as Task 1's baseline, named in the notes).

- [ ] **Step 4: Commit check and the leak scan**

```bash
flock "$ROOD_HEAVY_LOCK" docker run --rm --cpus=2 -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work rood-reverse-dev:local make commit-check 2>&1 | tail -2
deny="$(sed -n "s/^    deny='\(.*\)'$/\1/p" pc/tools/publish.sh)|$(git config publish.deny)"   # the publish scan's own pattern
git diff main -- . ':!pc/tools/publish.sh' | grep -a '^+' | grep -ainE "$deny" || echo "leak scan clean"
git diff --numstat main | awk '$1=="-"{print $3}'
git status --short --ignored | grep -E '\.zig-cache|zig-out|\.snap|\.rec' | grep -v '^!!' || true
```

Expected: `All files match`; `leak scan clean`; no binary files added; no Zig caches, states or recordings tracked.

- [ ] **Step 5: Commit and hand off**

```bash
git add pc/docs/Ownership.md pc/README.md THIRD_PARTY.md pc/tools/windows_dist.py
git commit -m "Ownership: documentation, README, third-party notice, Windows licences

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Use superpowers:finishing-a-development-branch. Report: the four builds, the suites, the differential results per scenario and architecture, the coverage not reached, the commit check, and the user's open questions from the design doc that are still open. Do not push.

---

### Task 31 (optional): The translate-c experiment on one menu overlay

Judge whether mechanical translation is a good first step for low-risk, UI-heavy code. MENU1 (the Break Arts menu: one unit, six functions, one global entry `vs_menu1_exec`) is the candidate; a read-only pilot with Zig 0.16.0 translated it, demoted 3 of 6 functions to `extern` (bitfield member access) and did not compile as-is (block-scope `extern` declarations become unused local structs).

**Files:**
- Create: `pc/tools/zig_translate_unit.py`, `game/menu1/root.zig`
- Modify: `game/ownership.toml`, `pc/tools/build_boot.py`, `pc/tests/diff/scenarios.py`, `pc/docs/Ownership.md`

- [ ] **Step 1: Normalise and translate**

`pc/tools/zig_translate_unit.py UNIT.i OUT.zig --target T`: (1) drop linemarkers; (2) hoist block-scope `extern` declarations to file scope (delete the inner one when the file already declares the name); (3) turn the unit's file-scope `static` variables into `extern` declarations of `<overlay>_<name>` (the data stays in the C object, so both implementations share one state); (4) `zig translate-c`; (5) remove the static-assert blocks (as `zig_c.py`); (6) record per function: translated / demoted, and why. Print a table.

- [ ] **Step 2: Share the data**

In `build_boot.py`, for the experiment's unit: globalise its static variables in the C object and its twin (`objcopy --globalize-symbol` then `--redefine-sym name=menu1_name`, as main's statics are exposed), and keep its overlay statics sections as they are (restarted at load, compared by the differ).

- [ ] **Step 3: Route it and hand-fix the demoted functions**

`game/ownership.toml`: `[subsystem.menu1_translated]` with `overlay = "menu1"`, `unit = "src/MENU/MENU1.PRG/30.c"`, `functions = [{ addr = <vs_menu1_exec's address>, name = "vs_menu1_exec" }]`, views manifest from `subsystem_map.py menu1_translated`. `game/menu1/root.zig` imports the generated translation and exports `vs_menu1_exec` through `route.exportAll`; the demoted functions are written by hand against the views (bitfields through `psx.getBits`). Count the lines changed by hand.

- [ ] **Step 4: Prove it**

Add a scenario: field menu → Abilities → Break Arts, browse two categories and a few arts, back out (with the modern menus off for this scenario: `g.call("ui_menu", modern=False)`, so MENU1 runs). Run `differ.py menu1_translated --mode call,tick,alternate` on 32 and 64 bits.

- [ ] **Step 5: Report (in `pc/docs/Ownership.md`, "translate-c experiment")**

Functions translated / demoted / hand-fixed, lines generated vs hand-written, readability (casts, `@as`/`@bitCast` noise, `_ = &x;` lines, `[*c]` pointers), compile fixes needed per Zig version, differential result per mode and architecture, the agent time it took, and a recommendation: translate-then-clean for menus, or hand-port. Commit:

```bash
git add pc/tools/zig_translate_unit.py game pc/tools/build_boot.py pc/tests/diff/scenarios.py pc/docs/Ownership.md
git commit -m "translate-c experiment: MENU1 translated, routed and diffed; findings in Ownership.md

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Self-review

| Requirement (brief) | Where |
|---|---|
| Zig pinned, official tarball + SHA-256, cached outside the repo, gated download | Tasks 2–3 (`zig_toolchain.py`, `--from-tarball`, `--fetch` only with a yes) |
| build_boot invokes Zig for 32-bit, `--battle`, 64-bit, Windows (MinGW link) | Task 5 (`compile_zig`, targets table, COFF twin), verified in Tasks 5, 9, 13, 29, 30 |
| `@cImport` of the decomp headers: headers, include paths, defines, LP64, pitfalls | Design §4; Task 13 (`zig_c.py`: gcc's preprocessing with build_boot's flags, decls only; Windows `c_long`); Task 12 (views for what translate-c demotes) |
| Typed views over guest memory | Tasks 4 (`psx.zig`), 12 (DWARF views), 13 (manifest) |
| Ownership routing table, owner decides symbols, both compiled in, runtime switch | Tasks 6–10 (table, definition rename, trampolines, `pc_owner[]`, RPC/overlay/env) |
| Differential runner: save states, N ticks per owner, full state every tick, first tick/region/field, noise, recorded inputs, one instance with restore | Tasks 14–20 (digests, mark/rewind, record/replay, shadow with double oracle, masks, field map, corpus, runner) |
| Call-graph and state mapper | Task 11 |
| Battle-action module in Zig: state machine, typed access, native true skip, hooks removed, settings via registry | Tasks 21–29 |
| Process rules: commit-check, PS1 untouched, four targets, thermal, no game data, leak scan | Ground rules; Tasks 1, 29, 30 |
| translate-c experiment, last and optional | Task 31 |

Gaps knowingly left for M1: more than one original unit per overlay (one `c`/`views` module per overlay in M0); differential testing on Windows (no save states there); host side effects (renderer, audio output) not compared; Lua events from the owned module.
