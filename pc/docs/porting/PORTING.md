# Porting the decompiled C to Zig

Status: guide for phase D of `pc/docs/plans/2026-10-18-freedom-and-fast-iteration.md`
(the bulk port, battle first). Companions: `battle/STATE.tsv` (every global the
battle code touches), `battle/FUNCTIONS.tsv` (every battle function, its
neighbours and PS1 features), `REVIEW-NOTES.md` (what the adversarial reviews
of this guide found).

The port is **faithful first**. A ported function does exactly what the C
object of the same target does, byte for byte in game state, and the oracle
(shadow mode, tick mode) proves it. Restructuring and freeing the
data from the PS1 layout come later (phase E); readable names come now, with
the original symbol kept as the key (§16). If a rule here makes the Zig
uglier than it could be, follow the rule anyway; phase E is where it gets
pretty.

Contents:

1. What the oracle is, and what to read
2. The modules a port uses
3. Types
4. Integers: promotions, conversions, overflow, shifts, division
5. Pointers and the RAM image
6. Globals and statics
7. Structs, unions and bitfields
8. The GTE and fixed point
9. The RNG and evaluation order
10. Function pointers, dispatch tables, varargs
11. Control flow and decomp idioms
12. `ROOD_PC` code
13. Calling still-C code; the owner switch
14. What must be bit-identical, and what may differ
15. Errors and panics
16. Names
17. Comments
18. Never do this
19. Reviewer checklist
20. Prerequisites this guide assumes (not built yet)

---

## 1. What the oracle is, and what to read

