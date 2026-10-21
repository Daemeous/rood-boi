```zig
//! Unit 38C1C (battle actor bones and points).
const c = @import("c_38C1C");

/// orig: func_800A1AF8 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// A bone's point through func_800A1C10: its negative result, else 0.
pub fn func_800A1AF8(actor: c_int, bone: c_int, result: [*c]c_ushort, endpoint: c_int) callconv(.c) c_int {
    const r = c.func_800A1C10(actor, bone, result, endpoint);
    if (r < 0) return r;
    return 0;
}
```