- **The oracle is the PC build's C object for the same target** (32-bit,
  64-bit), not the PS1 assembly. Where they differ (the `ROOD_PC` branches, the
  LP64 adaptations, GCC's `-fwrapv`), the PC object wins: that is what shadow
  mode compares against.
- **Port from the preprocessed unit**, `pc/build/<tree>/battle_<unit>.i`, not
  from the `.c`. The `.i` is what GCC compiled: `ROOD_PC` branches selected,
  macros expanded (`psx_get`, `PC_GTE_UNNARROWED`, the `gte_*` macros),
  `.inc` files inlined, and the string and character literals inside
  `#pragma vsstring` blocks already re-encoded to the game's text encoding.
  Keep the `.c` open beside it for names and comments.
- **Functions that are MIPS assembly on the PS1** (the `*.s` units, and the
  global `__asm__` blocks under `#ifndef ROOD_PC`) have no C in `src/`. Their
  PC implementation is port-written C in `pc/src` (`battle_movement.c`,
  `battle_helpers.c`, `battle_motion.c`, `actor_*.c`, ...;
  `FUNCTIONS.tsv` names the file and line). That C is their oracle, and
  porting it is the D2 pass, after battle flips (§20); until then the first
  pass calls those functions through `c.*` like any other C.
- The compiler flags that define the C semantics (`pc/tools/build_boot.py`):
  `-O2 -fwrapv -funsigned-char -fno-strict-aliasing -fno-builtin -std=gnu17
  -fgnu89-inline`, 32-bit `-m32` (i686, x87), 64-bit `-m64 -fpie` with
  `ROOD_PC_LP64`. Every rule below follows from these:
  - `-fwrapv`: signed overflow wraps (two's complement), it is not UB.
  - `-funsigned-char`: plain `char` is **unsigned** (`u8`); `signed char` is
    `i8`.
  - `-fno-strict-aliasing`: type punning through pointer casts is fine.
  - gnu17: GNU C rules for `<<` of negative values (two's complement).
- When the C relies on undefined behaviour that GCC happens to compile one
  way (an uninitialised local, an out-of-range shift, a missing `return`),
  read the object (`objdump -d --no-show-raw-insn -r battle_<unit>.o`) and
  port what it does; say so in one comment line.
- **Before any game run, diff the port against its C body**
  (`pc/tools/zig_diff.py FUNCTION`, CTest `zig_diff`): the build's own
  `FUNCTION__orig` (from the routed `.i`, compiled with the game's flags) and
  the Zig port run side by side on random and edge inputs, with every global
  either reaches defined at its C size, every external call a recording stub
  (arguments, and a hash of the state at the call) and the scratchpad mapped;
  touched memory, call traces and return values must agree. It takes
  seconds, needs no game, and sees branches and values a corpus recipe never
  reaches (the trial's K3, T2, T3, N3 slipped through the corpus; it catches
  all of them). Each ported function gets a spec in `pc/tests/zig_diff/`
  (a `prepare` that builds valid inputs, biased to the C's edges, and
  `classes` naming the paths the inputs must reach) and its reviewers' wrong
  variants as `mutation`s in `specs.toml`; `--self-check` requires every
  mutation caught and every class reached. `--coverage` adds the coverage
  lane: the original again at `-O0` with gcov and value probes, on the
  spec's inputs, failing on any branch of the C the inputs never take and
  on value edges branch coverage cannot see (an ordered compare whose two
  sides were never, or only rarely, equal; a nonzero test only ever seen
  with the sign bit set); gaps nothing can reach are waived in `specs.toml`
  with a reason. It complements the oracle, it
  does not replace it: it does not see state a stub hides (what a callee
  would have done with the arguments it got), nor inputs the spec cannot
  build.

## 2. The modules a port uses

Owned battle code lives in `game/battle/`, in modules named by meaning
(§16: `score.zig`, `actor_transform.zig`), each function with its `orig:`
line; D2's `pc/src` asm ports go to `game/battle/pc/`. Routing subsystems are
one per original unit (`battle_146C`, ...: the owner switch bisects by unit),
whatever module a function lives in. Each file imports:

| Module | What it gives | Source |
|---|---|---|
| `common` → `psx` | `Ptr(T)`, `get`, `conv`, `getBits`/`setBits`, `global`/`globalMany`, `at`, `cptr`, `addr` | `game/common/psx.zig` (exists) |
| `common` → `route` | `exportAll`: `F__owned`/`F__shadow` exports per routed function | `game/common/route.zig` (M0 task 9) |
| `c` | translate-c of the unit's own declarations: every prototype, typedef and enum constant **that unit** was compiled with, its block-scope `extern`s included; its data definitions become `extern` declarations (translate-c would export a second definition) | `pc/tools/zig_c.py`, one per unit (`c_146C`) |
| `views` | struct layouts, bitfield tables and global accessors `v.g.<name>()` (readable names, §16) from the unit object's DWARF, per target, for the types and globals listed in the subsystem's `views` manifest in `ownership.toml` | `pc/tools/zig_views.py`, one per unit (`views_146C`) |
| `gte` | one function per `gte_*` macro, exactly the PC macro's register sequence | `game/common/gte.zig` (to write, §8) |

Rules:
- **Use the `c` and `views` of the unit you are porting**, never another
  unit's. Units declare the same function or type differently
  (`func_800A4A24` returns `func_8008D2C0_t*` in `3A1A0.c` and `SVECTOR*` in
  `38C1C.c`; `D_800EB9B8_t` is 72 bytes in `573B8.c` and 19212 bytes in
  `58578.c`). Each unit's machine code used its own declaration; so does its
  port. (`pc/tools/decl_mismatch.py` reports the dangerous ones; fixing them
  is a separate change to the C, not something a port does silently.)
- No `@cImport`, no `extern var`, no hand-written struct layouts for guest
  data. If a type is missing from the views, add it to the views manifest.

## 3. Types

| C (as GCC compiled it) | Zig | Note |
|---|---|---|
| `char`, `u_char`, `unsigned char` | `u8` | `-funsigned-char`: `(char)x` is a **u8** truncation |
| `signed char` | `i8` | |
| `short` / `u_short` | `i16` / `u16` | |
| `int` / `u_int`, `unsigned` | `c_int` / `c_uint` (= `i32` / `u32`) | use `c_int` in signatures, `i32` is the same type |
| `long` / `unsigned long` **in game code** | `c_long` on Linux, 8 bytes on the Windows twin | see the `long` row below |
| `u_long`, `long` **in SDK declarations** | `u32` / `i32` | the 64-bit build reads SDK headers with `long` spelled `int` (`pc/include/psx_sdk_long_begin.h`); the `c` module already says so |
| `long long` | `i64` | only where the C has it (`_fixedPointMult`) |
| `size_t`, `sizeof` | `usize` | 4 bytes on 32-bit, 8 on 64-bit: see §4.6 |
| `T*` to guest data | the view's field type: `?*T` or `psx.Ptr(T)` | §5 |
| `psx_ptr(T)` field | `?*T` on 32-bit, `psx.Ptr(T)` on 64-bit (from DWARF); access only through `psx.get`/`psx.put` | §5.2 |
| function pointer | `?*const fn (...) callconv(.c) R` | §10 |
| `float` | `f32` | only host code (`.inc` settings); the game logic is integer |
| `_Bool`, comparison results | `c_int` 0/1 in C; `bool` in Zig | §4.7 |
| bitfield | `psx.getBits`/`psx.setBits` through the view's `bitfields` table | §7.3 |
| `enum` type | `c_uint` (GCC's type when no enumerator is negative), else `c_int`; constants as that type | never a Zig `enum` for guest data: a value outside it is not representable. An enum bitfield takes the enum's signedness |

The `long` row: a plain `long` in battle code is the host width (4 bytes on
32-bit, 8 on 64-bit Linux, and 8 on the Windows twin, which compiles with
`long` rewritten to `long long`). Zig's `c_long` is 4 bytes on Windows, so
`c_long` is wrong there. Spell it `psx.Long` (proposed addition, §20). In
battle `long` appears only in pointer casts (`return (long)&arg0[k];` in the
script opcodes), where the value is then truncated to `int` anyway: port those
as `psx.addr` (§5.5).

## 4. Integers

### 4.1 Promotions and the usual arithmetic conversions

C never computes in a type narrower than `int`. Before any arithmetic, `u8`,
`i8`, `u16`, `i16` **and bitfields narrower than 32 bits (even `u_int x : 3`)**
become `int`. Then, if one operand is `unsigned int` and the other `int`, both
become `unsigned int`. Zig does none of this: `u8 + u8` is a `u8` and traps at
256, and a comparison between `i32` and `u32` compiles and compares the
mathematical values.

So: **widen narrow operands to `c_int` before the operator, and convert to
`c_uint` explicitly where C would.**

```c
short a; u_char b;
int r = a * b - 300;          /* int arithmetic */
u_int u; int i;
if (i < u) ...                /* unsigned comparison: -1 < 1u is false */
```
```zig
const r: c_int = @as(c_int, a) *% b -% 300;                // b peer-widens to c_int; §4.4
if (psx.conv(c_uint, i) < u) ...                         // what C compares
```

Equality too: `i == u` in C is `(unsigned)i == u` (`-1 == 0xFFFFFFFF` is
true); in Zig it is false unless you convert. **A negative constant against
an unsigned operand** is the same trap and compiles silently: `u == -1` with
`u: c_uint` is always false in Zig (checked on 0.17), true in C for
`0xFFFFFFFF`. Write the unsigned value (`u == 0xFFFF_FFFF`) or
`psx.conv(c_uint, @as(c_int, -1))`; the same for `!=`, `<`, `>` and `case`
labels (§11.1).

**Unary `~` and `-` promote too.** `~m` with `m: u8` is a `u8` in Zig
(`0xF0`) but an `int` in C (`0xFFFFFFF0`): `f &= ~m` on a `u16` keeps `f`'s high
byte in C and clears it in Zig, with no error (checked). Widen first:
`f = psx.conv(u16, @as(c_int, f) & ~@as(c_int, m))`. Negation is `-%` on the
promoted value (`-%@as(c_int, s)`); on a `c_uint`, `-%u`.

`u16` and `int` mixed: the `u16` becomes `int`, the comparison is signed, and
Zig's mathematical comparison gives the same answer. Only a 32-bit
`unsigned` (or `size_t`) operand makes C's comparison unsigned.

### 4.2 Conversions: `psx.conv`

`psx.conv(T, x)` is C's integer conversion: narrowing keeps the low bits
(modulo 2^bits), widening sign- or zero-extends by **the source's**
signedness. Use it for every implicit or explicit C conversion between
integer types: assignment, initialisation, argument passing, `return`, and
casts.

```c
u_char  n = value;            /* truncation */
short   s = (short)(x >> 4);
u_int   w = (short)h;         /* sign-extends, then reinterprets */
```
```zig
var n: u8 = psx.conv(u8, value);
var s: i16 = psx.conv(i16, x >> 4);
var w: c_uint = psx.conv(c_uint, psx.conv(i16, h));     // whatever h's type
```

`psx.conv` takes integer types only: a literal goes through `@as` first
(`psx.conv(u8, @as(c_int, -1))`), a comparison through `@intFromBool`.

Never `@intCast` where C converts: `@intCast` traps when the value does not
fit, C does not. `@intCast` is right only for values the C itself guarantees
in range (an index just checked against the bound, a shift amount `% 32`).

`@as` is for widening that cannot change the value (`@as(c_int, some_u8)`).

### 4.3 Compound assignment and `++` on narrow storage

`x += y` with `x` narrow means `x = (T)(x + y)` computed in `int`: it wraps.

```c
u_char count; count++;          /* 255 -> 0 */
short hp; hp -= damage;         /* int subtraction, then truncation */
```
```zig
count +%= 1;                                        // same as conv(u8, count + 1)
hp = psx.conv(i16, @as(c_int, hp) -% damage);
```

A 32-bit target with an `int` right side (`u_int total; total += x;`):
convert the right side to the target's type, then wrap:
`total +%= psx.conv(c_uint, x);`.

### 4.4 Overflow

The C wraps everywhere: the PS1 code is `addu`/`subu`/`mult`, and the PC
objects are built with `-fwrapv`. Shadow mode needs the same behaviour, so:

1. **Integer `+`, `-`, `*` (and unary `-`) wrap by default**: `+%`, `-%`,
   `*%`, `-%x`, and `+%=`/`-%=`/`*%=`, on the C's promoted type (§4.1),
   signed or unsigned.
2. A plain (trapping) operator is allowed only where the C provably cannot
   overflow **and** a one-line comment says why (`// i < 17: the slot count`).
   When in doubt, wrap.
3. Narrowing stores never trap: they go through `psx.conv` (§4.2).
4. ReleaseSafe's other checks stay on: bounds, `.?` on null, `unreachable`,
   `@intCast`, alignment, division by zero (§4.8, §15).

Widen first, then wrap: `@as(c_int, a) +% b` with `a: i16`, never `a +% b`
(that wraps at 16 bits, which C never does).

### 4.5 Shifts

- `x << n`, `x >> n`: the result has the type of **the promoted left
  operand**; the right operand's type does not matter. Widen `x` first
  (`(u_char)0x80 << 1` is `0x100` in C, `0` in Zig on a `u8`).
- GNU C defines `<<` on negative `int` as the two's-complement shift; Zig's
  `<<` on `i32` discards the shifted-out bits without a safety check (checked
  on 0.17: `1 << 31` is `minInt(i32)`, `0x40000000 << 2` is 0). Do not use
  `@shlExact` (it traps).
- `>>` on signed is arithmetic in both (`-3 >> 1` is `-2`); on unsigned,
  logical. **A signed `x / 4096` is not `x >> 12`** (they differ for negative
  `x`): keep what the C wrote.
- The amount must be a `u5` for 32-bit operands (`u6` for 64-bit). For a
  runtime amount use `@intCast` when the C guarantees `0..31` (C makes
  anything else UB), else `@truncate` (both x86 and MIPS shift by `n & 31`)
  and say so in a comment.
- `1 << n` into an `int` array (`flags[n / 32] |= 1 << (n % 32)`): shift a
  `c_uint` and bitcast, so bit 31 is no question. With `n: c_uint`:
  `flags[n / 32] |= @bitCast(@as(c_uint, 1) << @intCast(n % 32));` (checked).
  With `n: c_int`, Zig rejects `%` and `/` on signed operands: use
  `@rem(n, 32)` and `@divTrunc(n, 32)` (§4.8); a negative remainder makes the
  C shift undefined, so `@intCast` to `u5` (trap) is right.

### 4.6 `sizeof`, `size_t`

`sizeof` is unsigned and pointer-sized: an `int` mixed with it becomes
unsigned (and 64-bit on LP64). `randArr[randIndex] * (sizeof randArr /
sizeof randArr[0])` is an `unsigned long` product on 64-bit, truncated to
`int` on assignment; the low 32 bits are the same as on 32-bit, so compute it
as `c_uint` with `*%` and `psx.conv` the result. That holds only when the
product goes **straight** into a 32-bit store: if it is shifted, divided or
compared first, the 64-bit and 32-bit oracles compute different values;
compute in `usize` (which is exactly C's type on each target) and say so in
`REVIEW-NOTES.md` if the two oracles disagree. A comparison `i < sizeof x`
with a negative `i` is false in C: `psx.conv(usize, i) < @sizeOf(T)`.

### 4.7 Booleans, `!`, `&&`, `||`, `?:`

C's relational and logical operators yield an `int` 0 or 1:

```c
count += (a < b);        flag = !value;        r = x ? y : z;
```
```zig
count += @intFromBool(a < b);
flag = @intFromBool(value == 0);
r = if (x != 0) y else z;          // y and z converted to C's common type
```

`&&`/`||` short-circuit in both: keep every call on the same side of the
operator (§9). A ternary's result has C's common type of both arms (an
`int` and a `u_int` arm give `u_int`).

### 4.8 Division and remainder

| C | Zig |
|---|---|
| signed `a / b` | `@divTrunc(a, b)` |
| signed `a % b` | `@rem(a, b)` (sign of `a`, like C) |
| unsigned `a / b`, `a % b` | `a / b`, `a % b` |

Never `@divFloor`/`@mod` (they round differently for negatives). Division by
zero and `INT_MIN / -1` trap in Zig and raise SIGFPE in the x86 C: both stop
the game, and a corpus run that reaches them is a bug report, not a
difference.

### 4.9 Floats

The game logic is integer. `float` appears only in host code compiled into
units (the charge-skip settings in `charge_skip.inc`, widescreen math). Port
those with `f32` and the same operation order; float to int is
`@intFromFloat` (truncates toward zero like C; out of range it traps where the
x86 C gives `0x80000000`: clamp first where the C value can be out of range).
On 32-bit, GCC's x87 code keeps excess precision and LLVM rounds at other
points, so `f32` results can differ in the last bit: float sites are host
presentation (hold times, widescreen), not compared state; if one ever feeds
game state, compare it in the oracle and record it in `REVIEW-NOTES.md`.

## 5. Pointers and the RAM image

### 5.1 The memory model

- Guest RAM is mapped at its PS1 address (`0x80000000..0x80200000`), the
  scratchpad at `0x1F800000..0x1F800400`, on both builds. A guest address is
  a valid host pointer; zero-extended to 64 bits it still is.
- Data the decompilation only *declares* (`extern` with no C definition; most
  battle globals) is linked to its PS1 address: it lives **in the guest RAM
  image**, loaded from the disc with its overlay (`STATE.tsv` "kind").
- On 64-bit, pointer-bearing data imports are moved to native regions
  (`rood_lp64_*`, 8-byte pointer slots); the symbol resolves there and the
  64-bit views say so. On 32-bit the same symbol is in guest RAM.
- Data the C *defines* (tables, `static`s, the `ROOD_PC` additions) is host
  data in the object (BATTLE's in sections restored on each `BATTLE.PRG`
  load, §6.4). Its address is below 4 GB (non-PIE link), so it fits a 4-byte
  guest pointer too.

### 5.2 Pointer fields: `psx.get`, `psx.put`

A `psx_ptr(T)` field is a `?*T` in the 32-bit views and a `psx.Ptr(T)` (a
4-byte `raw`) in the 64-bit views. The same Zig source must compile for both,
so **read every pointer field with `psx.get`, write with `psx.put`**:

```c
vs_battle_actor2* a = psx_get(enemy->unk3C);
psx_set(D_800F57A0.curveBlock, NULL);
```
```zig
const a = psx.get(enemy.unk3C);                 // ?*v.vs_battle_actor2
psx.put(&v.g.D_800F57A0().curveBlock, null);    // psx.put: proposed, §20
```

`psx.get` already accepts both shapes. Never touch `.raw` directly except
where the C uses `psx_raw` (then `psx.rawOf`, proposed).

`psx.put` into a **native** pointer field `@alignCast`s the pointer to the
field's type: a misaligned pointer (from `psx.byteAt`, `psx.lowRam` or
integer arithmetic) stored into a `T*` field traps in ReleaseSafe where the C
just stores it. Store such a pointer as the C does, by its bits
(`@as(*usize, @ptrCast(field)).* = @intFromPtr(p)`), with a comment; the
trial's stores (`vs_main_allocHeapR`'s 16-byte blocks) are aligned.

A plain native pointer field (a runtime struct the 64-bit build keeps in the
native layout) is `?*T` on both; `psx.get`/`psx.put` pass it through.

### 5.3 Null, and the `0x80000000` guard

A C dereference of `NULL` crashes the PC build; in Zig, unwrap with `.?`
(ReleaseSafe reports it). The PS1 does **not** crash: it reads physical
address 0, which the host maps at `0x80000000`. The port's `ROOD_PC` branches
emulate exactly the reads the corpus showed the game does through `NULL`
(empty actor slots, an unopened ability menu, a missing platform):

```c
D_800F18EC_t* ability = D_800F18EC != NULL ? D_800F18EC : (D_800F18EC_t*)0x80000000;
```
```zig
const ability: *v.D_800F18EC_t = v.g.D_800F18EC().* orelse psx.lowRam(v.D_800F18EC_t);
```

Port these guards **exactly where the PC C has them, and nowhere else**. Do
not add a guard the C lacks (that hides a crash the original PC build has),
and do not drop one (the oracle survives that path). `psx.lowRam(T)` is
`@as(*T, @ptrFromInt(0x8000_0000))` (proposed helper, §20).

### 5.4 Pointer arithmetic

| C | Zig |
|---|---|
| `p + k`, `p[k]` with `k >= 0` known | `@as([*]T, @ptrCast(p)) + @as(usize, @intCast(k))` (keep `p`'s alignment: `[*]align(1) T` from an `align(1)` pointer) |
| `p[k]` with a signed `k` (may be negative) | `psx.at(p, k).*` (after the §20 fixes to `at`) |
| `(char*)p + off` | `psx.byteAt(T, p, off)` → `*align(1) T` (proposed, §20) |
| `p - q` (elements) | `@divExact(@as(isize, @bitCast(@intFromPtr(p) -% @intFromPtr(q))), @sizeOf(T))` (traps on a non-multiple, which is UB in C: intended) |
| `p++` on a `psx_ptr` field | `psx.put(&f, psx.at(psx.get(f).?, 1))` |

Arrays in views are Zig arrays: `a[i]` is bounds-checked. The C is not. When
the C indexes past an array's end on purpose (`D_800F19CC->unk854[unk0 & 3]`
is fine; `mappedChainAbilities[x - 1]` with `x == 0` is not), use `psx.at`
and keep the comment that says the original does it. The same when the index
type can exceed the array even if the data never does: `enemyKills[6]`
indexed by `u_int enemyClass : 3` (0..7) is `psx.at(&score.enemyKills, class)`
(`_updateEnemyKills`); a plain index would trap where the C writes into the
next field. Use a plain index only where the C's own checks bound it, with
the one-line reason (`subType < 256`: below 8 of 26).

`psx.at`'s result is aligned to `min(base alignment, the largest power of two
dividing @sizeOf(E))`: `&score.enemyKills` is `*align(4) [6]u16`, and its odd
entries sit at 2 mod 4, so `psx.at` returns `*align(2) u16`, not
`*align(4) u16`. (Until the trial fix it kept the base's alignment, and
ReleaseSafe trapped "incorrect alignment" on the first odd class: a Beast,
Phantom or Evil kill.) Any helper of your own that forms `&base[k]` must do
the same; never give an element pointer its base's alignment, and never
`@alignCast` one up to it.

`psx.at` on a `[*c]T` base drops `allowzero`: `psx.at(null_cptr, k)` panics
where C computes `NULL + k`. Where the C takes the address of an element of
a possibly-null base without dereferencing it, test the base first and keep
the C's result (`if (p) |q| psx.at(q, k) else null`, or the plain integer
arithmetic of §5.5).

Alignment: a pointer built from a byte offset or an integer is `*align(1)
T` unless the C type's alignment provably divides it. MIPS would have trapped
on a misaligned access, x86 does not, and the oracle is x86: `align(1)` never
traps. `@alignCast` is only for pointers the C already had as `T*`.

### 5.5 Pointers and integers

```c
u_int a = psx_to_int(u_int, actor);              /* (u_int)actor */
T* p = psx_to_ptr(T*, base + 0x18A4);            /* (T*)(base + 0x18A4) */
return (long)&script[off];                       /* a script opcode's next pc */
```
```zig
const a: c_uint = psx.addr(actor);                       // traps above 4 GB, like psx_addr
const p: *align(1) T = @ptrFromInt(@as(usize, psx.conv(u32, base) +% 0x18A4)); // via u32, never sign-extended
return @bitCast(psx.addr(psx.at(script, off)));          // int return: the low 32 bits
```

`psx.addr` is the only pointer-to-integer conversion (it checks the 4 GB
limit as `psx_addr` does). Integer-to-pointer goes through `u32` (zero
extension, as `psx_to_ptr` does) and then `usize`, after the C's own (32-bit,
wrapping) integer arithmetic; an `int` base is `psx.conv(u32, base)` first.

### 5.6 Casts between struct types

`-fno-strict-aliasing` makes them plain reinterpretations:

```c
((SVECTOR*)&arg0->unk8.unk4C[29].armorDp)->vx = x;
vs_battleMovementModeFlags* flags = (void*)((char*)actor + 0x5B0);
```
```zig
@as(*align(1) v.SVECTOR, @ptrCast(&arg0.unk8.unk4C[29].armorDp)).vx = x;
const flags = psx.byteAt(v.vs_battleMovementModeFlags, actor, 0x5B0);
```

To pass a view pointer to a `c` function that takes the C type, use
`psx.cptr(c.T, p)`: it checks at comptime that the names and sizes match.

### 5.7 Scratchpad and fixed addresses

The decompilation addresses scratchpad slots by number
(`(void*)0x1F8003A8`, `getScratchAddr(n)`, `vs_scratch`). Keep the numbers:

```zig
const scratch: *v.actorTransformScratch = @ptrFromInt(0x1F80035C);   // 0x...5C: a multiple of 4, the type's alignment
const matrix: *align(1) v.MATRIX = @ptrFromInt(0x1F8003C2);          // 0x...C2: not a multiple of 4, so align(1)
```

The rule: use `*T` only when the slot's number is a multiple of `@alignOf(T)`
**on every target** the game builds (i686, x86_64 Linux, the Windows twin:
a view with an 8-byte member is 8-aligned on 64-bit only); otherwise
`*align(1) T`. A comptime address is checked at compile time, so a wrong
`*T` fails the build of that target rather than the run. With `*T` (most
slots), its
fields' addresses then go to `c` functions through `psx.cptr` (which cannot
raise an `align(1)` pointer's alignment) and to `gte` as they are
(`func_800B07DC`). The field mapper names such a block's fields from an
ownership root `TYPE@ADDRESS` (`actorTransformScratch@0x1F80035C`), so a
shadow mismatch in it reads `actorTransformScratch@0x1F80035C.offset.vy`.

Two numbers can name the same bytes (`0x1F8003A8` is
`scratch->translation`, `0x1F8003D4` is `matrix->t`): keep both spellings as
the C has them, so the aliasing stays visible. Scratchpad pointer slots
(`*(void**)0x1F800000`, the packet cursor) hold 4-byte guest addresses on
every target: read them as `u32` and widen (`psx_load`/`psx_slot_get` in C).

### 5.8 Pointers to locals that escape

The C sometimes returns or stores the address of a stack array (`func_800A4A24`
returns `sp10`; the PC branch made it `static`). A Zig function cannot return
a pointer into its own frame, and owned code has no statics (§6.3): such a
buffer stays a C definition (the existing `static` in the unit, made global
by the build, or a definition in `pc/src`) and the port uses it through
`psx.global`. `STATE.tsv` lists all of them ("function-local static").

## 6. Globals and statics

### 6.1 Access

Every global, guest or host, is reached through the views accessor
`v.g.<name>()` (a `*T`) or `psx.global(T, "name")` / `psx.globalMany(T,
"name")` for unsized arrays. No `extern var`: on Windows it goes through a
`.refptr` stub the overlay renames cannot follow, and the build rejects it.

```c
extern u_int _cameraMode;          _cameraMode = 5;
extern D_800F4538_t* D_800F4538[17];   D_800F4538[i]->unk0.position
```
```zig
v.g._cameraMode().* = 5;
v.g.D_800F4538()[@intCast(i)].?.unk0.position
```

Overlay-local names (`_fontTable` is TITLE's and BATTLE's, different data):
write the plain name. The build renames the battle Zig object's reference to
`name__battle` because the object is named `battle_zig.o`.

### 6.2 Names that alias

splat names any address code takes, so one byte range can have two names:
`D_800F5798` is `D_800F56C8.unkD0`, `D_800F192C` is `vs_battle_actors[1]`.
The C uses whichever name its author used; so does the port. `STATE.tsv`
marks aliases ("inside X+0xNN" in `ps1_specific`, and in `zig_plan` the other
names in the same 64-bit `rood_lp64_*` region). On 64-bit an alias of a moved
import is a hazard the C already had to fix (`6F014.c`); port the fixed C.
The build maps a 4-byte integer alias of a moved pointer word to the low half
of the 8-byte slot: **never write a pointer word through a 4-byte integer
alias** unless the C does exactly that (the high half would stay stale).

### 6.3 Owned code has no statics

The build rejects any nonzero `.data`/`.bss` in an overlay's Zig object. All
state stays where the C keeps it, so save states, rewinds, overlay reloads,
the owner switch and hot swap see one state. Consequences:

- A C `static` variable (file or function scope) is **not** ported as a Zig
  `var`. It stays a C definition, made reachable by name (§6.5), and the port
  reads and writes it with `psx.global`.
- Zig `const` tables (`.rodata`) are allowed, but during D read the C table
  through `psx.global` instead: one source of truth, and the address never
  points into a library that hot swap unloads (§10.3).
- A `const` static the C keeps in `.rodata` is not restarted per load and is
  not compared state: it may stay C (globalised) or become a Zig `const` with
  the same value. Three `static const` locals have no storage at all (GCC
  folded them: `D_800689E8`, `D_800689F8`, `initialUp`): port them as Zig
  `const` values.

### 6.4 Lifetimes (overlay loads)

| Where the data is | Restarted when |
|---|---|
| BATTLE's guest image `0x80068800..0x800F5924` (data and bss are both in the file: `BATTLE.PRG` is 577 828 = 0x8D124 bytes, checked) | every disc load of `BATTLE.PRG` |
| BATTLE's C definitions (`.data`/`.bss`, including `-fdata-sections`' `.data.name`, renamed to `rood_ovl_battle_battle_prg_*`) and the 64-bit `rood_lp64_*` regions inside the BATTLE range | every load of `BATTLE.PRG` (`pc/src/overlay_statics.c`) |
| BATTLE's C `const` data (`.rodata`) | never (and not compared) |
| SLUS data, `main.c` definitions (`rood_state_*`) | never (whole session) |
| host settings (`pc_qol_*`) | never; owned code only reads them |

BATTLE is the gameplay overlay (field and battle), loaded on entering the
game and after ENDING; "per overlay load" is roughly "per session". What
resets per battle, room or action is decided by code that writes the value,
not by its storage: `STATE.tsv` "lifetime" says which where it is known.

### 6.5 Statics the Zig object cannot see

A file `static` is a local symbol: `psx.global` cannot link to it. Before a
function using one is ported, the build must globalise it (an `objcopy
--globalize-symbol` step for the listed names, as `build_boot.py` already does
for some static functions of `main.o` and MAINMENU), keeping its section so
the overlay restart still covers it. Function-local statics get a GCC suffix
(`sp10.0`): same treatment, with the suffixed name, which must be unique in
the object (`func_800A4A24`'s `sp10` and its inlined copy's `platforms` are
two). `STATE.tsv` lists the 30 that battle code touches, with the exact object
symbol: 6 file statics in `146C.c` (one `const` table and the five
charging-skip variables of `charge_skip.inc`), 13 in the `pc/src` asm ports
(`room_render.c`, `room_geometry.c`; D2), and 11 function-local ones, of which
3 were folded away (no symbol) and 2 are `const`. So D's first pass globalises
at most 5 file statics and 6 function-local ones. The `.N` suffix is GCC's
choice and changes with the compiler or the source: the build step finds the
symbol by pattern (`name.[0-9]+` among the unit's local data symbols, exactly
one match) and fails otherwise. Globalised names must not collide: prefix them
(`battle_146C__charge_skip_held`).

Never reach a static through `c`: translate-c would give the Zig object its
own copy (a second `charge_skip_held`, a `.rodata` copy of `D_80068BEC` at
another address, which `psx_addr` rejects on 64-bit). `zig_c.py` therefore
emits every file-scope static of the unit (variables, tables, static
functions, header `static inline`s such as `psx_addr`) as a `@compileError`
that names it: using one fails the build. Reach the globalised symbol through
`psx.global` instead, or the `psx` helper (`psx.addr`). Owned objects keep no
`.data`/`.bss` of their own either: the build rejects them in an overlay's
object, ELF and COFF twin alike (`check_zig_object`).

## 7. Structs, unions and bitfields

### 7.1 Layouts come from DWARF

`views` emits each struct as an `extern struct` with explicit `_pad_N` and
`_bits_N` byte arrays, `align(1)` on misaligned fields, and comptime asserts
of every offset and the size against DWARF, per target. Never write a guest
layout by hand, never use a non-`extern` Zig struct for guest data, and never
`packed struct` (its bit order is not GCC's).

### 7.2 Copies and comparisons

A struct assignment in C copies every byte (GCC uses `memcpy` or wide
moves). The views name every byte (padding included), so a Zig assignment of
a view copies the same bytes. For a struct the views do not have (a C-local
typedef), copy with `@memcpy` on the byte slices. Never compare structs
field by field where the C uses `memcmp`/`vs_main_memcmp`: call the same
function.

### 7.3 Bitfields

```c
if (actor->unk0.unkA_3 != 0) return;
flags->mode = 2;
total += actor->enemyClass * 3;      /* u_int enemyClass : 3 promotes to int */
```
```zig
if (psx.getBits(&actor.unk0, "unkA_3") != 0) return;
psx.setBits(flags, "mode", 2);
total +%= @as(c_int, psx.getBits(actor, "enemyClass")) *% 3;
psx.setBits(b, "f", psx.conv(u5, @as(c_int, psx.getBits(b, "f")) +% 1));   // b->f++ (u_int f : 5)
```

- `getBits` returns the field's exact width (`u3`, `i5`); widen to `c_int`
  for arithmetic (§4.1). A bitfield of 32 bits declared `u_int` promotes to
  `unsigned`, narrower ones to `int`.
- `setBits` takes the exact type `getBits` returns (`u5` for an unsigned
  field, `i5` for a signed one): the C truncates the stored value, so
  `psx.setBits(p, "f", psx.conv(u5, value))` or `psx.conv(i5, value)`.
- Pass the pointer to the struct that declares the bitfield (the view type
  whose `bitfields` table names it): for `actor->unk0.unkA_3` that is
  `&actor.unk0`, not `actor`. A bitfield inside an anonymous member is reached
  through that member's view type (a views-generator contract to check in
  the trial).
- Signed bitfields sign-extend on read (`getBits` does).
- A read-modify-write of one bitfield writes the bytes the field spans, as
  GCC does; adjacent fields in the same bytes keep their values.

### 7.4 Unions

`extern union` in views; write the member the C writes, read the member the
C reads. Reading another member than the one last written is a
reinterpretation and is correct here (it is what the C does).

### 7.5 Zero-length and unsized arrays

`T x[]` / `T x[0]` at a struct's end, and unsized `extern` arrays: index
through `psx.at` (no bounds) or `psx.globalMany`.

## 8. The GTE and fixed point

### 8.1 What the C really runs

On the PC every `gte_*` macro is PsyCross's (`pc/include/inline_c.h` →
`pc/third_party/psycross/include/psx/inline_c.h`, plus `src/include/vs_inline_c.h`
for the `*2` variants): a sequence of calls to the software GTE, `MTC2`,
`CTC2`, `MFC2`, `CFC2` and `doCOP2(op)`. The GTE registers are host state
(`gteRegs`, saved with the game state), and they carry values **between
functions**: a function that loads a matrix and returns leaves it for the
next one.

### 8.2 `game/common/gte.zig`

One Zig function per macro the battle code uses, named after it, doing the
PsyCross macro's calls in the same order with the same register numbers and
the same 32-bit loads and stores:

```zig
/// gte_ldv0: VXY0, VZ0 from a SVECTOR.
pub inline fn ldv0(r0: *const anyopaque) void {
    MTC2(load32(r0, 0), 0);
    MTC2(load32(r0, 4), 1);
}
/// gte_rtv0tr2 (vs_inline_c.h): gte_rtv0tr on the PC.
pub inline fn rtv0tr2() void {
    _ = doCOP2(0x0480012);
}
```

The macros the battle code uses (counts in `src/BATTLE` and its `pc/src`
ports): `SetRotMatrix` 13, `ldv0` 13, `stlvnl` 11, `SetTransMatrix` 9,
`rtv0tr2` 9, `nclip` 7, `rtpt` 5, `rtps2` 5, `rtps` 4, `avsz3` 4, `ldsxy3` 3,
`zrtr` 2, `stsxy` 2, `mvmva` 2, `stszotz`, `stotz`, `stopz`, `rt`, `op0`,
`nclip2`, `gpf12`, `dpcs` 1 each. Note the PC differences: PsyCross's
`gte_ldsxy3` loads through pointers (the PS1 macro takes values; see
`58578.c`), and `gte_zrtr` is `CTC2(0, 5..7)`.

Rules:
- Call the `gte` function where the C has the macro, in the same order
  relative to every other call (a `c.*` call may itself use the GTE).
- Do not replace a register sequence with a libgte call that "does the same"
  (`RotTrans` for `ldv0; rtv0tr; stlvnl`): flags, `IR` registers and the
  leftover register state differ.
- libgte functions the C calls (`RotTransPers`, `rsin`, `rcos`, `ratan2`,
  `VectorNormal`, `SetRotMatrix`...) are C: call them through `c.*`.

### 8.3 `PC_GTE_UNNARROWED`

The widescreen hook brackets a projection with `++pc_gte_anchored; ...;
--pc_gte_anchored;` (host state in the GTE unit). Port it literally:

```zig
v.g.pc_gte_anchored().* +%= 1;
gte.rtps2();
v.g.pc_gte_anchored().* -%= 1;
```

### 8.4 Fixed point

PS1 fixed point is 4.12 (`ONE` = 4096) for vectors and matrices and 12-bit
angles (`0x1000` = a turn). Rules:

- Keep the C's operation and order: `(a * b) >> 12` multiplies in 32 bits
  (wraps in the original: `*%`, §4.4), `_fixedPointMult` multiplies in 64
  bits because its PC branch does (`@truncate((@as(i64, a) * b) >> 12)`).
- `x / 4096` stays `@divTrunc(x, 4096)`; `x >> 12` stays `>> 12`.
- Angle sums wrap and are masked (`& 0xFFF`): wrapping operators.
- `rsin`/`rcos` are table lookups in C (libgte): call them.

## 9. The RNG and evaluation order

The battle RNG is `vs_main_getRand` / `vs_main_getRandSmoothed`
(`main.c`, `randArr[97]` and `randIndex`, refilled by the SDK's `rand()`),
and `vs_battle_randUniformInt` (BATTLE `5BF94.c`). They stay C or are ported
with the same state; the port calls them **the same number of times, in the
same order**, with the same arguments.

C leaves the evaluation order of function arguments and of most operands
unspecified; GCC picks one per target. When one expression contains two
calls with side effects (two RNG draws, a draw and a state change) the order
GCC emitted is the original behaviour. Read it from the object and write the
calls as separate statements in that order, with a comment. If the 32-bit
and 64-bit objects disagree, say so in `REVIEW-NOTES.md` (the oracles then
differ from each other).

Short-circuit and conditional evaluation must be kept: a call on the right
of `&&` happens only when the left is true.

## 10. Function pointers, dispatch tables, varargs

### 10.1 Calling through a table

Tables of functions are either C definitions (`_opcodeFunctionTable[256]` in
`4A0A8.c`) or guest tables the build rehomes into native ones
(`function_tables.c`, `event_callbacks.inc`, `effect_callbacks.inc`). Either
way their entries are the **public names**, which the routing trampolines
define: calling through the table reaches the owner the switch selects.
During D, call through the same table:

```c
ret = _opcodeFunctionTable[*script](script, arg1);
```
```zig
const Op = *const fn ([*c]u8, c_short) callconv(.c) c_int;
const table = psx.globalMany(?Op, "_opcodeFunctionTable");
ret = table[script[0]].?(script, arg1);
```

A `NULL` entry the C calls crashes in C: `.?` traps in Zig. Keep the C's own
checks (`pc_require_native_callback`) where it has them.

### 10.2 Unprototyped and mis-typed calls

The decompilation calls some functions without a prototype (`func_8008D418()`
in `146C.c`) or through a cast that adds arguments
(`((void (*)())func_800D6E24)(arg0->unk18, ...)`). Port the call the object
makes: the argument count and types the caller passes. Cast the `c` function
to the pointer type the C cast to:

```zig
const F = *const fn (?*anyopaque, ?*anyopaque) callconv(.c) void;
@as(F, @ptrCast(&c.func_800D6E24))(psx.get(arg0.unk18), @ptrFromInt(0x1F80015C));
```

### 10.3 Never store an owned function's address

Owned functions are reached through the trampoline name `F` only. Never write
the address of a Zig function, or of Zig `.rodata`, into game memory or a C
table: a hot swap unloads it (§20), and the owner switch would be bypassed.
Store the C symbol's address (`&c.F`), which is the trampoline.

### 10.4 Varargs

Battle defines no variadic functions; it calls `sprintf`, `vs_battle_printf`
and friends (11 functions). Zig calls C variadics directly; pass every
integer argument as `c_int`/`c_uint` (C's default promotions) and pointers as
pointers:

```zig
_ = c.sprintf(&buf, "unknown code detected:%d,$%08x,$%02x", @as(c_int, arg1), psx.addr(arg0), @as(c_uint, arg0[0]));
```

Game text is never a Zig string literal: literals inside `#pragma vsstring`
blocks are re-encoded by the build. Use the encoded bytes from the `.i`, or
reference the game's string tables (`vs_battle_menuStrings`). Host-only debug
text (`pc_log`, `sprintf` diagnostics) may be a literal.

## 11. Control flow and decomp idioms

### 11.1 `switch`

Zig has no fall-through. A C `case` that falls into the next becomes a
labeled `switch` with `continue :sw next_case`, which is exactly a jump:

```c
switch (state) {
case 0: init(); /* fallthrough */
case 1: step(); break;
default: return;
}
```
```zig
sw: switch (state) {
    0 => {
        init();
        continue :sw 1;
    },
    1 => step(),
    else => return,
}
```

The C's `default:` placement and its gaps (no `case 2`) must be explicit
(`else`). C converts every `case` constant to the **promoted type of the
controlling expression**: switch on that type (`c_int` for a `short` or
`char` operand, `c_uint` for a `u_int`) and write each label as C converts it
(`case -1` on a `u_int` is the `0xFFFF_FFFF` prong; on a `u8` promoted to
`c_int` a `case -1` can never match and the prong is dropped, with a comment).

A C `break` inside a `case` leaves the switch, which a Zig prong does by
ending: drop the `break`, or `break :sw` out of a labeled switch when it
leaves early. **A literal `break` in a Zig prong leaves the enclosing loop**,
which changes the behaviour silently.

### 11.2 `goto`

76 battle functions use `goto` (decompiled branch spaghetti, mostly
"skip to the common tail" or a retry loop). Map each to:
- a labeled block and `break :label` for forward jumps to a common tail;
- `while (true)` + `continue` for backward jumps;
- a labeled `switch` state machine (`continue :sw .label`) when the jumps
  cross, one enum value per C label.

Keep the C's label names as the block or enum names.

### 11.3 Loops

- `do { body } while (c);` →
  `while (true) { body: { ... } if (!(c)) break; }`, with each C `continue` in
  the body written `break :body` (in C it jumps to the test; a Zig `continue`
  there would skip the test).
- `for (init; cond; inc) { ... }` → `init; while (cond) : (inc) { ... }`; a C
  `continue` runs `inc`, and so does Zig's. An increment with a comma or a
  call keeps its order: `while (cond) : ({ a +%= 1; b -%= 1; }) { }`.
- Inside a `switch` inside a loop: C's `continue` refers to the loop (label
  it: `continue :loop`); C's `break` leaves the switch only (§11.1).

### 11.4 Decomp idioms

| In the C | In the Zig |
|---|---|
| `func_800xxxxx`, `D_800xxxxx`, `unkXX`, `temp_v0`, `var_s1`, `sp10`, `new_var` | readable names where the meaning is known, with `orig:` comments; the original where it is not (§16) |
| `static inline` "inlined copy" of another unit's function (`40564.c` `func_800A4A24`) | its own private Zig function in that unit's file, with its own statics; do not call the original instead |
| gnu89 `inline` without `static` (`3A1A0.c`) | a normal function (it has an external definition) |
| `INCLUDE_ASM` with a `ROOD_PC` C replacement | port the replacement (§12) |
| block-scope `extern` declarations (the `ROOD_PC` hooks declare their host functions inside the body) | nothing: `ownership.strip_bodies` hoists them, so `c` declares them (a weak one is still called through `psx.weak`) |
| a missing `return` in a non-void function (`func_8008C8A8`) | return what the object returns (read `%eax`); comment it |
| uninitialised locals read | what GCC's object does when it is deterministic; else initialise to 0, comment "uninitialised in the original" and expect the double oracle to mask it |
| `do { } while (0)` from macros | plain statements |

## 12. `ROOD_PC` code

The PC build's C differs from the PS1 source in four ways. `FUNCTIONS.tsv`
column `rood_pc` names the blocks per function.

| Kind | Examples | In D |
|---|---|---|
| **Replacement of PS1 assembly** (`INCLUDE_ASM` or `__asm__` under `#ifndef ROOD_PC`, `.inc` replacements) | `func_8006CB04`, `vs_battle_memcpy`, `_fixedPointMult`, `func_800E3600`, `actor_commands.inc`, `scene_submit.inc` (`func_8008AC78`), `room_shadows.inc` (`func_8008CEB0`); in D2 also `pc/src/battle_*.c`, `actor_*.c`, `room_*.c` | port the PC C: it is the original on the PC |
| **Safety guards** (reads of PS1 address 0, empty actor slots, stack arrays made `static`, uninitialised registers) | `146C.c:1002`, `30DB0.c:2422`, `38C1C.c:206..379`, `3A1A0.c:347..2350`, `40564.c:351..697`, `44F14.c:499..1027`, `5BF94.c:6939..8536` | port exactly; they define where the oracle survives |
| **LP64 adaptations** (`psx_get`/`psx_set`, `psx_load`, `#ifdef ROOD_PC_LP64`, pointer globals that were aliases) | `6F014.c:321`, `146C.c:6632`, `30DB0.c:1650`, `44F14.c:1370`, `4A0A8.c:2989`, `74088.c:1219` | they come out naturally from §5 and the views; port the arithmetic they do (on guest addresses, 32-bit) |
| **PC features**: calls into `pc/src` (charging skip, right-stick camera, widescreen, modern menus, weapon slots, fast travel, room warp, scene capture, text glyph capture, talk bubbles, diagnostics) | `pc_charge_skip*`, `pc_camera_stick_*`, `PC_GTE_UNNARROWED`, `PC_ANCHOR_PACKETS`, `pc_ui_menu_*`, `pc_weapon_slots_*`, `pc_fast_travel_*`, `pc_room_warp_*`, `pc_scene_*`, `pc_text_*`, `pc_bubble_*` | port the call as it is, with the same guard |

Feature hooks in D: the owned function calls the same host function at the
same point. The C declares many of them `__attribute__((weak))` and tests the
address; keep the test:

```c
extern void pc_hw_anchor_packets(const void*, const void*, int) __attribute__((weak));
if (pc_hw_anchor_packets) pc_hw_anchor_packets(start, end, 2);
```
```zig
const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
if (psx.weak(Anchor, "pc_hw_anchor_packets")) |f| f(start, end, 2);
```

Do **not** write `@extern(?F, .{ .linkage = .weak })` and test it directly:
Zig 0.17 folds that test to "present" and calls unconditionally (checked on
both ELF targets: the object has the weak symbol, the null test is gone, an
absent hook crashes). `psx.weak` (proposed, §20) launders the address through
a variable the optimiser cannot see into:

```zig
pub inline fn weak(comptime F: type, comptime name: []const u8) ?F {
    var f: ?F = @extern(?F, .{ .name = name, .linkage = .weak });   // ELF (COFF: below)
    std.mem.doNotOptimizeAway(&f);
    return f;
}
```

(Checked on `x86-linux-none` and `x86_64-linux-none` linked by gcc: null when
the hook is absent, called when present. On COFF the trial found that LLVM
reaches a weak external only through a `.refptr` stub, which the build
rejects, where MinGW's C uses a PC-relative `lea`; `psx.weak` there is a
strong PC-relative reference, never null: every Windows build links the
whole game, so the hooks are defined, and a missing one is a link error
rather than a skipped hook.) The twelve weak hooks battle uses today are
all defined in every build that links BATTLE, so the test is always true
there; it stays for the probes and smaller builds.

The charging skip is special: its game side is `pc/src/charge_skip.inc`,
compiled into `146C.c`, with state (`charge_skip_held` and the others) as
file statics there. In D it stays C and the owned `vs_battle_exec` and
`vs_battle_displaySceneMessage` call `pc_charge_skip`/`pc_charge_skip_name`
through `c.*`, as the C does. At the flip (D4) the skip becomes native owned
code (behind the same settings, which at their original values reproduce the
oracle), its state moves to a C definition the owned code reaches through
`psx.global`, and the hooks are removed from `src/`.

What "delete the `ROOD_PC` hooks" at the flip means: the **feature** hooks
only. The safety guards, the asm replacements and the LP64 adaptations stay
in `src/`: without them the oracle cannot run on a PC at all. After the flip
the oracle is the game without PC features, so every feature in owned code
needs a setting whose original value makes it inert (widescreen off, modern
menus off, camera stick idle, weapon slots off, ...), and the differential
runner pins them all off. Corpora used after the flip are recorded with that
profile. A feature hook that touches game state (the charging skip's
`pc_charge_skip_reseed` and its RNG pin, the effect loader pump) is removed
only after its own tick-mode check: owned code with the feature on equals the
original with its hook (the M0 plan's Task 28 pattern).

## 13. Calling still-C code; the owner switch

- Call any C function through `c.<name>`. If that function is routed, the
  call goes to its trampoline and the owner byte decides who runs it, so an
  owned function calling a not-yet-owned one, or the other way round, needs
  nothing special. In shadow runs nested routed calls follow the run's side.
- Never call `F__orig` or `F__owned` by name (only `route.zig` does).
- **A `static` C function cannot be routed** (`ownership.py` refuses it) and
  cannot be called from another object. 41 battle functions are `static`.
  When a ported function calls one, the static is ported too, as a private
  Zig function in the same file. It is then checked only through its owned
  callers (shadow mode cannot call it alone).
- Functions in a `pc/src/*.inc` compiled into a unit (charge skip, scene
  submit, actor commands, room shadows, room callbacks) are routable only once
  `ownership.py` renames definitions in the included text too (it works on the
  `.i`, so it can; check it). The generated `event_callbacks.inc` /
  `effect_callbacks.inc` wrappers are `static`: not routable, ported with
  their callers.
- Routing relies on every call to `F` inside its unit staying a call: the
  rename makes them external. The build must check, after routing, that the
  routed unit's object has no call to `F__orig` from inside it and that no
  routed function was inlined (an `objdump -dr` relocation scan); gnu89
  `inline` definitions (`3A1A0.c`) are the case to watch.
- Pass view pointers where `c` wants C pointers with `psx.cptr(c.T, p)`.
- Struct arguments and returns by value: on x86-64 SysV `callconv(.c)` with
  `extern struct` types matches GCC; on i386 GCC returns every struct through
  a hidden pointer the callee pops (`ret $4`), and Zig's i386 aggregate ABI
  must be checked on the trial (§20) before any such function is routed.

## 14. What must be bit-identical, and what may differ

Shadow mode compares, after each routed call: guest RAM, the scratchpad,
the I/O page, the game's host statics (`rood_state_*`: the SDK layer, the
software GTE registers, the CD drive, MDEC, XA, libspu, `main.c`'s data),
the overlays' C statics (`rood_ovl_*`, which include BATTLE's), the SPU
state, and the return value.

Must be identical:
- every byte of that state, including GTE registers left behind and bytes
  the C writes that nobody reads later;
- every call into C that changes state, in the same order and with the same
  arguments (the callee's effects are compared);
- the return value.

Also identical, because they live in `rood_ovl_battle_*` and are compared:
the charging skip's file statics and the log-once latches (`reported` in
`func_800AE47C`/`func_800AEF94`): the owned code must set them exactly when
the C does.

May differ:
- dead stack bytes and values derived from uninitialised stack (the double
  oracle masks bytes that differ between two original runs);
- host side effects outside that state: log lines, renderer uploads, host
  counters in units not saved with the game state (`charge_skip.c`'s
  presentation counters, `pc_charge_skip_pumped`), timing;
- the host stack layout, register use, inlining, code size.

A feature hook whose host state is in a `pc/src` unit the build compiles as
a game unit (`hud/hud_bridge.c`'s name-window record, which
`pc_hud_name_window` updates at the end of `vs_battle_displaySceneMessage`)
is inside the compared state sections: shadow mode restores it between the
runs, so the hook runs once in effect. Check where a hook's state lives
before assuming item 7 of §20 applies.

Hazards the comparison does not see (so the port must get them right by
construction):
- writes through a pointer into the **caller's stack** (out-parameters:
  `func_800A1108(index, &sp10)`): if the caller is C and the callee owned,
  shadow mode does not compare the caller's frame (§20, oracle item 4);
- host state read by the C that is not in the compared ranges (settings,
  input state the replay supplies);
- feature hooks run three times per shadowed call (original twice, owned
  once). A hook with host state outside the compared ranges (`pc_text_*`,
  `pc_bubble_*`, `pc_scene_*`, the renderer anchors) must either keep that
  state inside them or be suppressed during the extra runs (§20, oracle
  item 7).

## 15. Errors and panics

- Owned code is built ReleaseSafe everywhere. Bounds, `.?` on null,
  `unreachable`, a bad `@intCast`, misalignment, division by zero and the
  rare documented non-wrapping operator (§4.4) trap and go to
  `pc_zig_panic` (`game/common/panic.zig` → `pc/src/ownership.c`), which
  prints `[zig] panic: <msg>` with the return address and takes the
  `pc_unimplemented` crash path.
- A trap is right where the C would crash or is undefined (null dereference,
  division by zero, an out-of-range shift). A trap where the C quietly goes on
  (an overflow it wraps, an index past an array that stays inside the
  struct) is a porting bug.
- `unreachable` only for cases the C cannot reach by construction (a
  `switch` over a value the code just masked). Never to silence a path.
- No Zig errors (`!T`, `try`) cross the C ABI; owned functions have the C
  signature. No allocation, no I/O, no `std.debug.print`: call the C
  (`pc_log`, `pc_unimplemented`) where the C does.
- Keep the C's `pc_unimplemented("...")` calls with the same message.

## 16. Names

**Readable names from day one; the original symbol stays the key.**

- **Functions** get a readable Zig name when they are ported, in lower
  camel case: from the decompilation's own name when it has one
  (`_updateEnemyKills` → `updateEnemyKills`, `vs_battle_displaySceneMessage`
  → `displaySceneMessage`: drop the `vs_battle_`/`vs_main_` prefix and a
  leading `_`), else from `battle/MEANINGS.tsv`, else from what the port
  reveals (`func_800B07DC` rotates the scratchpad offset by the actor's
  rotation and adds its position: `offsetToWorld`). Name what the function
  does, not how; no address in the name.
- **Every ported function carries the line** `/// orig: <original symbol>
  (<unit>)` as the first line of its doc comment, with the unit as
  `ownership.toml` spells it (`/// orig: func_800B07DC
  (src/BATTLE/BATTLE.PRG/44F14.c)`). A `static` C function ported as a
  private Zig function carries it too.
- **The ownership table maps the original symbol to the Zig function**:
  `{ addr = 0x800B07DC, name = "func_800B07DC", zig =
  "actor_transform.offsetToWorld" }`. `zig` is the declaration's path from
  the overlay's `root.zig` (which `pub const`-imports the module). Without
  `zig` the Zig function has the original name (the M0 pass-through module).
  `route.exportAll(..., ownership.routesOf(.<subsystem>), @This())` exports
  `name__owned`/`name__shadow` (and the hot-swap `name__candidate`) from the
  Zig function, so routing, the owner switch, shadow reports, hot swap and
  the corpus all keep pairing by the **original** name. The build checks that
  each `zig` path is a `pub fn` whose doc comment has the matching `orig:`
  line (`ownership.orig_comment_problems`).
- **Globals** are reached through the views by a readable accessor when
  `battle/MEANINGS.tsv` gives one (its `zig_name` column):
  `v.g.nameWindowText()` for `D_800EB9D8`. The accessor's doc comment is
  `/// orig: D_800EB9D8`; the symbol it links to is the original. Without a
  `zig_name` the accessor has the original name. Add the name to MEANINGS.tsv
  when the port reveals what a global is, not in the Zig.
- **Fields** likewise: a MEANINGS.tsv row `Type.field` (`vs_battle_actor.unk3C`)
  with a `zig_name` renames that field in the view of `Type` (with its
  `/// orig:` comment); the offset asserts still pin it to DWARF. Bitfields
  are renamed in the view's `bitfields` table the same way. Rename a field
  only when its meaning is established (read from its users, not guessed).
- **Parameters and locals** get readable names (`enemy`, `actor`, `message`,
  `action`), not `arg0`/`temp_v0`, except where the C's own name is already
  the best one. Keep a C local's name when it is a decompiler artefact whose
  meaning is unknown, rather than invent one.
- **Files**: one Zig module per routing subsystem's topic in `game/<overlay>/`
  (`score.zig`, `actor_transform.zig`, `scene_message.zig`), named by meaning;
  each file's top comment names the units its functions come from. The
  per-unit `c`/`views` modules are `@import("c_146C")`/`@import("views_146C")`
  (§2).
- A name Zig cannot take (a keyword, a leading digit) uses `@"name"`.

Reviewers diff the Zig against the C side by side through the `orig:` lines
and the views' `orig:` comments: the readable names must not change what is
compared (one Zig function per C function, the C's statements in the C's
order).

## 17. Comments

- Each function: the `/// orig: <symbol> (<unit>)` line (§16), then one short
  `///` line with its meaning. Nothing else by default.
- Inline comments only where the Zig is not an obvious reading of the C:
  a kept original bug, a `ROOD_PC` guard and why it exists (one line,
  pointing at the C comment), an evaluation order taken from the object, an
  uninitialised value, a wrapping operation that matters.
- Never narrate the port ("ported from", "TODO: check"), never explain Zig.
  If a construct needs a paragraph, the construct is wrong (§18).

## 18. Never do this

- **Stubs.** No `F__owned` that calls the original, returns a constant,
  `@panic("TODO")`, or leaves a branch out "for later". A function is either
  ported entirely or not routed.
- **Silent behaviour changes.** No fixed original bugs, no reordered calls,
  no dropped or added guards, no "equivalent" library call for a register
  sequence, no reordered fields, no changed table, no float for fixed point.
  A deliberate change is phase E work behind a setting whose original value
  reproduces the oracle.
- **A paragraph to justify a workaround.** If the code needs a long comment
  to argue it is still equivalent, rewrite it until it is plainly equivalent,
  or stop and ask.
- `@intCast` where C converts; plain comparisons of mixed signedness;
  arithmetic on unpromoted `u8`/`u16`.
- `extern var`, Zig `var` globals, Zig-allocated memory, addresses of Zig
  functions or constants stored in game memory.
- Editing `src/` to make a port easier (the only `src/` edit in D is the
  feature-hook removal at the flip).
- "Fixing" a prototype or a struct layout in one unit to match another.
- Skipping or masking an oracle mismatch without a recorded reason; widening
  a noise mask to make a queue empty.
- `orelse` defaults or `if (p) |q|` guards where the C dereferences
  unconditionally (they hide crashes the oracle has).
- Renames, reformatting into "idiomatic" Zig, helper abstractions across
  units during D.

## 19. Reviewer checklist

For every ported function, against the `.i` and the object:

1. Same calls, same order, same arguments (including GTE macros, RNG draws,
   hook calls and their guards)?
2. Every narrow operand widened before arithmetic? Every implicit conversion
   a `psx.conv`? Every mixed-signedness comparison converted?
3. Every shift: left operand promoted, amount cast correctly, signed `/`
   not turned into `>>`?
4. Wrapping operators on every `+ - *` and unary `-` (§4.4), each plain one
   with its one-line reason? `~` and `-` on widened operands? Negative
   constants against unsigned operands written as C converts them?
5. Pointer fields through `psx.get`/`psx.put`; byte offsets through
   `psx.byteAt` with `align(1)`; no new or missing null guard?
6. Globals through `v.g`/`psx.global`; no statics; statics of the C still in
   C?
7. Bitfields through `getBits`/`setBits`, widths and signedness right?
8. `switch` fall-throughs, `break`s in prongs, `continue` in `do`/`while`, and
   `goto`s mapped without changing which code runs?
9. `ROOD_PC` blocks: the PC branch ported, feature hooks kept with their
   weak-symbol tests?
10. Readable names per §16, each function with its `/// orig:` line and its
    `zig` entry in `ownership.toml`; comments only where §17 allows?
11. A `pc/tests/zig_diff` spec whose classes cover every branch of the C, with
    the wrong variants the review tried as mutations, passing
    `zig_diff.py --self-check --coverage` (every waiver with a reason that
    holds)?

## 20. Prerequisites this guide assumes (not built yet)

**Scope of D's first pass:** the 23 units of `src/BATTLE/BATTLE.PRG` and the
host code compiled into them (`battle/README.md`). **D2**, after battle flips:
INITBTL, and the `pc/src` C that implements BATTLE's PS1 assembly
(`battle_movement.c`, `battle_helpers.c`, `battle_motion.c`, `battle_gte.c`,
`actor_*.c`, `room_*.c`, ...), which is the oracle for those functions until
then. The table rows for them say "D2".

Things D needs that did not exist on `zig-ownership-m0`. The trial (plan
step D.2, `battle/TRIAL.md`) built items 1-4 to the extent its three functions
needed them: `pc/tools/zig_views.py` and `zig_c.py` per unit (generated by
`build_boot.py` from the `views` manifest in `ownership.toml`, also for the
COFF twin and the hot-swap library), `gte.zig` with the five macros
`func_800B07DC` uses, and the `psx` additions with the `at` fixes. Item 5's
per-unit subsystems exist for the three trial units; the statics
globalisation, the post-routing inlining check and the i386 struct-return
probe do not.

**Zig side (M0 tasks 9-13, extended):**
1. `views` and `c` **per unit** for every battle unit (M0's Task 13 supports
   one unit per overlay), namespaced per unit. Views must emit a pointer to
   a struct whose natural alignment exceeds 4 as `?*align(4) T` (a guest
   address is only 4-aligned; `@ptrFromInt` checks alignment), and enums as
   their integer type.
2. `game/common/gte.zig` (§8.2).
3. Proposed `psx.zig` additions, each with a unit test like the existing
   ones: `put(field_ptr, ?*T)` (uniform pointer-field store), `rawOf(field)`,
   `lowRam(T)`, `byteAt(T, base, off: isize) *align(1) T`, `Long`
   (`c_long`, `i64` on Windows), `weak(F, name) ?F` (§12).
4. Fixes to the existing `psx.at`: it rejects a single-item `*T` base today
   (`std.meta.Elem` compile error, checked on 0.17), its index is `isize`
   so a `u32` index compiles on 64-bit and not on 32-bit, its address
   arithmetic uses trapping `+`/`*` (guest addresses are negative as an
   `isize` on 32-bit, so a negative `k` near `0x80000000` traps where C
   wraps), and it drops the base's `align(1)`. Proposed: accept `*T`,
   `[*]T`, `[*c]T` (and their `align(1)` forms), take any integer index
   converted as C converts it, compute with `+%`/`*%`, and keep the base's
   alignment as far as the element offset keeps it (built; the first version
   kept all of it, which trapped on `enemyKills`' odd entries: §5.4).
5. Build steps: globalise the battle statics `STATE.tsv` lists (§6.5, by
   pattern); ownership subsystems **one per unit** (`battle_146C`,
   `battle_5BF94`, ...: the owner switch then bisects, and "flip battle" sets
   them all) with `pc_owner[]` sized for them and the menus to come; the
   post-routing check of §13 (no inlined or locally resolved routed
   function); a probe of struct-by-value returns on i386 (§13).

**The oracle (phase B) must provide:**
0. **Routing wrappers for every battle signature.** M0's `route.zig`
   `withSignature` (which builds `F__shadow` and the pass-through) stops at
   **4 parameters**; battle has 130 five-, 9 six-, 4 seven- and 3
   eight-parameter functions. B must lift it to at least 8 (or generate the
   wrappers), or those functions cannot be routed at all.
1. Shadow mode over every routed battle function, with per-function
   mismatch reports mapped to fields (`field_map.py` over `STATE.tsv`'s roots).
2. Recoverable panics in shadow runs: a Zig panic during the owned run is
   reported as a mismatch (function, message) and the game continues on the
   oracle's state, instead of aborting.
3. The software GTE registers, `pc_gte_anchored` and the other host state
   the battle C writes inside the compared ranges (check `gteRegs` is in
   `rood_state_*`).
4. Out-parameters on the caller's stack: compare the caller's frame (from the
   call's stack pointer up to the caller's frame) for routed functions that
   take pointers to locals, or the mapper lists them and they are checked at
   the caller.
5. A corpus that covers the whole BATTLE overlay, not only actions: field
   roaming, rooms and doors, events and scripts, cutscenes, menus and HUD in
   BATTLE (`5BF94.c`), effects, traps, loot, saves points; and a
   per-function report of routed functions the corpus never calls.
6. The settings profile that makes every PC feature inert (widescreen off,
   modern menus off, camera stick idle, QoL off), pinned by the runner, for
   after the flip (§12).
7. Feature hooks with host state outside the compared ranges either moved
   inside them or suppressed during the extra shadow runs (§14).
8. For the trial: corpus paths that reach each trial function (an enemy kill
   for `_updateEnemyKills`, an actor whose transform calls `func_800B07DC`,
   an action name window for `vs_battle_displaySceneMessage`).

**Hot swap (phase C) must provide** (all four are prerequisites: without
them a reload either keeps running the old code or crashes):
1. Trampolines that jump through a pointer table (repointed on reload), not
   direct jumps to `F__owned` in the executable.
2. A `psx.global` for the development shared library: hidden-visibility
   PC-relative references cannot reach the executable's symbols from a
   `.so`; the dev build switches `psx.global` to default visibility (GOT),
   the executable exports its symbols (`--export-dynamic`), and the overlay
   renames (`name__battle`) apply to the library too. Keeping every access
   behind `psx.global` (§6.1) makes this a one-place change.
3. A reload only between frames, never while an owned function is on the
   stack (the event script interpreter calls back into owned code).
4. The rule of §10.3 enforced: a check that no game memory holds an address
   inside the library before it is unloaded.
